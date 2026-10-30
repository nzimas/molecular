/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR: the host tests. Shapes (every rule of the molecule over thousands of seeds), sound (levels, silence,
 * clipping, runaway), the controls' flows with each screen saved as a PNG (build/screens), the memory, and a bench.
 * Exit status 0 when everything passes. */
#define HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include "../core/molecular.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

/* --------------------------------------------------------- PNG and WAV --- */
static uint32_t crc_tab[256];
static uint32_t crc32_of(uint32_t c, const uint8_t *p, size_t n)
{
    size_t i;
    if (!crc_tab[1])
        for (i = 0; i < 256; i++) {
            uint32_t k, x = (uint32_t)i;
            for (k = 0; k < 8; k++)
                x = x & 1 ? 0xEDB88320u ^ (x >> 1) : x >> 1;
            crc_tab[i] = x;
        }
    c = ~c;
    for (i = 0; i < n; i++)
        c = crc_tab[(c ^ p[i]) & 255u] ^ (c >> 8);
    return ~c;
}
static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void png_chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n)
{
    uint8_t h[8];
    uint32_t c;
    be32(h, n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    fwrite(d, 1, n, f);
    c = crc32_of(crc32_of(0, (const uint8_t *)type, 4), d, n);
    be32(h, c);
    fwrite(h, 1, 4, f);
}
/* the frame, 4 times its size (as the owner would see it larger), as an uncompressed PNG */
static void save_png(const char *path)
{
    enum { S = 4, W = SCR_W * S, H = SCR_H * S, ROW = 1 + W * 3 };
    static uint8_t raw[H * ROW], z[H * ROW + (H * ROW / 65535 + 1) * 5 + 6];
    uint8_t ihdr[13] = {0};
    uint32_t i, x, y, a = 1, b = 0, zn = 0, left;
    FILE *f = fopen(path, "wb");
    for (y = 0; y < H; y++) {
        raw[y * ROW] = 0;
        for (x = 0; x < W; x++) {
            uint16_t c = gfx_fb[y / S][x / S];
            uint8_t *p = raw + y * ROW + 1 + x * 3;
            p[0] = (uint8_t)((c >> 11) << 3);
            p[1] = (uint8_t)(((c >> 5) & 63u) << 2);
            p[2] = (uint8_t)((c & 31u) << 3);
        }
    }
    z[zn++] = 0x78;
    z[zn++] = 0x01;
    for (i = 0; i < H * ROW; i += 65535) {                /* stored deflate blocks */
        uint32_t n = H * ROW - i < 65535 ? H * ROW - i : 65535;
        z[zn++] = i + n >= H * ROW;
        z[zn++] = (uint8_t)n;
        z[zn++] = (uint8_t)(n >> 8);
        z[zn++] = (uint8_t)~n;
        z[zn++] = (uint8_t)(~n >> 8);
        memcpy(z + zn, raw + i, n);
        zn += n;
    }
    for (i = 0, left = H * ROW; i < left; i++) {
        a = (a + raw[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    be32(z + zn, (b << 16) | a);
    zn += 4;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    be32(ihdr, W);
    be32(ihdr + 4, H);
    ihdr[8] = 8;
    ihdr[9] = 2;
    png_chunk(f, "IHDR", ihdr, 13);
    png_chunk(f, "IDAT", z, zn);
    png_chunk(f, "IEND", 0, 0);
    fclose(f);
}
static void save_wav(const char *path, const int16_t *s, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    uint32_t data = frames * 4u, v;
    fwrite("RIFF", 1, 4, f); v = 36 + data; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    { uint16_t h[2] = {1, 2}; fwrite(h, 2, 2, f); }
    v = FS; fwrite(&v, 4, 1, f); v = FS * 4; fwrite(&v, 4, 1, f);
    { uint16_t h[2] = {4, 16}; fwrite(h, 2, 2, f); }
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(s, 4, frames, f);
    fclose(f);
}

/* ------------------------------------------------------------- tests --- */
static void test_shapes(void)
{
    static mol_shape_t s;
    uint32_t seed, i, hist[MOL_NODES + 1] = {0}, kinds[AT_N] = {0};
    for (seed = 1; seed <= 3000u; seed++) {
        uint32_t nd = 0, nr = 0, nch[MOL_NODES] = {0};
        mol_draw_shape(&s, seed);
        hist[s.n]++;
        CHECK(s.n >= 2u && s.n <= MOL_NODES, "seed %u: %u nodes", seed, s.n);
        for (i = 0; i < s.n; i++) {
            const mol_node_t *a = &s.node[i];
            int32_t r = node_r(a);
            kinds[a->kind]++;
            CHECK(a->x - r >= 0 && a->x + r < SCR_W && a->y - r >= 0 && a->y + r < SCR_H,
                  "seed %u: node %u off the screen (%d, %d)", seed, i, a->x, a->y);
            if (i) {
                CHECK(a->parent < i, "seed %u: node %u before its parent", seed, i);
                nch[a->parent]++;
                CHECK(a->kind != AT_GEN, "seed %u: a second generator", seed);
            }
            nd += a->kind == AT_DELAY;
            nr += a->kind == AT_REVERB;
            if (fx_heavy(a->kind))
                CHECK(a->nchild == 0u, "seed %u: a heavy atom with children", seed);
        }
        CHECK(nd <= 1u && nr <= 1u, "seed %u: %u delays, %u reverbs", seed, nd, nr);
        CHECK(nch[0] <= 3u, "seed %u: the generator binds %u", seed, nch[0]);
        for (i = 1; i < s.n; i++)
            CHECK(nch[i] <= 2u && nch[i] == s.node[i].nchild, "seed %u: atom %u binds %u", seed, i, nch[i]);
    }
    printf("shapes: 3000 drawn; atoms 1..5: %u %u %u %u %u; kinds FL %u FO %u RG %u CR %u CH %u DL %u RV %u\n",
           hist[2], hist[3], hist[4], hist[5], hist[6], kinds[AT_FILTER], kinds[AT_FOLD], kinds[AT_RING],
           kinds[AT_CRUSH], kinds[AT_CHORUS], kinds[AT_DELAY], kinds[AT_REVERB]);
}

#define SECS 10u
static int16_t wav[FS * SECS * 2u];

/* a molecule from a seed, rendered for SECS seconds; returns the blocks' render time in ns, on average */
static double render(uint32_t seed, int shake)
{
    uint32_t b, nb = FS * SECS / CTL;
    struct timespec t0, t1;
    mol_new(seed);
    while (ATOMIC_GET(mol_phase) != MP_HELD) {
        static int16_t junk[2 * CTL];
        mol_block(junk);
    }
    mol_poll();
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (b = 0; b < nb; b++) {
        if (shake && b == nb / 2u) {
            mol_shake_gen(seed * 7u);
            mol_shake_fx(seed * 13u);
        }
        mol_block(wav + b * 2u * CTL);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / nb;
}

static void test_sound(void)
{
    uint32_t seed, i, sec;
    double ns_sum = 0;
    mol_volume(Q15);
    for (seed = 1; seed <= 40u; seed++) {
        double rms[SECS];
        uint32_t clip = 0;
        ns_sum += render(seed, seed % 2u);
        for (sec = 0; sec < SECS; sec++) {
            double e = 0;
            for (i = sec * FS * 2u; i < (sec + 1u) * FS * 2u; i++) {
                e += (double)wav[i] * wav[i];
                clip += wav[i] >= 32000 || wav[i] <= -32000;
            }
            rms[sec] = sqrt(e / (FS * 2.0)) / 32768.0;
        }
        CHECK(rms[4] > 0.005, "seed %u: silent after the swell (rms %.4f)", seed, rms[4]);
        CHECK(clip < FS / 100u, "seed %u: %u samples at the rails", seed, clip);
        CHECK(rms[SECS - 1u] < 6.0 * rms[4] + 0.02, "seed %u: running away (%.3f -> %.3f)", seed, rms[4], rms[SECS - 1u]);
        if (seed <= 4u) {
            char path[64];
            snprintf(path, sizeof path, "build/molecule_%u.wav", seed);
            save_wav(path, wav, FS * SECS);
        }
        if (seed <= 6u)
            printf("  seed %2u: %-9s %u atoms  rms 1s %.3f  4s %.3f  9s %.3f  rails %u\n", seed,
                   FF_NAME[mol_d.gen.patch.family], mol_d.shape.n - 1u, rms[1], rms[4], rms[9], clip);
    }
    printf("sound: 40 molecules x %u s; a block (%u samples) takes %.1f us on this host, %.2f %% of real time\n",
           SECS, CTL, ns_sum / 40 / 1000, ns_sum / 40 / (1e9 * CTL / FS) * 100);
}

/* the controls, with a clock of our own; each screen saved */
static uint32_t now_ms;
static int cut_seen;             /* app_tick asked for the power to be cut (until_cut takes it) */
static void run_ms(uint32_t ms)
{
    static int16_t lr[2 * CTL];
    uint32_t end = now_ms + ms;
    while (now_ms < end) {
        uint32_t b;
        for (b = 0; b < 44u; b++)                       /* ~1 ms of audio a step */
            mol_block(lr);
        now_ms += 32;
        cut_seen |= app_tick(now_ms);
    }
}
static void shot(const char *name)
{
    char path[96];
    app_draw(now_ms);
    snprintf(path, sizeof path, "build/screens/%s.png", name);
    save_png(path);
}
static void press_short(void)
{
    app_press(now_ms);
    run_ms(100);
    app_release(now_ms);
    run_ms(50);
}
static void press_long(void)
{
    app_press(now_ms);
    run_ms(900);
    app_release(now_ms);
    run_ms(50);
}
/* ticks until the power is cut; 1 if it was within ms */
static int until_cut(uint32_t ms)
{
    static int16_t lr[2 * CTL];
    uint32_t end = now_ms + ms, b;
    if (cut_seen) {
        cut_seen = 0;
        return 1;
    }
    while (now_ms < end) {
        for (b = 0; b < 44u; b++)
            mol_block(lr);
        now_ms += 32;
        if (app_tick(now_ms))
            return 1;
    }
    return 0;
}
static void test_flows(void)
{
    now_ms = 1000;
    app_init(42, now_ms, 1);                            /* booted by the held encoder */
    run_ms(100);
    CHECK(app.screen == SC_WAKE && !app.yes, "booting did not ask POWER ON? with NO first");
    shot("01_power_on_no");
    app_release(now_ms);                                /* letting go of the hold that booted us answers nothing */
    run_ms(50);
    CHECK(app.screen == SC_WAKE, "the boot hold's release answered POWER ON?");
    CHECK(ATOMIC_GET(mol_phase) == MP_HELD, "sounding before YES");
    app_turn(1, now_ms);
    run_ms(50);
    shot("02_power_on_yes");
    press_short();                                      /* YES: the first molecule */
    CHECK(app.screen == SC_MOL, "YES did not power on");
    run_ms(100);
    shot("03_new_molecule_family");
    run_ms(4000);
    shot("04_molecule");
    CHECK(ATOMIC_GET(mol_phase) == MP_RUN, "not sounding after YES");
    app_turn(-3, now_ms);
    run_ms(100);
    shot("05_volume");
    CHECK(app.vol == 64, "volume %d after three turns down", app.vol);
    app_left(now_ms);
    run_ms(100);
    shot("06_left_lfo");
    app_right(now_ms);
    run_ms(100);
    shot("07_right_fx");
    {
        uint32_t before = mol_d.seed;
        press_short();                                  /* a short press: a new molecule */
        run_ms(1500);
        CHECK(mol_d.seed != before, "a short press made no new molecule");
        shot("08_another_molecule");
    }
    press_long();                                       /* held: the menu, which stays after the release */
    CHECK(app.screen == SC_MENU, "a long press did not open the menu");
    shot("09_menu");
    press_short();                                      /* POWER: the confirmation, NO first */
    CHECK(app.screen == SC_CONFIRM && !app.yes, "no confirmation, or YES chosen first");
    shot("10_power_off_no");
    press_short();                                      /* NO: back to the molecule, the sound untouched */
    CHECK(app.screen == SC_MOL && ATOMIC_GET(mol_phase) == MP_RUN, "NO did not return to the sounding molecule");
    press_long();
    press_long();                                       /* held in the menu: back */
    CHECK(app.screen == SC_MOL, "a long press in the menu did not go back");
    press_long();
    press_short();
    app_turn(1, now_ms);
    run_ms(50);
    shot("11_power_off_yes");
    app_press(now_ms);
    run_ms(50);
    app_release(now_ms);
    run_ms(150);
    shot("12_off");
    CHECK(until_cut(3000), "YES never cut the power");
    CHECK(ATOMIC_GET(mol_phase) == MP_HELD && app.screen == SC_ASLEEP, "not silent and dark after powering off");
    app_turn(1, now_ms);                                /* asleep: nothing but a held encoder does anything */
    app_left(now_ms);
    press_short();
    CHECK(app.screen == SC_ASLEEP, "asleep, a short press or a turn woke it");
    press_long();                                       /* held: POWER ON? again, NO first */
    CHECK(app.screen == SC_WAKE && !app.yes, "a long press asleep did not ask POWER ON?");
    press_short();                                      /* NO: off again at once */
    CHECK(until_cut(200) && app.screen == SC_ASLEEP, "NO did not cut the power");
    press_long();
    cut_seen = 0;
    CHECK(until_cut(APP_WAKE_MS + 500u), "POWER ON? unanswered did not cut the power");
    press_long();
    app_turn(1, now_ms);
    press_short();
    run_ms(2000);
    CHECK(app.screen == SC_MOL && ATOMIC_GET(mol_phase) == MP_RUN, "powering on a second time made no sound");
    printf("flows: the controls and screens as designed; screens in build/screens\n");
}

static void test_font(void)
{
    gfx_clear(C_BLACK);
    gfx_text(2, 4, "ABCDEFGHI", 2, C_WHITE);
    gfx_text(2, 24, "JKLMNOPQR", 2, C_WHITE);
    gfx_text(2, 44, "STUVWXYZ", 2, C_WHITE);
    gfx_text(2, 64, "0123456789", 2, C_WHITE);
    gfx_text(2, 84, "? % - + . :", 2, C_WHITE);
    gfx_power(64, 130, 20, C_WHITE);
    save_png("build/screens/00_font.png");
}

int main(void)
{
    test_shapes();
    test_sound();
    test_flows();
    test_font();
    printf("memory: design %zu B, generator %zu B, atoms %zu B, chorus lines %zu B, delay %zu B, reverb %zu B, "
           "frame %zu B\n", sizeof mol_d, sizeof mol_gs, sizeof mol_fs, sizeof fx_line, sizeof dl_a, sizeof rv_mem,
           sizeof gfx_fb);
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
