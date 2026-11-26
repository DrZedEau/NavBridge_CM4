/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Park distance control. The PDC module (60) gives its sensors' distances only when asked.
 * After "PDC ON" from the app (host.c), the board asks it while the car is in reverse, as
 * BlueBus does: from the diagnosis address, at once and then every PDC_INTERVAL_MS, on the
 * car's I-Bus:
 *   3F 03 60 1B
 * Its answer (60 .. 3F A0 ...) reaches the Pi like any other frame.
 *
 * Reverse is the IKE's sensor status, 80 .. 13 xx yy: the high half of yy is 1 in reverse,
 * with a manual or an automatic gearbox. The asking stops at the end of reverse, when the
 * ignition leaves KL-15, after "PDC OFF", and when the Pi closes the port: the board starts
 * with it off, and the app says "PDC ON" again each time it opens the port.
 *
 * No hardware access, so it can be tested on a PC: frames go out through the function given
 * to pdc_init.
 */
#ifndef PDC_H
#define PDC_H

#include <stdint.h>

typedef enum {
    PDC_OFF,     /* not asked to ("PDC OFF", or the port is closed) */
    PDC_ON,      /* asked to, but the car is not in reverse */
    PDC_ASKING,  /* in reverse: asking the PDC for its distances */
} pdc_state_t;

/* send: queue a complete frame (checksum included); returns 0 if it could not. */
void pdc_init(int (*send)(const uint8_t *frame, uint32_t len));

void pdc_enable(int on);

/* The IKE's ignition status byte (80 04 BF 11 xx). */
void pdc_ignition(uint8_t status);

/* Every valid frame heard, from either bus. */
void pdc_frame(const uint8_t *frame, uint32_t len);

/* From the main loop. */
void pdc_poll(uint32_t now_ms);

pdc_state_t pdc_state(void);

#endif
