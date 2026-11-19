/* SPDX-License-Identifier: GPL-2.0-only
 *
 * The board monitor's and steering wheel's buttons: which picture the display shows, and
 * which of their frames go on to the navigation module. No hardware access, so it can be
 * tested on a PC.
 *
 * Picture: the telephone button toggles between the navigation module and the Pi (the Pi
 * only while the CM is on); AM, FM, MODE, the radio BM button, INFO, TONE and SELECT bring
 * the navigation picture back.
 *
 * Navigation module: while the Pi's picture is shown, the app buttons (ibus_app_button in
 * ibus_msg.h: presets, < >, MENU, CLOCK, tape direction, the knob, and the steering
 * wheel's search, send/end and R/T) are not passed on to it, so that its menus do
 * not move behind the Pi's picture. A button's hold and release frames go where its press
 * went, so the navigation module never sees a press without its release. Every other
 * frame, including the buttons that switch the picture, always goes on.
 */
#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>

/* Whether the Pi's picture is shown after this frame (from either bus). */
int buttons_picture(const uint8_t *frame, uint32_t len, int pi_shown, int cm_on);

/* Whether this frame from the car goes on to the navigation module, given the picture
 * before and after it (buttons_picture). Frames other than app buttons always do. */
int buttons_to_nav(const uint8_t *frame, uint32_t len, int pi_before, int pi_after);

/* App button frames from the car that were not passed on to the navigation module. */
uint32_t buttons_kept(void);

#endif
