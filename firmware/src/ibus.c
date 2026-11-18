/* SPDX-License-Identifier: GPL-2.0-only
 *
 * I-Bus: 9600 baud, 8 data bits, even parity, 1 stop bit, on a single-wire bus driven by
 * LIN transceivers. Frame: source, length (bytes that follow), destination, data...,
 * checksum (XOR of all the bytes is 0).
 *
 * Receive: every byte goes through a ring buffer to the main loop, which cuts frames out of
 * the stream by their length byte and checksum. A byte with a parity, framing, noise or
 * overrun error ends whatever was being assembled. When the bus goes quiet with bytes that
 * make no frame, they are dropped one at a time until the rest lines up again.
 *
 * Transmit: a frame waits until the bus has been quiet for IBUS_IDLE_US. The transceiver
 * echoes the bus on RXD, so each byte is checked as it comes back and the next one is only
 * sent then. A byte that comes back different means someone else was talking: we stop, keep
 * what was heard as received data, and send the whole frame again later. Our own echo is
 * never taken as a received frame.
 */
#include "ibus.h"
#include <string.h>
#include "board.h"
#include "config.h"
#include "hardware.h"

#define BAUD     9600u
#define RX_RING  256u      /* power of two */
#define RX_ERROR 0x100u

#define USART_ERRORS (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE)
#define USART_ERROR_CLEAR (USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF)

enum { TX_IDLE, TX_BUSY, TX_DONE, TX_FAILED, TX_TIMEOUT };

typedef struct {
    USART_TypeDef *uart;
    IRQn_Type irq;

    /* Received bytes, from the interrupt to the main loop. */
    volatile uint16_t rx_ring[RX_RING];
    volatile uint32_t rx_head;
    volatile uint32_t rx_tail;
    volatile uint8_t rx_overflow;
    volatile uint32_t last_rx_us;

    /* Frame being assembled. */
    uint8_t buf[IBUS_MAX_FRAME];
    uint32_t len;

    /* Frames waiting to be sent; the oldest one is on the wire while tx_state != TX_IDLE. */
    uint8_t queue[IBUS_TX_QUEUE][IBUS_MAX_FRAME];
    uint8_t queue_len[IBUS_TX_QUEUE];
    uint32_t q_head;
    uint32_t q_tail;
    uint32_t q_count;
    volatile uint8_t tx_state;
    volatile uint32_t tx_pos;
    uint32_t tx_len;
    const uint8_t *tx_buf;
    volatile uint32_t tx_last_us;
    uint32_t retries;
    uint32_t hold_start_us;
    uint32_t hold_us;

    ibus_stats_t stats;
} bus_t;

static bus_t buses[IBUS_BUSES];
static ibus_frame_handler_t frame_handler;
static uint32_t last_activity_ms;  /* last byte heard or sent on either bus */

static inline void rx_push(bus_t *b, uint16_t value)
{
    const uint32_t head = b->rx_head;
    if (head - b->rx_tail >= RX_RING) {
        b->rx_overflow = 1;
        return;
    }
    b->rx_ring[head & (RX_RING - 1u)] = value;
    b->rx_head = head + 1u;
}

static void uart_isr(bus_t *b)
{
    USART_TypeDef *const u = b->uart;
    const uint32_t isr = u->ISR;
    const uint32_t errors = isr & USART_ERRORS;

    if (errors != 0u) {
        u->ICR = USART_ERROR_CLEAR;
    }
    if ((isr & USART_ISR_RXNE_RXFNE) == 0u) {
        if (errors != 0u) {
            rx_push(b, RX_ERROR);
        }
        return;
    }

    const uint8_t byte = (uint8_t)u->RDR;  /* 8 data bits; bit 8 would be the parity */
    const uint32_t now = board_micros();
    b->last_rx_us = now;

    if (b->tx_state == TX_BUSY) {
        if (errors == 0u && byte == b->tx_buf[b->tx_pos]) {
            b->tx_last_us = now;
            if (++b->tx_pos < b->tx_len) {
                u->TDR = b->tx_buf[b->tx_pos];
            } else {
                b->tx_state = TX_DONE;
            }
            return;  /* our own echo */
        }
        b->tx_state = TX_FAILED;  /* collision: what we heard belongs to someone else */
    }
    rx_push(b, (uint16_t)(byte | (errors != 0u ? RX_ERROR : 0u)));
}

void USART1_IRQHandler(void)
{
    uart_isr(&buses[IBUS_CAR]);
}

void USART2_IRQHandler(void)
{
    uart_isr(&buses[IBUS_NAV]);
}

static void drop(bus_t *b, uint32_t n)
{
    memmove(b->buf, b->buf + n, b->len - n);
    b->len -= n;
}

/* Cuts the frames found at the start of the buffer. At the end of a burst (bus quiet),
 * a start that cannot complete is not a frame: skip a byte and look again. */
static void extract(bus_t *b, uint32_t bus, int end_of_burst)
{
    while (b->len >= 2u) {
        const uint32_t length = b->buf[1];
        if (length < IBUS_MIN_LENGTH || length > IBUS_MAX_LENGTH) {
            drop(b, 1);
            continue;
        }
        const uint32_t total = length + 2u;
        if (b->len < total) {
            if (!end_of_burst) {
                return;
            }
            drop(b, 1);
            continue;
        }
        uint8_t sum = 0;
        for (uint32_t i = 0; i < total; i++) {
            sum ^= b->buf[i];
        }
        if (sum == 0u) {
            b->stats.rx_frames++;
            frame_handler(bus, b->buf, total);
            drop(b, total);
        } else {
            b->stats.bad_frames++;
            drop(b, 1);
        }
    }
    if (end_of_burst) {
        b->len = 0;
    }
}

static void rx_poll(bus_t *b, uint32_t bus)
{
    if (b->rx_overflow) {
        b->rx_overflow = 0;
        b->stats.rx_errors++;
        b->len = 0;
    }
    while (b->rx_tail != b->rx_head) {
        const uint16_t value = b->rx_ring[b->rx_tail & (RX_RING - 1u)];
        b->rx_tail = b->rx_tail + 1u;
        b->stats.rx_bytes++;
        last_activity_ms = board_millis();
        if ((value & RX_ERROR) != 0u) {
            b->stats.rx_errors++;
            b->len = 0;  /* no frame can include this byte */
            continue;
        }
        if (b->len >= IBUS_MAX_FRAME) {
            drop(b, 1);  /* not reached: extract() decides on any full buffer */
        }
        b->buf[b->len++] = (uint8_t)value;
        extract(b, bus, 0);
    }
    if (b->len != 0u && board_micros() - b->last_rx_us > IBUS_GAP_US) {
        extract(b, bus, 1);
    }
}

static void tx_pop(bus_t *b)
{
    b->q_tail = (b->q_tail + 1u) % IBUS_TX_QUEUE;
    b->q_count--;
    b->retries = 0;
}

static void tx_poll(bus_t *b)
{
    const uint32_t now = board_micros();

    switch (b->tx_state) {
    case TX_BUSY:
        if (now - b->tx_last_us > IBUS_ECHO_TIMEOUT_US) {
            /* No echo: transceiver disabled, or the bus held dominant. */
            NVIC_DisableIRQ(b->irq);
            if (b->tx_state == TX_BUSY) {
                b->tx_state = TX_TIMEOUT;
            }
            NVIC_EnableIRQ(b->irq);
        }
        return;
    case TX_DONE:
        b->stats.tx_frames++;
        tx_pop(b);
        b->tx_state = TX_IDLE;
        break;
    case TX_FAILED:
    case TX_TIMEOUT:
        if (b->tx_state == TX_TIMEOUT) {
            b->stats.echo_timeouts++;
        } else {
            b->stats.collisions++;
        }
        if (++b->retries > IBUS_TX_RETRIES) {
            b->stats.tx_dropped++;
            tx_pop(b);
        }
        /* Up to ~3 ms more than the usual quiet time, so that two senders that collided
         * do not start together again. */
        b->hold_start_us = now;
        b->hold_us = (((now >> 3) ^ (now >> 11)) & 0x3FFu) * 3u;
        b->tx_state = TX_IDLE;
        break;
    default:
        break;
    }

    if (b->q_count == 0u || now - b->hold_start_us < b->hold_us) {
        return;
    }
    if (b->len != 0u || b->rx_tail != b->rx_head || now - b->last_rx_us < IBUS_IDLE_US) {
        return;  /* bus busy, or a frame is coming in */
    }

    b->tx_buf = b->queue[b->q_tail];
    b->tx_len = b->queue_len[b->q_tail];
    b->tx_pos = 0;
    b->hold_us = 0;
    last_activity_ms = board_millis();
    NVIC_DisableIRQ(b->irq);
    b->tx_last_us = now;
    b->tx_state = TX_BUSY;
    b->uart->TDR = b->tx_buf[0];
    NVIC_EnableIRQ(b->irq);
}

int ibus_send(uint32_t bus, const uint8_t *frame, uint32_t len)
{
    bus_t *const b = &buses[bus];

    if (len < IBUS_MIN_LENGTH + 2u || len > IBUS_MAX_FRAME || b->q_count >= IBUS_TX_QUEUE) {
        b->stats.tx_dropped++;
        return 0;
    }
    memcpy(b->queue[b->q_head], frame, len);
    b->queue_len[b->q_head] = (uint8_t)len;
    b->q_head = (b->q_head + 1u) % IBUS_TX_QUEUE;
    b->q_count++;
    return 1;
}

/* ---- Transceiver supply --------------------------------------------------------------
 *
 * The ATA663231 run from +BATT. The car-side one (U8) also has the 3.3 V regulator that
 * feeds +3V3_LIN_REG, which the MCU's power mux (U11, TPS2117, PR1 on VIN1) prefers as soon
 * as it rises above about 1 V. With no 12 V (bench, USB-C only), driving EN or TXD high, or
 * pulling RXD up, feeds that rail through the transceiver's protection diodes: the mux
 * switches the MCU to it, the rail collapses and the MCU browns out, over and over.
 *
 * So the LIN pins stay floating (EN low through its pull-downs, RXD and TXD pulled down)
 * until U8 is known to be powered:
 *   - the CM 3.3 V rail is absent: then U8's regulator is what powers the MCU;
 *   - or U8 drives RXD or TXD high against the MCU pull-downs (Silent mode after a warm
 *     reset: both high). An unpowered U8 cannot.
 */
#define LIN_PROBE_MS    2u   /* back to Normal mode within ~6 ms of a wake-up */
#define LIN_PROBE_HITS  3u

static uint8_t lin_on;
static uint32_t probe_ms;
static uint8_t probe_hits;
static uint32_t sleep_failures;

static void lin_pins_off(void)
{
    gpio_pull(GPIOB, 6, PULL_DOWN);  /* LIN1 TX (U8 TXD) */
    gpio_pull(GPIOB, 7, PULL_DOWN);  /* LIN1 RX (U8 RXD) */
    gpio_pull(GPIOA, 4, PULL_DOWN);  /* LIN2 TX (U10 TXD) */
    gpio_pull(GPIOA, 5, PULL_DOWN);  /* LIN2 RX (U10 RXD) */
    gpio_mode(GPIOB, 6, PIN_INPUT);
    gpio_mode(GPIOB, 7, PIN_INPUT);
    gpio_mode(GPIOA, 4, PIN_INPUT);
    gpio_mode(GPIOA, 5, PIN_INPUT);
}

static int lin_supply_present(void)
{
    if (!hw_cm_3v3_present()) {
        return 1;
    }
    return gpio_read(GPIOB, 7) || gpio_read(GPIOB, 6);
}

static void lin_enable(void)
{
    /* EN first: out of Fail-safe or Silent mode into Normal mode. In Fail-safe mode TXD is
     * an output, so the MCU only drives it once the transceiver is in Normal mode. */
    hw_set_lin_enable(1);
    board_delay_us(200);

    gpio_pull(GPIOB, 6, PULL_NONE);
    gpio_pull(GPIOA, 4, PULL_NONE);
    gpio_pull(GPIOB, 7, PULL_UP);
    gpio_pull(GPIOA, 5, PULL_UP);
    gpio_af(GPIOB, 6, 0);  /* USART1_TX -> U8 TXD, idles high (recessive) */
    gpio_af(GPIOB, 7, 0);  /* USART1_RX <- U8 RXD */
    gpio_af(GPIOA, 4, 1);  /* USART2_TX -> U10 TXD */
    gpio_af(GPIOA, 5, 1);  /* USART2_RX <- U10 RXD */

    const uint32_t now = board_micros();
    for (uint32_t bus = 0; bus < IBUS_BUSES; bus++) {
        bus_t *const b = &buses[bus];
        NVIC_DisableIRQ(b->irq);
        b->uart->ICR = USART_ERROR_CLEAR;
        (void)b->uart->RDR;
        b->rx_tail = b->rx_head;  /* whatever came in while the pins were floating */
        b->len = 0;
        b->last_rx_us = now;
        NVIC_EnableIRQ(b->irq);
    }
    lin_on = 1;
}

static void lin_poll(void)
{
    const uint32_t now = board_millis();
    if (now - probe_ms < LIN_PROBE_MS) {
        return;
    }
    probe_ms = now;
    probe_hits = lin_supply_present() ? (uint8_t)(probe_hits + 1u) : 0u;
    if (probe_hits >= LIN_PROBE_HITS) {
        lin_enable();
    }
}

int ibus_lin_on(void)
{
    return lin_on;
}

uint32_t ibus_idle_ms(void)
{
    return board_millis() - last_activity_ms;
}

int ibus_busy(void)
{
    for (uint32_t bus = 0; bus < IBUS_BUSES; bus++) {
        const bus_t *const b = &buses[bus];
        if (b->q_count != 0u || b->tx_state != TX_IDLE || b->len != 0u || b->rx_tail != b->rx_head) {
            return 1;
        }
    }
    return 0;
}

/* ---- Sleep -----------------------------------------------------------------------------
 *
 * ATA663231 datasheet, section 1.2.3: a falling edge at EN while TXD is low switches the IC
 * into Sleep mode (VCC regulator off, LIN termination off, about 9 uA). EN may fall up to
 * 3.2 us before TXD, which avoids any dominant pulse on the bus; "the easiest and best way
 * is by having two falling edges at TXD and EN at the same time". A dominant LIN level for
 * more than 50..150 us (any I-Bus byte) wakes it into Fail-Safe mode with VCC back on, and
 * EN high then switches it to Normal mode.
 *
 * U8 (car side) has EN and TXD on the same port (PB5, PB6): one write drops both. U10 (nav
 * side) has EN on PB4 and TXD on PA4: EN first, TXD a few cycles later.
 */
#define LIN1_TX_PIN 6u  /* GPIOB */
#define LIN2_TX_PIN 4u  /* GPIOA */
#define SLEEP_POWER_WAIT_MS 500u

void ibus_sleep(void)
{
    if (!lin_on) {
        return;
    }
    for (uint32_t bus = 0; bus < IBUS_BUSES; bus++) {
        NVIC_DisableIRQ(buses[bus].irq);
    }

    /* TXD from the UART to GPIO, still high (recessive) as the UART left it. */
    gpio_write(GPIOA, LIN2_TX_PIN, 1);
    gpio_mode(GPIOA, LIN2_TX_PIN, PIN_OUTPUT);
    gpio_write(GPIOB, LIN1_TX_PIN, 1);
    gpio_mode(GPIOB, LIN1_TX_PIN, PIN_OUTPUT);

    /* Navigation side first. */
    __disable_irq();
    GPIOB->BSRR = 1u << (LIN2_EN_PIN + 16u);
    GPIOA->BSRR = 1u << (LIN2_TX_PIN + 16u);
    __enable_irq();
    board_delay_us(100);  /* mode change: 20 us at most */

    /* Car side: EN and TXD fall in the same write. Its regulator powers this MCU while the
     * CM is off, so the MCU switches off here. */
    lin_on = 0;
    GPIOB->BSRR = (1u << (LIN1_EN_PIN + 16u)) | (1u << (LIN1_TX_PIN + 16u));

    const uint32_t start = board_millis();
    while (board_millis() - start < SLEEP_POWER_WAIT_MS) {
    }

    /* Still running: something else powers the MCU (the CM came on). TXD back to recessive
     * before EN, so that the transceivers do not wake up with TXD low (dominant), then
     * Normal mode again. */
    gpio_write(GPIOB, LIN1_TX_PIN, 1);
    gpio_write(GPIOA, LIN2_TX_PIN, 1);
    board_delay_us(50);
    lin_enable();
    last_activity_ms = board_millis();
    sleep_failures++;
}

uint32_t ibus_sleep_failures(void)
{
    return sleep_failures;
}

void ibus_poll(void)
{
    if (!lin_on) {
        lin_poll();
        return;
    }
    for (uint32_t bus = 0; bus < IBUS_BUSES; bus++) {
        rx_poll(&buses[bus], bus);
        tx_poll(&buses[bus]);
    }
}

void ibus_get_stats(uint32_t bus, ibus_stats_t *stats)
{
    *stats = buses[bus].stats;
    stats->queued = buses[bus].q_count;
}

static void uart_init(bus_t *b)
{
    USART_TypeDef *const u = b->uart;

    u->CR1 = 0;
    u->BRR = (SYSCLK_HZ + BAUD / 2u) / BAUD;
    u->CR2 = 0;  /* 1 stop bit */
    u->CR3 = 0;  /* 3-sample majority, noise detection on */
    /* 9-bit word = 8 data bits + parity, even parity (PS = 0). */
    u->CR1 = USART_CR1_M0 | USART_CR1_PCE | USART_CR1_RXNEIE_RXFNEIE
           | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
    u->ICR = USART_ERROR_CLEAR;
}

void ibus_init(ibus_frame_handler_t handler)
{
    frame_handler = handler;
    memset(buses, 0, sizeof buses);
    buses[IBUS_CAR].uart = USART1;
    buses[IBUS_CAR].irq = USART1_IRQn;
    buses[IBUS_NAV].uart = USART2;
    buses[IBUS_NAV].irq = USART2_IRQn;

    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN;
    RCC->APBENR2 |= RCC_APBENR2_USART1EN;
    RCC->APBENR1 |= RCC_APBENR1_USART2EN;
    (void)RCC->APBENR1;

    /* The UARTs run, but only take their pins once the transceivers are powered (lin_poll). */
    uart_init(&buses[IBUS_CAR]);
    uart_init(&buses[IBUS_NAV]);
    lin_on = 0;
    probe_hits = 0;
    probe_ms = board_millis();
    last_activity_ms = board_millis();
    /* Back from the bootloader with EN still high: the transceivers were powered, and
     * pulling TXD down now would put a dominant glitch on the bus. */
    const int was_on = hw_lin_enabled();
    if (!was_on) {
        lin_pins_off();
    }

    const uint32_t now = board_micros();
    for (uint32_t bus = 0; bus < IBUS_BUSES; bus++) {
        buses[bus].last_rx_us = now;
        NVIC_SetPriority(buses[bus].irq, 1);
        NVIC_ClearPendingIRQ(buses[bus].irq);
        NVIC_EnableIRQ(buses[bus].irq);
    }
    if (was_on) {
        lin_enable();
    }
}
