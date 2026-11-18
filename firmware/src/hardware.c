/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Board I/O: LED, buck enable, RGB switch, CM 3.3 V sense, LIN transceiver enables.
 */
#include "hardware.h"
#include "board.h"

/* PWR_BKP0R is cleared by power-on and brown-out resets only, and keeps 16 bits. It
 * remembers that the buck was on, so that a warm reset (RESET button, crash) does not cut
 * the Raspberry Pi's 5 V while it runs from the car. */
#define BKP_MAGIC   0x4E00u
#define BKP_BUCK_ON 0x0001u

#define LIN1_TX_PIN 6u  /* GPIOB */
#define LIN2_TX_PIN 4u  /* GPIOA */
#define VSYNC_PIN   6u  /* GPIOA */
#define HSYNC_PIN   8u  /* GPIOA */
#define CSYNC_PIN   9u  /* GPIOA */

void hw_early_init(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN;
    (void)RCC->IOPENR;

    gpio_write(GPIOA, LED_PIN, 1);
    gpio_mode(GPIOA, LED_PIN, PIN_OUTPUT);

    /* After a power-on the buck starts off. After a warm reset it stays as it was
     * (PWR_BKP0R). Started by the other program with a jump, the pin is still driven high
     * if the buck was on: keep it so, whatever the backup register says. */
    const int driven_high = ((GPIOA->MODER >> (BUCK_EN_PIN * 2u)) & 3u) == PIN_OUTPUT
                         && ((GPIOA->ODR >> BUCK_EN_PIN) & 1u) != 0u;
    const int keep_buck = driven_high || (PWR->BKP0R & 0xFFFFu) == (BKP_MAGIC | BKP_BUCK_ON);
    gpio_write(GPIOA, BUCK_EN_PIN, keep_buck);
    gpio_mode(GPIOA, BUCK_EN_PIN, PIN_OUTPUT);

    gpio_write(GPIOA, RGB_SEL_PIN, 0);
    gpio_mode(GPIOA, RGB_SEL_PIN, PIN_OUTPUT);

    gpio_pull(GPIOA, SENSE_3V3_PIN, PULL_DOWN);
    gpio_mode(GPIOA, SENSE_3V3_PIN, PIN_INPUT);

    /* The LIN pins are left alone here, see ibus.c: they are only driven once the car-side
     * ATA663231 is known to be powered. Pulling EN low while TXD is low would also put it to
     * sleep, and its regulator is what powers this MCU while the CM is off. */
}

int hw_boot_button_held(void)
{
    /* Only after RESET or power-on: started by the ST bootloader or by a software reset,
     * PA14 may be in another state, and the button cannot be what brought us here. */
    if (!board_pin_reset()) {
        return 0;
    }
    /* PA14 is SWCLK with its internal pull-down; the BOOT button pulls it to 3.3 V. The ST
     * bootloader may have changed the pin (a blank chip's first start keeps its power-on
     * flags), so put back the reset configuration first. Require it high for about 2 ms so
     * that SWD clocking does not count as a press. */
    gpio_pull(GPIOA, BOOT0_PIN, PULL_DOWN);
    gpio_af(GPIOA, BOOT0_PIN, 0);
    for (volatile uint32_t d = 0; d < 100u; d++) {
    }
    for (uint32_t i = 0; i < 64u; i++) {
        if (!gpio_read(GPIOA, BOOT0_PIN)) {
            return 0;
        }
        for (volatile uint32_t d = 0; d < 100u; d++) {
        }
    }
    return 1;
}

void hw_set_led(int on)
{
    gpio_write(GPIOA, LED_PIN, on);
}

void hw_set_buck(int on)
{
    PWR->BKP0R = BKP_MAGIC | (on ? BKP_BUCK_ON : 0u);
    gpio_write(GPIOA, BUCK_EN_PIN, on);
}

int hw_buck_on(void)
{
    return (int)((GPIOA->ODR >> BUCK_EN_PIN) & 1u);
}

void hw_set_rgb(rgb_source_t source)
{
    gpio_write(GPIOA, RGB_SEL_PIN, source == RGB_RPI);
}

rgb_source_t hw_rgb(void)
{
    return ((GPIOA->ODR >> RGB_SEL_PIN) & 1u) ? RGB_RPI : RGB_NAV;
}

int hw_cm_3v3_present(void)
{
    return gpio_read(GPIOA, SENSE_3V3_PIN);
}

int hw_lin_enabled(void)
{
    /* Driven high: only an earlier run of the firmware does that (a reset floats them). */
    const uint32_t out1 = ((GPIOB->MODER >> (LIN1_EN_PIN * 2u)) & 3u) == PIN_OUTPUT;
    return out1 && ((GPIOB->ODR >> LIN1_EN_PIN) & 1u) != 0u;
}

void hw_set_lin_enable(int on)
{
    gpio_write(GPIOB, LIN1_EN_PIN, on);
    gpio_write(GPIOB, LIN2_EN_PIN, on);
    gpio_mode(GPIOB, LIN1_EN_PIN, PIN_OUTPUT);
    gpio_mode(GPIOB, LIN2_EN_PIN, PIN_OUTPUT);
}

void hw_prepare_bootloader(void)
{
    /* TXD floating: the transceiver's own pull-up holds it recessive. No MCU pull-up: with
     * no 12 V the transceiver is unpowered, and a pull-up would feed +3V3_LIN_REG. */
    gpio_pull(GPIOB, LIN1_TX_PIN, PULL_NONE);
    gpio_mode(GPIOB, LIN1_TX_PIN, PIN_ANALOG);
    gpio_pull(GPIOA, LIN2_TX_PIN, PULL_NONE);
    gpio_mode(GPIOA, LIN2_TX_PIN, PIN_ANALOG);

    hw_set_rgb(RGB_NAV);

    gpio_pull(GPIOA, VSYNC_PIN, PULL_NONE);
    gpio_pull(GPIOA, HSYNC_PIN, PULL_NONE);
    gpio_mode(GPIOA, VSYNC_PIN, PIN_ANALOG);
    gpio_mode(GPIOA, HSYNC_PIN, PIN_ANALOG);
    gpio_mode(GPIOA, CSYNC_PIN, PIN_ANALOG);
}
