/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Low-level helpers for the STM32C071 USB DRD full-speed peripheral, shared by the
 * firmware's CDC device and the bootloader's DFU device.
 *
 * Endpoint registers (CHEPnR): STAT_TX, STAT_RX, DTOG_TX and DTOG_RX toggle when written
 * with 1; VTRX and VTTX are cleared by writing 0. Every write below keeps the other bits by
 * writing them back as read, with the toggles at 0 and VTRX/VTTX at 1.
 *
 * Packet memory (2 KB, 32-bit access): the buffer descriptor table (8 bytes per endpoint)
 * at 0, then the buffers.
 */
#ifndef USB_DRD_H
#define USB_DRD_H

#include <stdint.h>
#include "board.h"

/* Receive buffer descriptor for 64 bytes: BLSIZE = 1 (32-byte blocks), NUM_BLOCK = 1. */
#define RXBD_64 (0x80000000u | (1u << 26))
#define BD_COUNT(bd) (((bd) >> 16) & 0x3FFu)

#define EPR(n) (((volatile uint32_t *)&USB_DRD_FS->CHEP0R)[n])
#define BDT    USB_DRD_PMA_BUFF

#define EP_TOGGLES (USB_CHEP_TX_STTX | USB_CHEP_RX_STRX | USB_CHEP_DTOG_TX | USB_CHEP_DTOG_RX)
#define EP_KEEP    (USB_CHEP_VTRX | USB_CHEP_VTTX)

/* Standard requests. */
#define REQ_GET_STATUS        0x00u
#define REQ_CLEAR_FEATURE     0x01u
#define REQ_SET_FEATURE       0x03u
#define REQ_SET_ADDRESS       0x05u
#define REQ_GET_DESCRIPTOR    0x06u
#define REQ_GET_CONFIGURATION 0x08u
#define REQ_SET_CONFIGURATION 0x09u
#define REQ_GET_INTERFACE     0x0Au
#define REQ_SET_INTERFACE     0x0Bu

static inline void pma_write(uint32_t offset, const uint8_t *src, uint32_t n)
{
    volatile uint32_t *dst = (volatile uint32_t *)(USB_DRD_PMAADDR + offset);
    for (; n >= 4u; n -= 4u, src += 4u) {
        *dst++ = (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16)
               | ((uint32_t)src[3] << 24);
    }
    if (n != 0u) {
        uint32_t word = 0;
        for (uint32_t i = 0; i < n; i++) {
            word |= (uint32_t)src[i] << (8u * i);
        }
        *dst = word;
    }
}

static inline void pma_read(uint32_t offset, uint8_t *dst, uint32_t n)
{
    const volatile uint32_t *src = (const volatile uint32_t *)(USB_DRD_PMAADDR + offset);
    for (; n >= 4u; n -= 4u, dst += 4u) {
        const uint32_t word = *src++;
        dst[0] = (uint8_t)word;
        dst[1] = (uint8_t)(word >> 8);
        dst[2] = (uint8_t)(word >> 16);
        dst[3] = (uint8_t)(word >> 24);
    }
    if (n != 0u) {
        const uint32_t word = *src;
        for (uint32_t i = 0; i < n; i++) {
            dst[i] = (uint8_t)(word >> (8u * i));
        }
    }
}

static inline void ep_set_tx(uint32_t ep, uint32_t stat)
{
    const uint32_t v = EPR(ep);
    EPR(ep) = ((v & (USB_CHEP_REG_MASK | USB_CHEP_TX_STTX)) ^ stat) | EP_KEEP;
}

static inline void ep_set_rx(uint32_t ep, uint32_t stat)
{
    const uint32_t v = EPR(ep);
    EPR(ep) = ((v & (USB_CHEP_REG_MASK | USB_CHEP_RX_STRX)) ^ stat) | EP_KEEP;
}

static inline void ep_clear_vtrx(uint32_t ep)
{
    const uint32_t v = EPR(ep);
    EPR(ep) = (v & USB_CHEP_REG_MASK & ~USB_CHEP_VTRX) | USB_CHEP_VTTX;
}

static inline void ep_clear_vttx(uint32_t ep)
{
    const uint32_t v = EPR(ep);
    EPR(ep) = (v & USB_CHEP_REG_MASK & ~USB_CHEP_VTTX) | USB_CHEP_VTRX;
}

/* Type and address; both data toggles back to DATA0; the given TX and RX states; no
 * transfer pending. */
static inline void ep_open(uint32_t ep, uint32_t type, uint32_t tx_stat, uint32_t rx_stat)
{
    const uint32_t v = EPR(ep);
    EPR(ep) = type | ep | ((v & EP_TOGGLES) ^ (tx_stat | rx_stat));
}

static inline void ep_reset_toggle(uint32_t ep, uint32_t dtog)
{
    const uint32_t v = EPR(ep);
    if ((v & dtog) != 0u) {
        EPR(ep) = (v & USB_CHEP_REG_MASK) | dtog | EP_KEEP;
    }
}

/* String descriptor from ASCII into out (at most max bytes). Returns its length. */
static inline uint32_t usb_string_desc(const char *s, uint8_t *out, uint32_t max)
{
    uint32_t n = 2;
    while (*s != '\0' && n + 2u <= max) {
        out[n++] = (uint8_t)*s++;
        out[n++] = 0;
    }
    out[0] = (uint8_t)n;
    out[1] = 3;
    return n;
}

/* 48 MHz USB clock from HSIUSB48, trimmed by the CRS on the host's SOF (1 kHz); fresh
 * peripheral; D+ pull-up; reset and transfer interrupts. Returns 0 if the oscillator does
 * not start. */
static inline int usb_drd_power_on(void)
{
    RCC->CR |= RCC_CR_HSIUSB48ON;
    const uint32_t start = board_micros();
    while ((RCC->CR & RCC_CR_HSIUSB48RDY) == 0u) {
        if (board_micros() - start > 10000u) {
            return 0;
        }
    }
    RCC->CCIPR2 &= ~RCC_CCIPR2_USBSEL;
    RCC->APBENR1 |= RCC_APBENR1_USBEN | RCC_APBENR1_CRSEN;
    (void)RCC->APBENR1;
    RCC->APBRSTR1 |= RCC_APBRSTR1_USBRST | RCC_APBRSTR1_CRSRST;
    RCC->APBRSTR1 &= ~(RCC_APBRSTR1_USBRST | RCC_APBRSTR1_CRSRST);
    CRS->CR |= CRS_CR_AUTOTRIMEN | CRS_CR_CEN;  /* reset config: USB SOF, 48 MHz reload */

    USB_DRD_FS->CNTR = USB_CNTR_USBRST;  /* powered (PDWN = 0), held in reset */
    board_delay_us(10);
    USB_DRD_FS->CNTR = 0;
    USB_DRD_FS->ISTR = 0;

    /* D+ pull-up first. Without it the lines sit at SE0, which the peripheral reads as a
     * bus reset over and over: with the interrupt already on, it would never get here. */
    USB_DRD_FS->BCDR |= USB_BCDR_DPPU;
    board_delay_us(100);
    USB_DRD_FS->ISTR = 0;
    USB_DRD_FS->CNTR = USB_CNTR_CTRM | USB_CNTR_RESETM;
    NVIC_SetPriority(USB_DRD_FS_IRQn, 2);
    NVIC_ClearPendingIRQ(USB_DRD_FS_IRQn);
    NVIC_EnableIRQ(USB_DRD_FS_IRQn);
    return 1;
}

/* D+ released, peripheral and its clocks off. */
static inline void usb_drd_power_off(void)
{
    NVIC_DisableIRQ(USB_DRD_FS_IRQn);
    USB_DRD_FS->BCDR &= ~USB_BCDR_DPPU;
    USB_DRD_FS->CNTR = USB_CNTR_USBRST | USB_CNTR_PDWN;
    CRS->CR &= ~(CRS_CR_AUTOTRIMEN | CRS_CR_CEN);
    RCC->APBENR1 &= ~(RCC_APBENR1_USBEN | RCC_APBENR1_CRSEN);
    RCC->CR &= ~RCC_CR_HSIUSB48ON;
}

#endif
