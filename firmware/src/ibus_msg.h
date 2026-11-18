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

/* Buttons that bring the factory navigation picture back: AM, FM, TONE, MODE, the radio
 * BM button, MENU (F0 04 FF 48 34) and INFO (F0 05 FF 47 00 38). */
static inline int ibus_is_back_to_nav(const uint8_t *f, uint32_t len)
{
    static const uint8_t menu[] = { 0x48, 0x34 };
    static const uint8_t info[] = { 0x47, 0x00, 0x38 };
    return ibus_is_bmbt_rad_button(f, len, 0x21u)     /* AM */
        || ibus_is_bmbt_rad_button(f, len, 0x31u)     /* FM */
        || ibus_is_bmbt_rad_button(f, len, 0x04u)     /* TONE */
        || ibus_is_bmbt_rad_button(f, len, 0x23u)     /* MODE */
        || ibus_is_bmbt_rad_button(f, len, 0x30u)     /* radio BM */
        || ibus_is(f, len, IBUS_ADDR_BMBT, IBUS_ADDR_LOC, menu, sizeof menu)
        || ibus_is(f, len, IBUS_ADDR_BMBT, IBUS_ADDR_LOC, info, sizeof info);
}

#endif
