/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Text lines to and from the Raspberry Pi over USB CDC (see host.h).
 */
#include "host.h"
#include "board.h"
#include "config.h"
#include "csync.h"
#include "flash.h"
#include "hardware.h"
#include "ibus.h"
#include "layout.h"
#include "power.h"
#include "usb_cdc.h"

#define LINE_MAX 160u  /* room for IBUS SEND with a full frame in hex */
#define DFU_FLUSH_MS 200u

#define RGB_UNKNOWN 0xFFu

static char line[LINE_MAX];
static uint32_t line_len;
static uint8_t line_too_long;
static uint8_t rgb_reported = RGB_UNKNOWN;  /* video source the Pi was last told */

static char *put_str(char *p, const char *s)
{
    while (*s != '\0') {
        *p++ = *s++;
    }
    return p;
}

static char *put_u32(char *p, uint32_t value)
{
    char tmp[10];
    uint32_t n = 0;
    do {
        tmp[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (n != 0u) {
        *p++ = tmp[--n];
    }
    return p;
}

static char *put_hex8(char *p, uint8_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    *p++ = digits[value >> 4];
    *p++ = digits[value & 0x0Fu];
    return p;
}

/* TIM1 ticks as microseconds with 2 decimals. */
static char *put_us(char *p, uint32_t ticks)
{
    const uint32_t centi = (ticks * 100u + CSYNC_TICKS_PER_US / 2u) / CSYNC_TICKS_PER_US;
    p = put_u32(p, centi / 100u);
    *p++ = '.';
    *p++ = (char)('0' + (centi / 10u) % 10u);
    *p++ = (char)('0' + centi % 10u);
    return put_str(p, "us");
}

static char *put_bus(char *p, const char *name, uint32_t bus)
{
    ibus_stats_t s;
    ibus_get_stats(bus, &s);
    p = put_str(p, name);
    p = put_str(p, "=rx:");
    p = put_u32(p, s.rx_frames);
    p = put_str(p, ",tx:");
    p = put_u32(p, s.tx_frames);
    p = put_str(p, ",err:");
    p = put_u32(p, s.rx_errors);
    p = put_str(p, ",bad:");
    p = put_u32(p, s.bad_frames);
    p = put_str(p, ",col:");
    p = put_u32(p, s.collisions);
    p = put_str(p, ",tmo:");
    p = put_u32(p, s.echo_timeouts);
    p = put_str(p, ",drop:");
    p = put_u32(p, s.tx_dropped);
    p = put_str(p, ",q:");
    p = put_u32(p, s.queued);
    p = put_str(p, ",bytes:");
    p = put_u32(p, s.rx_bytes);
    return p;
}

static void reply(const char *start, char *end)
{
    *end++ = '\n';
    (void)usb_cdc_write(start, (uint32_t)(end - start));
}

void host_report_frame(uint32_t bus, const uint8_t *frame, uint32_t len)
{
    char out[24 + 2u * IBUS_MAX_FRAME];

    if (!usb_cdc_ready()) {
        return;
    }
    char *p = put_str(out, bus == IBUS_CAR ? "IBUS1 RX " : "IBUS2 RX ");
    p = put_u32(p, board_millis());
    *p++ = ' ';
    for (uint32_t i = 0; i < len; i++) {
        p = put_hex8(p, frame[i]);
    }
    reply(out, p);  /* dropped whole if the USB buffer is full */
}

void host_report_rgb(void)
{
    if (!usb_cdc_ready()) {
        rgb_reported = RGB_UNKNOWN;  /* told again when the port is next opened */
        return;
    }
    const rgb_source_t now = hw_rgb();
    if (now == rgb_reported) {
        return;
    }
    static const char rpi[] = "RGB RPI\n";
    static const char nav[] = "RGB NAV\n";
    /* If the USB buffer is full it is sent from host_poll, as soon as there is room. */
    if (usb_cdc_write(now == RGB_RPI ? rpi : nav, sizeof rpi - 1u)) {
        rgb_reported = (uint8_t)now;
    }
}

static void send_status(void)
{
    char out[640];
    csync_status_t cs;
    csync_get_status(&cs);

    char *p = put_str(out, "STATUS cm=");
    p = put_str(p, power_cm_on() ? "on" : "off");
    p = put_str(p, " buck=");
    p = put_str(p, hw_buck_on() ? "on" : "off");
    p = put_str(p, " rgb=");
    p = put_str(p, hw_rgb() == RGB_RPI ? "rpi" : "nav");
    p = put_str(p, " lin=");
    p = put_str(p, ibus_lin_on() ? "on" : "off");
    p = put_str(p, " idle=");
    p = put_u32(p, ibus_idle_ms() / 1000u);
    p = put_str(p, "s sleepfail=");
    p = put_u32(p, ibus_sleep_failures());
    p = put_str(p, " csync=");
    p = put_str(p, !cs.enabled ? "off" : cs.locked ? "locked" : "searching");
    if (cs.locked) {
        p = put_str(p, " line=");
        p = put_us(p, cs.period_ticks);
        p = put_str(p, " sync=");
        p = put_us(p, cs.sync_ticks);
        p = put_str(p, " broad=");
        p = put_us(p, cs.broad_ticks);
    }
    p = put_str(p, " vsync=");
    p = put_str(p, cs.vsync_seen ? "yes" : "no");
    /* Since the previous STATUS: where VSYNC edges fell in the line (TIM1 ticks), how they
     * were applied, and the spread of the measured line period. */
    p = put_str(p, " filter=");
    p = put_u32(p, cs.filter);
    p = put_str(p, " vspos=");
    p = put_u32(p, cs.vs_pos_min);
    p = put_str(p, "..");
    p = put_u32(p, cs.vs_pos_max);
    p = put_str(p, " vsnow=");
    p = put_u32(p, cs.vs_now);
    p = put_str(p, " vsnext=");
    p = put_u32(p, cs.vs_next);
    p = put_str(p, " vswait=");
    p = put_u32(p, cs.vs_waited);
    p = put_str(p, " period=");
    p = put_u32(p, cs.period_min);
    p = put_str(p, "..");
    p = put_u32(p, cs.period_max);
    p = put_str(p, " locks=");
    p = put_u32(p, cs.locks);
    p = put_str(p, " losses=");
    p = put_u32(p, cs.losses);
    p = put_str(p, " ");
    p = put_bus(p, "ibus1", IBUS_CAR);
    p = put_str(p, " ");
    p = put_bus(p, "ibus2", IBUS_NAV);
    p = put_str(p, " boot0=");
    p = put_str(p, board_boot0_pin_enabled() ? "pin" : "ignored");
    p = put_str(p, " up=");
    p = put_u32(p, board_millis());
    reply(out, p);
}

static int same(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        char c = *a;
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (c != *b) {
            return 0;
        }
    }
    return *a == *b;
}

static void enter_bootloader(void)
{
    /* Let "DFU OK" reach the Pi if it is reading. */
    const uint32_t start = board_millis();
    while (!usb_cdc_tx_empty() && board_millis() - start < DFU_FLUSH_MS) {
        usb_cdc_poll();
    }
    /* Without valid firmware info the bootloader stays in update mode, across resets too,
     * until a complete new firmware is written. */
    (void)app_info_clear();
    PWR->BKP1R = BOOTLOADER_REQUEST;
    hw_prepare_bootloader();
    board_jump(BOOTLOADER_BASE);
}

static char *put_hex32(char *p, uint32_t value)
{
    for (int shift = 28; shift >= 0; shift -= 4) {
        *p++ = "0123456789ABCDEF"[(value >> shift) & 0xFu];
    }
    return p;
}

static void send_diag(void)
{
    char out[160];

    /* Backup register check (16 bits kept): BKP3R is not used by anything else. */
    const uint32_t before = PWR->BKP3R;
    PWR->BKP3R = 0xA5C3u;
    const uint32_t readback = PWR->BKP3R & 0xFFFFu;
    PWR->BKP3R = before;

    char *p = put_str(out, "DIAG resetflags=");
    p = put_hex32(p, board_reset_flags());
    p = put_str(p, " csr2=");
    p = put_hex32(p, RCC->CSR2);
    p = put_str(p, " bkp0=");
    p = put_hex32(p, PWR->BKP0R);
    p = put_str(p, " bkp1=");
    p = put_hex32(p, PWR->BKP1R);
    p = put_str(p, " bkptest=");
    p = put_str(p, readback == 0xA5C3u ? "ok" : "fail");
    p = put_str(p, " optr=");
    p = put_hex32(p, FLASH->OPTR);
    reply(out, p);
}

/* The rest of cmd after a case-insensitive prefix, or NULL. */
static const char *after(const char *cmd, const char *prefix)
{
    for (; *prefix != '\0'; cmd++, prefix++) {
        char c = *cmd;
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (c != *prefix) {
            return 0;
        }
    }
    return cmd;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* CSYNC FILTER n: TI1 input filter, which moves the CSYNC edge against the DAC clock. */
static void csync_filter_command(const char *arg)
{
    char out[64];
    uint32_t n = 0;
    const char *s = arg;

    if (*s == '\0') {
        reply(out, put_str(out, "ERROR CSYNC FILTER 0..15"));
        return;
    }
    for (; *s >= '0' && *s <= '9'; s++) {
        n = n * 10u + (uint32_t)(*s - '0');
    }
    if (*s != '\0' || n > 15u) {
        reply(out, put_str(out, "ERROR CSYNC FILTER 0..15"));
        return;
    }
    csync_set_filter(n);
    reply(out, put_u32(put_str(out, "CSYNC FILTER "), n));
}

/* IBUS SEND <1|2> <frame in hex>: queue a frame (checksum included) on the car (1) or the
 * navigation (2) I-Bus, for tests. */
static void ibus_send_command(const char *arg)
{
    char out[64];
    uint8_t frame[IBUS_MAX_FRAME];
    uint32_t len = 0;

    const uint32_t bus = arg[0] == '1' ? IBUS_CAR : arg[0] == '2' ? IBUS_NAV : 99u;
    if (bus == 99u || arg[1] != ' ') {
        reply(out, put_str(out, "ERROR IBUS SEND <1|2> <hex frame>"));
        return;
    }
    for (const char *s = arg + 2; *s != '\0'; s += 2) {
        const int hi = hex_digit(s[0]);
        const int lo = hex_digit(s[1]);
        if (hi < 0 || lo < 0 || len >= sizeof frame) {
            reply(out, put_str(out, "ERROR hex frame"));
            return;
        }
        frame[len++] = (uint8_t)((hi << 4) | lo);
    }
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++) {
        sum ^= frame[i];
    }
    if (len < 5u || frame[1] != len - 2u || sum != 0u) {
        reply(out, put_str(out, "ERROR frame: length byte or checksum"));
        return;
    }
    if (!ibus_send(bus, frame, len)) {
        reply(out, put_str(out, "ERROR queue full"));
        return;
    }
    reply(out, put_str(out, ibus_lin_on() ? "IBUS SEND queued" : "IBUS SEND queued (lin=off)"));
}

static void command(const char *cmd)
{
    char out[64];
    const char *arg;

    if (same(cmd, "DFU")) {
        reply(out, put_str(out, "DFU OK"));
        enter_bootloader();
    } else if (same(cmd, "VERSION")) {
        reply(out, put_str(out, "VERSION NavBridge-CM4 " FW_VERSION));
    } else if (same(cmd, "STATUS")) {
        send_status();
    } else if (same(cmd, "DIAG")) {
        send_diag();
    } else if (same(cmd, "RGB")) {
        rgb_reported = RGB_UNKNOWN;
        host_report_rgb();
    } else if ((arg = after(cmd, "CSYNC FILTER ")) != 0) {
        csync_filter_command(arg);
    } else if ((arg = after(cmd, "IBUS SEND ")) != 0) {
        ibus_send_command(arg);
    } else {
        reply(out, put_str(out, "ERROR unknown command"));
    }
}

void host_poll(void)
{
    uint8_t buf[32];
    uint32_t n;

    while ((n = usb_cdc_read(buf, sizeof buf)) != 0u) {
        for (uint32_t i = 0; i < n; i++) {
            const char c = (char)buf[i];
            if (c == '\r' || c == '\n') {
                if (line_len != 0u && !line_too_long) {
                    line[line_len] = '\0';
                    command(line);
                }
                line_len = 0;
                line_too_long = 0;
            } else if (line_len < LINE_MAX - 1u) {
                line[line_len++] = c;
            } else {
                line_too_long = 1;
            }
        }
    }
    /* The video source when the port has just been opened, or one that did not fit. */
    host_report_rgb();
}
