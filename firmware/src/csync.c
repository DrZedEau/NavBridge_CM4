/* SPDX-License-Identifier: GPL-2.0-only
 *
 * HSYNC + VSYNC -> composite sync (progressive scan) with TIM1.
 *
 * Same output as the progressive program of raspberrypi/utils piolib/examples/dpi_csync.c
 * (Copyright (c) 2024 Raspberry Pi Ltd) and of mbt28/pico-csync-generator:
 *   - CSYNC asserts on the HSYNC leading edge and lasts one HSYNC width (5.0 us);
 *   - while VSYNC is active, the pulse lasts one line minus one HSYNC width (58.5 us).
 *
 * TIM1 does it in hardware, with no interrupt per line:
 *   PA8 HSYNC -> TI1. Slave reset mode: each HSYNC leading edge restarts the counter, and
 *                CH1 captures the counter at that edge, which is the line period.
 *   PA9 CSYNC <- CH2 in PWM mode 1: asserted while CNT < CCR2, i.e. for CCR2 ticks from the
 *                HSYNC leading edge. CCR2 is preloaded: a new width takes effect at the next
 *                leading edge, so a pulse is never cut short or stretched half-way.
 *   CH3          compare 4 lines after the last edge: only reached if HSYNC stops.
 *   PA6 VSYNC -> EXTI6, whose interrupt picks the pulse width. The RP1 program reads VSYNC
 *                at the end of HSYNC; here too, a VSYNC change during the HSYNC part of a
 *                pulse applies to that pulse, a later one to the next line.
 *
 * The widths come from the DPI mode in config.h, in pixels (HSYNC 40 of 508), scaled to the
 * line period that TIM1 measures. So the internal oscillator's tolerance cancels out: the
 * pulses are 5.0 us and 58.5 us with the accuracy of the Raspberry Pi's crystal. CSYNC only
 * starts once the line period is within 1/16 of the expected 63.5 us.
 */
#include "csync.h"
#include "board.h"
#include "config.h"

#define HS_PIN 8u  /* PA8, AF2 = TIM1_CH1 */
#define CS_PIN 9u  /* PA9, AF2 = TIM1_CH2 */
#define VS_PIN 6u  /* PA6, EXTI6 */
#define TIM_AF 2u

#define TICKS_PER_MS   (SYSCLK_HZ / 1000u)
#define NOMINAL_PERIOD (CSYNC_LINE_PIXELS * TICKS_PER_MS / CSYNC_PIXEL_CLOCK_KHZ)  /* 3048 */
#define LOCK_TOLERANCE (NOMINAL_PERIOD / 16u)
#define LOCK_SAMPLES   16u    /* consecutive good periods before CSYNC starts */
#define RELOCK_BAD     8u     /* periods off the average before CSYNC stops */
#define APPLY_MARGIN   48u    /* 1 us: time for the VSYNC interrupt to rewrite CCR2 */
#define END_MARGIN     144u   /* 3 us: a VSYNC edge this close to the next line waits for it */
#define VSYNC_SEEN_MS  100u

/* TI1 input filter (TIMx_CCMR1 IC1F). It also sets the delay from the HSYNC edge to the
 * CSYNC edge (about 0.1 us + the filter time), hence where that edge falls against the
 * ADV7125's 8 MHz sampling clock: 0 none, 1: 2 samples, 2: 4 samples (83 ns), 3: 8, 4: 12,
 * 5: 16, 6: 24, 7: 32 (667 ns) at 48 MHz. Adjustable at run time (CSYNC FILTER n).
 *
 * Tested in the car on the BMW display (prototype, CM5): 2 puts the edge on a DAC sampling
 * instant and the picture shakes horizontally; 1 and 0 are steady. 0 is the farthest from
 * the bad phase (about 40 ns either way). HSYNC comes from the CM over a short trace and its
 * period shows no glitches, so no filter is needed. */
#ifndef CSYNC_INPUT_FILTER
#define CSYNC_INPUT_FILTER 0u
#endif

#define SMS_RESET TIM_SMCR_SMS_2                   /* 0100: reset mode */
#define TS_TI1FP1 (TIM_SMCR_TS_2 | TIM_SMCR_TS_0)  /* 00101: filtered TI1, CC1P polarity */

#define OC2M_FORCE_INACTIVE TIM_CCMR1_OC2M_2
#define OC2M_PWM1           (TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1)

#define INPUT_IDLE_PULL (CSYNC_HSYNC_ACTIVE_LOW ? PULL_UP : PULL_DOWN)

#if NOMINAL_PERIOD * 5u > 0xFF00u
#error "line period too long for TIM1 at this clock"
#endif

static volatile uint8_t enabled;
static volatile uint8_t locked;
static volatile uint8_t vs_active;
static volatile uint16_t pulse_normal;
static volatile uint16_t pulse_broad;
static uint32_t period_acc;  /* line period x 16 (running average) */
static uint32_t good_samples;
static uint32_t sample_acc;
static uint32_t bad_lines;
static uint32_t locks;
static volatile uint32_t losses;
static volatile uint32_t vs_edges;
static volatile uint32_t vs_last_ms;
static volatile uint8_t ic_filter = CSYNC_INPUT_FILTER;

/* Diagnostics, since the last csync_get_status(). */
static volatile uint16_t vs_pos_min = 0xFFFFu;  /* TIM1 count (line position) at VSYNC edges */
static volatile uint16_t vs_pos_max;
static volatile uint32_t vs_now;     /* applied to the line in progress */
static volatile uint32_t vs_next;    /* applied from the next line */
static volatile uint32_t vs_waited;  /* waited for the next line to start */
static uint16_t period_min = 0xFFFFu;
static uint16_t period_max;

static inline void vs_update(void)
{
    const uint32_t level = (GPIOA->IDR >> VS_PIN) & 1u;
    vs_active = CSYNC_VSYNC_ACTIVE_LOW ? (level == 0u) : (level != 0u);
}

static inline uint16_t pulse_width(void)
{
    return vs_active ? pulse_broad : pulse_normal;
}

static inline uint32_t sync_ticks(uint32_t period)
{
    return (period * CSYNC_HSYNC_PIXELS + CSYNC_LINE_PIXELS / 2u) / CSYNC_LINE_PIXELS;
}

/* With interrupts masked, or from an interrupt at the CSYNC priority. */
static void stop_output(void)
{
    TIM1->DIER = 0;
    TIM1->CCMR1 = (TIM1->CCMR1 & ~TIM_CCMR1_OC2M) | OC2M_FORCE_INACTIVE;
    locked = 0;
}

static void start_output(uint32_t period)
{
    const uint32_t sync = sync_ticks(period);

    period_acc = period * 16u;
    bad_lines = 0;
    TIM1->CCR3 = period * 4u;

    __disable_irq();
    pulse_normal = (uint16_t)sync;
    pulse_broad = (uint16_t)(period - sync);
    /* PWM on with an active width of 0 for the rest of this line, then queue the real width:
     * it loads at the next HSYNC leading edge, so the first pulse is a whole one. */
    TIM1->CCMR1 &= ~TIM_CCMR1_OC2PE;
    TIM1->CCR2 = 0;
    TIM1->CCMR1 = (TIM1->CCMR1 & ~TIM_CCMR1_OC2M) | OC2M_PWM1 | TIM_CCMR1_OC2PE;
    vs_update();
    TIM1->CCR2 = pulse_width();
    locked = 1;
    __enable_irq();

    TIM1->SR = ~TIM_SR_CC3IF;
    TIM1->DIER = TIM_DIER_CC3IE;
    locks++;
}

static void pins_off(void)
{
    /* The CM and the video DAC are unpowered: no current into them. CSYNC low, sync inputs
     * pulled down rather than up. */
    gpio_write(GPIOA, CS_PIN, 0);
    gpio_mode(GPIOA, CS_PIN, PIN_OUTPUT);
    gpio_mode(GPIOA, HS_PIN, PIN_INPUT);
    gpio_mode(GPIOA, VS_PIN, PIN_INPUT);
    gpio_pull(GPIOA, HS_PIN, PULL_DOWN);
    gpio_pull(GPIOA, VS_PIN, PULL_DOWN);
}

void csync_init(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    RCC->APBENR2 |= RCC_APBENR2_TIM1EN;
    (void)RCC->APBENR2;

    GPIOA->OSPEEDR = (GPIOA->OSPEEDR & ~(3u << (CS_PIN * 2u))) | (2u << (CS_PIN * 2u));
    pins_off();
    enabled = 0;
    locked = 0;

    NVIC_SetPriority(EXTI4_15_IRQn, 0);  /* VSYNC: chooses the pulse width */
    NVIC_SetPriority(TIM1_CC_IRQn, 0);   /* HSYNC lost (same level: both touch CCMR1) */
}

void csync_enable(int on)
{
    const uint32_t vs_mask = 1u << VS_PIN;

    if ((on != 0) == (enabled != 0)) {
        return;
    }

    if (!on) {
        NVIC_DisableIRQ(EXTI4_15_IRQn);
        NVIC_DisableIRQ(TIM1_CC_IRQn);
        EXTI->IMR1 &= ~vs_mask;
        EXTI->RTSR1 &= ~vs_mask;
        EXTI->FTSR1 &= ~vs_mask;
        __disable_irq();
        stop_output();
        __enable_irq();
        TIM1->CR1 = 0;
        TIM1->SMCR = 0;
        enabled = 0;
        pins_off();
        return;
    }

    /* Sync inputs: weak pulls hold them inactive until the DPI output drives them. */
    gpio_pull(GPIOA, HS_PIN, INPUT_IDLE_PULL);
    gpio_pull(GPIOA, VS_PIN, CSYNC_VSYNC_ACTIVE_LOW ? PULL_UP : PULL_DOWN);
    gpio_af(GPIOA, HS_PIN, TIM_AF);
    gpio_mode(GPIOA, VS_PIN, PIN_INPUT);

    TIM1->CR1 = 0;  /* URS = 0: the slave-mode restart also loads the preloaded CCR2 */
    TIM1->PSC = 0;
    TIM1->ARR = 0xFFFFu;
    TIM1->CCMR1 = TIM_CCMR1_CC1S_0 | ((uint32_t)ic_filter << TIM_CCMR1_IC1F_Pos)
                | OC2M_FORCE_INACTIVE | TIM_CCMR1_OC2PE;
    TIM1->CCMR2 = 0;  /* CH3: frozen compare, only for the "HSYNC lost" interrupt */
    TIM1->CCER = TIM_CCER_CC1E | (CSYNC_HSYNC_ACTIVE_LOW ? TIM_CCER_CC1P : 0u)
               | TIM_CCER_CC2E | (CSYNC_ACTIVE_LOW ? TIM_CCER_CC2P : 0u);
    TIM1->CR2 = CSYNC_ACTIVE_LOW ? TIM_CR2_OIS2 : 0u;  /* idle level should MOE ever drop */
    TIM1->BDTR = TIM_BDTR_MOE | TIM_BDTR_OSSI;
    TIM1->CCR2 = 0;
    TIM1->DIER = 0;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->SMCR = 0;  /* TS may only change while the slave mode is off */
    TIM1->SMCR = TS_TI1FP1;
    TIM1->SMCR = TS_TI1FP1 | SMS_RESET;
    TIM1->SR = 0;
    TIM1->CR1 = TIM_CR1_CEN;
    gpio_af(GPIOA, CS_PIN, TIM_AF);  /* TIM1_CH2 takes the pin, forced inactive (high) */

    /* VSYNC: EXTI6 on port A, both edges. */
    EXTI->EXTICR[1] &= ~EXTI_EXTICR2_EXTI6;
    EXTI->RTSR1 |= vs_mask;
    EXTI->FTSR1 |= vs_mask;
    EXTI->RPR1 = vs_mask;
    EXTI->FPR1 = vs_mask;
    EXTI->IMR1 |= vs_mask;

    good_samples = 0;
    sample_acc = 0;
    locked = 0;
    vs_edges = 0;
    vs_update();
    enabled = 1;

    NVIC_ClearPendingIRQ(EXTI4_15_IRQn);
    NVIC_ClearPendingIRQ(TIM1_CC_IRQn);
    NVIC_EnableIRQ(EXTI4_15_IRQn);
    NVIC_EnableIRQ(TIM1_CC_IRQn);
}

void csync_poll(void)
{
    if (!enabled || (TIM1->SR & TIM_SR_CC1IF) == 0u) {
        return;
    }
    const uint32_t period = TIM1->CCR1;  /* clears CC1IF; skipped lines do not matter */
    TIM1->SR = ~TIM_SR_CC1OF;

    if (!locked) {
        if (period + LOCK_TOLERANCE < NOMINAL_PERIOD || period > NOMINAL_PERIOD + LOCK_TOLERANCE) {
            good_samples = 0;
            sample_acc = 0;
            return;
        }
        sample_acc += period;
        if (++good_samples >= LOCK_SAMPLES) {
            const uint32_t average = (sample_acc + LOCK_SAMPLES / 2u) / LOCK_SAMPLES;
            good_samples = 0;
            sample_acc = 0;
            start_output(average);
        }
        return;
    }

    if (period < period_min) {
        period_min = (uint16_t)period;
    }
    if (period > period_max) {
        period_max = (uint16_t)period;
    }

    /* Follow the line period (oscillator drift); stop if the timing changes. */
    const uint32_t ref = (period_acc + 8u) / 16u;
    const uint32_t tolerance = 2u + ref / 64u;
    if (period + tolerance < ref || period > ref + tolerance) {
        if (++bad_lines >= RELOCK_BAD) {
            __disable_irq();
            stop_output();
            losses++;
            __enable_irq();
        }
        return;
    }
    bad_lines = 0;
    period_acc = period_acc - period_acc / 16u + period;

    const uint32_t average = (period_acc + 8u) / 16u;
    const uint32_t sync = sync_ticks(average);
    if (sync != pulse_normal || average - sync != pulse_broad) {
        __disable_irq();
        pulse_normal = (uint16_t)sync;
        pulse_broad = (uint16_t)(average - sync);
        TIM1->CCR2 = pulse_width();  /* preloaded: from the next HSYNC leading edge */
        __enable_irq();
    }
}

void csync_get_status(csync_status_t *st)
{
    __disable_irq();
    st->enabled = enabled;
    st->locked = locked;
    st->vsync_seen = enabled && vs_edges != 0u && board_millis() - vs_last_ms < VSYNC_SEEN_MS;
    st->period_ticks = locked ? (uint16_t)((period_acc + 8u) / 16u) : 0u;
    st->sync_ticks = locked ? pulse_normal : 0u;
    st->broad_ticks = locked ? pulse_broad : 0u;
    st->locks = locks;
    st->losses = losses;
    st->filter = ic_filter;
    st->vs_pos_min = vs_pos_min == 0xFFFFu ? 0u : vs_pos_min;
    st->vs_pos_max = vs_pos_max;
    st->vs_now = vs_now;
    st->vs_next = vs_next;
    st->vs_waited = vs_waited;
    st->period_min = period_min == 0xFFFFu ? 0u : period_min;
    st->period_max = period_max;
    vs_pos_min = 0xFFFFu;
    vs_pos_max = 0;
    vs_now = 0;
    vs_next = 0;
    vs_waited = 0;
    period_min = 0xFFFFu;
    period_max = 0;
    __enable_irq();
}

void csync_set_filter(uint32_t filter)
{
    if (filter > 15u) {
        return;
    }
    __disable_irq();
    ic_filter = (uint8_t)filter;
    if (enabled) {
        TIM1->CCMR1 = (TIM1->CCMR1 & ~TIM_CCMR1_IC1F) | (filter << TIM_CCMR1_IC1F_Pos);
    }
    __enable_irq();
}

void TIM1_CC_IRQHandler(void)
{
    if ((TIM1->SR & TIM_SR_CC3IF) != 0u) {
        TIM1->SR = ~TIM_SR_CC3IF;
        if (locked) {
            stop_output();  /* HSYNC stopped: CSYNC idle until it is back */
            losses++;
        }
    }
}

void EXTI4_15_IRQHandler(void)
{
    const uint32_t mask = 1u << VS_PIN;
    if (((EXTI->RPR1 | EXTI->FPR1) & mask) == 0u) {
        return;
    }
    EXTI->RPR1 = mask;
    EXTI->FPR1 = mask;

    vs_update();
    vs_edges++;
    vs_last_ms = board_millis();
    if (!locked) {
        return;
    }

    uint32_t cnt = TIM1->CNT;
    if (cnt < vs_pos_min) {
        vs_pos_min = (uint16_t)cnt;
    }
    if (cnt > vs_pos_max) {
        vs_pos_max = (uint16_t)cnt;
    }

    /* Right before the next HSYNC leading edge, whether a preloaded width still makes it into
     * the next line depends on a few CPU cycles, so the broad pulses could start one line
     * later in some frames than in others. Wait for the line to start (at most END_MARGIN)
     * and apply the change to it. */
    const uint32_t period = (uint32_t)pulse_normal + pulse_broad;
    if (cnt + END_MARGIN >= period) {
        const uint32_t start = cnt;
        for (uint32_t guard = 0; TIM1->CNT >= start && guard < 2000u; guard++) {
        }
        cnt = TIM1->CNT;
        vs_waited++;
    }

    const uint32_t width = pulse_width();
    if (cnt + APPLY_MARGIN < pulse_normal) {
        /* Still in the HSYNC part of the current pulse: change this pulse already. */
        TIM1->CCMR1 &= ~TIM_CCMR1_OC2PE;
        TIM1->CCR2 = width;
        TIM1->CCMR1 |= TIM_CCMR1_OC2PE;
        vs_now++;
    } else {
        TIM1->CCR2 = width;  /* preloaded: from the next HSYNC leading edge */
        vs_next++;
    }
}
