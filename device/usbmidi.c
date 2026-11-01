/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR on the Pocket Master: a USB-MIDI device of its own, on the SDK's OTG device driver (libDriver
 * otg_device_hcd.o), polled from the main loop.
 *
 *   identity  VID 0x84EF, PID 0x0286 (stock app 0x0284, flash_boot 0x0285), product "Pocket Master", so the Mac's
 *             MIDI port has the same name as the stock firmware's
 *   log       usb_log() keeps a line; the lines go out as SysEx  F0 7D 4D <ASCII> F7 (0x7D: the non-commercial ID;
 *             0x4D 'M'), one a poll, only once the Mac has sent anything on the port (0.18: 0.14 .. 0.17 sent
 *             lines right after the configuration, before the Mac read the port, and left the bus 20 ms later:
 *             the bulk send's own timeout); every line is sent again after each (re)configuration
 *   update    the command 11 6F, framed as the Sonicake Manager sends it, is acknowledged (14 6F 00) and hands the
 *             chip to flash_boot's update mode, as the stock firmware does: Molecular can be replaced from the Mac
 *
 * Descriptors: USB-MIDI 1.0 (the class specification's Appendix B), one cable, bulk endpoints 0x03 out, 0x82 in
 * (the SDK's endpoint numbers, otg_device_standard_request.h). */
#include <stdint.h>
#include <string.h>
#include "type.h"
#include "otg_device_hcd.h"
#include "timeout.h"
#include "hal.h"
#include "trace.h"

#define EP_IN 0x82u
#define EP_OUT 0x03u

static const uint8_t DEV_DESC[18] = {
    18, 1, 0x10, 0x01, 0, 0, 0, 64, 0xEF, 0x84, 0x86, 0x02, 0x00, 0x01, 1, 2, 3, 1,
};
static const uint8_t CFG_DESC[101] = {
    9, 2, 101, 0, 2, 1, 0, 0x80, 50,                          /* configuration: 2 interfaces, bus powered 100 mA */
    9, 4, 0, 0, 0, 1, 1, 0, 0,                                /* audio control interface */
    9, 0x24, 1, 0x00, 0x01, 9, 0, 1, 1,                       /*   header: ADC 1.0, collection: interface 1 */
    9, 4, 1, 0, 2, 1, 3, 0, 0,                                /* MIDI streaming interface, 2 endpoints */
    7, 0x24, 1, 0x00, 0x01, 65, 0,                            /*   header: MS 1.0, 65 bytes */
    6, 0x24, 2, 1, 1, 0,                                      /*   MIDI IN jack, embedded, id 1 */
    6, 0x24, 2, 2, 2, 0,                                      /*   MIDI IN jack, external, id 2 */
    9, 0x24, 3, 1, 3, 1, 2, 1, 0,                             /*   MIDI OUT jack, embedded, id 3, from jack 2 */
    9, 0x24, 3, 2, 4, 1, 1, 1, 0,                             /*   MIDI OUT jack, external, id 4, from jack 1 */
    9, 5, EP_OUT, 2, 64, 0, 0, 0, 0,                          /* bulk OUT */
    5, 0x25, 1, 1, 1,                                         /*   into embedded IN jack 1 */
    9, 5, EP_IN, 2, 64, 0, 0, 0, 0,                           /* bulk IN */
    5, 0x25, 1, 1, 3,                                         /*   from embedded OUT jack 3 */
};
static const char *const STRINGS[4] = {0, "Molecular", "Pocket Master", "MOLECULAR"};
                                                     /* (a fixed serial: the Mac blocks an accessory whose identity
                                                      * changes until the user allows it, 0.17) */

static void rx_poll(void);
extern void GIE_DISABLE(void);
extern void GIE_ENABLE(void);
void usb_start(void);
static uint8_t setup[8], ctl[64];
static int configured;
static int started;                                  /* usb_start done: before it, usb_poll touches nothing */

/* ------------------------------------------------------------------- the log --- */
#define LOG_LINES 48u
#define LOG_LEN 40u
/* 0.34: request and reply. The last LOG_LINES lines are kept (the oldest overwritten); they go out only during
 * OPEN_MS after the Mac sends any MIDI (F0 7D 4C F7, pmmidi send), a line a poll. 0.32 and 0.33 dropped every line
 * after the first 48 until the Mac asked, then skipped them, and wrote to the port while nobody read it. */
#define OPEN_MS 5000u
static char log_text[LOG_LINES][LOG_LEN];
static uint32_t log_w, log_r;                        /* lines written, lines sent (counts, not indices) */
static int host_open;                                /* the Mac has sent MIDI within OPEN_MS */
static uint32_t open_at;

static void send_sysex(const uint8_t *m, uint32_t n)  /* m: F0 .. F7; packed as USB-MIDI events, cable 0 */
{
    uint8_t buf[64];
    uint32_t i = 0, k = 0;
    while (i < n) {
        uint32_t left = n - i, take = left > 3u ? 3u : left;
        uint8_t cin = left > 3u ? 0x4 : left == 3u ? 0x7 : left == 2u ? 0x6 : 0x5;
        buf[k] = cin;
        buf[k + 1] = m[i];
        buf[k + 2] = take > 1u ? m[i + 1] : 0;
        buf[k + 3] = take > 2u ? m[i + 2] : 0;
        k += 4;
        i += take;
        if (k == sizeof buf || i == n) {
            GIE_DISABLE();                           /* (0.42: masked again, as 0.35 .. 0.40: without it the log
                                                      * stops; it sends only while the Mac reads) */
            OTG_DeviceBulkSend(EP_IN, buf, k, 20);
            GIE_ENABLE();
            k = 0;
        }
    }
}
static void send_line(const char *s)
{
    uint8_t m[LOG_LEN + 4];
    uint32_t n = 0;
    m[n++] = 0xF0;
    m[n++] = 0x7D;
    m[n++] = 0x4D;
    while (*s && n < sizeof m - 1u)
        m[n++] = (uint8_t)(*s++ & 0x7F);
    m[n++] = 0xF7;
    send_sysex(m, n);
}
void usb_log(const char *s)
{
    char *t = log_text[log_w % LOG_LINES];
    strncpy(t, s, LOG_LEN - 1u);
    t[LOG_LEN - 1u] = 0;
    log_w++;
    if (log_w - log_r > LOG_LINES)
        log_r = log_w - LOG_LINES;
}
/* a line with one number (decimal): "label 123" */
void usb_logn(const char *label, uint32_t v)
{
    char s[LOG_LEN], d[12];
    uint32_t i = 0, n = 0;
    while (label[i] && i < LOG_LEN - 13u) {
        s[i] = label[i];
        i++;
    }
    s[i++] = ' ';
    do {
        d[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        s[i++] = d[--n];
    s[i] = 0;
    usb_log(s);
}

/* -------------------------------------------------------------- the requests --- */
static void send_descriptor(const uint8_t *d, uint32_t len)
{
    uint32_t want = (uint32_t)setup[7] << 8 | setup[6];
    OTG_DeviceControlSend((uint8_t *)d, len < want ? len : want, 3);
}
static void get_descriptor(void)
{
    uint8_t type = setup[3], idx = setup[2];
    if (type == 1)
        send_descriptor(DEV_DESC, sizeof DEV_DESC);
    else if (type == 2)
        send_descriptor(CFG_DESC, sizeof CFG_DESC);
    else if (type == 3 && idx == 0) {
        static const uint8_t LANG[4] = {4, 3, 0x09, 0x04};
        send_descriptor(LANG, 4);
    } else if (type == 3 && idx < 4u) {
        const char *s = STRINGS[idx];
        uint32_t n = 0;
        while (s[n] && n < 30u) {
            ctl[2u + 2u * n] = (uint8_t)s[n];
            ctl[3u + 2u * n] = 0;
            n++;
        }
        ctl[0] = (uint8_t)(2u + 2u * n);
        ctl[1] = 3;
        send_descriptor(ctl, ctl[0]);
    } else
        OTG_DeviceStallSend(0);
}

void usb_poll(void)
{
    uint32_t n = 0;
    uint16_t zero = 0;
    if (!started)
        return;
    if (OTG_DeviceBusEventGet() & 0x04u) {           /* a bus reset */
        OTG_DeviceAddressSet(0);
        configured = 0;
    }
    if (OTG_DeviceSetupReceive(setup, 8, &n) != DEVICE_NONE_ERR)
        goto log_again;
    TRACE[3] = (uint32_t)setup[0] << 24 | (uint32_t)setup[1] << 16 | (uint32_t)setup[3] << 8 | setup[2];
    trace_mark(0x10);                                /* a request: 0x10 .. 0x1F while it is answered */
    if (!(setup[0] & 0x80u) && (setup[6] | setup[7]))  /* an OUT request with data: taken, not used */
        OTG_DeviceControlReceive(ctl, sizeof ctl, &n, 10);
    if (((setup[0] >> 5) & 3u) != 0) {               /* class or vendor requests: none here */
        OTG_DeviceStallSend(0);
        goto log_again;
    }
    trace_mark(0x11);
    switch (setup[1]) {
    case 0:                                          /* GET_STATUS */
        OTG_DeviceControlSend((uint8_t *)&zero, 2, 3);
        break;
    case 5:                                          /* SET_ADDRESS */
        OTG_DeviceAddressSet(setup[2] & 0x7Fu);
        break;
    case 6:                                          /* GET_DESCRIPTOR */
        get_descriptor();
        break;
    case 8:                                          /* GET_CONFIGURATION */
        ctl[0] = (uint8_t)configured;
        OTG_DeviceControlSend(ctl, 1, 3);
        break;
    case 9:                                          /* SET_CONFIGURATION */
        trace_mark(0x13);
        OTG_DeviceEndpointReset(EP_IN, TYPE_BULK_IN);
        OTG_DeviceEndpointReset(EP_OUT, TYPE_BULK_OUT);
        trace_mark(0x14);
        configured = setup[2] != 0;
        host_open = 0;
        break;
    case 10:                                         /* GET_INTERFACE */
        ctl[0] = 0;
        OTG_DeviceControlSend(ctl, 1, 3);
        break;
    case 11:                                         /* SET_INTERFACE */
    case 1:                                          /* CLEAR_FEATURE */
        break;
    default:
        OTG_DeviceStallSend(0);
    }
    trace_mark(0x12);
log_again:
    rx_poll();
    if (host_open && hal_ms() - open_at > OPEN_MS)
        host_open = 0;
    if (configured && host_open && log_r != log_w) { /* the log, a line a poll, while the Mac reads the port */
        trace_mark(0x20);                            /* 0x20: a log line going out */
        send_line(log_text[log_r++ % LOG_LINES]);
        trace_mark(0x21);
    }
}

/* ------------------------------------------------- incoming: the update command --- */
static uint8_t crc8(const uint8_t *d, uint32_t n)
{
    uint8_t c = 0;
    uint32_t i, b;
    for (i = 0; i < n; i++) {
        c ^= d[i];
        for (b = 0; b < 8u; b++)
            c = (uint8_t)(c & 0x80u ? (c << 1) ^ 0x07u : c << 1);
    }
    return c;
}
static void send_ack_6f(void)                         /* frame [crc, 1, 0, 3, 14 6F 00], nibbles, F0 .. F7 */
{
    uint8_t f[7] = {0, 1, 0, 3, 0x14, 0x6F, 0x00}, m[16];
    uint32_t i;
    f[0] = crc8(f, 7);
    m[0] = 0xF0;
    for (i = 0; i < 7u; i++) {
        m[1u + 2u * i] = f[i] >> 4;
        m[2u + 2u * i] = f[i] & 0x0Fu;
    }
    m[15] = 0xF7;
    send_sysex(m, 16);
}

static uint8_t rx_msg[64];
static uint32_t rx_len;
static int cmd_6f;
static void rx_poll(void)                              /* what the Mac sent, if anything: opens the log, spots 11 6F */
{
    static const uint8_t CMD_6F[14] = {0xF0, 0x0F, 0x0C, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x01, 0x01, 0x06, 0x0F, 0xF7};
    uint8_t buf[64];
    uint32_t n = 0, i, k;
    if (!configured || OTG_DeviceBulkReceive(EP_OUT, buf, sizeof buf, &n, 1) != DEVICE_NONE_ERR)
        return;
    if (n) {
        host_open = 1;
        open_at = hal_ms();
    }
    for (i = 0; i + 4u <= n; i += 4u) {             /* (USB-MIDI event packets: 4 bytes each) */
        uint8_t cin = buf[i] & 0x0Fu, cnt = cin == 0x5 ? 1 : cin == 0x6 ? 2 : 3;
        if (cin < 0x4 || cin > 0x7)
            continue;
        for (k = 0; k < cnt; k++) {
            uint8_t b = buf[i + 1u + k];
            if (b == 0xF0)
                rx_len = 0;
            if (rx_len < sizeof rx_msg)
                rx_msg[rx_len++] = b;
            if (b == 0xF7) {
                if (rx_len == sizeof CMD_6F && !memcmp(rx_msg, CMD_6F, sizeof CMD_6F)) {
                    send_ack_6f();
                    cmd_6f = 1;
                }
                rx_len = 0;
            }
        }
    }
}
/* 1 once the update command 11 6F has arrived (acknowledged already, in usb_poll) */
int usb_midi_rx(void)
{
    int r = cmd_6f;
    cmd_6f = 0;
    return r;
}

/* --------------------------------------------------------------------- start --- */
int usb_configured(void) { return configured; }

int usb_host_open(void) { return configured && host_open; }

/* (re)starts the device: the Mac sees it leave and come back, and asks for the descriptors again */
void usb_start(void)
{
    configured = 0;
    OTG_DeviceDisConnect();
    DelayMs(200);
    OTG_DeviceInit();                                /* no USB interrupt (0.18): everything is polled, as in the
                                                      * SDK's own USB example */
    OTG_DeviceConnect();
    started = 1;
}
