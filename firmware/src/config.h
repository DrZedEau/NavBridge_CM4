/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Build-time settings of the NavBridge CM4 MCU firmware (STM32C071KBT6, U9).
 */
#ifndef CONFIG_H
#define CONFIG_H

/* Set by the Makefile from `git describe`. */
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

/* ---------------------------------------------------------------------------------------
 * CSYNC
 *
 * DPI mode of the BMW display, as in the Raspberry Pi config.txt (vc4-kms-dpi-generic):
 *   clock-frequency=8000000, hactive=400, hfp=8, hsync=40, hbp=60, hsync-invert, vsync-invert
 *
 *   HSYNC width = hsync pixels / pixel clock = 40 / 8 MHz  = 5.0 us
 *   Line period = total pixels / pixel clock = 508 / 8 MHz = 63.5 us
 *
 * CSYNC follows HSYNC (5.0 us). While VSYNC is active, as in the progressive program of
 * raspberrypi/utils piolib/examples/dpi_csync.c, the pulse lasts one line minus one HSYNC
 * width: 63.5 - 5.0 = 58.5 us.
 */
#define CSYNC_PIXEL_CLOCK_KHZ  8000u
#define CSYNC_HSYNC_PIXELS     40u
#define CSYNC_LINE_PIXELS      508u   /* 400 + 8 + 40 + 60 */
#define CSYNC_HSYNC_ACTIVE_LOW 1      /* hsync-invert */
#define CSYNC_VSYNC_ACTIVE_LOW 1      /* vsync-invert */
#define CSYNC_ACTIVE_LOW       1      /* ADV7125 SYNC input: low = sync tip */

/* ---------------------------------------------------------------------------------------
 * I-Bus (9600 baud, 8 data bits, even parity, 1 stop bit)
 *
 * LIN1 = U8, car I-Bus (USART1). LIN2 = U10, navigation module I-Bus (USART2).
 * Every valid frame heard on one side is sent unchanged on the other side.
 */
#define IBUS_FORWARD_CAR_TO_NAV 1
#define IBUS_FORWARD_NAV_TO_CAR 1

/* Frame bytes: source, length, destination, data..., checksum. The length byte counts the
 * bytes after itself. */
#define IBUS_MIN_LENGTH      3u    /* destination, message id, checksum */
#define IBUS_MAX_LENGTH      62u   /* longest I-Bus messages are well below this */
#define IBUS_MAX_FRAME       (IBUS_MAX_LENGTH + 2u)

#define IBUS_GAP_US          5000u /* silence that ends an incomplete frame */
#define IBUS_IDLE_US         3000u /* silence after the last byte before we transmit */
#define IBUS_ECHO_TIMEOUT_US 3000u /* our own byte must come back from the transceiver */
#define IBUS_TX_RETRIES      8u    /* collisions before a frame is dropped */
#define IBUS_TX_QUEUE        8u    /* frames waiting per bus */

/* ---------------------------------------------------------------------------------------
 * Power
 */
#define POWER_SENSE_DEBOUNCE_MS     50u     /* +3V3_SENSE must be stable this long */
#define POWER_BUCK_OFF_DELAY_MS     2000u   /* CM 3.3 V gone this long: buck off */
#define POWER_BUCK_START_TIMEOUT_MS 30000u  /* buck on but the CM never came up: buck off */

/* Parked car: with the CM and the buck off, both LIN transceivers go to Sleep once the
 * I-Bus has been quiet this long. The car-side one's regulator powers the MCU, so the MCU
 * switches off with it; the next I-Bus activity wakes the transceiver and the MCU starts
 * again. */
#define IBUS_SLEEP_TIMEOUT_MS       30000u

/* ---------------------------------------------------------------------------------------
 * CD changer emulator (src/cdchanger.h): answers the radio as a CD changer (address 18)
 * while the ignition is on, unless the car has a real one.
 */
#define CDCHANGER_EMULATOR    1        /* 0: never */
#define CDCHANGER_ANSWER_MS   200u     /* a radio poll unanswered this long: no real changer */
#define CDCHANGER_LISTEN_MS   10000u   /* ignition on this long without a radio poll: start */

/* ---------------------------------------------------------------------------------------
 * USB (CDC ACM, to the Raspberry Pi through the USB2514B hub)
 *
 * On the Pi: /dev/serial/by-id/usb-NavBridge_NavBridge_CM4_MCU-if00
 */
#define USB_VID           0x0483u   /* STMicroelectronics */
#define USB_PID           0x5740u   /* ST Virtual COM Port */
#define USB_MANUFACTURER  "NavBridge"
#define USB_PRODUCT       "NavBridge CM4"
#define USB_SERIAL        "MCU"

/* The bootloader, a USB DFU device (dfu-util -d 0483:5741). Not ST's own bootloader,
 * which is 0483:df11 and only used to install this one on a blank chip. */
#define USB_PID_BOOTLOADER     0x5741u
#define USB_PRODUCT_BOOTLOADER "NavBridge CM4 bootloader"
#define USB_DFU_INTERFACE      "NavBridge CM4 firmware"

/* Bootloader version, as its USB bcdDevice (lsusb, /sys/.../bcdDevice): 0x0100 was the
 * first build, 0x0101 keeps the buck on and does not start the firmware early. */
#define BOOTLOADER_VERSION     0x0101u

#endif
