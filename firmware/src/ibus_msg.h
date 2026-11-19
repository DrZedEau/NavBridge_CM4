/* SPDX-License-Identifier: GPL-2.0-only
 *
 * The I-Bus messages the board reacts to (same as the CM5 NavBridge firmware).
 * A frame is: source, length, destination, data..., checksum.
 */
#ifndef IBUS_MSG_H
#define IBUS_MSG_H

#include <stdint.h>

#define IBUS_ADDR_IKE  0x80u  /* instrument cluster */
#define IBUS_ADDR_GLO  0xBFu  /* global, low priority */
#define IBUS_ADDR_BMBT 0xF0u  /* board monitor buttons */
#define IBUS_ADDR_LOC  0xFFu  /* local broadcast */
#define IBUS_ADDR_RAD  0x68u  /* radio */
#define IBUS_ADDR_MFL  0x50u  /* steering wheel buttons */

static inline uint32_t ibus_data_len(uint32_t len)
{
    return len - 4u;  /* minus source, length, destination, checksum */
}

static inline int ibus_is(const uint8_t *f, uint32_t len, uint8_t src, uint8_t dst,
                          const uint8_t *data, uint32_t n)
{
    if (f[0] != src || f[2] != dst || ibus_data_len(len) < n) {
        return 0;
    }
    for (uint32_t i = 0; i < n; i++) {
        if (f[3u + i] != data[i]) {
            return 0;
        }
    }
    return 1;
}

/* 80 04 BF 11 xx: ignition status from the IKE. xx bit 0 = KL-R (position 1),
 * bit 1 = KL-15 (position 2), bit 2 = KL-50 (start). Returns -1 if not this message. */
static inline int ibus_ignition(const uint8_t *f, uint32_t len)
{
    if (f[0] != IBUS_ADDR_IKE || f[2] != IBUS_ADDR_GLO || ibus_data_len(len) < 2u
        || f[3] != 0x11u) {
        return -1;
    }
    return f[4];
}

#define IBUS_IGN_KL15 0x02u

/* F0 04 FF 48 08: telephone button pressed. */
static inline int ibus_is_telephone_pressed(const uint8_t *f, uint32_t len)
{
    static const uint8_t d[] = { 0x48, 0x08 };
    return ibus_is(f, len, IBUS_ADDR_BMBT, IBUS_ADDR_LOC, d, sizeof d);
}

/* BMBT -> radio button: F0 04 68 48 xx. */
static inline int ibus_is_bmbt_rad_button(const uint8_t *f, uint32_t len, uint8_t code)
{
    const uint8_t d[] = { 0x48, code };
    return ibus_is(f, len, IBUS_ADDR_BMBT, IBUS_ADDR_RAD, d, sizeof d);
}

/* Buttons that bring the factory navigation picture back: AM, FM, MODE, the radio BM
 * button and INFO (F0 05 FF 47 00 38). MENU and TONE are app buttons (ibus_app_button). */
static inline int ibus_is_back_to_nav(const uint8_t *f, uint32_t len)
{
    static const uint8_t info[] = { 0x47, 0x00, 0x38 };
    return ibus_is_bmbt_rad_button(f, len, 0x21u)     /* AM */
        || ibus_is_bmbt_rad_button(f, len, 0x31u)     /* FM */
        || ibus_is_bmbt_rad_button(f, len, 0x23u)     /* MODE */
        || ibus_is_bmbt_rad_button(f, len, 0x30u)     /* radio BM */
        || ibus_is(f, len, IBUS_ADDR_BMBT, IBUS_ADDR_LOC, info, sizeof info);
}

/* The buttons the app uses while the Pi's picture is shown, whatever the frame's
 * destination (as the app reads them):
 *   board monitor, F0 .. 48 xx (low six bits; 0x40 held, 0x80 released):
 *     presets 1..6 (11 01 12 02 13 03), < and > (10 00), TONE (04), MENU (34), CLOCK (07),
 *     tape direction (14), knob press (05)
 *   board monitor, F0 .. 47 00 xx: SELECT (0F)
 *   board monitor knob, F0 .. 49 xx: every turn
 *   steering wheel, 50 .. 3B xx (0x10 held, 0x20 released): search up (01), search down
 *     (08), send/end (80), R/T (40)
 * *key names the button: the message id, and the code without its hold and release bits.
 * *press is 1 for a press or a knob turn, 0 for a hold or a release.
 * Returns 0 for any other frame. */
static inline int ibus_app_button(const uint8_t *f, uint32_t len, uint16_t *key, int *press)
{
    if (ibus_data_len(len) < 2u) {
        return 0;
    }
    const uint8_t msg = f[3];
    uint8_t code = f[4];
    uint8_t phase;  /* the hold and release bits */
    if (f[0] == IBUS_ADDR_BMBT && msg == 0x48u) {
        switch (code & 0x3Fu) {
        case 0x11: case 0x01: case 0x12: case 0x02: case 0x13: case 0x03:  /* presets */
        case 0x10: case 0x00:  /* < and > */
        case 0x04:             /* TONE */
        case 0x34:             /* MENU */
        case 0x07:             /* CLOCK */
        case 0x14:             /* tape direction */
        case 0x05:             /* knob press */
            break;
        default:
            return 0;
        }
        phase = 0xC0u;
    } else if (f[0] == IBUS_ADDR_BMBT && msg == 0x47u && ibus_data_len(len) >= 3u
               && (f[5] & 0x3Fu) == 0x0Fu) {   /* SELECT */
        code = f[5];
        phase = 0xC0u;
    } else if (f[0] == IBUS_ADDR_BMBT && msg == 0x49u) {
        code = 0;       /* each turn stands alone */
        phase = 0;
    } else if (f[0] == IBUS_ADDR_MFL && msg == 0x3Bu) {
        switch (code & (uint8_t)~0x30u) {
        case 0x01:  /* search up */
        case 0x08:  /* search down */
        case 0x80:  /* send/end */
        case 0x40:  /* R/T */
            break;
        default:
            return 0;
        }
        phase = 0x30u;
    } else {
        return 0;
    }
    *key = (uint16_t)(((uint32_t)msg << 8) | (code & (uint8_t)~phase));
    *press = (code & phase) == 0u;
    return 1;
}

#endif
