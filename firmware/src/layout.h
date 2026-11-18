/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Flash layout of the NavBridge CM4 MCU (STM32C071KB: 128 KB in 2 KB pages) and the
 * contract between the bootloader and the firmware.
 *
 *   0x08000000  bootloader       14 KB  (pages 0-6), installed once
 *   0x08003800  firmware info     2 KB  (page 7): size and CRC of the firmware, written
 *                                        by the bootloader once a download is complete
 *   0x08004000  firmware        112 KB  (pages 8-63)
 *
 * The bootloader runs first at every reset. It starts the firmware unless the firmware
 * asked for an update (BOOTLOADER_REQUEST in PWR_BKP1R), BOOT (SW3) is held at RESET, or
 * the firmware info does not match the firmware (none, interrupted download, corrupted).
 * The two hand over by jumping, never by resetting, so GPIO (the 5 V buck enable) keeps
 * its state.
 */
#ifndef LAYOUT_H
#define LAYOUT_H

#include <stdint.h>

#define FLASH_PAGE_BYTES   2048u
#define FLASH_END_ADDR     0x08020000u

#define BOOTLOADER_BASE    0x08000000u
#define APP_INFO_ADDR      0x08003800u
#define APP_BASE           0x08004000u
#define APP_MAX_SIZE       (FLASH_END_ADDR - APP_BASE)

#define APP_INFO_MAGIC     0x5041424Eu  /* "NBAP" */

typedef struct {
    uint32_t magic;      /* APP_INFO_MAGIC */
    uint32_t size;       /* firmware bytes from APP_BASE, a multiple of 8 */
    uint32_t crc;        /* CRC-32 (MCU CRC unit defaults) of those bytes */
    uint32_t check;      /* ~magic ^ size ^ crc */
} app_info_t;

/* PWR_BKP1R: the firmware asks the bootloader to stay in update mode. The PWR backup
 * registers only keep their low 16 bits. The firmware also clears the firmware info before
 * handing over, which keeps the bootloader in update mode on its own. */
#define BOOTLOADER_REQUEST 0xB007u

#endif
