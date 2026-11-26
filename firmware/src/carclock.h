/* SPDX-License-Identifier: GPL-2.0-only
 *
 * The car's clock, set from the Pi. The app sends its local date and time,
 * "TIME 2026-11-26 19:45:30" (host.c). The instrument cluster (IKE) takes hours and minutes
 * and starts a new minute when it is set, so the frames wait for the next minute: at
 * 19:46:00 the IKE gets, on the car's I-Bus, as BlueBus sends them,
 *   3B 07 80 40 02 DD MM YY   date: day, month, year - 2000
 *   3B 06 80 40 01 HH MM      time: 24-hour
 * in binary, from the navigation computer's address (GT, 3B), the date first. A line at
 * 19:46:00 sets 19:46 at once.
 *
 * No hardware access, so it can be tested on a PC: frames go out through the function given
 * to carclock_init.
 */
#ifndef CARCLOCK_H
#define CARCLOCK_H

#include <stdint.h>

typedef struct {
    uint16_t year;   /* 2000..2099 */
    uint8_t month;   /* 1..12 */
    uint8_t day;     /* 1..31 */
    uint8_t hour;    /* 0..23 */
    uint8_t minute;  /* 0..59 */
} carclock_minute_t;

/* send: queue a complete frame (checksum included); returns 0 if it could not. */
void carclock_init(int (*send)(const uint8_t *frame, uint32_t len));

/* text: "YYYY-MM-DD HH:MM:SS", nothing more. Returns 0 if it is not a valid date and time
 * (years 2000 to 2099), and changes nothing. Otherwise replaces any time still waiting, and
 * gives the minute the car's clock will be set to and how long until then. */
int carclock_set(const char *text, uint32_t now_ms, carclock_minute_t *minute,
                 uint32_t *wait_ms);

/* From the main loop. Returns 1 when the frames have just been queued, -1 when they could
 * not be (queue full), 0 otherwise; *minute then holds the minute that was sent. */
int carclock_poll(uint32_t now_ms, carclock_minute_t *minute);

int carclock_waiting(void);

#endif
