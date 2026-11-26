/* SPDX-License-Identifier: GPL-2.0-only
 *
 * The car's clock, set from the Pi (see carclock.h).
 */
#include "carclock.h"
#include "ibus_msg.h"

#define GT_ADDR             0x3Bu  /* navigation computer */
#define MSG_IKE_SET         0x40u  /* 3B .. 80 40 xx ...: set an IKE value */
#define IKE_SET_TIME        0x01u
#define IKE_SET_DATE        0x02u

static int (*send_frame)(const uint8_t *frame, uint32_t len);
static uint8_t waiting;
static uint32_t due_ms;
static carclock_minute_t target;

void carclock_init(int (*send)(const uint8_t *frame, uint32_t len))
{
    send_frame = send;
    waiting = 0;
}

/* The next n characters of *s as a decimal number, or -1. */
static int digits(const char **s, uint32_t n)
{
    int value = 0;
    for (uint32_t i = 0; i < n; i++) {
        const char c = (*s)[i];
        if (c < '0' || c > '9') {
            return -1;
        }
        value = value * 10 + (c - '0');
    }
    *s += n;
    return value;
}

static int separator(const char **s, char c)
{
    if (**s != c) {
        return 0;
    }
    (*s)++;
    return 1;
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    /* Every fourth year is a leap year from 2000 to 2099 (2000 is one). */
    return (month == 2u && year % 4u == 0u) ? 29u : days[month - 1u];
}

static void next_minute(carclock_minute_t *m)
{
    if (++m->minute < 60u) {
        return;
    }
    m->minute = 0;
    if (++m->hour < 24u) {
        return;
    }
    m->hour = 0;
    if (++m->day <= days_in_month(m->year, m->month)) {
        return;
    }
    m->day = 1;
    if (++m->month <= 12u) {
        return;
    }
    m->month = 1;
    m->year++;
}

int carclock_set(const char *text, uint32_t now_ms, carclock_minute_t *minute,
                 uint32_t *wait_ms)
{
    const char *s = text;
    const int year = digits(&s, 4);
    const int month = separator(&s, '-') ? digits(&s, 2) : -1;
    const int day = separator(&s, '-') ? digits(&s, 2) : -1;
    const int hour = separator(&s, ' ') ? digits(&s, 2) : -1;
    const int min = separator(&s, ':') ? digits(&s, 2) : -1;
    const int sec = separator(&s, ':') ? digits(&s, 2) : -1;

    if (*s != '\0' || year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1
        || day > days_in_month((uint16_t)year, (uint8_t)month) || hour < 0 || hour > 23
        || min < 0 || min > 59 || sec < 0 || sec > 59) {
        return 0;
    }
    carclock_minute_t m = { (uint16_t)year, (uint8_t)month, (uint8_t)day, (uint8_t)hour,
                            (uint8_t)min };
    uint32_t wait = 0;
    if (sec != 0) {
        next_minute(&m);
        wait = (60u - (uint32_t)sec) * 1000u;
    }
    if (m.year > 2099u) {
        return 0;  /* the year byte only goes to 99 */
    }
    target = m;
    due_ms = now_ms + wait;
    waiting = 1;
    *minute = m;
    *wait_ms = wait;
    return 1;
}

/* frame: source .. last data byte, frame[1] = length; the checksum is added here. */
static int send(uint8_t *frame)
{
    const uint32_t len = frame[1] + 2u;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len - 1u; i++) {
        sum ^= frame[i];
    }
    frame[len - 1u] = sum;
    return send_frame(frame, len);
}

int carclock_poll(uint32_t now_ms, carclock_minute_t *minute)
{
    if (!waiting || (int32_t)(now_ms - due_ms) < 0) {
        return 0;
    }
    waiting = 0;
    uint8_t date[9] = { GT_ADDR, 0x07, IBUS_ADDR_IKE, MSG_IKE_SET, IKE_SET_DATE, target.day,
                        target.month, (uint8_t)(target.year - 2000u) };
    uint8_t time[8] = { GT_ADDR, 0x06, IBUS_ADDR_IKE, MSG_IKE_SET, IKE_SET_TIME, target.hour,
                        target.minute };
    const int date_queued = send(date);
    const int time_queued = send(time);
    *minute = target;
    return date_queued && time_queued ? 1 : -1;
}

int carclock_waiting(void)
{
    return waiting;
}
