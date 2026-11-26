/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Park distance control: asks the PDC for its distances in reverse (see pdc.h).
 */
#include "pdc.h"
#include "config.h"
#include "ibus_msg.h"

#define DIA_ADDR               0x3Fu  /* diagnosis */
#define PDC_ADDR               0x60u
#define MSG_PDC_SENSOR_REQUEST 0x1Bu
#define MSG_IKE_SENSORS        0x13u  /* 80 .. 13 xx yy: yy >> 4 is the gear position */
#define GEAR_REVERSE           0x1u

static int (*send_frame)(const uint8_t *frame, uint32_t len);
static uint8_t enabled;
static uint8_t reverse;
static uint8_t asking;
static uint32_t next_ms;

void pdc_init(int (*send)(const uint8_t *frame, uint32_t len))
{
    send_frame = send;
    enabled = 0;
    reverse = 0;
    asking = 0;
}

void pdc_enable(int on)
{
    enabled = (uint8_t)(on != 0);
}

void pdc_ignition(uint8_t status)
{
    if ((status & IBUS_IGN_KL15) == 0u) {
        reverse = 0;  /* no sensor status may come to say that reverse ended */
    }
}

void pdc_frame(const uint8_t *frame, uint32_t len)
{
    if (frame[0] == IBUS_ADDR_IKE && ibus_data_len(len) >= 3u && frame[3] == MSG_IKE_SENSORS) {
        reverse = (uint8_t)((frame[5] >> 4) == GEAR_REVERSE);
    }
}

void pdc_poll(uint32_t now_ms)
{
    if (!enabled || !reverse) {
        asking = 0;
        return;
    }
    if (asking && (int32_t)(now_ms - next_ms) < 0) {
        return;
    }
    asking = 1;
    next_ms = now_ms + PDC_INTERVAL_MS;
    static const uint8_t question[] = {
        DIA_ADDR, 0x03, PDC_ADDR, MSG_PDC_SENSOR_REQUEST,
        DIA_ADDR ^ 0x03u ^ PDC_ADDR ^ MSG_PDC_SENSOR_REQUEST,
    };
    (void)send_frame(question, sizeof question);
}

pdc_state_t pdc_state(void)
{
    if (!enabled) {
        return PDC_OFF;
    }
    return asking ? PDC_ASKING : PDC_ON;
}
