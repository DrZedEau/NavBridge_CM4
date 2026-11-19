/* SPDX-License-Identifier: GPL-2.0-only
 *
 * NavBridge CM4 MCU firmware (STM32C071KBT6).
 *
 * - I-Bus bridge: every valid frame heard on LIN1 (car) is sent unchanged on LIN2
 *   (navigation module), and the other way round; while the Pi's picture is shown, the
 *   app buttons (buttons.h) are kept from the navigation module.
 * - Every frame is also reported to the Raspberry Pi over USB CDC ("IBUS1 RX ...").
 * - Ignition on (KL-15) turns the Raspberry Pi's 5 V buck on; it turns off once the CM
 *   3.3 V rail goes away (the Pi has shut down).
 * - The telephone button toggles the display between the navigation module and the Pi;
 *   AM, FM, MODE, radio BM, INFO, TONE and SELECT bring the navigation picture back.
 * - CSYNC for the ADV7125, from the DPI HSYNC and VSYNC (TIM1).
 * - CD changer emulator while the ignition is on, unless the car has a real CD changer, so
 *   that the radio's CD mode plays the Pi's audio.
 * - Over USB, the Pi can hand over to the bootloader to update this firmware.
 */
#include "board.h"
#include "buttons.h"
#include "cdchanger.h"
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
    /* The picture first (a GPIO), as it decides where the buttons go. */
    const int pi_before = hw_rgb() == RGB_RPI;
    const int pi_after = buttons_picture(frame, len, pi_before, power_cm_on());
    if (pi_after != pi_before) {
        hw_set_rgb(pi_after ? RGB_RPI : RGB_NAV);
    }

    /* Then forward: the other bus waits for it. While the Pi's picture is shown, the app
     * buttons are kept from the navigation module (buttons.h). */
#if IBUS_FORWARD_CAR_TO_NAV
    if (bus == IBUS_CAR && buttons_to_nav(frame, len, pi_before, pi_after)) {
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
        cdchanger_ignition((uint8_t)ignition, board_millis());
    }
    cdchanger_frame(frame, len, board_millis());

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

/* The CD changer emulator's frames: to the radio on the car side, and to the navigation
 * module as well, as a real changer's frames would be forwarded to it. */
static int cdchanger_send(const uint8_t *frame, uint32_t len)
{
    const int queued = ibus_send(IBUS_CAR, frame, len);
#if IBUS_FORWARD_CAR_TO_NAV
    (void)ibus_send(IBUS_NAV, frame, len);
#endif
    return queued;
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
    cdchanger_init(cdchanger_send);
    power_init();

    for (;;) {
        power_poll();   /* CM 3.3 V: USB and CSYNC on or off, buck off after shutdown */
        csync_poll();
        ibus_poll();
        cdchanger_poll(board_millis());
        host_poll();
        usb_cdc_poll();
        __WFI();        /* any interrupt, at least the 1 kHz tick, runs the loop again */
    }
}
