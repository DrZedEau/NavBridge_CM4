/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Flash programming, shared by the bootloader and the firmware. Only the firmware info page
 * and the firmware area can be written (src/layout.h): nothing here can touch the
 * bootloader's pages. The firmware only uses it to clear the firmware info, which makes the
 * bootloader stay in update mode until a complete new firmware is written.
 */
#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>

int flash_erase_page(uint32_t addr);                                /* 1 on success */
int flash_program(uint32_t addr, const uint8_t *data, uint32_t len); /* len: multiple of 8 */
uint32_t flash_crc(uint32_t addr, uint32_t len);                    /* len: multiple of 4 */

/* The firmware info matches the firmware: a complete download, not corrupted since. */
int app_valid(void);
uint32_t app_size(void);   /* 0 when not valid */
int app_info_write(uint32_t size);   /* computes the CRC of the firmware as written */
int app_info_clear(void);

#endif
