/* SPDX-License-Identifier: GPL-2.0-only
 *
 * MCU support: clocks, time, GPIO helpers and the jump to the ST USB DFU bootloader.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>
#include "stm32c0xx.h"

#define SYSCLK_HZ 48000000u

#define PIN_INPUT  0u
#define PIN_OUTPUT 1u
#define PIN_AF     2u
#define PIN_ANALOG 3u

#define PULL_NONE 0u
#define PULL_UP   1u
#define PULL_DOWN 2u

static inline void gpio_mode(GPIO_TypeDef *port, uint32_t pin, uint32_t mode)
{
    port->MODER = (port->MODER & ~(3u << (pin * 2u))) | (mode << (pin * 2u));
}

static inline void gpio_pull(GPIO_TypeDef *port, uint32_t pin, uint32_t pull)
{
    port->PUPDR = (port->PUPDR & ~(3u << (pin * 2u))) | (pull << (pin * 2u));
}

static inline void gpio_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
{
    const uint32_t shift = (pin & 7u) * 4u;
    port->AFR[pin >> 3] = (port->AFR[pin >> 3] & ~(15u << shift)) | (af << shift);
    gpio_mode(port, pin, PIN_AF);
}

static inline void gpio_write(GPIO_TypeDef *port, uint32_t pin, int high)
{
    port->BSRR = high ? (1u << pin) : (1u << (pin + 16u));
}

static inline int gpio_read(const GPIO_TypeDef *port, uint32_t pin)
{
    return (int)((port->IDR >> pin) & 1u);
}

/* First thing in main(), still on the reset clock: reset flags, backup register access. */
void board_early_init(void);
int board_pin_reset(void);          /* RESET button or power-on, not a software reset */
uint32_t board_reset_flags(void);   /* RCC_CSR2 flags at start (0 after a jump) */
int board_boot0_pin_enabled(void);  /* option nBOOT_SEL = 0 (factory: 1, pin ignored) */

void board_clock_init(void);   /* SYSCLK = HSI48 = 48 MHz, no crystal */
void board_init(void);         /* puts back what the other program left, starts the timers */

uint32_t board_micros(void);   /* free running, 1 MHz (TIM2, 32 bits) */
uint32_t board_millis(void);   /* since start, 1 kHz (SysTick) */
void board_delay_us(uint32_t us);

/* Start the other program (bootloader or firmware) whose vector table is at vector_base:
 * release USB (the Pi sees the device leave), stop the peripherals and jump. GPIO keeps its
 * state, so the buck stays on. Does not return. */
void board_jump(uint32_t vector_base) __attribute__((noreturn));

#endif
