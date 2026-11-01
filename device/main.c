/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR on the Pocket Master: the firmware's main loop. The core (../core) is the same code as the simulator's;
 * this file connects it to the board (hal.c) and to USB-MIDI (usbmidi.c).
 *
 * Order, from power-on:
 *   1. two keys held at a real power-on: flash_boot's update mode at once (no other code runs first)
 *   2. interrupts masked, the stock clocks, USB-MIDI (before the LCD: the USB start resets blocks the SPI master
 *      shares a register with), then the board (hal_init), then the ARM effects processor's settings over its
 *      serial link (armlink.c), then the app
 *   3. the log: kept, sent while the Mac reads (pmmidi send F07D4CF7)
 * Into flash_boot while running: the Mac's 11 6F, or both side keys held 3 s. No watchdog: a hang stays put.
 *
 * The core is compiled with HOST: here it only means plain sections (no code copied into RAM yet). */
#include <stdint.h>
#include <nds32_intrinsic.h>
#include <string.h>
#include "hal.h"
#include "usbmidi.h"
#include "armlink.h"
#include "molecular.c"
#include "chip_info.h"
#include "irqn.h"
#include "timeout.h"
#include "clk.h"
#include "watchdog.h"
#include "dac_interface.h"


static void audio_block(int16_t *lr) { mol_block(lr); }

/* the patches (core/patch.c): a flash sector a slot */
static int plat_patch_read(uint32_t slot, void *buf, uint32_t n)
{
    return slot < HAL_PATCH_SLOTS ? hal_flash_read(HAL_PATCH_BASE + slot * 4096u, buf, n) : -1;
}
static int plat_patch_write(uint32_t slot, const void *buf, uint32_t n)
{
    int r = slot < HAL_PATCH_SLOTS ? hal_flash_write(HAL_PATCH_BASE + slot * 4096u, buf, n) : -1;
    usb_logn(r ? "patch write failed" : "patch written", slot + 1u);
    return r;
}

static uint32_t bits(uint32_t m)
{
    uint32_t n = 0;
    for (; m; m &= m - 1u)
        n++;
    return n;
}

static void serve_usb(void)
{
    usb_poll();
    if (usb_midi_rx()) {
        usb_log("11 6F: to flash_boot");
        DelayMs(50);
        boot_recover();
    }
}

int main(void)
{
    uint32_t held, last_draw = 0, started, i;
    int wd, push_held, healthy = 0;
    WDG_Disable();                                   /* first of all, as stock (0x4FB24): flash_boot leaves the
                                                      * watchdog running (1 s), and 0.1 .. 0.11 were reset by it */
    held = hal_keys_at_boot();
    if (bits(held & 7u) >= 2u)
        boot_recover();
    wd = hal_reset_was_watchdog();
    for (i = 1; i <= 30u; i++)                       /* whatever flash_boot left enabled: off (the tick stays) */
        NVIC_DisableIRQ((IRQn_Type)i);
    hal_clocks();                                    /* 0.14: the stock clocks (USB's among them), the flash and the
                                                      * tick, as stock */
    WDG_Disable();                                   /* again, whatever the clocks did */
    GIE_ENABLE();
    usb_start();
    usb_log("Molecular build 64");
    usb_logn("keys held", held);
    usb_logn("watchdog reset", (uint32_t)wd);
    push_held = hal_init(held);
    arm_init();                                      /* 0.36: the effects processor's control link (armlink.c) */
    DelayMs(300);                                    /* (the ARM's own start; stock waits 200 ms after A17, A24) */
    arm_start();                                     /* 0.37: levels and a preset, as stock */
    hal_stage(12);
    app_init(Chip_RandomSeedGet() ^ 0x6D6F6Cu, hal_ms(), push_held);
    if (!push_held) {                                /* not powered on by the encoder (an install, flash_boot): */
        mol_wake();                                  /* no POWER ON? question, straight to a molecule, sounding */
        app_new_molecule();                          /* (0.27: the question timed out during every test) */
        app.screen = SC_MOL;
    }
    hal_stage(0xC1);
    hal_audio(audio_block);
    hal_stage(13);
    app_draw(hal_ms());
    hal_stage(0xD1);
    hal_lcd_frame(&gfx_fb[0][0], SCR_W, SCR_H);
    hal_stage(0xD2);
    hal_lcd_on(1);
    hal_backlight(90);
    hal_stage(14);
    started = hal_ms();
    for (;;) {
        uint32_t now = hal_ms();
        int e;
        hal_render();
        serve_usb();
        {   /* 0.50: both side keys held 3 s: update mode (flash_boot), as the Mac's 11 6F */
            static uint32_t sides_at;
            if (!hal_sides_held())
                sides_at = 0;
            else if (!sides_at)
                sides_at = now | 1u;
            else if (now - sides_at >= 3000u) {
                usb_log("side keys: to flash_boot");
                hal_backlight(0);
                DelayMs(50);
                boot_recover();
            }
        }
        arm_poll();                                  /* (the ARM's own frames: read, not kept) */
        {   /* the sound, every 2 s, in the log: the ARM's answers at the start, blocks made, the loudest sample, and
             * the link's exchanges short of audio or started late (each a possible click) */
            static uint32_t last_snd;
            extern volatile uint32_t hal_fed, hal_peak, hal_under, hal_late, hal_xfers, hal_relinks;
            if (now - last_snd >= 2000u) {
                last_snd = now;
                usb_logn("arm levels ok", arm_ok_levels);
                usb_logn("arm preset ok", arm_ok_preset);
                usb_logn("made", hal_fed);
                usb_logn("peak", hal_peak);
                usb_logn("short", hal_under);
                usb_logn("late", hal_late);
                usb_logn("xfers", hal_xfers);
                usb_logn("relinks", hal_relinks);
                {   /* 0.63: the audio's load, % of real time over the last 2 s, and its longest block (726 us each) */
                    extern volatile uint32_t hal_busy_us, hal_block_max_us;
                    usb_logn("load %", hal_busy_us / 20000u);
                    usb_logn("block max us", hal_block_max_us);
                    hal_busy_us = 0;
                    hal_block_max_us = 0;
                }
                hal_peak = 0;
            }
        }
        if (!healthy && now - started > 5000u) {
            hal_stage(15);
            healthy = 1;
        }
        while ((e = hal_event()) >= 0) {
            if (HAL_EV_KEY(e)) {                     /* every key by its input, down and up (which is which) */
                static const char *const IN[5] = {"", "B6", "A15", "A31", "PWR"};
                static char line[12];
                const char *nm = IN[HAL_EV_KEY(e)], *ud = e & HAL_EV_UP ? " up" : " down";
                uint32_t n = 0;
                while (*nm) line[n++] = *nm++;
                while (*ud) line[n++] = *ud++;
                line[n] = 0;
                usb_log(line);
            }
            switch (e & 0x8F) {
            case HAL_EV_LEFT: app_left(now); break;
            case HAL_EV_RIGHT: app_right(now); break;
            case HAL_EV_LEFT | HAL_EV_UP: app_left_up(now); break;
            case HAL_EV_RIGHT | HAL_EV_UP: app_right_up(now); break;
            case HAL_EV_PUSH: app_press(now); break;
            case HAL_EV_PUSH | HAL_EV_UP: app_release(now); break;
            case HAL_EV_CW: app_turn(1, now); break;
            case HAL_EV_CCW: app_turn(-1, now); break;
            }
        }
        if (app_tick(now)) {                         /* POWER OFF: the board off as stock (hal.c). On battery that
                                                      * is the end; on USB power the encoder held 1 s comes back
                                                      * here, and the board starts again: the ARM, then POWER ON? */
            usb_log("power off");
            hal_power_off();
            usb_log("power back");
            hal_audio(audio_block);
            arm_init();
            DelayMs(300);
            for (i = 0; i < 3u; i++) {               /* 0.56: the ARM powered again: asked until it answers */
                arm_start();
                if (arm_ok_levels && arm_ok_preset)
                    break;
                DelayMs(200);
            }
            hal_link_restart();                      /* 0.56: the link anew (0.55 never restarted it: silent,
                                                      * and every fade waited for good) */
            while (hal_event() >= 0)
                ;
            app_init(Chip_RandomSeedGet() ^ 0x6D6F6Cu, hal_ms(), 1);
        }
        if (now - last_draw >= 40u) {                /* 25 frames a second at most */
            last_draw = now;
            app_draw(now);
            hal_lcd_frame(&gfx_fb[0][0], SCR_W, SCR_H);
        }
    }
}
