/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR: the simulator. The Pocket Master's screen four times its size, its sound through the Mac's output, and
 * the keyboard as its three controls:
 *
 *   left arrow   the left button          up / down arrows, or the wheel   the encoder, turned
 *   right arrow  the right button         space or return                  the encoder, pressed (hold for the menu)
 *   escape       quit
 *
 * It starts as the device does once the encoder has been held: POWER ON?. Powered off, the window goes dark; hold
 * space or return to be asked again. */
#define HOST 1
#include <SDL.h>
#include <stdio.h>
#include <time.h>
#include "../core/molecular.c"

#define ZOOM 4

/* the patches' flash (core/patch.c): one file of 64 KB, read and written whole, as NOR flash (erased: 0xFF; written:
 * bits to 0 only), in SDL's place for the user's files. An older file (builds up to 74: a 4 KB record a slot) reads as
 * it was: those slots are where they were */
#define PF_BYTES 0x10000u
static uint8_t pf_mem[PF_BYTES];
static FILE *patch_file(const char *mode)
{
    static char path[512];
    if (!path[0]) {
        char *dir = SDL_GetPrefPath("molecular", "sim");
        snprintf(path, sizeof path, "%spatches.bin", dir ? dir : "");
        SDL_free(dir);
    }
    return fopen(path, mode);
}
static void pf_load(void)
{
    FILE *f = patch_file("rb");
    memset(pf_mem, 0xFF, sizeof pf_mem);
    if (f) {
        size_t n = fread(pf_mem, 1, sizeof pf_mem, f);
        (void)n;
        fclose(f);
    }
}
static int pf_store(void)
{
    FILE *f = patch_file("wb");
    int ok = f && fwrite(pf_mem, 1, sizeof pf_mem, f) == sizeof pf_mem;
    return (f && fclose(f)) || !ok ? -1 : 0;
}
static int plat_flash_read(uint32_t off, void *buf, uint32_t n)
{
    if (off > PF_BYTES || n > PF_BYTES - off)
        return -1;
    pf_load();
    memcpy(buf, pf_mem + off, n);
    return 0;
}
static int plat_flash_erase(uint32_t off)
{
    if (off >= PF_BYTES || (off & 4095u))
        return -1;
    pf_load();
    memset(pf_mem + off, 0xFF, 4096u);
    return pf_store();
}
static int plat_flash_program(uint32_t off, const void *buf, uint32_t n)
{
    uint32_t i;
    if (off > PF_BYTES || n > PF_BYTES - off)
        return -1;
    pf_load();
    for (i = 0; i < n; i++)
        pf_mem[off + i] &= ((const uint8_t *)buf)[i];
    return pf_store();
}

static void audio_cb(void *u, Uint8 *stream, int len)
{
    int16_t *out = (int16_t *)stream;
    int frames = len / 4, done;
    (void)u;
    for (done = 0; done + CTL <= frames; done += CTL)    /* (the buffer is a multiple of CTL frames) */
        mol_block(out + done * 2);
}

int main(void)
{
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    SDL_AudioDeviceID dev;
    SDL_AudioSpec want, have;
    int run = 1;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    win = SDL_CreateWindow("Molecular (Pocket Master simulator)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           SCR_W * ZOOM, SCR_H * ZOOM, 0);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H);
    SDL_zero(want);
    want.freq = FS;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 256;                                 /* 8 blocks: ~6 ms */
    want.callback = audio_cb;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev || have.freq != FS || have.format != AUDIO_S16SYS || have.channels != 2 || have.samples % CTL) {
        fprintf(stderr, "audio: no 44.1 kHz stereo 16-bit output (%s)\n", SDL_GetError());
        return 1;
    }
    app_init((uint32_t)time(NULL) ^ SDL_GetTicks(), SDL_GetTicks(), 0);
    SDL_PauseAudioDevice(dev, 0);
    while (run) {
        SDL_Event e;
        uint32_t now = SDL_GetTicks();
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT)
                run = 0;
            else if (e.type == SDL_MOUSEWHEEL)
                app_turn(e.wheel.y > 0 ? 1 : -1, now);
            else if ((e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) && !e.key.repeat) {
                int down = e.type == SDL_KEYDOWN;
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: run = 0; break;
                case SDLK_LEFT: down ? app_left(now) : app_left_up(now); break;
                case SDLK_RIGHT: down ? app_right(now) : app_right_up(now); break;
                case SDLK_UP: if (down) app_turn(1, now); break;
                case SDLK_DOWN: if (down) app_turn(-1, now); break;
                case SDLK_SPACE:
                case SDLK_RETURN: down ? app_press(now) : app_release(now); break;
                }
            }
        }
        (void)app_tick(now);                            /* (powered off: the device cuts the power; here, dark) */
        app_draw(now);
        SDL_UpdateTexture(tex, NULL, gfx_fb, SCR_W * 2);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    SDL_CloseAudioDevice(dev);
    SDL_Quit();
    return 0;
}
