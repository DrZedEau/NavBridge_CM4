/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Bootloader updater: a firmware (linked at APP_BASE like the real one) that carries a new
 * bootloader image and writes it over the old one. Installed through the bootloader like
 * any firmware (mcu-flash.sh --bootloader-update), it runs once:
 *
 *   1. checks the image it carries (size, vector table);
 *   2. if the bootloader in flash differs: erases page 0 first, writes pages 1..6, then
 *      writes page 0 last and verifies everything (up to 3 attempts);
 *   3. clears the firmware info and hands over to the new bootloader, which stays in update
 *      mode for the real firmware.
 *
 * While page 0 is erased the chip counts as empty: if power is lost then, it starts ST's own
 * DFU bootloader at power-on, from which mcu-flash.sh reinstalls everything. On a failure
 * the STAT LED blinks slowly (1 Hz) and the updater stays put; use SWD, or power-cycle the
 * board (page 0 empty: ST's DFU).
 *
 * Like the bootloader, it never drives the LIN pins and leaves the buck as it was.
 */
#include <string.h>
#include "board.h"
#include "flash.h"
#include "hardware.h"
#include "layout.h"

#define BOOTLOADER_SIZE (APP_INFO_ADDR - BOOTLOADER_BASE)  /* 14 KB, pages 0..6 */
#define ATTEMPTS 3u

extern const uint8_t bootloader_image[];
extern const uint8_t bootloader_image_end[];

static uint8_t page_buf[FLASH_PAGE_BYTES];

static uint32_t le32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16)
         | ((uint32_t)b[3] << 24);
}

static int image_ok(const uint8_t *img, uint32_t len)
{
    if (len < 8u || len > BOOTLOADER_SIZE) {
        return 0;
    }
    const uint32_t sp = le32(img);
    const uint32_t pc = le32(img + 4);
    return sp >= SRAM_BASE && sp <= SRAM_BASE + SRAM_SIZE_MAX
        && pc >= BOOTLOADER_BASE && pc < BOOTLOADER_BASE + len && (pc & 1u) != 0u;
}

/* One page of the image, padded with 0xFF to a multiple of 8. */
static int write_page(const uint8_t *img, uint32_t len, uint32_t page)
{
    const uint32_t offset = page * FLASH_PAGE_BYTES;
    const uint32_t addr = BOOTLOADER_BASE + offset;

    if (!flash_erase_page(addr)) {
        return 0;
    }
    if (offset >= len) {
        return 1;  /* past the image: left erased */
    }
    const uint32_t n = len - offset < FLASH_PAGE_BYTES ? len - offset : FLASH_PAGE_BYTES;
    const uint32_t padded = (n + 7u) & ~7u;
    memcpy(page_buf, img + offset, n);
    memset(page_buf + n, 0xFF, padded - n);
    return flash_program(addr, page_buf, padded);
}

static int write_bootloader(const uint8_t *img, uint32_t len)
{
    const uint32_t pages = BOOTLOADER_SIZE / FLASH_PAGE_BYTES;

    /* Page 0 first: from here until the end the chip boots ST's DFU if power is lost. */
    if (!flash_erase_page(BOOTLOADER_BASE)) {
        return 0;
    }
    for (uint32_t page = 1; page < pages; page++) {
        if (!write_page(img, len, page)) {
            return 0;
        }
    }
    if (!write_page(img, len, 0)) {
        return 0;
    }
    return memcmp((const void *)BOOTLOADER_BASE, img, len) == 0;
}

static void fail_forever(void)
{
    for (;;) {
        hw_set_led((board_millis() / 500u) & 1u);
        __WFI();
    }
}

int main(void)
{
    board_early_init();
    hw_early_init();
    board_clock_init();
    board_init();

    const uint8_t *img = bootloader_image;
    const uint32_t len = (uint32_t)(bootloader_image_end - bootloader_image);
    if (!image_ok(img, len)) {
        fail_forever();
    }

    if (memcmp((const void *)BOOTLOADER_BASE, img, len) != 0) {
        int done = 0;
        for (uint32_t attempt = 0; attempt < ATTEMPTS && !done; attempt++) {
            done = write_bootloader(img, len);
        }
        if (!done) {
            fail_forever();
        }
    }

    /* The new bootloader stays in update mode: the real firmware comes next. */
    if (!app_info_clear()) {
        fail_forever();
    }
    PWR->BKP1R = BOOTLOADER_REQUEST;
    board_jump(BOOTLOADER_BASE);
}
