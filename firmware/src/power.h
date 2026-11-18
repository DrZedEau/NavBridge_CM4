/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Raspberry Pi power and everything that follows the Compute Module's 3.3 V rail.
 */
#ifndef POWER_H
#define POWER_H

#include <stdint.h>

void power_init(void);
void power_poll(void);

/* Ignition status byte from the IKE (80 04 BF 11 xx). KL-15 on: buck on. */
void power_ignition(uint8_t status);

int power_cm_on(void);  /* debounced +3V3_SENSE */

#endif
