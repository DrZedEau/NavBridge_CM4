/* SPDX-License-Identifier: GPL-2.0-only
 *
 * NavBridge CM4 MCU firmware (STM32C071KBT6).
 *
 * - I-Bus bridge: every valid frame heard on LIN1 (car) is sent unchanged on LIN2
 *   (navigation module), and the other way round.
 * - Every frame is also reported to the Raspberry Pi over USB CDC ("IBUS1 RX ...").
 * - Ignition on (KL-15) turns the Raspberry Pi's 5 V buck on; it turns off once the CM
 *   3.3 V rail goes away (the Pi has shut down).
 * - The telephone button toggles the display between the navigation module and the Pi;
 *   the radio and menu buttons bring the navigation picture back.
 * - CSYNC for the ADV7125, from the DPI HSYNC and VSYNC (TIM1).
 * - Over USB, the Pi can hand over to the bootloader to update this firmware.
 */
#include "board.h"
#include "config.h"
#include "csync.h"
#include "hardware.h"
#include "host.h"
#include "ibus.h"
#include "ibus_msg.h"
#include "power.h"
#include "usb_cdc.h"

static void on_ibus_frame(uint32_t bus, const uint8_t *frame, uint32_t len)
{
    /* Forward first: the other bus waits for it. */
#if IBUS_FORWARD_CAR_TO_NAV
    if (bus == IBUS_CAR) {
        (void)ibus_send(IBUS_NAV, frame, len);
    }
#endif
#if IBUS_FORWARD_NAV_TO_CAR
    if (bus == IBUS_NAV) {
        (void)ibus_send(IBUS_CAR, frame, len);
    }
#endif

    const int ignition = ibus_ignition(frame, len);
    if (ignition >= 0) {
        power_ignition((uint8_t)ignition);
    }

    if (ibus_is_telephone_pressed(frame, len)) {
        /* The Pi picture only makes sense while the CM is running. */
        if (hw_rgb() == RGB_RPI) {
            hw_set_rgb(RGB_NAV);
        } else if (power_cm_on()) {
            hw_set_rgb(RGB_RPI);
        }
    } else if (ibus_is_back_to_nav(frame, len)) {
        hw_set_rgb(RGB_NAV);
    }

    /* The app acts on the buttons only while the Pi's picture is shown ("RGB RPI"). The
     * buttons that switch the picture are meant for the board, not the app, so their frame
     * is reported while "RGB NAV" holds: a switch to the navigation picture is announced
     * before the frame, a switch to the Pi after it. */
    if (hw_rgb() == RGB_NAV) {
        host_report_rgb();
    }
    host_report_frame(bus, frame, len);
    host_report_rgb();
}

int main(void)
{
    /* Started by the bootloader (src/layout.h), which already checked the BOOT button. */
    board_early_init();
    hw_early_init();
    board_clock_init();
    board_init();
    csync_init();
    ibus_init(on_ibus_frame);
    power_init();

    for (;;) {
        power_poll();   /* CM 3.3 V: USB and CSYNC on or off, buck off after shutdown */
        csync_poll();
        ibus_poll();
        host_poll();
        usb_cdc_poll();
        __WFI();        /* any interrupt, at least the 1 kHz tick, runs the loop again */
    }
}
