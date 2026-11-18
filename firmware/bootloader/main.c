/* SPDX-License-Identifier: GPL-2.0-only
 *
 * NavBridge CM4 bootloader (first 14 KB of flash, see src/layout.h).
 *
 * At every reset it starts the firmware at APP_BASE right away, unless:
 *   - the firmware asked for an update (DFU command from the Pi: PWR_BKP1R),
 *   - BOOT (SW3) is held while RESET (SW2) is pressed,
 *   - there is no valid firmware (blank, interrupted update, corrupted).
 * Then it is a USB DFU device (0483:5741) that the Pi writes with dfu-util, the STAT LED
 * blinks fast, and the new firmware starts when the download is complete. Entered from a
 * valid firmware, it goes back to it after a minute without an update.
 *
 * It keeps the board safe like the firmware does: the buck stays as it was, the LIN pins
 * are never driven, and USB is only on while the CM 3.3 V rail is present.
 */
#include "board.h"
#include "config.h"
#include "dfu.h"
#include "flash.h"
#include "hardware.h"
#include "layout.h"

#define SENSE_DEBOUNCE_MS 50u
#define IDLE_TIMEOUT_MS   60000u
#define LED_HALF_PERIOD   125u

int main(void)
{
    board_early_init();
    hw_early_init();

    const int requested = (PWR->BKP1R & 0xFFFFu) == BOOTLOADER_REQUEST;
    PWR->BKP1R = 0;
    const int button = hw_boot_button_held();
    if (!requested && !button && app_valid()) {
        board_jump(APP_BASE);
    }

    board_clock_init();
    board_init();

    int cm_on = 0;
    int sense = hw_cm_3v3_present();
    uint32_t sense_since = board_millis();

    for (;;) {
        const uint32_t now = board_millis();

        /* USB only while the CM is powered: no current into the unpowered hub. */
        const int raw = hw_cm_3v3_present();
        if (raw != sense) {
            sense = raw;
            sense_since = now;
        } else if (raw != cm_on && now - sense_since >= SENSE_DEBOUNCE_MS) {
            cm_on = raw;
            if (cm_on) {
                dfu_connect();
            } else {
                dfu_disconnect();
            }
        }

        dfu_poll();  /* may stall for a flash write: take the time again after it */

        if (dfu_start_requested() && app_valid()) {
            board_jump(APP_BASE);
        }
        /* Signed: a request seen during the flash write is newer than `now`. */
        const int32_t idle = (int32_t)(board_millis() - dfu_last_activity_ms());
        if (!button && idle >= (int32_t)IDLE_TIMEOUT_MS && app_valid()) {
            board_jump(APP_BASE);  /* nobody came to update: back to the firmware */
        }

        hw_set_led((now / LED_HALF_PERIOD) & 1u);
        __WFI();
    }
}
