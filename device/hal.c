/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR on the Pocket Master: the board.
 *
 * Everything here copies what Sonicake's stock firmware v1.3.3 does (docs/hardware/BOARD.md has the addresses): the pins
 * are set in the same order to the same states, the LCD is set up with the same commands, the update hand-over is
 * the same instruction sequence. What the stock firmware does not show is marked "guess".
 *
 *   LCD     ST7735S, 160 x 128 landscape, SPIM port 0, CS A9, DC A7, RST A16, backlight PWM on A10 (TIMER5)
 *   keys    B6, A15, A31 (active low)          encoder  A29, A30 (polled; a detent with both low)
 *   audio   the chip's DAC0, 44.1 kHz, fed from a 1 ms timer interrupt
 *   update  flash_boot (flash 0x0 .. 0xFFFF, never written by an update) takes over when bit 1 (PC) and bit 5
 *           (update) of 0x4000100C are set and the chip jumps to 0: the stock firmware's command 11 6F.
 */
#include <stdint.h>
#include <string.h>
#include <nds32_intrinsic.h>
#include "type.h"
#include "gpio.h"
#include "clk.h"
#include "spim.h"
#include "pwm.h"
#include "timer.h"
#include "irqn.h"
#include "watchdog.h"
#include "reset.h"
#include "remap.h"
#include "spi_flash.h"
#include "dma.h"
#include "dac.h"
#include "dac_interface.h"
#include "audio_adc.h"
#include "adc_interface.h"
#include "spis.h"
#include "chip_info.h"
#include "core_d1088.h"
#include "backup.h"
#include "timeout.h"
#include "hal.h"
#include "usbmidi.h"
#include "trace.h"

/* in libDriver (core_d1088.o, timeout.o) with no prototype in its headers */
extern void ICacheInvalidAll(void);
extern void SysTickInit(void);
extern void SysTickDeInit(void);

#define PIN(n) ((GPIO_INDEX)(1u << (n)))

/* ----------------------------------------------------------- the update hand-over --- */
#define FLASH_BOOT_FLAGE (*(volatile uint32_t *)0x4000100C)

/* flash_boot's PC update mode: the Sonicake Manager can then write any firmware (the stock one included). The
 * sequence is the stock firmware's (0x41FE0), which is the SDK's start_up_grate() with the PC source. */
void boot_recover(void)
{
    uint32_t keep, i;
    volatile uint32_t spin;
    GIE_DISABLE();
    trace_mark(0xB0);
    GPIO_RegOneBitClear(GPIO_A_OUT, 1u << 17);       /* as stock before it (0x40790): A17, A24 low, ~100 ms */
    GPIO_RegOneBitClear(GPIO_A_OUT, 1u << 24);
    for (spin = 0; spin < 3000000u; spin++)          /* (no DelayMs: this may run from a crash handler) */
        ;
    FLASH_BOOT_FLAGE |= 0x02u;                       /* PC */
    FLASH_BOOT_FLAGE |= 0x20u;                       /* update */
    keep = FLASH_BOOT_FLAGE;
    WDG_Enable(WDG_STEP_1S);
    DisableIDCache();
    DataCacheInvalidAll();
    ICacheInvalidAll();
    SysTickDeInit();
    SysTimerIntFlagClear();
    *(volatile uint32_t *)0x4000D100 = 0;            /* the DMA controller cleared */
    for (i = 0x4000D000u; i < 0x4000D104u; i += 4u)
        *(volatile uint32_t *)i = 0;
    *(volatile uint32_t *)0x40022000 &= ~0x77FFF8u;  /* registers reset, the flash and the audio PLL kept */
    *(volatile uint32_t *)0x40022000 |= 0x7FFFF8u;
    *(volatile uint32_t *)0x40022004 &= ~0x7FFFF7FFu;
    *(volatile uint32_t *)0x40022004 |= 0x7FFFF7FFu;
    FLASH_BOOT_FLAGE = keep;
    __nds32__mtsr(0, NDS32_SR_INT_MASK2);
    __nds32__mtsr(0, NDS32_SR_HSP_CTL);
    __asm__ volatile("nop");
    GPIO_RegisterResetMask();
    ((void (*)(void))0)();
    for (;;)
        ;
}

/* ------------------------------------------------------------------------ pins --- */
static void pin_out(int port_b, uint32_t mask, int high)
{
    uint8_t pu = port_b ? GPIO_B_PU : GPIO_A_PU, pd = port_b ? GPIO_B_PD : GPIO_A_PD;
    uint8_t ie = port_b ? GPIO_B_IE : GPIO_A_IE, oe = port_b ? GPIO_B_OE : GPIO_A_OE;
    uint8_t out = port_b ? GPIO_B_OUT : GPIO_A_OUT;
    GPIO_RegOneBitSet(pu, mask);                     /* {pu, pd} = 11: no pull */
    GPIO_RegOneBitSet(pd, mask);
    GPIO_RegOneBitClear(ie, mask);
    GPIO_RegOneBitSet(oe, mask);
    if (high)
        GPIO_RegOneBitSet(out, mask);
    else
        GPIO_RegOneBitClear(out, mask);
}
/* an input; pull 1: pull-up ({pu, pd} = 10), 0: none (00) */
static void pin_in(int port_b, uint32_t mask, int pull)
{
    uint8_t pu = port_b ? GPIO_B_PU : GPIO_A_PU, pd = port_b ? GPIO_B_PD : GPIO_A_PD;
    uint8_t ie = port_b ? GPIO_B_IE : GPIO_A_IE, oe = port_b ? GPIO_B_OE : GPIO_A_OE;
    if (pull)
        GPIO_RegOneBitSet(pu, mask);
    else
        GPIO_RegOneBitClear(pu, mask);
    GPIO_RegOneBitClear(pd, mask);
    GPIO_RegOneBitSet(ie, mask);
    GPIO_RegOneBitClear(oe, mask);
}

#define KEY_B6 0u
#define KEY_A15 1u
#define KEY_A31 2u
#define KEY_PWR 3u                                   /* the chip's power key (its pin state, as at boot = down) */
static uint32_t pwr_down_level;
static int key_raw(uint32_t k)                       /* 1 = down (the keys are active low) */
{
    if (k == KEY_PWR)
        return (uint32_t)BACKUP_PowerKeyPinStateGet() == pwr_down_level;
    if (k == KEY_B6)
        return !GPIO_RegOneBitGet(GPIO_B_IN, PIN(6));
    if (k == KEY_A15)
        return !GPIO_RegOneBitGet(GPIO_A_IN, PIN(15));
    return !GPIO_RegOneBitGet(GPIO_A_IN, PIN(31));
}

/* the keys only, before anything else (the escape: hal_keys_at_boot) */
static void keys_pins(void)
{
    GPIO_PortBModeSet((GPIO_PortB)PIN(6), 0);
    GPIO_PortAModeSet((GPIO_PortA)PIN(15), 0);
    GPIO_PortAModeSet((GPIO_PortA)PIN(31), 0);
    pin_in(1, PIN(6), 1);
    pin_in(0, PIN(15), 1);
    pin_in(0, PIN(31), 1);
}

/* the keys held as we start (bit k: key k; bit 3: the chip's power key), before the clocks are set: no DelayMs */
uint32_t hal_keys_at_boot(void)
{
    uint32_t m = 0, k;
    volatile uint32_t spin;
    keys_pins();
    for (spin = 0; spin < 200000u; spin++)           /* the pull-ups settle */
        ;
    for (k = 0; k < 3u; k++)
        m |= (uint32_t)key_raw(k) << k;
    if (BACKUP_PowerKeyPinStateGet())
        m |= 1u << 3;
    return m;
}

/* GPIO_PortAModeSet and GPIO_PortAOutDsSet take one pin: they switch on the mask (libDriver), and a mask of several
 * pins does nothing. 0.1 .. 0.23 passed A7|A9|A16 (the LCD's DC, CS, reset) and A17|A22: never GPIO, the LCD white. */
static void a_mode(uint32_t mask, uint8_t mode)
{
    uint32_t k;
    for (k = 0; k < 32u; k++)
        if ((mask >> k) & 1u)
            GPIO_PortAModeSet((GPIO_PortA)(1u << k), mode);
}
static void a_drive(uint32_t mask, uint8_t ds)
{
    uint32_t k;
    for (k = 0; k < 32u; k++)
        if ((mask >> k) & 1u)
            GPIO_PortAOutDsSet((GPIO_PortA)(1u << k), ds);
}

/* the stock firmware's board set-up (0x420C4), in its order */
static void board_pins(void)
{
    a_mode(PIN(17) | PIN(22), 0);                             /* 0x40808: A17 low, A22 high (purpose unknown) */
    pin_out(0, PIN(17) | PIN(22), 0);
    GPIO_RegOneBitSet(GPIO_A_OUT, PIN(22));
    hal_stage(0x61);
    GPIO_PortAModeSet((GPIO_PortA)PIN(24), 0);                /* 0x40944: A24 low */
    pin_out(0, PIN(24), 0);
    hal_stage(0x62);
    pin_in(1, PIN(4) | PIN(2), 1);                            /* 0x40F80: B4, B2 inputs, pulled up */
    GPIO_PortBModeSet((GPIO_PortB)PIN(3), 0);                 /* 0x409EC: B3 high; B2 input, no pull */
    pin_out(1, PIN(3), 1);
    pin_in(1, PIN(2), 0);
    hal_stage(0x63);
    pin_in(0, PIN(29) | PIN(30), 1);                          /* 0x5964: the encoder, pulled up */
    keys_pins();
    hal_stage(0x64);
    SPIM_IoConfig(0);                                         /* 0x4088C: the LCD: SPIM port 0, A7 A9 A16 high */
    a_mode(PIN(7) | PIN(9) | PIN(16), 0);
    GPIO_RegOneBitSet(GPIO_A_PU, PIN(7) | PIN(9) | PIN(16));
    GPIO_RegOneBitClear(GPIO_A_PD, PIN(7) | PIN(9) | PIN(16));
    a_drive(PIN(7) | PIN(9) | PIN(16), 2);                    /* strong drive, as stock */
    GPIO_RegOneBitClear(GPIO_A_IE, PIN(7) | PIN(9) | PIN(16));
    GPIO_RegOneBitSet(GPIO_A_OE, PIN(7) | PIN(9) | PIN(16));
    GPIO_RegOneBitSet(GPIO_A_OUT, PIN(7) | PIN(9) | PIN(16));
    hal_stage(0x65);
    GPIO_PortBModeSet((GPIO_PortB)PIN(5), 0);                 /* 0x409A0 sets B5 high; stock's 0x6008 then */
    pin_out(1, PIN(5), 0);                                    /* drives it low with USB unplugged. 0.45: low:
                                                               * high left the whole output faint (0.37 .. 0.43;
                                                               * found 0.44, B5 low from the Mac: full level) */
    hal_stage(0x66);
}

/* ---------------------------------------------------------------------- backlight --- */
/* 0 .. 100: the stock firmware's 0x40F54, whose argument is a whole PWM_IOCTRL_ARG (output type 6, divider 100: its
 * values at 0x40EF8) with the duty changed. 0.1 passed zeros but the duty: the backlight stayed dark. */
static PWM_IOCTRL_ARG bl_arg = {PWM_OUTPUT_SINGLE_1, 100, 0};
void hal_backlight(uint32_t pct)
{
    bl_arg.Duty = (uint16_t)(pct > 100u ? 100u : pct);
    PWM_IOCTRl(TIMER5, OUTPUT_DUTY_UPDATE, &bl_arg);
}
static void backlight_init(void)                     /* the stock firmware's 0x40EF8 */
{
    PWM_StructInit p;
    PWM_GpioConfig(TIMER5_PWM_A10_A24_B0, 0, PWM_IO_MODE_NONE);
    PWM_GpioConfig(TIMER5_PWM_A10_A24_B0, 0, PWM_IO_MODE_OUT);
    memset(&p, 0, sizeof p);
    p.CounterMode = PWM_COUNTER_MODE_DOWN;
    p.OutputType = PWM_OUTPUT_SINGLE_1;
    p.FreqDiv = 4000;
    p.Duty = 0;
    PWM_Config(TIMER5, &p);
    PWM_Enable(TIMER5);
}

/* --------------------------------------------------------------------------- LCD --- */
#define LCD_DC PIN(7)
#define LCD_CS PIN(9)
#define LCD_RST PIN(16)
static void lcd_cmd(uint8_t c)                       /* the stock firmware's 0x40ABC */
{
    GPIO_RegOneBitClear(GPIO_A_OUT, LCD_DC);
    GPIO_RegOneBitClear(GPIO_A_OUT, LCD_CS);
    SPIM_Send(&c, 1);
    GPIO_RegOneBitSet(GPIO_A_OUT, LCD_CS);
    GPIO_RegOneBitSet(GPIO_A_OUT, LCD_DC);
}
static void lcd_data(const uint8_t *d, uint32_t n)   /* 0x40B04 / 0x40B34 */
{
    GPIO_RegOneBitClear(GPIO_A_OUT, LCD_CS);
    SPIM_Send((uint8_t *)d, n);
    GPIO_RegOneBitSet(GPIO_A_OUT, LCD_CS);
}
static void lcd_seq(uint8_t c, const uint8_t *d, uint32_t n)
{
    lcd_cmd(c);
    if (n)
        lcd_data(d, n);
}
static void lcd_init(void)                           /* the stock firmware's 0x40C7C, command for command */
{
    static const uint8_t B1[] = {0x05, 0x3A, 0x3A}, B3[] = {0x05, 0x3A, 0x3A, 0x05, 0x3A, 0x3A}, B4[] = {0x03};
    static const uint8_t C0[] = {0x62, 0x02, 0x04}, C1[] = {0xC0}, C2[] = {0x0D, 0x00}, C3[] = {0x8D, 0x6A};
    static const uint8_t C4[] = {0x8D, 0xEE}, C5[] = {0x12}, MAD[] = {0x68}, COL[] = {0x55};
    static const uint8_t E0[] = {0x03, 0x1B, 0x12, 0x11, 0x3F, 0x3A, 0x32, 0x34, 0x2F, 0x2B, 0x30, 0x3A, 0x00, 0x01, 0x02, 0x05};
    static const uint8_t E1[] = {0x03, 0x1B, 0x12, 0x11, 0x32, 0x2F, 0x2A, 0x2F, 0x2E, 0x2C, 0x35, 0x3F, 0x00, 0x00, 0x01, 0x05};
    SPIM_ClrIntFlag();                               /* 0x40C64: as stock, then mode 0, the fastest clock */
    SPIM_Init(0, 0);
    hal_stage(0x91);
    GPIO_RegOneBitSet(GPIO_A_OUT, LCD_RST);
    DelayMs(1);
    GPIO_RegOneBitClear(GPIO_A_OUT, LCD_RST);
    DelayMs(1);
    GPIO_RegOneBitSet(GPIO_A_OUT, LCD_RST);
    DelayMs(130);
    hal_stage(0x92);
    lcd_cmd(0x11);
    hal_stage(0x93);
    DelayMs(120);
    lcd_seq(0xB1, B1, 3);
    lcd_seq(0xB2, B1, 3);
    lcd_seq(0xB3, B3, 6);
    lcd_seq(0xB4, B4, 1);
    lcd_seq(0xC0, C0, 3);
    lcd_seq(0xC1, C1, 1);
    lcd_seq(0xC2, C2, 2);
    lcd_seq(0xC3, C3, 2);
    lcd_seq(0xC4, C4, 2);
    lcd_seq(0xC5, C5, 1);
    lcd_seq(0xE0, E0, 16);
    lcd_seq(0xE1, E1, 16);
    lcd_seq(0x36, MAD, 1);                           /* landscape, BGR */
    lcd_seq(0x3A, COL, 1);                           /* RGB565 */
    hal_stage(0x94);
}

/* the frame, whole: rows of RGB565, high byte first on the wire */
void hal_lcd_frame(const uint16_t *px, uint32_t w, uint32_t h)
{
    static uint8_t row[2u * 160u];
    uint8_t win[4];
    uint32_t x, y;
    win[0] = 0; win[1] = 0; win[2] = (uint8_t)((w - 1u) >> 8); win[3] = (uint8_t)(w - 1u);
    lcd_seq(0x2A, win, 4);
    win[2] = (uint8_t)((h - 1u) >> 8); win[3] = (uint8_t)(h - 1u);
    lcd_seq(0x2B, win, 4);
    lcd_cmd(0x2C);
    GPIO_RegOneBitClear(GPIO_A_OUT, LCD_CS);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            uint16_t c = px[y * w + x];
            row[2u * x] = (uint8_t)(c >> 8);
            row[2u * x + 1u] = (uint8_t)c;
        }
        SPIM_Send(row, 2u * w);
    }
    GPIO_RegOneBitSet(GPIO_A_OUT, LCD_CS);
}
void hal_lcd_on(int on) { lcd_cmd(on ? 0x29 : 0x28); }

/* ------------------------------------------------------------- the 1 ms interrupt --- */
volatile uint32_t hal_fed, hal_peak;                 /* blocks made, the largest sample since read */

/* ----------------------------------------------------------- the DSP link (SPIS) --- */
/* The output jack is not the chip's DAC: stock (1.3.3) sends its audio to a second processor, the DSP, over SPI with
 * this chip as the slave (0x41D20, 0x41EE0; the done interrupt 0x123A0). Each exchange, clocked by the DSP: 512 bytes
 * each way; bytes 0..255 out are 64 frames of 16-bit stereo at 44.1 kHz from a ring, zeros when it is short; sending
 * starts once more than 2 KB wait (stock: 0x801). The DSP's 512 bytes in are not used yet. */
#define SPIS_LEN 512u
#define RING_FRAMES 4096u                            /* 93 ms; filled from the main loop (hal_render) */
/* The link to the ARM effects processor (the master: it clocks 512 bytes each way, 689 times a second). What we
 * send: 64 stereo 16-bit frames in bytes 0..255, zeros after (the ARM decodes our bytes 0..255 only, its 0x5A14;
 * our second half reaches nothing: 0.43 was silent). Two buffers each way: the done interrupt starts the next
 * exchange at once with a frame filled one exchange earlier, then fills the other. */
static uint8_t spis_txb[2][SPIS_LEN], spis_rxb[2][SPIS_LEN];
static uint32_t spis_cur;
static uint32_t ring[RING_FRAMES];                   /* frames: L | R << 16 */
static volatile uint32_t ring_w, ring_r;             /* frame counts: written (main loop), read (link) */
static volatile int spis_go;
volatile uint32_t hal_xfers, hal_under, hal_late;    /* exchanges; short of audio; started late (overrun) */
static void spis_fill(uint8_t *buf)                  /* the next frame: 64 frames of audio in bytes 0..255 */
{
    uint32_t i, n = ring_w - ring_r, *tx = (uint32_t *)buf;
    memset(buf, 0, SPIS_LEN);
    if (!spis_go && n > 512u)
        spis_go = 1;
    if (spis_go && n >= 64u) {
        for (i = 0; i < 64u; i++)
            tx[i] = ring[(ring_r + i) & (RING_FRAMES - 1u)];
        ring_r += 64u;
    } else if (spis_go)
        hal_under++;
}
static void spis_done(void)                          /* the DMA done interrupt of SPIS RX */
{
    uint32_t done;
    if (!SPIS_DMA_SendRecvState())
        return;
    if (SPIS_GetRxOverrunFlag())                     /* bytes came while no exchange was running: late */
        hal_late++;
    SPIS_ClrRxIntFlag();
    SPIS_ClrRxOverrunFlag();
    DMA_InterruptFlagClear(PERIPHERAL_ID_SPIS_RX, DMA_DONE_INT);
    DMA_InterruptFlagClear(PERIPHERAL_ID_SPIS_TX, DMA_DONE_INT);
    done = spis_cur;
    spis_cur ^= 1u;
    SPIS_DMA_StartSendRecv(spis_txb[spis_cur], spis_rxb[spis_cur], SPIS_LEN);   /* first: the next exchange */
    spis_fill(spis_txb[done]);                       /* the one after it */
    hal_xfers++;
}
/* the analog side as stock starts it (0x434F4 .. 0x43572): the ADC's reference VCOM, the analog front end, MicBias1
 * and ADC1's gain stages on inputs MIC1 / MIC2, then ADC1 itself. Stock never powers its DAC up (0.21 .. 0.31 did,
 * and no build reached the jack, not even 0.31 sending the DSP's own input straight back). */
extern void ADC_Disable(void);
extern void SPDIF_AnalogModuleDisable(void);
static uint32_t adc1_fifo[1024];
static void analog_start(void)
{
    ADC_Disable();
    AudioADC_PowerDown();
    AudioADC_VcomConfig(2);
    SPDIF_AnalogModuleDisable();
    AudioADC_AnaInit();
    AudioADC_VcomConfig(1);
    AudioADC_MicBias1Enable(1);
    AudioADC_PGASel(ADC1_MODULE, CHANNEL_LEFT, LINEIN3_LEFT_OR_MIC1);
    AudioADC_PGASel(ADC1_MODULE, CHANNEL_RIGHT, LINEIN3_RIGHT_OR_MIC2);
    AudioADC_PGAGainSet(ADC1_MODULE, CHANNEL_LEFT, LINEIN3_LEFT_OR_MIC1, 15, 4);
    AudioADC_PGAGainSet(ADC1_MODULE, CHANNEL_RIGHT, LINEIN3_RIGHT_OR_MIC2, 15, 4);
    AudioADC_DigitalInit(ADC1_MODULE, 44100, adc1_fifo, sizeof adc1_fifo);
}
static void irq_priority(uint32_t irq, uint32_t pri)  /* stock's 0x52854: two bits an interrupt */
{
    uint32_t sh = 2u * (irq & 15u), v;
    if (irq < 16u) {
        v = __nds32__mfsr(NDS32_SR_INT_PRI);
        __nds32__mtsr((v & ~(3u << sh)) | (pri & 3u) << sh, NDS32_SR_INT_PRI);
    } else {
        v = __nds32__mfsr(NDS32_SR_INT_PRI2);
        __nds32__mtsr((v & ~(3u << sh)) | (pri & 3u) << sh, NDS32_SR_INT_PRI2);
    }
}
static void spis_start(void)                         /* stock's 0x41D20, then its first exchange (0x41F7C) */
{
    SPIS_IoConfig(1);
    SPIS_Init(0);
    DMA_ChannelDisable(PERIPHERAL_ID_SPIS_RX);
    DMA_ChannelDisable(PERIPHERAL_ID_SPIS_TX);
    SPIS_DMA_Init(spis_rxb[0], SPIS_LEN, spis_txb[0], SPIS_LEN);
    SPIS_BlockDMA_Init();
    SPIS_RxDmaModeSet(1);
    irq_priority(DMA4_IRQn, 0);
    DMA_InterruptFunSet(PERIPHERAL_ID_SPIS_RX, DMA_DONE_INT, spis_done);
    DMA_InterruptEnable(PERIPHERAL_ID_SPIS_RX, DMA_DONE_INT, 1);
    SPIS_ClrRxIntFlag();
    SPIS_ClrRxOverrunFlag();
    DMA_InterruptFlagClear(PERIPHERAL_ID_SPIS_RX, DMA_DONE_INT);
    DMA_InterruptFlagClear(PERIPHERAL_ID_SPIS_TX, DMA_DONE_INT);
    SPIS_ClrRxFIFO();
    SPIS_ClrTxFIFO();
    DMA_ChannelEnable(PERIPHERAL_ID_SPIS_RX);
    DMA_ChannelEnable(PERIPHERAL_ID_SPIS_TX);
    NVIC_EnableIRQ(DMA4_IRQn);
    spis_cur = 0;
    SPIS_DMA_StartSendRecv(spis_txb[0], spis_rxb[0], SPIS_LEN);
}
static uint8_t DMA_MAP[29] = {
    4, 5,                                            /* PERIPHERAL_ID_SPIS_RX, _TX: channels 4 and 5 (stock: DMA4) */
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 2,                                          /* PERIPHERAL_ID_AUDIO_ADC1_RX (19): channel 2 */
    0,                                               /* PERIPHERAL_ID_AUDIO_DAC0_TX (20): channel 0 */
    1,                                               /* PERIPHERAL_ID_AUDIO_DAC1_TX (21): channel 1 */
    255, 255, 255, 255, 255, 255, 255,
};
static hal_block_fn audio_fn;


/* no console: the SDK's and libDriver's prints go nowhere (puts and printf from libc would reach UART0) */
int puts(const char *s) { (void)s; return 0; }
int printf(const char *f, ...) { (void)f; return 0; }
static volatile uint32_t ms_now;
static volatile uint32_t ev_head, ev_tail;
static volatile uint8_t ev_q[32];
static uint32_t key_cnt[4], key_state[4], key_map[4], nkeys = 3;   /* key_map: physical key -> HAL_EV_* */
static uint8_t enc_prev, enc_now;

static void ev_put(uint8_t e)
{
    uint32_t h = ev_head, n = (h + 1u) & 31u;
    if (n != ev_tail) {
        ev_q[h] = e;
        ev_head = n;
    }
}
int hal_event(void)                                  /* -1: none */
{
    uint32_t t = ev_tail;
    int e;
    if (t == ev_head)
        return -1;
    e = ev_q[t];
    ev_tail = (t + 1u) & 31u;
    return e;
}

/* the encoder as the stock firmware reads it (0x59D8): 1 (A29 0, A30 1), 2 (1, 0), 0 (1, 1), 3 (0, 0) */
static uint8_t enc_state(void)
{
    int a = GPIO_RegOneBitGet(GPIO_A_IN, PIN(29)), b = GPIO_RegOneBitGet(GPIO_A_IN, PIN(30));
    if (!a && b)
        return 1;
    if (a && !b)
        return 2;
    return a ? 0 : 3;
}

#define SYNTH_GAIN 1                                 /* 0.45: x1 (x2 clipped once B5 opened the output) */
/* 0.41: the synth is rendered here, from the main loop, no longer in the 1 ms timer interrupt. The link needs its
 * done interrupt served within microseconds (the ARM clocks on regardless); a long timer interrupt in the way let
 * bytes go out before the next exchange started, shifting our samples by a varying count: noise, the patch faint. */
/* 0.56: the link restarted (the ARM powered off and on again, or its exchanges stopped): the ring emptied, sending
 * waits for 2 KB again, the DMA and its interrupt set up afresh */
volatile uint32_t hal_relinks;
void hal_link_restart(void)
{
    GIE_DISABLE();
    NVIC_DisableIRQ(DMA4_IRQn);
    DMA_ChannelDisable(PERIPHERAL_ID_SPIS_RX);
    DMA_ChannelDisable(PERIPHERAL_ID_SPIS_TX);
    ring_w = ring_r = 0;
    spis_go = 0;
    memset(spis_txb, 0, sizeof spis_txb);
    spis_start();
    hal_relinks++;
    GIE_ENABLE();
}
/* 0.63: the time in microseconds (the 1 ms tick and the timer's remainder within it), for the audio's load */
static uint32_t hal_us(void)
{
    uint32_t ms, left;
    do {
        ms = ms_now;
        left = Timer_GetResidualTime(TIMER2);
    } while (ms != ms_now);
    return ms * 1000u + (left < 1000u ? 1000u - left : 0u);
}
volatile uint32_t hal_busy_us, hal_block_max_us;    /* time spent making blocks (since read), the longest block */
void hal_render(void)
{
    static uint32_t seen_xfers, seen_at;
    uint32_t n;
    if (audio_fn) {                                  /* 0.56: no exchange for 200 ms: the link restarted. With the
                                                      * link stopped nothing was read, no block was made, and every
                                                      * fade (a new molecule, POWER OFF) waited for good (0.55) */
        if (hal_xfers != seen_xfers) {
            seen_xfers = hal_xfers;
            seen_at = ms_now;
        } else if (ms_now - seen_at >= 200u) {
            hal_link_restart();
            seen_at = ms_now;
        }
    }
    for (n = 0; n < 1000u && audio_fn && RING_FRAMES - (ring_w - ring_r) >= HAL_BLOCK; n++) {
        static int16_t lr[2u * HAL_BLOCK];           /* 0.29: to the DSP link (SPIS); the DACs reach no jack */
        uint32_t i, w = ring_w, t0 = hal_us(), dt;
        audio_fn(lr);
        dt = hal_us() - t0;
        hal_busy_us += dt;
        if (dt > hal_block_max_us && dt < 100000u)
            hal_block_max_us = dt;
        for (i = 0; i < HAL_BLOCK; i++) {
            /* 0.38: x4 (+12 dB), clipped: the synth peaked near -11 dBFS and came out of the ARM very faint */
            int32_t a = lr[2u * i] * SYNTH_GAIN, c = lr[2u * i + 1u] * SYNTH_GAIN;
            uint32_t v;
            a = a > 32767 ? 32767 : a < -32767 ? -32767 : a;
            c = c > 32767 ? 32767 : c < -32767 ? -32767 : c;
            v = (uint32_t)(a < 0 ? -a : a);
            if (v > hal_peak)
                hal_peak = v;
            ring[(w + i) & (RING_FRAMES - 1u)] = (uint16_t)a | (uint32_t)(uint16_t)c << 16;
        }
        ring_w = w + HAL_BLOCK;
        hal_fed++;
    }
}

void Timer2Interrupt(void)
{
    uint32_t k;
    Timer_InterruptFlagClear(TIMER2, UPDATE_INTERRUPT_SRC);
    ms_now++;
    for (k = 0; k < nkeys; k++) {                    /* keys: 20 ms to be believed */
        uint32_t d = (uint32_t)key_raw(k);
        if (d != key_state[k]) {
            if (++key_cnt[k] >= 20u) {
                key_state[k] = d;
                key_cnt[k] = 0;
                ev_put((uint8_t)(key_map[k] | (k + 1u) << 4 | (d ? 0u : HAL_EV_UP)));
            }
        } else
            key_cnt[k] = 0;
    }
    enc_now = enc_state();                           /* the stock firmware's 0x5A48: a step at each detent */
    if (enc_now != enc_prev) {
        if (enc_now == 3u && enc_prev == 1u)
            ev_put(HAL_EV_CW);
        else if (enc_now == 3u && enc_prev == 2u)
            ev_put(HAL_EV_CCW);
        enc_prev = enc_now;
    }
}
uint32_t hal_ms(void) { return ms_now; }

/* --------------------------------------------------------------------- start-up --- */
/* the clocks and the flash exactly as the stock firmware (0x4FB18, 0x4FA88), before anything else uses them (USB
 * included: 0.12 and 0.13 started USB on flash_boot's clocks and the Mac never saw it). 0.1 and 0.2 followed the
 * SDK's DAC example instead (Chip_Init, no APB divider, no Remap_DisableTcm). No voltage boost: stock raises the core
 * voltage only above 288 MHz, and 288 MHz is what this runs at (stock: 320). */
void hal_clocks(void)
{
    Clock_Config(1, 24000000);
    Clock_PllLock(288000);
    Clock_APllLock(240000);
    Clock_USBClkDivSet(4);
    Clock_SysClkSelect(PLL_CLK_MODE);
    Clock_USBClkSelect(APLL_CLK_MODE);
    Clock_UARTClkSelect(APLL_CLK_MODE);
    Clock_Timer3ClkSelect(RC_CLK_MODE);
    Clock_Module1Enable(ALL_MODULE1_CLK_SWITCH);
    Clock_Module2Enable(ALL_MODULE2_CLK_SWITCH);
    Clock_Module3Enable(ALL_MODULE3_CLK_SWITCH);
    Clock_ApbClkDivSet(5);
    Remap_DisableTcm();
    Remap_InitTcm(0x10000, 12);                      /* the first 12 KB of the application: the flash driver */
    SpiFlashInit(96000000, MODE_4BIT, 0, 1);
    SysTickInit();
}

/* held_mask: hal_keys_at_boot(). The keys, read from the pedal (0.25's log): A31 the left button, B6 the right one,
 * A15 the encoder's press; the chip's power key never reported a press. 1 if the encoder was held at the start. */
int hal_init(uint32_t held_mask)
{
    uint32_t k;
    hal_stage(6);
    board_pins();
    key_map[KEY_A31] = HAL_EV_LEFT;
    key_map[KEY_B6] = HAL_EV_RIGHT;
    key_map[KEY_A15] = HAL_EV_PUSH;
    for (k = 0; k < nkeys; k++)
        key_state[k] = (uint32_t)key_raw(k);
    enc_prev = enc_state();
    hal_stage(7);
    GPIO_RegOneBitSet(GPIO_A_OUT, PIN(24));          /* as stock after its board set-up (0x44344): A24, A17 high, */
    GPIO_RegOneBitSet(GPIO_A_OUT, PIN(17));          /* 200 ms (power for the audio path or the effects chip; guess) */
    DelayMs(200);

    hal_stage(8);
    backlight_init();
    WDG_Feed();
    hal_stage(9);
    lcd_init();
    WDG_Feed();

    hal_stage(10);
    DMA_ChannelAllocTableSet(DMA_MAP);
    hal_stage(0xA1);
    analog_start();                                  /* as stock; the chip's DAC reaches no jack, stock never starts it */
    hal_stage(0xA8);
    spis_start();                                    /* the DSP link: where the sound goes */

    hal_stage(11);
    Timer_Config(TIMER2, 1000, 0);
    Timer_InterrputSrcEnable(TIMER2, UPDATE_INTERRUPT_SRC);
    hal_stage(0xB1);
    NVIC_EnableIRQ(Timer2_IRQn);
    Timer_Start(TIMER2);
    GIE_ENABLE();
    hal_stage(0xB2);
    return (held_mask >> KEY_A15) & 1u;
}

void hal_audio(hal_block_fn fn) { audio_fn = fn; }
/* 0.50: 1 while both side keys are down (debounced): held 3 s, main hands over to update mode */
/* the patches' flash (main loop): HAL_PATCH_BASE, a 4 KB sector a slot, past the application (verify.py keeps the
 * image below it) and outside every range stock erases (its saves: 0x180000 .. 0x18FFFF, 0x1E1000, 0x1F3000 ..
 * 0x1FFFFF). The flash driver runs from the TCM (Remap_InitTcm); an erase is suspended for the interrupts, as stock
 * erases (SpiFlashErase(1, sector, 1)); the ring is topped up first (an erase takes tens of ms, the ring 93) */
int hal_flash_read(uint32_t addr, void *buf, uint32_t n)
{
    return SpiFlashRead(addr, (uint8_t *)buf, n, 10) == FLASH_NONE_ERR ? 0 : -1;
}
int hal_flash_write(uint32_t addr, const void *buf, uint32_t n)
{
    if (addr < HAL_PATCH_BASE || addr + n > HAL_PATCH_BASE + HAL_PATCH_SLOTS * 4096u || (addr & 4095u) || n > 4096u)
        return -1;
    hal_render();
    SpiFlashErase(SECTOR_ERASE, addr >> 12, 1);
    hal_render();
    return SpiFlashWrite(addr, (uint8_t *)buf, n, 0) == FLASH_NONE_ERR ? 0 : -1;
}

int hal_sides_held(void) { return key_state[KEY_A31] && key_state[KEY_B6]; }
void hal_feed(void) { WDG_Feed(); }
/* 1 if the last reset was the watchdog's; the flags are cleared (else one bite would be read at every boot) */
/* The diagnostic step: NVM byte 14 (battery-backed) holds the start-up step last begun (hal_stage). After a watchdog
 * reset or a crash, hal_report() waits 5 s for each step (2.5 s more after a crash) before handing over to flash_boot:
 * the time from the reset to flash_boot appearing on USB tells which step failed. */
#define STAGE_ADDR 14u
/* DIAG_STEP_MS (the progress meter): after each step that completes, wait this long feeding the watchdog. A hang in
 * step h then brings flash_boot back about 8 + DIAG_STEP_MS x h after the start; a start that completes stays off USB. */
#ifndef DIAG_STEP_MS
#define DIAG_STEP_MS 0
#endif
/* each start-up step announced over USB-MIDI (usbmidi.c) by number and name; nothing written to the NVM any more
 * (0.2 .. 0.7 wrote it at every step, and their start-ups failed in ways none of the reports explained) */
static const char *const STEP_NAME[] = {"", "start", "clocks", "tcm remap", "flash init", "systick", "pins",
    "a24 a17 power", "backlight", "lcd", "dac", "timer irq", "app init", "first frame", "main loop", "healthy"};
/* a start-up step, as a log line (0.21 and 0.22 showed it as the USB serial number, usb_mark: each step left USB
 * and came back, re-initialising the USB block, whose reset register 0x40022000 is also the SPI master's) */
void hal_stage(uint8_t n) { usb_logn("step", n); }
void hal_report(int crash)
{
    uint8_t st = 0;
    uint32_t i, halves;
    BACKUP_NvmRead(STAGE_ADDR, &st, 1);
    if (st > 20u)
        st = 0;
#ifdef DIAG_REPORT
    halves = 40u + 10u * st + (crash ? 5u : 0u);     /* 20 s, then 5 s a step: unmistakable on USB */
#else
    halves = 10u * st + (crash ? 5u : 0u);
#endif
    for (i = 0; i < halves; i++) {
        WDG_Feed();
        DelayMs(500);
    }
    boot_recover();
}

/* A crash or an unexpected interrupt: 50 s (no watchdog, no NVM: nothing that could fail again), then flash_boot,
 * so a crash is told from a hang (never back) and from a clean hand-over (back at once) by the time on USB. */
void hal_crashed(void)
{
    uint32_t i;
    GIE_DISABLE();
    TRACE[2] = 0xE0u | (TRACE[2] & 0xFu);           /* 0xE?: a crash after checkpoint ?'s low digit */
    for (i = 0; i < 100u; i++)                       /* 50 s: apart from the probes' 5 .. 20 s */
        DelayMs(500);
    boot_recover();
}

/* The start-up counter: NVM byte 15 (battery-backed, survives resets) holds 0xA0 + the starts that have not yet
 * reached hal_healthy(). Two such starts in a row (a hang the watchdog reset, a crash loop) and the third goes to
 * flash_boot. It does not depend on the reset flags, which flash_boot may clear before it runs us. */
#define BOOTCOUNT_ADDR 15u
void hal_bootcount(void)
{
    uint8_t v = 0, n;
    BACKUP_NvmRead(BOOTCOUNT_ADDR, &v, 1);
    n = (v & 0xF0u) == 0xA0u ? (uint8_t)(v & 0x0Fu) : 0;
    if (n >= 2u) {
        v = 0;
        BACKUP_NvmWrite(BOOTCOUNT_ADDR, &v, 1);
        boot_recover();
    }
    v = (uint8_t)(0xA0u | (n + 1u));
    BACKUP_NvmWrite(BOOTCOUNT_ADDR, &v, 1);
}
void hal_healthy(void)
{
    uint8_t v = 0;
    BACKUP_NvmWrite(BOOTCOUNT_ADDR, &v, 1);
}

int hal_reset_was_watchdog(void)
{
    int w = (Reset_FlagGet() & 0x4u) != 0;
    Reset_FlagClear();
    return w;
}

/* powering off: the sound and the screen first, then the chip's own power-down (guess: the stock firmware's way
 * is not traced yet); if the power stays, the screen stays dark */
/* POWER OFF, after stock's 0x43E24 (A17, B3, A24 low in that order); 0.57: B3 alone (below). B3 is the power hold (set high
 * at the board's start, 0x409EC): on battery the power goes there and this never returns. On USB power the chip
 * stays on: USB is served (11 6F reaches flash_boot), and the encoder held 1 s brings the board back up in stock's
 * start-up order (A24, A17, 200 ms, B3) with the LCD set up again; then 1 returns, for main to restart the rest.
 * (0.26 called BACKUP_SystemPowerDown and 0.27 released A22, not the hold: both needed a battery disconnect.) */
int hal_power_off(void)
{
    uint32_t t_down = 0;
    audio_fn = 0;
    hal_backlight(0);
    hal_lcd_on(0);
    /* 0.57: the power hold first. On battery the power goes there. On USB power the chip stays on, and A17, A24
     * stay high: the ARM keeps its power. 0.56 cut it as stock (A17, B3, A24) and at power back the ARM never
     * answered nor clocked the link again: silence */
    GPIO_RegOneBitClear(GPIO_B_OUT, PIN(3));         /* stock 0x420FC: the power hold */
    DelayMs(500);
    for (;;) {
        usb_poll();
        if (usb_midi_rx()) {
            DelayMs(50);
            boot_recover();
        }
        if (key_raw(KEY_A15)) {
            if (!t_down)
                t_down = hal_ms() | 1u;
            else if (hal_ms() - t_down >= 1000u)
                break;
        } else
            t_down = 0;
    }
    GPIO_RegOneBitSet(GPIO_B_OUT, PIN(3));
    lcd_init();
    hal_lcd_on(1);
    hal_backlight(90);
    return 1;
}
