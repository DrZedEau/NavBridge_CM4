/* SPDX-License-Identifier: GPL-2.0-only
 *
 * I-Bus on both ATA663231 LIN transceivers: IBUS_CAR = LIN1 (U8, USART1, car harness),
 * IBUS_NAV = LIN2 (U10, USART2, navigation module).
 */
#ifndef IBUS_H
#define IBUS_H

#include <stdint.h>

#define IBUS_CAR 0u
#define IBUS_NAV 1u
#define IBUS_BUSES 2u

typedef struct {
    uint32_t rx_bytes;       /* bytes heard from others (not our own echo) */
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t rx_errors;      /* bytes with a parity, framing, noise or overrun error */
    uint32_t bad_frames;     /* checksum errors and resynchronisations */
    uint32_t collisions;     /* our byte came back different: someone else talking, retried */
    uint32_t echo_timeouts;  /* our byte never came back (3 ms): transceiver or bus fault */
    uint32_t tx_dropped;     /* queue full, or too many failed attempts */
    uint32_t queued;         /* frames waiting now */
} ibus_stats_t;

/* Valid frame received: source, length, destination, data..., checksum (len bytes). Called
 * from ibus_poll(), in the main loop. */
typedef void (*ibus_frame_handler_t)(uint32_t bus, const uint8_t *frame, uint32_t len);

void ibus_init(ibus_frame_handler_t handler);
void ibus_poll(void);

/* Queue a whole frame (checksum included) for sending on a bus. Returns 0 if dropped. */
int ibus_send(uint32_t bus, const uint8_t *frame, uint32_t len);

void ibus_get_stats(uint32_t bus, ibus_stats_t *stats);

/* The transceivers are powered and in Normal mode (see ibus.c: no 12 V, no I-Bus). */
int ibus_lin_on(void);

uint32_t ibus_idle_ms(void);  /* since the last byte heard or sent on either bus */
int ibus_busy(void);          /* a frame coming in, waiting or being sent */

/* Both transceivers to Sleep, the navigation side first. With the CM off the MCU runs from
 * the car-side transceiver's regulator and switches off; the next I-Bus activity wakes the
 * transceiver and the MCU starts again. If the MCU is still powered 0.5 s later, both are
 * woken again and this returns (counted in ibus_sleep_failures). */
void ibus_sleep(void);
uint32_t ibus_sleep_failures(void);

#endif
