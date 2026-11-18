/* SPDX-License-Identifier: GPL-2.0-only
 *
 * USB DFU 1.1 device (DFU mode only) that writes the firmware area (src/layout.h). One
 * DFU block is one 2 KB flash page. Used with dfu-util:
 *
 *   dfu-util -d 0483:5741 -a 0 -D navbridge-cm4-mcu.bin
 */
#ifndef DFU_H
#define DFU_H

#include <stdint.h>

void dfu_connect(void);
void dfu_disconnect(void);
void dfu_poll(void);                 /* main loop: erases and programs the received blocks */

uint32_t dfu_last_activity_ms(void); /* last DFU request from the host */
int dfu_start_requested(void);       /* new firmware written and reported, or DFU_DETACH */

#endif
