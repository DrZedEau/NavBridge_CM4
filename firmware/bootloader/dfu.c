/* SPDX-License-Identifier: GPL-2.0-only
 *
 * USB DFU 1.1 device, DFU mode only, for the NavBridge CM4 bootloader.
 *
 * Everything goes through endpoint 0. A DFU_DNLOAD carries one block of up to 2 KB, which
 * is one flash page of the firmware area: block n is written at APP_BASE + n * 2 KB. The
 * interrupt only stores the block; the main loop (dfu_poll) erases and programs it while
 * the host polls DFU_GETSTATUS, which answers dfuDNBUSY meanwhile. Writing block 0 first
 * clears the firmware info, so the firmware only becomes valid again at the end: the
 * zero-length DFU_DNLOAD (manifestation) checks the image and writes its size and CRC.
 *
 * The device is manifestation tolerant: after manifestation it reports dfuIDLE, then starts
 * the new firmware on its own a moment later.
 */
#include "dfu.h"
#include <string.h>
#include "board.h"
#include "config.h"
#include "flash.h"
#include "layout.h"
#include "usb_drd.h"

#define EP0_SIZE   64u
#define PMA_EP0_TX 0x040u
#define PMA_EP0_RX 0x080u
#define BLOCK_SIZE FLASH_PAGE_BYTES

#define START_DELAY_MS 300u  /* after the host has read the final status */

/* DFU class requests. */
#define DFU_DETACH    0u
#define DFU_DNLOAD    1u
#define DFU_UPLOAD    2u
#define DFU_GETSTATUS 3u
#define DFU_CLRSTATUS 4u
#define DFU_GETSTATE  5u
#define DFU_ABORT     6u

enum {
    APP_IDLE, APP_DETACH, DFU_IDLE, DFU_DNLOAD_SYNC, DFU_DNBUSY, DFU_DNLOAD_IDLE,
    DFU_MANIFEST_SYNC, DFU_MANIFEST, DFU_MANIFEST_WAIT_RESET, DFU_UPLOAD_IDLE, DFU_ERROR
};

enum {
    ST_OK, ST_TARGET, ST_FILE, ST_WRITE, ST_ERASE, ST_CHECK_ERASED, ST_PROG, ST_VERIFY,
    ST_ADDRESS, ST_NOTDONE, ST_FIRMWARE, ST_VENDOR, ST_USBR, ST_POR, ST_UNKNOWN, ST_STALLEDPKT
};

enum { JOB_NONE, JOB_WRITE, JOB_MANIFEST };

enum { EP0_IDLE, EP0_DATA_IN, EP0_DATA_OUT, EP0_STATUS_IN, EP0_STATUS_OUT };

static const uint8_t device_desc[18] = {
    18, 1, 0x00, 0x02,
    0x00, 0x00, 0x00,       /* class at interface level */
    EP0_SIZE,
    (uint8_t)USB_VID, (uint8_t)(USB_VID >> 8),
    (uint8_t)USB_PID_BOOTLOADER, (uint8_t)(USB_PID_BOOTLOADER >> 8),
    (uint8_t)BOOTLOADER_VERSION, (uint8_t)(BOOTLOADER_VERSION >> 8),
    1, 2, 3,
    1,
};

#define CONFIG_LEN 27u
#define DFU_FUNC_OFFSET 18u
static const uint8_t config_desc[CONFIG_LEN] = {
    9, 2, CONFIG_LEN, 0, 1, 1, 0, 0xC0, 50,
    /* Interface 0: application specific, DFU, DFU mode protocol, string 4 */
    9, 4, 0, 0, 0, 0xFE, 0x01, 0x02, 4,
    /* DFU functional: can download, can upload, manifestation tolerant; detach timeout
     * 255 ms; transfer size 2048; DFU 1.1 */
    9, 0x21, 0x07, 0xFF, 0x00, (uint8_t)BLOCK_SIZE, (uint8_t)(BLOCK_SIZE >> 8), 0x10, 0x01,
};

static uint8_t connected;
static uint8_t configured;
static volatile uint8_t state = DFU_IDLE;
static volatile uint8_t status = ST_OK;
static volatile uint8_t job = JOB_NONE;
static volatile uint8_t start_requested;
static volatile uint32_t start_at_ms;
static volatile uint8_t start_scheduled;
static volatile uint32_t last_activity_ms;

static uint8_t block[BLOCK_SIZE];
static uint32_t block_len;
static uint32_t block_num;
static uint32_t next_block;
static uint32_t image_end;  /* bytes written from APP_BASE, a multiple of 8 */

static uint8_t ep0_state;
static const uint8_t *ep0_ptr;
static uint32_t ep0_left;
static uint8_t ep0_zlp;
static uint8_t ep0_buf[64];
static uint8_t pending_address;
static uint32_t out_len;
static uint32_t out_expected;
static uint32_t out_value;

/* ---- Control endpoint ----------------------------------------------------------------- */

static void ep0_in_next(void)
{
    const uint32_t n = ep0_left > EP0_SIZE ? EP0_SIZE : ep0_left;
    pma_write(PMA_EP0_TX, ep0_ptr, n);
    BDT[0].TXBD = (n << 16) | PMA_EP0_TX;
    ep0_ptr += n;
    ep0_left -= n;
    ep_set_tx(0, USB_EP_TX_VALID);
}

static void ep0_send(const uint8_t *data, uint32_t len, uint32_t requested)
{
    if (len > requested) {
        len = requested;
    }
    ep0_ptr = data;
    ep0_left = len;
    ep0_zlp = len != 0u && len < requested && (len % EP0_SIZE) == 0u;
    ep0_state = EP0_DATA_IN;
    ep0_in_next();
    ep_set_rx(0, USB_EP_RX_VALID);  /* the host may end the data stage early */
}

static void ep0_status_in(void)
{
    BDT[0].TXBD = PMA_EP0_TX;  /* zero length */
    ep0_state = EP0_STATUS_IN;
    ep_set_tx(0, USB_EP_TX_VALID);
}

static void ep0_stall(void)
{
    ep0_state = EP0_IDLE;
    ep_set_tx(0, USB_EP_TX_STALL);
    ep_set_rx(0, USB_EP_RX_STALL);  /* the next SETUP is still accepted */
}

static void get_descriptor(uint32_t value, uint32_t length)
{
    uint32_t n;

    switch (value >> 8) {
    case 1:
        ep0_send(device_desc, sizeof device_desc, length);
        return;
    case 2:
        ep0_send(config_desc, sizeof config_desc, length);
        return;
    case 3:
        switch (value & 0xFFu) {
        case 0:
            ep0_buf[0] = 4;
            ep0_buf[1] = 3;
            ep0_buf[2] = 0x09;  /* English (United States) */
            ep0_buf[3] = 0x04;
            n = 4;
            break;
        case 1: n = usb_string_desc(USB_MANUFACTURER, ep0_buf, sizeof ep0_buf); break;
        case 2: n = usb_string_desc(USB_PRODUCT_BOOTLOADER, ep0_buf, sizeof ep0_buf); break;
        case 3: n = usb_string_desc(USB_SERIAL, ep0_buf, sizeof ep0_buf); break;
        case 4: n = usb_string_desc(USB_DFU_INTERFACE, ep0_buf, sizeof ep0_buf); break;
        default: n = 0; break;
        }
        if (n != 0u) {
            ep0_send(ep0_buf, n, length);
            return;
        }
        break;
    case 0x21:  /* DFU functional descriptor */
        ep0_send(config_desc + DFU_FUNC_OFFSET, 9, length);
        return;
    default:
        break;  /* device qualifier included: full-speed only device */
    }
    ep0_stall();
}

/* ---- DFU ------------------------------------------------------------------------------ */

static void fail(uint8_t why)
{
    status = why;
    state = DFU_ERROR;
}

/* A DFU_DNLOAD block arrived in `block`: check it, the main loop writes it. */
static void dnload_block(void)
{
    if (block_num == 0u) {
        next_block = 0;
        image_end = 0;
    }
    if (block_num != next_block) {
        fail(ST_ADDRESS);
        return;
    }
    if (block_num * BLOCK_SIZE + block_len > APP_MAX_SIZE) {
        fail(ST_ADDRESS);
        return;
    }
    state = DFU_DNLOAD_SYNC;
    job = JOB_WRITE;
}

static void send_status(uint32_t length)
{
    uint32_t poll_ms = 0;
    uint8_t shown;

    if (job == JOB_WRITE) {
        shown = DFU_DNBUSY;
        poll_ms = 50;
    } else if (job == JOB_MANIFEST) {
        shown = DFU_MANIFEST;
        poll_ms = 100;
    } else {
        if (state == DFU_DNLOAD_SYNC || state == DFU_DNBUSY) {
            state = status == ST_OK ? DFU_DNLOAD_IDLE : DFU_ERROR;
        } else if (state == DFU_MANIFEST_SYNC || state == DFU_MANIFEST) {
            state = status == ST_OK ? DFU_IDLE : DFU_ERROR;
            if (status == ST_OK) {
                /* The host has the result: start the new firmware shortly. */
                start_at_ms = board_millis() + START_DELAY_MS;
                start_scheduled = 1;
            }
        }
        shown = state;
    }
    ep0_buf[0] = status;
    ep0_buf[1] = (uint8_t)poll_ms;
    ep0_buf[2] = (uint8_t)(poll_ms >> 8);
    ep0_buf[3] = (uint8_t)(poll_ms >> 16);
    ep0_buf[4] = shown;
    ep0_buf[5] = 0;
    ep0_send(ep0_buf, 6, length);
}

static void upload(uint32_t block_index, uint32_t length)
{
    const uint32_t size = app_size();
    const uint32_t offset = block_index * length;
    const uint32_t n = offset >= size ? 0u : (size - offset < length ? size - offset : length);
    state = n < length ? DFU_IDLE : DFU_UPLOAD_IDLE;
    ep0_send((const uint8_t *)(APP_BASE + offset), n, length);
}

static void dfu_request(uint32_t type, uint32_t request, uint32_t value, uint32_t length)
{
    last_activity_ms = board_millis();

    if (type == 0x21u) {
        switch (request) {
        case DFU_DNLOAD:
            if (state != DFU_IDLE && state != DFU_DNLOAD_IDLE) {
                break;
            }
            if (length == 0u) {
                if (state != DFU_DNLOAD_IDLE) {
                    break;  /* nothing downloaded */
                }
                state = DFU_MANIFEST_SYNC;
                job = JOB_MANIFEST;
                ep0_status_in();
                return;
            }
            if (length > BLOCK_SIZE) {
                break;
            }
            out_expected = length;
            out_len = 0;
            out_value = value;
            ep0_state = EP0_DATA_OUT;
            ep_set_rx(0, USB_EP_RX_VALID);
            return;
        case DFU_CLRSTATUS:
        case DFU_ABORT:
            if (job == JOB_NONE) {
                state = DFU_IDLE;
                status = ST_OK;
            }
            ep0_status_in();
            return;
        case DFU_DETACH:
            start_requested = 1;
            ep0_status_in();
            return;
        default:
            break;
        }
    } else if (type == 0xA1u) {
        switch (request) {
        case DFU_GETSTATUS:
            send_status(length);
            return;
        case DFU_GETSTATE:
            ep0_buf[0] = state;
            ep0_send(ep0_buf, 1, length);
            return;
        case DFU_UPLOAD:
            if (state == DFU_IDLE || state == DFU_UPLOAD_IDLE) {
                upload(value, length);
                return;
            }
            break;
        default:
            break;
        }
    }
    /* Not allowed in this state: DFU says stall and go to dfuERROR. */
    if (type == 0x21u || type == 0xA1u) {
        fail(ST_STALLEDPKT);
    }
    ep0_stall();
}

static void handle_setup(void)
{
    uint8_t s[8];

    pma_read(PMA_EP0_RX, s, sizeof s);
    ep_clear_vtrx(0);
    ep_set_tx(0, USB_EP_TX_NAK);  /* a SETUP ends whatever transfer was in progress */

    const uint32_t type = s[0];
    const uint32_t request = s[1];
    const uint32_t value = (uint32_t)s[2] | ((uint32_t)s[3] << 8);
    const uint32_t length = (uint32_t)s[6] | ((uint32_t)s[7] << 8);

    ep0_left = 0;
    ep0_zlp = 0;
    pending_address = 0;

    switch (type) {
    case 0x80:  /* standard, device, to host */
        if (request == REQ_GET_STATUS) {
            ep0_buf[0] = 0x01;  /* self-powered */
            ep0_buf[1] = 0;
            ep0_send(ep0_buf, 2, length);
            return;
        }
        if (request == REQ_GET_DESCRIPTOR) {
            get_descriptor(value, length);
            return;
        }
        if (request == REQ_GET_CONFIGURATION) {
            ep0_buf[0] = configured;
            ep0_send(ep0_buf, 1, length);
            return;
        }
        break;
    case 0x00:  /* standard, device, to device */
        if (request == REQ_SET_ADDRESS) {
            pending_address = (uint8_t)(value & 0x7Fu);  /* applied after the status stage */
            ep0_status_in();
            return;
        }
        if (request == REQ_SET_CONFIGURATION && value <= 1u) {
            configured = (uint8_t)value;
            ep0_status_in();
            return;
        }
        if (request == REQ_CLEAR_FEATURE || request == REQ_SET_FEATURE) {
            ep0_status_in();
            return;
        }
        break;
    case 0x81:  /* standard, interface, to host */
        if (request == REQ_GET_DESCRIPTOR) {
            get_descriptor(value, length);  /* DFU functional descriptor */
            return;
        }
        if (request == REQ_GET_STATUS || request == REQ_GET_INTERFACE) {
            ep0_buf[0] = 0;
            ep0_buf[1] = 0;
            ep0_send(ep0_buf, request == REQ_GET_STATUS ? 2u : 1u, length);
            return;
        }
        break;
    case 0x01:  /* standard, interface, to device */
        if (request == REQ_SET_INTERFACE && value == 0u) {
            ep0_status_in();
            return;
        }
        break;
    case 0x82:  /* standard, endpoint, to host */
        if (request == REQ_GET_STATUS) {
            ep0_buf[0] = 0;
            ep0_buf[1] = 0;
            ep0_send(ep0_buf, 2, length);
            return;
        }
        break;
    case 0x02:  /* standard, endpoint, to device */
        if (request == REQ_CLEAR_FEATURE || request == REQ_SET_FEATURE) {
            ep0_status_in();
            return;
        }
        break;
    case 0x21:
    case 0xA1:
        dfu_request(type, request, value, length);
        return;
    default:
        break;
    }
    ep0_stall();
}

static void ep0_ctr(void)
{
    const uint32_t v = EPR(0);

    if ((v & USB_CHEP_VTTX) != 0u) {
        ep_clear_vttx(0);
        if (ep0_state == EP0_DATA_IN) {
            if (ep0_left != 0u) {
                ep0_in_next();
            } else if (ep0_zlp) {
                ep0_zlp = 0;
                ep0_in_next();
            } else {
                ep0_state = EP0_STATUS_OUT;
            }
        } else if (ep0_state == EP0_STATUS_IN) {
            if (pending_address != 0u) {
                USB_DRD_FS->DADDR = USB_DADDR_EF | pending_address;
                pending_address = 0;
            }
            ep0_state = EP0_IDLE;
            ep_set_rx(0, USB_EP_RX_VALID);
        }
    }

    if ((v & USB_CHEP_VTRX) != 0u) {
        if ((v & USB_CHEP_SETUP) != 0u) {
            handle_setup();
        } else if (ep0_state == EP0_DATA_OUT) {
            /* A DFU_DNLOAD block, 64 bytes per packet. */
            const uint32_t n = BD_COUNT(BDT[0].RXBD);
            const uint32_t room = out_expected - out_len;
            pma_read(PMA_EP0_RX, block + out_len, n < room ? n : room);
            out_len += n < room ? n : room;
            ep_clear_vtrx(0);
            if (out_len >= out_expected || n < EP0_SIZE) {
                block_num = out_value;
                block_len = out_len;
                dnload_block();
                ep0_status_in();
            }
            ep_set_rx(0, USB_EP_RX_VALID);
        } else {
            /* Status stage of a control read (possibly ending it early). */
            ep_clear_vtrx(0);
            if (ep0_state == EP0_DATA_IN) {
                ep_set_tx(0, USB_EP_TX_NAK);
            }
            ep0_state = EP0_IDLE;
            ep_set_rx(0, USB_EP_RX_VALID);
        }
    }
}

static void bus_reset(void)
{
    configured = 0;
    pending_address = 0;
    ep0_state = EP0_IDLE;

    BDT[0].TXBD = PMA_EP0_TX;
    BDT[0].RXBD = RXBD_64 | PMA_EP0_RX;
    ep_open(0, USB_EP_CONTROL, USB_EP_TX_NAK, USB_EP_RX_VALID);
    for (uint32_t ep = 1; ep < USB_DRD_FS_EP_NBR; ep++) {
        ep_open(ep, USB_EP_BULK, USB_EP_TX_DIS, USB_EP_RX_DIS);
    }
    USB_DRD_FS->DADDR = USB_DADDR_EF;  /* address 0 */

    /* A reset after a completed download (dfu-util -R): start the new firmware. */
    if (state == DFU_IDLE && start_scheduled) {
        start_requested = 1;
    }
}

void USB_DRD_FS_IRQHandler(void)
{
    if ((USB_DRD_FS->ISTR & USB_ISTR_RESET) != 0u) {
        USB_DRD_FS->ISTR = ~USB_ISTR_RESET;
        bus_reset();
    }
    uint32_t istr;
    while (((istr = USB_DRD_FS->ISTR) & USB_ISTR_CTR) != 0u) {
        const uint32_t ep = istr & USB_ISTR_IDN;
        if (ep == 0u) {
            ep0_ctr();
        } else {
            const uint32_t v = EPR(ep);
            EPR(ep) = v & USB_CHEP_REG_MASK & ~(USB_CHEP_VTRX | USB_CHEP_VTTX);
        }
    }
}

/* ---- Main loop ------------------------------------------------------------------------ */

static void write_block(void)
{
    const uint32_t addr = APP_BASE + block_num * BLOCK_SIZE;
    const uint32_t len = (block_len + 7u) & ~7u;
    uint8_t why = ST_OK;

    memset(block + block_len, 0xFF, len - block_len);
    if (block_num == 0u && !app_info_clear()) {
        why = ST_ERASE;  /* from now on the old firmware is no longer valid */
    } else if (!flash_erase_page(addr)) {
        why = ST_ERASE;
    } else if (!flash_program(addr, block, len)) {
        why = ST_PROG;
    }
    if (why == ST_OK) {
        next_block = block_num + 1u;
        image_end = block_num * BLOCK_SIZE + len;
    } else {
        fail(why);
    }
}

static void manifest(void)
{
    const uint32_t *vectors = (const uint32_t *)APP_BASE;

    if (image_end == 0u) {
        fail(ST_NOTDONE);
    } else if (vectors[0] < SRAM_BASE || vectors[0] > SRAM_BASE + SRAM_SIZE_MAX
               || vectors[1] < APP_BASE || vectors[1] >= APP_BASE + image_end
               || (vectors[1] & 1u) == 0u) {
        fail(ST_FIRMWARE);  /* not a firmware linked for APP_BASE */
    } else if (!app_info_write(image_end)) {
        fail(ST_WRITE);
    }
}

void dfu_poll(void)
{
    if (job == JOB_WRITE) {
        write_block();
        job = JOB_NONE;
    } else if (job == JOB_MANIFEST) {
        manifest();
        job = JOB_NONE;
    }
    if (start_scheduled && (int32_t)(board_millis() - start_at_ms) >= 0) {
        start_requested = 1;
    }
}

void dfu_connect(void)
{
    if (connected) {
        return;
    }
    configured = 0;
    if (usb_drd_power_on()) {
        connected = 1;
    }
}

void dfu_disconnect(void)
{
    if (!connected) {
        return;
    }
    usb_drd_power_off();
    configured = 0;
    connected = 0;
}

uint32_t dfu_last_activity_ms(void)
{
    return last_activity_ms;
}

int dfu_start_requested(void)
{
    return start_requested;
}
