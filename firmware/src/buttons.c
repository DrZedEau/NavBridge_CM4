/* SPDX-License-Identifier: GPL-2.0-only
 *
 * The board monitor's and steering wheel's buttons (see buttons.h).
 */
#include "buttons.h"
#include "ibus_msg.h"

static uint16_t last_key;    /* the button last pressed on the car side */
static uint8_t last_to_nav;  /* whether that press went to the navigation module */
static uint32_t kept;

int buttons_picture(const uint8_t *frame, uint32_t len, int pi_shown, int cm_on)
{
    if (ibus_is_telephone_pressed(frame, len)) {
        /* The Pi picture only makes sense while the CM is running. */
        return pi_shown ? 0 : (cm_on != 0);
    }
    if (ibus_is_back_to_nav(frame, len)) {
        return 0;
    }
    return pi_shown;
}

int buttons_to_nav(const uint8_t *frame, uint32_t len, int pi_before, int pi_after)
{
    uint16_t key;
    int press;

    if (!ibus_app_button(frame, len, &key, &press)) {
        return 1;
    }
    /* A hold or a release follows its press. One without a press seen (lost frame, or the
     * button held since before start-up) is decided like a press. */
    if (press || key != last_key) {
        last_key = key;
        /* Kept only while the Pi's picture is shown (app buttons do not switch it). */
        last_to_nav = !pi_before || !pi_after;
    }
    if (!last_to_nav) {
        kept++;
    }
    return last_to_nav;
}

uint32_t buttons_kept(void)
{
    return kept;
}
