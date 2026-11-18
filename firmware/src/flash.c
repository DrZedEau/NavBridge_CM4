/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Flash programming for the bootloader (STM32C071: 2 KB pages, 64-bit programming).
 *
 * The CPU runs from the same flash, so it simply stalls while a page is erased (~22 ms) or
 * a double word is programmed; the USB peripheral NAKs the host meanwhile.
 */
#include "flash.h"
#include "board.h"
#include "layout.h"

#define FLASH_KEY1 0x45670123u
#define FLASH_KEY2 0xCDEF89ABu
#define FLASH_SR_ERRORS (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR \
                         | FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISERR \
                         | FLASH_SR_FASTERR | FLASH_SR_RDERR | FLASH_SR_OPTVERR)

/* Only the bootloader updater (built with FLASH_BOOTLOADER_WRITE) may write the
 * bootloader's pages; the bootloader and the firmware cannot. */
#ifdef FLASH_BOOTLOADER_WRITE
#define WRITABLE_START BOOTLOADER_BASE
#else
#define WRITABLE_START APP_INFO_ADDR
#endif

static int writable(uint32_t addr, uint32_t len)
{
    return addr >= WRITABLE_START && addr <= FLASH_END_ADDR && len <= FLASH_END_ADDR - addr;
}

static void unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0u) {
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
}

static void lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

/* Waits for the operation, clears its flags; 1 if it had no error. */
static int wait_done(void)
{
    while ((FLASH->SR & (FLASH_SR_BSY1 | FLASH_SR_CFGBSY)) != 0u) {
    }
    const uint32_t errors = FLASH->SR & FLASH_SR_ERRORS;
    FLASH->SR = errors | FLASH_SR_EOP;
    return errors == 0u;
}

int flash_erase_page(uint32_t addr)
{
    if (!writable(addr, FLASH_PAGE_BYTES) || (addr % FLASH_PAGE_BYTES) != 0u) {
        return 0;
    }
    const uint32_t page = (addr - FLASH_BASE) / FLASH_PAGE_BYTES;

    unlock();
    (void)wait_done();
    FLASH->CR = (FLASH->CR & ~(FLASH_CR_PNB | FLASH_CR_PG | FLASH_CR_PER))
              | FLASH_CR_PER | (page << FLASH_CR_PNB_Pos);
    FLASH->CR |= FLASH_CR_STRT;
    int ok = wait_done();
    FLASH->CR &= ~(FLASH_CR_PER | FLASH_CR_PNB);
    lock();

    const volatile uint32_t *p = (const volatile uint32_t *)addr;
    for (uint32_t i = 0; ok && i < FLASH_PAGE_BYTES / 4u; i++) {
        ok = p[i] == 0xFFFFFFFFu;
    }
    return ok;
}

static uint32_t le32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16)
         | ((uint32_t)b[3] << 24);
}

int flash_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (!writable(addr, len) || (addr % 8u) != 0u || (len % 8u) != 0u) {
        return 0;
    }

    int ok = 1;
    unlock();
    (void)wait_done();
    FLASH->CR |= FLASH_CR_PG;
    for (uint32_t i = 0; ok && i < len; i += 8u) {
        *(volatile uint32_t *)(addr + i) = le32(data + i);
        __ISB();
        *(volatile uint32_t *)(addr + i + 4u) = le32(data + i + 4u);
        ok = wait_done();
    }
    FLASH->CR &= ~FLASH_CR_PG;
    lock();

    const volatile uint8_t *p = (const volatile uint8_t *)addr;
    for (uint32_t i = 0; ok && i < len; i++) {
        ok = p[i] == data[i];
    }
    return ok;
}

uint32_t flash_crc(uint32_t addr, uint32_t len)
{
    RCC->AHBENR |= RCC_AHBENR_CRCEN;
    (void)RCC->AHBENR;
    CRC->INIT = 0xFFFFFFFFu;
    CRC->CR = CRC_CR_RESET;  /* polynomial 0x04C11DB7, 32-bit, no reversal */
    const volatile uint32_t *p = (const volatile uint32_t *)addr;
    for (uint32_t i = 0; i < len / 4u; i++) {
        CRC->DR = p[i];
    }
    return CRC->DR;
}

static const app_info_t *info(void)
{
    return (const app_info_t *)APP_INFO_ADDR;
}

int app_valid(void)
{
    const app_info_t *const i = info();
    if (i->magic != APP_INFO_MAGIC || i->check != (~i->magic ^ i->size ^ i->crc)) {
        return 0;
    }
    if (i->size < 8u || i->size > APP_MAX_SIZE || (i->size % 8u) != 0u) {
        return 0;
    }
    const uint32_t *vectors = (const uint32_t *)APP_BASE;
    if (vectors[0] < SRAM_BASE || vectors[0] > SRAM_BASE + SRAM_SIZE_MAX
        || vectors[1] < APP_BASE || vectors[1] >= APP_BASE + i->size || (vectors[1] & 1u) == 0u) {
        return 0;
    }
    return flash_crc(APP_BASE, i->size) == i->crc;
}

uint32_t app_size(void)
{
    return app_valid() ? info()->size : 0u;
}

int app_info_write(uint32_t size)
{
    app_info_t record;
    record.magic = APP_INFO_MAGIC;
    record.size = size;
    record.crc = flash_crc(APP_BASE, size);
    record.check = ~record.magic ^ record.size ^ record.crc;
    return flash_erase_page(APP_INFO_ADDR)
        && flash_program(APP_INFO_ADDR, (const uint8_t *)&record, sizeof record);
}

int app_info_clear(void)
{
    return flash_erase_page(APP_INFO_ADDR);
}
