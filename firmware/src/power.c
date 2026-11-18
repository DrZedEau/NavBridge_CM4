/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Raspberry Pi power.
 *
 * - The 5 V buck (LMR14030) is off at power-on. The ignition message (KL-15 on) turns it on.
 * - It turns off again once the CM 3.3 V rail (+3V3_SENSE) has been seen and then stays low
 *   for POWER_BUCK_OFF_DELAY_MS: the Pi was shut down (it decides that itself, from the
 *   ignition-off message it receives over USB). If the rail never shows up within
 *   POWER_BUCK_START_TIMEOUT_MS, the buck is turned off too, so it cannot stay on forever.
 * - The MCU stays powered from the car while the CM is off. USB and CSYNC are only active
 *   while the CM 3.3 V rail is present, so the MCU never back-powers the USB hub, the video
 *   DAC or the CM's DPI pins. With the CM off the navigation picture is selected.
 * - Parked car: with the CM and the buck off and the I-Bus quiet for IBUS_SLEEP_TIMEOUT_MS,
 *   both LIN transceivers go to Sleep and the MCU, powered by the car-side one, switches
 *   off. The next I-Bus activity wakes the transceiver, which powers the MCU up again.
 */
#include "power.h"
#include "board.h"
#include "config.h"
#include "csync.h"
#include "hardware.h"
#include "ibus.h"
#include "ibus_msg.h"
#include "usb_cdc.h"

static uint8_t cm_on;          /* debounced +3V3_SENSE */
static uint8_t sense_raw;
static uint32_t sense_since_ms;
static uint8_t cm_seen;        /* the CM came up since the buck was turned on */
static uint32_t buck_on_ms;
static uint32_t cm_off_ms;

static void buck_set(int on)
{
    if (on && !hw_buck_on()) {
        buck_on_ms = board_millis();
        cm_seen = cm_on;
    }
    hw_set_buck(on);
}

void power_init(void)
{
    /* The buck may already be on after a warm reset: the Pi then runs from it. */
    buck_on_ms = board_millis();
    cm_seen = 0;
    cm_on = 0;
    sense_raw = (uint8_t)hw_cm_3v3_present();
    sense_since_ms = board_millis();
    cm_off_ms = board_millis();
}

void power_ignition(uint8_t status)
{
    if ((status & IBUS_IGN_KL15) != 0u) {
        buck_set(1);
    }
}

int power_cm_on(void)
{
    return cm_on;
}

static void cm_changed(int on)
{
    cm_on = (uint8_t)on;
    if (on) {
        csync_enable(1);
        usb_cdc_connect();
    } else {
        cm_off_ms = board_millis();
        usb_cdc_disconnect();
        csync_enable(0);
        hw_set_rgb(RGB_NAV);
    }
}

void power_poll(void)
{
    const uint32_t now = board_millis();
    const uint8_t raw = (uint8_t)hw_cm_3v3_present();

    if (raw != sense_raw) {
        sense_raw = raw;
        sense_since_ms = now;
    } else if (raw != cm_on && now - sense_since_ms >= POWER_SENSE_DEBOUNCE_MS) {
        cm_changed(raw);
    }

    /* Parked: the MCU runs from the car-side LIN transceiver's regulator (CM off, buck off)
     * and the I-Bus has been quiet: put both transceivers to sleep, which switches the MCU
     * off until the next I-Bus activity (see ibus_sleep). */
    if (!hw_buck_on() && !cm_on && !raw && ibus_lin_on() && !ibus_busy()
        && ibus_idle_ms() >= IBUS_SLEEP_TIMEOUT_MS) {
        ibus_sleep();
        return;  /* only reached if the MCU stayed powered */
    }

    if (!hw_buck_on()) {
        return;
    }
    if (cm_on) {
        cm_seen = 1;
    } else if (cm_seen) {
        if (now - cm_off_ms >= POWER_BUCK_OFF_DELAY_MS) {
            hw_set_buck(0);  /* the Pi has shut down */
        }
    } else if (now - buck_on_ms >= POWER_BUCK_START_TIMEOUT_MS) {
        hw_set_buck(0);  /* the CM never came up */
    }
}
