/* SPDX-License-Identifier: GPL-2.0-only
 *
 * CD changer emulator (see cdchanger.h). The frames and status values are those of the
 * CM5 NavBridge firmware's CdcEmulator, which worked in the car.
 */
#include "cdchanger.h"
#include "config.h"
#include "ibus_msg.h"

#define CDC_ADDR    0x18u
#define RADIO_ADDR  0x68u

#define MSG_DEVICE_STATUS_REQUEST 0x01u  /* 68 03 18 01: is a CD changer there? */
#define MSG_DEVICE_STATUS         0x02u  /* 18 04 FF 02 xx: 01 just powered on, 00 ready */
#define MSG_CDC_REQUEST           0x38u  /* 68 05 18 38 cc aa: command, argument */
#define MSG_CDC_STATUS            0x39u  /* 18 0A 68 39 ...: status */

enum { STOPPED, PAUSED, PLAYING };

static int (*send_frame)(const uint8_t *frame, uint32_t len);
static uint8_t ignition_on;
static uint32_t ignition_on_ms;
static uint8_t real_changer;   /* sticky until the MCU restarts */
static uint8_t active;
static uint8_t poll_seen;      /* a radio poll is waiting for a real changer's answer */
static uint32_t poll_ms;
static uint8_t playback;

/* frame: source .. last data byte, frame[1] = length; the checksum is added here. */
static void send(uint8_t *frame)
{
    const uint32_t len = frame[1] + 2u;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len - 1u; i++) {
        sum ^= frame[i];
    }
    frame[len - 1u] = sum;
    (void)send_frame(frame, len);
}

static void send_device_status(uint8_t value)
{
    uint8_t f[6] = { CDC_ADDR, 0x04, IBUS_ADDR_LOC, MSG_DEVICE_STATUS, value };
    send(f);
}

/* 18 0A 68 39 ss aa 00 01 00 01 01: state, audio state, no error, disc 1 present, disc 1,
 * track 1 (BCD). */
static void send_status_values(uint8_t state, uint8_t audio)
{
    uint8_t f[12] = { CDC_ADDR, 0x0A, RADIO_ADDR, MSG_CDC_STATUS, state, audio,
                      0x00, 0x01, 0x00, 0x01, 0x01 };
    send(f);
}

static void send_status(void)
{
    switch (playback) {
    case PLAYING:
        send_status_values(0x02, 0x09);  /* playing, audio on: the radio plays the input */
        break;
    case PAUSED:
        send_status_values(0x01, 0x02);
        break;
    default:
        send_status_values(0x00, 0x02);
        break;
    }
}

static void stop_emulating(void)
{
    active = 0;
    poll_seen = 0;
    playback = STOPPED;
}

static void start_emulating(void)
{
    active = 1;
    poll_seen = 0;
    playback = STOPPED;
    send_device_status(0x01);  /* "just powered on": the radio polls at once */
}

static void request(uint8_t command, uint8_t argument)
{
    switch (command) {
    case 0x01:  /* stop */
        playback = STOPPED;
        break;
    case 0x02:  /* pause */
        playback = PAUSED;
        break;
    case 0x03:  /* play */
    case 0x06:  /* select disc: only disc 1 */
    case 0x0A:  /* next / previous track: only track 1 */
        playback = PLAYING;
        break;
    case 0x04:  /* seek: 00 back, 01 forward */
        if (playback == PLAYING) {
            send_status_values(argument == 0x00 ? 0x04 : 0x03, 0x09);
            return;
        }
        break;
    default:    /* 00 status, 05 alternate track change, 0C ID3, unknown: just answer */
        break;
    }
    send_status();
}

void cdchanger_init(int (*send_fn)(const uint8_t *frame, uint32_t len))
{
    send_frame = send_fn;
    ignition_on = 0;
    real_changer = 0;
    stop_emulating();
}

void cdchanger_ignition(uint8_t status, uint32_t now_ms)
{
    const uint8_t on = (status & IBUS_IGN_KL15) != 0u;
    if (on && !ignition_on) {
        ignition_on_ms = now_ms;
    }
    if (!on) {
        stop_emulating();
    }
    ignition_on = on;
}

void cdchanger_frame(const uint8_t *f, uint32_t len, uint32_t now_ms)
{
    if (f[0] == CDC_ADDR) {
        /* Not ours: a real CD changer. */
        real_changer = 1;
        stop_emulating();
        return;
    }
    if (f[2] != CDC_ADDR || ibus_data_len(len) < 1u || real_changer) {
        return;
    }
    const uint8_t msg = f[3];
    if (msg == MSG_DEVICE_STATUS_REQUEST) {
        if (active) {
            send_device_status(0x00);
            send_status();
        } else if (ignition_on && !poll_seen) {
            poll_seen = 1;  /* a real changer answers within milliseconds */
            poll_ms = now_ms;
        }
    } else if (msg == MSG_CDC_REQUEST && active && ibus_data_len(len) >= 3u) {
        request(f[4], f[5]);
    }
}

void cdchanger_poll(uint32_t now_ms)
{
    if (!CDCHANGER_EMULATOR || !ignition_on || real_changer || active) {
        return;
    }
    if ((poll_seen && now_ms - poll_ms >= CDCHANGER_ANSWER_MS)
        || now_ms - ignition_on_ms >= CDCHANGER_LISTEN_MS) {
        start_emulating();
    }
}

cdchanger_state_t cdchanger_state(void)
{
    if (!CDCHANGER_EMULATOR) {
        return CDCHANGER_OFF;
    }
    if (real_changer) {
        return CDCHANGER_REAL;
    }
    if (active) {
        return CDCHANGER_ACTIVE;
    }
    return ignition_on ? CDCHANGER_WAITING : CDCHANGER_OFF;
}
