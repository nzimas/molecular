/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR on the Pocket Master: the board's interface to the firmware (hal.c). */
#pragma once
#include <stdint.h>

#define HAL_BLOCK 32u            /* frames a block: the core's CTL */

/* events from the 1 ms interrupt: a key down, or with HAL_EV_UP its release; the encoder a detent either way */
enum { HAL_EV_LEFT = 1, HAL_EV_RIGHT = 2, HAL_EV_PUSH = 3, HAL_EV_CW = 4, HAL_EV_CCW = 5 };
#define HAL_EV_UP 0x80u
#define HAL_EV_KEY(e) (((e) >> 4) & 7u)  /* a key's event: 1 + its input (B6, A15, A31, the power key); 0: the encoder */

typedef void (*hal_block_fn)(int16_t *lr);   /* one block, interleaved stereo int16 */

uint32_t hal_keys_at_boot(void);
int hal_reset_was_watchdog(void);
void hal_bootcount(void);
void hal_healthy(void);
void hal_stage(uint8_t n);
void hal_report(int crash) __attribute__((noreturn));
void hal_clocks(void);
int hal_init(uint32_t held_mask);
void boot_recover(void) __attribute__((noreturn));
void hal_backlight(uint32_t pct);
void hal_lcd_frame(const uint16_t *px, uint32_t w, uint32_t h);
void hal_lcd_on(int on);
void hal_link_restart(void);            /* the audio link to the ARM set up afresh */
void hal_render(void);                  /* fills the audio ring; call often from the main loop */
int hal_sides_held(void);               /* both side keys down */
#define HAL_PATCH_BASE 0x170000u        /* the patches' flash: a 4 KB sector a slot */
#define HAL_PATCH_SLOTS 16u             /* (room kept; the core uses PATCH_SLOTS of them) */
int hal_flash_read(uint32_t addr, void *buf, uint32_t n);
int hal_flash_write(uint32_t addr, const void *buf, uint32_t n);   /* one sector, erased first */
void hal_audio(hal_block_fn fn);
int hal_event(void);
uint32_t hal_ms(void);
void hal_feed(void);
int hal_power_off(void);                /* returns only on USB power, when the encoder brings it back */
