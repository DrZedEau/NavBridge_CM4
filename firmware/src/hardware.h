/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Board I/O of the NavBridge CM4 (schematic sheet MCU_Lin, U9 = STM32C071KBT6):
 *
 *   PA1  (8)   STAT LED D6, active high                    always on
 *   PA2  (9)   MCU_BUCK_EN, LMR14030 5 V buck enable       10k pull-down, off at power-on
 *   PA3  (10)  +3V3_SENSE, CM 3.3 V rail through 1k        high while the CM is on
 *   PA4  (11)  MCU_LIN2_TX  USART2_TX AF1  -> U10 (navigation I-Bus)
 *   PA5  (12)  MCU_LIN2_RX  USART2_RX AF1  <- U10
 *   PA6  (13)  VSYNC        EXTI6          <- CM DPI
 *   PA8  (18)  HSYNC        TIM1_CH1 AF2   <- CM DPI
 *   PA9  (19)  CSYNC        TIM1_CH2 AF2   -> ADV7125 SYNC
 *   PA10 (21)  MCU_SWITCHER_IN, TMUX1574 SEL: low = factory navigation, high = Raspberry Pi
 *   PA11/PA12  USB D-/D+ to the USB2514B hub
 *   PA13/PA14  SWD; PA14 is also BOOT0 (BOOT button SW3 to 3.3 V through 1k)
 *   PB4  (28)  MCU_LIN2_EN, U10 EN (10k pull-down)
 *   PB5  (29)  MCU_LIN1_EN, U8 EN (10k pull-down)
 *   PB6  (30)  MCU_LIN1_TX  USART1_TX AF0  -> U8 (car I-Bus)
 *   PB7  (31)  MCU_LIN1_RX  USART1_RX AF0  <- U8
 */
#ifndef HARDWARE_H
#define HARDWARE_H

#include <stdint.h>

#define LED_PIN       1u   /* GPIOA */
#define BUCK_EN_PIN   2u   /* GPIOA */
#define SENSE_3V3_PIN 3u   /* GPIOA */
#define RGB_SEL_PIN   10u  /* GPIOA */
#define BOOT0_PIN     14u  /* GPIOA */
#define LIN2_EN_PIN   4u   /* GPIOB */
#define LIN1_EN_PIN   5u   /* GPIOB */

typedef enum {
    RGB_NAV = 0,  /* factory navigation picture (TMUX1574 SEL low) */
    RGB_RPI = 1,  /* Raspberry Pi picture through the ADV7125 */
} rgb_source_t;

/* On the reset clock, before anything else: LED on, buck back on if it was on before a warm
 * reset (the Raspberry Pi may be running from it), RGB switch on the navigation picture. */
void hw_early_init(void);


void hw_set_led(int on);
void hw_set_buck(int on);
int hw_buck_on(void);

void hw_set_rgb(rgb_source_t source);
rgb_source_t hw_rgb(void);

int hw_cm_3v3_present(void);  /* raw +3V3_SENSE level */

void hw_set_lin_enable(int on);  /* both ATA663231, normal mode when on */
int hw_lin_enabled(void);        /* EN still driven high from before a jump */

/* BOOT button (SW3) held at RESET. */
int hw_boot_button_held(void);

/* Before the firmware jumps to the bootloader: pins the bootloader does not use in a quiet
 * state (LIN TX recessive, navigation picture on screen, CSYNC released). */
void hw_prepare_bootloader(void);

#endif
