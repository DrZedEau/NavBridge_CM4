/* SPDX-License-Identifier: GPL-2.0-only
 *
 * USB CDC ACM device (virtual serial port) on the STM32C071 USB DRD peripheral, clocked by
 * HSIUSB48 trimmed by the CRS from the host's start-of-frame packets.
 */
#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdint.h>

void usb_cdc_connect(void);     /* power the peripheral and pull D+ up */
void usb_cdc_disconnect(void);  /* release D+ and power the peripheral down */

int usb_cdc_ready(void);        /* configured, and the host has the port open (DTR) */

/* Queue bytes for the host: all of them, or none (returns 0) if the port is not open or
 * the buffer lacks room. */
int usb_cdc_write(const void *data, uint32_t len);
int usb_cdc_tx_empty(void);

uint32_t usb_cdc_read(uint8_t *buf, uint32_t max);

void usb_cdc_poll(void);        /* from the main loop: starts pending transfers */

#endif
