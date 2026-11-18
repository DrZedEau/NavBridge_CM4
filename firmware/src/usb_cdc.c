/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Minimal USB CDC ACM device for the STM32C071 USB DRD full-speed peripheral.
 *
 * Endpoints: 0 control (64 bytes), 1 IN interrupt (CDC notifications, never used),
 * 2 OUT and IN bulk (64 bytes, the serial data). Register and packet memory helpers are in
 * usb_drd.h.
 */
#include "usb_cdc.h"
#include "board.h"
#include "config.h"
#include "usb_drd.h"

#define EP0_SIZE   64u
#define NOTIF_EP   1u
#define NOTIF_SIZE 8u
#define DATA_EP    2u
#define DATA_SIZE  64u

#define PMA_EP0_TX 0x040u
#define PMA_EP0_RX 0x080u
#define PMA_EP1_TX 0x0C0u
#define PMA_EP2_TX 0x100u
#define PMA_EP2_RX 0x140u

#define TX_RING 2048u  /* power of two */
#define RX_RING 256u   /* power of two */

/* CDC requests. */
#define CDC_SET_LINE_CODING        0x20u
#define CDC_GET_LINE_CODING        0x21u
#define CDC_SET_CONTROL_LINE_STATE 0x22u
#define CDC_SEND_BREAK             0x23u

enum { EP0_IDLE, EP0_DATA_IN, EP0_DATA_OUT, EP0_STATUS_IN, EP0_STATUS_OUT };

static const uint8_t device_desc[18] = {
    18, 1,                  /* bLength, DEVICE */
    0x00, 0x02,             /* USB 2.0 */
    0x02, 0x00, 0x00,       /* class CDC (communications), at interface level */
    EP0_SIZE,
    (uint8_t)USB_VID, (uint8_t)(USB_VID >> 8),
    (uint8_t)USB_PID, (uint8_t)(USB_PID >> 8),
    0x00, 0x01,             /* bcdDevice 1.00 */
    1, 2, 3,                /* manufacturer, product, serial number strings */
    1,                      /* one configuration */
};

#define CONFIG_LEN 67u
static const uint8_t config_desc[CONFIG_LEN] = {
    9, 2, CONFIG_LEN, 0, 2, 1, 0, 0xC0, 50,   /* 2 interfaces, self-powered, 100 mA */
    /* Interface 0: CDC communication, ACM, no AT command protocol */
    9, 4, 0, 0, 1, 0x02, 0x02, 0x00, 0,
    5, 0x24, 0x00, 0x10, 0x01,                /* header, CDC 1.10 */
    5, 0x24, 0x01, 0x00, 0x01,                /* call management: data on interface 1 */
    4, 0x24, 0x02, 0x02,                      /* ACM: line coding and control line state */
    5, 0x24, 0x06, 0, 1,                      /* union: master 0, slave 1 */
    7, 5, 0x80 | NOTIF_EP, 0x03, NOTIF_SIZE, 0, 16,
    /* Interface 1: CDC data */
    9, 4, 1, 0, 2, 0x0A, 0x00, 0x00, 0,
    7, 5, DATA_EP, 0x02, DATA_SIZE, 0, 0,
    7, 5, 0x80 | DATA_EP, 0x02, DATA_SIZE, 0, 0,
};

static uint8_t connected;
static volatile uint8_t configured;
static volatile uint8_t dtr;
static uint8_t line_coding[7] = { 0x80, 0x25, 0x00, 0x00, 0, 0, 8 };  /* 9600 8N1 */

static uint8_t ep0_state;
static const uint8_t *ep0_ptr;
static uint32_t ep0_left;
static uint8_t ep0_zlp;
static uint8_t ep0_buf[64];
static uint8_t pending_address;

static volatile uint8_t tx_ring[TX_RING];
static volatile uint32_t tx_head;  /* main loop */
static volatile uint32_t tx_tail;  /* interrupt */
static volatile uint8_t in_busy;
static volatile uint8_t in_zlp;

static volatile uint8_t rx_ring[RX_RING];
static volatile uint32_t rx_head;  /* interrupt */
static volatile uint32_t rx_tail;  /* main loop */

/* ---- Bulk data ------------------------------------------------------------------------ */

/* From the USB interrupt, or with it masked. */
static void tx_start(void)
{
    uint8_t packet[DATA_SIZE];

    if (!configured || in_busy) {
        return;
    }
    const uint32_t avail = tx_head - tx_tail;
    if (avail == 0u) {
        if (in_zlp) {
            /* A full last packet: end the transfer with a zero-length one. */
            in_zlp = 0;
            in_busy = 1;
            BDT[DATA_EP].TXBD = PMA_EP2_TX;
            ep_set_tx(DATA_EP, USB_EP_TX_VALID);
        }
        return;
    }
    const uint32_t n = avail > DATA_SIZE ? DATA_SIZE : avail;
    const uint32_t tail = tx_tail;
    for (uint32_t i = 0; i < n; i++) {
        packet[i] = tx_ring[(tail + i) & (TX_RING - 1u)];
    }
    pma_write(PMA_EP2_TX, packet, n);
    BDT[DATA_EP].TXBD = (n << 16) | PMA_EP2_TX;
    tx_tail = tail + n;
    in_zlp = n == DATA_SIZE;
    in_busy = 1;
    ep_set_tx(DATA_EP, USB_EP_TX_VALID);
}

static void data_ep_ctr(void)
{
    const uint32_t v = EPR(DATA_EP);

    if ((v & USB_CHEP_VTRX) != 0u) {
        uint8_t packet[DATA_SIZE];
        uint32_t n = BD_COUNT(BDT[DATA_EP].RXBD);
        if (n > DATA_SIZE) {
            n = DATA_SIZE;
        }
        pma_read(PMA_EP2_RX, packet, n);
        ep_clear_vtrx(DATA_EP);
        for (uint32_t i = 0; i < n; i++) {
            const uint32_t head = rx_head;
            if (head - rx_tail >= RX_RING) {
                break;  /* main loop not reading: drop */
            }
            rx_ring[head & (RX_RING - 1u)] = packet[i];
            rx_head = head + 1u;
        }
        ep_set_rx(DATA_EP, USB_EP_RX_VALID);
    }
    if ((v & USB_CHEP_VTTX) != 0u) {
        ep_clear_vttx(DATA_EP);
        in_busy = 0;
        tx_start();
    }
}

static int set_configuration(uint32_t value)
{
    if (value > 1u) {
        return 0;
    }
    configured = (uint8_t)value;
    in_busy = 0;
    in_zlp = 0;
    tx_tail = tx_head;
    if (value != 0u) {
        BDT[NOTIF_EP].TXBD = PMA_EP1_TX;
        BDT[NOTIF_EP].RXBD = 0;
        ep_open(NOTIF_EP, USB_EP_INTERRUPT, USB_EP_TX_NAK, USB_EP_RX_DIS);
        BDT[DATA_EP].TXBD = PMA_EP2_TX;
        BDT[DATA_EP].RXBD = RXBD_64 | PMA_EP2_RX;
        ep_open(DATA_EP, USB_EP_BULK, USB_EP_TX_NAK, USB_EP_RX_VALID);
    } else {
        ep_open(NOTIF_EP, USB_EP_INTERRUPT, USB_EP_TX_DIS, USB_EP_RX_DIS);
        ep_open(DATA_EP, USB_EP_BULK, USB_EP_TX_DIS, USB_EP_RX_DIS);
        dtr = 0;
    }
    return 1;
}

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

static uint32_t string_desc(uint32_t index, uint8_t *out)
{
    const char *s;

    switch (index) {
    case 0:
        out[0] = 4;
        out[1] = 3;
        out[2] = 0x09;  /* English (United States) */
        out[3] = 0x04;
        return 4;
    case 1: s = USB_MANUFACTURER; break;
    case 2: s = USB_PRODUCT; break;
    case 3: s = USB_SERIAL; break;
    default: return 0;
    }
    uint32_t n = 2;
    while (*s != '\0' && n + 2u <= sizeof ep0_buf) {
        out[n++] = (uint8_t)*s++;
        out[n++] = 0;
    }
    out[0] = (uint8_t)n;
    out[1] = 3;
    return n;
}

static void get_descriptor(uint32_t value, uint32_t length)
{
    switch (value >> 8) {
    case 1:
        ep0_send(device_desc, sizeof device_desc, length);
        return;
    case 2:
        ep0_send(config_desc, sizeof config_desc, length);
        return;
    case 3: {
        const uint32_t n = string_desc(value & 0xFFu, ep0_buf);
        if (n != 0u) {
            ep0_send(ep0_buf, n, length);
            return;
        }
        break;
    }
    default:
        break;  /* device qualifier included: full-speed only device */
    }
    ep0_stall();
}

static int endpoint_feature(uint32_t request, uint32_t index)
{
    const uint32_t ep = index & 0x0Fu;
    const int in = (index & 0x80u) != 0u;

    if (ep != DATA_EP && ep != NOTIF_EP) {
        return ep == 0u;
    }
    if (request == REQ_CLEAR_FEATURE) {
        if (in) {
            ep_reset_toggle(ep, USB_CHEP_DTOG_TX);
            ep_set_tx(ep, USB_EP_TX_NAK);
            if (ep == DATA_EP) {
                in_busy = 0;
                tx_start();
            }
        } else {
            ep_reset_toggle(ep, USB_CHEP_DTOG_RX);
            ep_set_rx(ep, ep == DATA_EP ? USB_EP_RX_VALID : USB_EP_RX_DIS);
        }
    } else if (in) {
        ep_set_tx(ep, USB_EP_TX_STALL);
    } else {
        ep_set_rx(ep, USB_EP_RX_STALL);
    }
    return 1;
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
    const uint32_t index = (uint32_t)s[4] | ((uint32_t)s[5] << 8);
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
        if (request == REQ_SET_CONFIGURATION && set_configuration(value & 0xFFu)) {
            ep0_status_in();
            return;
        }
        if (request == REQ_CLEAR_FEATURE || request == REQ_SET_FEATURE) {
            ep0_status_in();  /* remote wakeup: accepted, not used */
            return;
        }
        break;
    case 0x81:  /* standard, interface, to host */
        if (request == REQ_GET_STATUS) {
            ep0_buf[0] = 0;
            ep0_buf[1] = 0;
            ep0_send(ep0_buf, 2, length);
            return;
        }
        if (request == REQ_GET_INTERFACE) {
            ep0_buf[0] = 0;
            ep0_send(ep0_buf, 1, length);
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
            const uint32_t ep = index & 0x0Fu;
            const uint32_t v = ep < 8u ? EPR(ep) : 0u;
            const uint32_t stat = (index & 0x80u) ? (v & USB_CHEP_TX_STTX) : (v & USB_CHEP_RX_STRX);
            ep0_buf[0] = (stat == USB_EP_TX_STALL || stat == USB_EP_RX_STALL) ? 1u : 0u;
            ep0_buf[1] = 0;
            ep0_send(ep0_buf, 2, length);
            return;
        }
        break;
    case 0x02:  /* standard, endpoint, to device */
        if ((request == REQ_CLEAR_FEATURE || request == REQ_SET_FEATURE) && value == 0u
            && endpoint_feature(request, index)) {
            ep0_status_in();
            return;
        }
        break;
    case 0x21:  /* class, interface, to device */
        if (request == CDC_SET_LINE_CODING && length == sizeof line_coding) {
            ep0_state = EP0_DATA_OUT;
            ep_set_rx(0, USB_EP_RX_VALID);
            return;
        }
        if (request == CDC_SET_CONTROL_LINE_STATE) {
            const uint8_t open = (uint8_t)(value & 1u);
            if (open != dtr) {
                tx_tail = tx_head;  /* nothing stale for the next reader */
            }
            dtr = open;
            ep0_status_in();
            return;
        }
        if (request == CDC_SEND_BREAK) {
            ep0_status_in();
            return;
        }
        break;
    case 0xA1:  /* class, interface, to host */
        if (request == CDC_GET_LINE_CODING) {
            ep0_send(line_coding, sizeof line_coding, length);
            return;
        }
        break;
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
            if (BD_COUNT(BDT[0].RXBD) >= sizeof line_coding) {
                pma_read(PMA_EP0_RX, line_coding, sizeof line_coding);
            }
            ep_clear_vtrx(0);
            ep0_status_in();
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
    dtr = 0;
    in_busy = 0;
    in_zlp = 0;
    pending_address = 0;
    ep0_state = EP0_IDLE;

    BDT[0].TXBD = PMA_EP0_TX;
    BDT[0].RXBD = RXBD_64 | PMA_EP0_RX;
    ep_open(0, USB_EP_CONTROL, USB_EP_TX_NAK, USB_EP_RX_VALID);
    for (uint32_t ep = 1; ep < USB_DRD_FS_EP_NBR; ep++) {
        ep_open(ep, USB_EP_BULK, USB_EP_TX_DIS, USB_EP_RX_DIS);
    }
    USB_DRD_FS->DADDR = USB_DADDR_EF;  /* address 0 */
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
        } else if (ep == DATA_EP) {
            data_ep_ctr();
        } else {
            const uint32_t v = EPR(ep);
            EPR(ep) = v & USB_CHEP_REG_MASK & ~(USB_CHEP_VTRX | USB_CHEP_VTTX);
        }
    }
}

/* ---- API ------------------------------------------------------------------------------ */

void usb_cdc_connect(void)
{
    if (connected) {
        return;
    }
    configured = 0;
    dtr = 0;
    in_busy = 0;
    in_zlp = 0;
    tx_tail = tx_head;
    rx_tail = rx_head;
    if (usb_drd_power_on()) {
        connected = 1;
    }
    /* Otherwise the oscillator did not start: power_poll() tries again on the next change. */
}

void usb_cdc_disconnect(void)
{
    if (!connected) {
        return;
    }
    usb_drd_power_off();
    configured = 0;
    dtr = 0;
    in_busy = 0;
    in_zlp = 0;
    tx_tail = tx_head;
    connected = 0;
}

int usb_cdc_ready(void)
{
    return configured && dtr;
}

int usb_cdc_write(const void *data, uint32_t len)
{
    const uint8_t *p = data;

    if (!usb_cdc_ready()) {
        return 0;
    }
    const uint32_t head = tx_head;
    if (len > TX_RING - (head - tx_tail)) {
        return 0;
    }
    for (uint32_t i = 0; i < len; i++) {
        tx_ring[(head + i) & (TX_RING - 1u)] = p[i];
    }
    __DMB();
    tx_head = head + len;
    usb_cdc_poll();
    return 1;
}

int usb_cdc_tx_empty(void)
{
    return !configured || (tx_head == tx_tail && !in_busy);
}

uint32_t usb_cdc_read(uint8_t *buf, uint32_t max)
{
    uint32_t n = 0;
    while (n < max && rx_tail != rx_head) {
        buf[n++] = rx_ring[rx_tail & (RX_RING - 1u)];
        rx_tail = rx_tail + 1u;
    }
    return n;
}

void usb_cdc_poll(void)
{
    if (!connected) {
        return;
    }
    NVIC_DisableIRQ(USB_DRD_FS_IRQn);
    tx_start();
    NVIC_EnableIRQ(USB_DRD_FS_IRQn);
}
