/* SPDX-License-Identifier: GPL-2.0-only
 *
 * CD changer emulator. The radio's CD changer input is where the Pi's line output goes, and
 * the radio only plays that input (CD mode, chosen with MODE) when a CD changer answers on
 * the I-Bus at address 18. This answers as one: always disc 1, track 1, as the CM5
 * NavBridge firmware did. The radio's play, pause, stop and track requests (68 05 18 38 ..)
 * are reported to the Pi like any other frame, for the app.
 *
 * Active only while the ignition is on (KL-15), and never when the car has a real CD
 * changer: any frame from address 18 that is not ours (our own frames are never received)
 * turns the emulator off until the MCU restarts, which it does at every wake-up of a parked
 * car. After the ignition comes on, the emulator waits for a radio poll (68 03 18 01) that
 * goes unanswered, or for CDCHANGER_LISTEN_MS without any, before it announces itself, so
 * that a real changer has the time to show up first.
 *
 * No hardware access, so it can be tested on a PC: frames go out through the function given
 * to cdchanger_init.
 */
#ifndef CDCHANGER_H
#define CDCHANGER_H

#include <stdint.h>

typedef enum {
    CDCHANGER_OFF,      /* ignition off */
    CDCHANGER_WAITING,  /* ignition on, giving a real CD changer the time to answer */
    CDCHANGER_ACTIVE,   /* answering the radio as its CD changer */
    CDCHANGER_REAL,     /* a real CD changer was heard: never active until the MCU restarts */
} cdchanger_state_t;

/* send: queue a complete frame (checksum included); returns 0 if it could not. */
void cdchanger_init(int (*send)(const uint8_t *frame, uint32_t len));

/* The IKE's ignition status byte (80 04 BF 11 xx). */
void cdchanger_ignition(uint8_t status, uint32_t now_ms);

/* Every valid frame heard, from either bus. */
void cdchanger_frame(const uint8_t *frame, uint32_t len, uint32_t now_ms);

/* From the main loop. */
void cdchanger_poll(uint32_t now_ms);

cdchanger_state_t cdchanger_state(void);

#endif
