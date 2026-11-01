/* SPDX-License-Identifier: GPL-3.0-only
 * The control link to the effects processor (an ARM Cortex-M4F; its firmware is section c of the update). Stock
 * talks to it over UART0 at 460800 8N1, TX on A1 (mode 0xC), RX on A0 (mode 2) (stock 0x41008, called at 0x58A4).
 * The audio link (SPIS, hal.c) only carries samples; everything else (presets, levels, the looper) goes here.
 *
 * A frame (stock 0x5600): [crc] [n lo] [n hi] [type] [payload], n = payload length + 4, crc = CRC-8 (poly 0x07,
 * init 0, stock's table at 0xCF318) over the payload; type 0 sets, 1 asks. The payload: [id lo] [id hi] [len lo]
 * [len hi] [data], len = payload length. The ARM dispatches on the id (its 0x639E): top nibble a group, low byte
 * the entry. At start Molecular sends what stock does first (arm_start): the levels (0x1000), then a preset (0x0000). */
#include <stdint.h>
#include <string.h>
#include "type.h"
#include "gpio.h"
#include "uarts.h"
#include "usbmidi.h"
#include "armlink.h"

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

volatile uint32_t arm_ok_levels, arm_ok_preset;
extern void GIE_DISABLE(void);
extern void GIE_ENABLE(void);
extern uint32_t hal_ms(void);
static uint8_t frame[8 + 0x180];

/* 0.37: one frame, then its answer (the ARM reads all it has at each poll and wants exactly one frame: 0.36 sent two
 * at once and both came back 01 04 00 13, its reject). No interrupt inside a frame: a gap could end it early. */
void arm_send(uint8_t type, uint16_t id, const uint8_t *data, uint32_t dlen)
{
    uint32_t len = 4u + dlen, n = len + 4u;
    if (dlen > 0x180u - 4u)
        return;
    frame[4] = (uint8_t)id;
    frame[5] = (uint8_t)(id >> 8);
    frame[6] = (uint8_t)len;
    frame[7] = (uint8_t)(len >> 8);
    if (dlen)
        memcpy(frame + 8, data, dlen);
    frame[0] = crc8(frame + 4, len);
    frame[1] = (uint8_t)n;
    frame[2] = (uint8_t)(n >> 8);
    frame[3] = type;
    GIE_DISABLE();
    UART0_Send(frame, n, 100);
    GIE_ENABLE();
}

/* waits up to ms for the answer and logs it ("A " and its bytes in hex); 1 if it was 01 04 00 14 (accepted) */
static int arm_answer(uint32_t ms)
{
    static const char HX[] = "0123456789ABCDEF";
    char line[40];
    uint32_t t0 = hal_ms(), last = t0, n = 2, got = 0;
    uint8_t b, first[4] = {0, 0, 0, 0};
    line[0] = 'A';
    line[1] = ' ';
    while (hal_ms() - t0 < ms && !(got && hal_ms() - last > 5u)) {
        if (UART0_RecvByte(&b)) {
            if (got < 4u)
                first[got] = b;
            got++;
            last = hal_ms();
            if (n < 34u) {
                line[n++] = HX[b >> 4];
                line[n++] = HX[b & 15u];
            }
        }
    }
    if (!got) {
        usb_log("A none");
        return 0;
    }
    line[n] = 0;
    usb_log(line);
    return first[0] == 1 && first[1] == 4 && first[3] == 0x14;
}

#include "preset_sweet_od.h"
/* the stock start (0x3FA24): 0x1000 (levels and trims, stock 0x3F2D8), then the preset 0x0000 (stock 0x3F3DC). The
 * ARM settings start at zero: its levels (+0x2F0, +0x2F4, 0.005 .. 1, set by 0x1000) were never set before 0.37. */
void arm_start(void)
{
    static uint8_t p[0x16C];
    uint8_t g[12];
    float lvl = 0.8f;                                /* 0.39: as 0.37 (0.38 had 1.0) */
    uint32_t i;
    arm_poll();                                      /* 0.56: whatever came while it started (power back) */
    memset(g, 0, sizeof g);
    memcpy(g + 0, &lvl, 4);                          /* payload +4: the ARM +0x2F0 */
    memcpy(g + 4, &lvl, 4);                          /* payload +8: the ARM +0x2F4 */
    arm_send(0, 0x1000, g, sizeof g);                /* +12 .. +15: trims, 0 */
    arm_ok_levels = (uint32_t)arm_answer(50);
    usb_logn("ok 1000", arm_ok_levels);

    memset(p, 0, sizeof p);
    {   /* the record level byte (+0x15) as a float, as stock 0x40258 */
        float v = (float)PRESET_SWEET_OD[0x15];      /* 0.39: the record (50), as 0.37 (0.38 had 100) */
        memcpy(p + 0, &v, 4);
    }
    p[4] = PRESET_SWEET_OD[0x16];                    /* +0x16 (16 bits), stock 0x3E9D4 */
    p[5] = PRESET_SWEET_OD[0x17];
    p[6] = PRESET_SWEET_OD[0x18];                    /* the modules on, a bit each (the ARM +0xA) */
    p[7] = PRESET_SWEET_OD[0x19];
    for (i = 0; i < 10u; i++)
        p[8 + i] = PRESET_SWEET_OD[0x1C + i];        /* the chain order, stock 0x3EA04 */
    memcpy(p + 0x2C, PRESET_SWEET_OD + 0x4E, 0x140);   /* the parameters: the record from +0x4E (stock gp-10951) */
    arm_send(0, 0x0000, p, sizeof p);
    arm_ok_preset = (uint32_t)arm_answer(100);
    usb_logn("ok preset", arm_ok_preset);
}

void arm_init(void)
{
    GPIO_PortAModeSet(GPIOA0, 2);                    /* uart0 rxd */
    GPIO_PortAModeSet(GPIOA1, 0xC);                  /* uart0 txd */
    UART0_Init(460800, 8, 0, 1);
}

/* the ARM's own frames (status, every few seconds): read and dropped */
void arm_poll(void)
{
    uint8_t b;
    while (UART0_RecvByte(&b))
        ;
}
