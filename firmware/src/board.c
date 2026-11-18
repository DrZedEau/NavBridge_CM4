/* SPDX-License-Identifier: GPL-2.0-only
 *
 * MCU support shared by the bootloader and the firmware: clocks, time, and the jump from
 * one program to the other.
 */
#include "board.h"

static volatile uint32_t millis_count;
static uint32_t reset_flags;

/* Called by __libc_init_array from ST's startup code; it normally comes from crti.o, which
 * is not linked (-nostartfiles). Nothing needs it here. */
void _init(void)
{
}

void board_early_init(void)
{
    /* We run from the main flash, so it is not empty. A blank chip boots the ST bootloader
     * (empty check) and keeps that "empty" flag until the next power-on, even once
     * programmed over DFU: clear it, or the next reset would start the ST bootloader. */
    FLASH->ACR &= ~FLASH_ACR_PROGEMPTY;

    /* PWR holds the backup registers: buck state, update request. */
    RCC->APBENR1 |= RCC_APBENR1_PWREN;
    (void)RCC->APBENR1;

    /* Started by the other program with a jump, the flags are those it cleared: 0. */
    reset_flags = RCC->CSR2 & 0xFE000000u;
    if ((reset_flags & RCC_CSR2_PWRRSTF) != 0u) {
        PWR->BKP0R = 0;  /* power-on or brown-out reset: nothing to keep */
        PWR->BKP1R = 0;
    }
    RCC->CSR2 |= RCC_CSR2_RMVF;
}

int board_pin_reset(void)
{
    /* An internal reset also drives NRST, so it sets PINRSTF too: exclude software resets. */
    return (reset_flags & RCC_CSR2_PINRSTF) != 0u && (reset_flags & RCC_CSR2_SFTRSTF) == 0u;
}

uint32_t board_reset_flags(void)
{
    return reset_flags;
}

int board_boot0_pin_enabled(void)
{
    return (FLASH->OPTR & FLASH_OPTR_nBOOT_SEL) == 0u;
}

void board_clock_init(void)
{
    /* Whatever ran before: SYSCLK = HSISYS = HSI48 / 1, AHB and APB undivided. */
    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0u) {
    }

    /* One flash wait state is required above 24 MHz; set it before speeding up. */
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_0
               | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN;
    while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_0) {
    }

    RCC->CFGR &= ~(RCC_CFGR_SW | RCC_CFGR_HPRE | RCC_CFGR_PPRE);
    while ((RCC->CFGR & RCC_CFGR_SWS) != 0u) {
    }
    RCC->CR &= ~(RCC_CR_HSIDIV | RCC_CR_SYSDIV);

    /* No crystal on the board. */
    RCC->CR &= ~(RCC_CR_CSSON | RCC_CR_HSEON);
    RCC->CR &= ~RCC_CR_HSEBYP;

    /* ST's SystemCoreClockUpdate() does not recognise every clock source of the C071. */
    SystemCoreClock = SYSCLK_HZ;
}

static void peripherals_off(void)
{
    /* Back to their reset state. GPIO is not touched (the buck enable, LED and LIN enables
     * keep their level), nor PWR (it holds the backup registers). */
    RCC->APBRSTR1 = RCC_APBRSTR1_TIM2RST | RCC_APBRSTR1_TIM3RST | RCC_APBRSTR1_USBRST
                  | RCC_APBRSTR1_CRSRST | RCC_APBRSTR1_USART2RST;
    RCC->APBRSTR1 = 0;
    RCC->APBRSTR2 = RCC_APBRSTR2_TIM1RST | RCC_APBRSTR2_USART1RST | RCC_APBRSTR2_SYSCFGRST;
    RCC->APBRSTR2 = 0;
    NVIC->ICER[0] = 0xFFFFFFFFu;
    NVIC->ICPR[0] = 0xFFFFFFFFu;
    EXTI->IMR1 &= ~0x0007FFFFu;  /* configurable lines 0..18 */
    EXTI->RTSR1 = 0;
    EXTI->FTSR1 = 0;
    EXTI->RPR1 = 0x0007FFFFu;
    EXTI->FPR1 = 0x0007FFFFu;
    for (uint32_t i = 0; i < 4u; i++) {
        EXTI->EXTICR[i] = 0;
    }
}

void board_init(void)
{
    /* The other program may have left peripherals running. */
    peripherals_off();

    /* 1 kHz tick: board_millis(), and it wakes the main loop from WFI. Lowest priority. */
    SysTick_Config(SYSCLK_HZ / 1000u);

    /* TIM2 (32 bits) as a 1 MHz free-running counter: board_micros(). */
    RCC->APBENR1 |= RCC_APBENR1_TIM2EN;
    (void)RCC->APBENR1;
    TIM2->PSC = (SYSCLK_HZ / 1000000u) - 1u;
    TIM2->ARR = 0xFFFFFFFFu;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 = TIM_CR1_CEN;

    /* Started by a jump, interrupts are still masked. Nothing but SysTick is enabled yet. */
    __enable_irq();
}

void SysTick_Handler(void)
{
    millis_count++;
}

uint32_t board_millis(void)
{
    return millis_count;
}

uint32_t board_micros(void)
{
    return TIM2->CNT;
}

void board_delay_us(uint32_t us)
{
    const uint32_t start = board_micros();
    while (board_micros() - start < us) {
    }
}

void board_jump(uint32_t vector_base)
{
    __disable_irq();
    SysTick->CTRL = 0;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    /* Release D+ so that the Pi sees the device leave before the other program connects. */
    if ((RCC->APBENR1 & RCC_APBENR1_USBEN) != 0u) {
        USB_DRD_FS->BCDR &= ~USB_BCDR_DPPU;
        USB_DRD_FS->CNTR = USB_CNTR_USBRST | USB_CNTR_PDWN;
        /* About 50 ms at 48 MHz: no timer needed. */
        for (volatile uint32_t i = 0; i < 400000u; i++) {
        }
    }

    peripherals_off();
    RCC->APBENR1 &= ~(RCC_APBENR1_TIM2EN | RCC_APBENR1_TIM3EN | RCC_APBENR1_USBEN
                      | RCC_APBENR1_CRSEN | RCC_APBENR1_USART2EN);
    RCC->APBENR2 &= ~(RCC_APBENR2_TIM1EN | RCC_APBENR2_USART1EN);
    RCC->CR &= ~RCC_CR_HSIUSB48ON;

    /* Interrupts stay masked: the other program's board_init() unmasks them. */
    const volatile uint32_t *vectors = (const volatile uint32_t *)vector_base;
    const uint32_t sp = vectors[0];
    const uint32_t pc = vectors[1];
    SCB->VTOR = vector_base;
    __asm volatile("msr msp, %0\n\tbx %1" : : "r"(sp), "r"(pc) : "memory");
    for (;;) {
    }
}
