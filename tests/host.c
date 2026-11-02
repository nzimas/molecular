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

/* 1 if the slots' map holds slot only (-1: none) */
static int map_is(const uint8_t *m, int32_t only)
{
    uint32_t i;
    for (i = 0; i < PATCH_SLOTS; i++)
        if ((int32_t)(m[i >> 3] >> (i & 7u) & 1u) != ((int32_t)i == only))
            return 0;
    return 1;
}

/* the patches' flash: in memory, as NOR flash (erased: 0xFF; written: bits to 0 only). pf_ops_left: the power cut
 * after that many erases and writes (-1: never) */
static uint8_t pf_mem[0x10000];
static int32_t pf_ops_left = -1;
static int pf_power(void) { return pf_ops_left < 0 || pf_ops_left-- > 0; }
static int plat_flash_read(uint32_t off, void *buf, uint32_t n)
{
    if (off > sizeof pf_mem || n > sizeof pf_mem - off)
        return -1;
    memcpy(buf, pf_mem + off, n);
    return 0;
}
static int plat_flash_erase(uint32_t off)
{
    if (off >= sizeof pf_mem || (off & 4095u) || !pf_power())
        return -1;
    memset(pf_mem + off, 0xFF, 4096u);
    return 0;
}
static int plat_flash_program(uint32_t off, const void *buf, uint32_t n)
{
    uint32_t i;
    if (off > sizeof pf_mem || n > sizeof pf_mem - off || !pf_power())
        return -1;
    for (i = 0; i < n; i++)
        pf_mem[off + i] &= ((const uint8_t *)buf)[i];
    return 0;
}

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
/* every rule of a shape; the kinds counted into kinds (or not: 0) */
static void check_shape(const mol_shape_t *s, uint32_t seed, const char *what, uint32_t *kinds)
{
    uint32_t nd = 0, nr = 0, nch[MOL_NODES] = {0}, bytes = 0, i;
    CHECK(s->n >= 2u && s->n <= MOL_NODES, "%s %u: %u nodes", what, seed, s->n);
    for (i = 0; i < s->n; i++) {
        const mol_node_t *a = &s->node[i];
        int32_t r = node_r(a);
        if (kinds)
            kinds[a->kind]++;
        CHECK(a->x - r >= 0 && a->x + r < SCR_W && a->y - r >= 0 && a->y + r < SCR_H,
              "%s %u: node %u off the screen (%d, %d)", what, seed, i, a->x, a->y);
        if (i) {
            CHECK(a->parent < i, "%s %u: node %u before its parent", what, seed, i);
            nch[a->parent]++;
            CHECK(a->kind != AT_GEN, "%s %u: a second generator", what, seed);
        }
        bytes += fx_bytes(a->kind);
        nd += a->kind == AT_DELAY;
        nr += a->kind == AT_REVERB;
        if (fx_heavy(a->kind))
            CHECK(a->nchild == 0u, "%s %u: a heavy atom with children", what, seed);
    }
    CHECK(nd <= 1u && nr <= 1u, "%s %u: %u delays, %u reverbs", what, seed, nd, nr);
    CHECK(bytes <= FX_ARENA, "%s %u: atoms need %u B, the arena has %u", what, seed, bytes, FX_ARENA);
    CHECK(nch[0] <= 3u, "%s %u: the generator binds %u", what, seed, nch[0]);
    for (i = 1; i < s->n; i++)
        CHECK(nch[i] <= 2u && nch[i] == s->node[i].nchild, "%s %u: atom %u binds %u", what, seed, i, nch[i]);
}

/* the same atoms, by what they are (kind, values, LFO), in any order */
static int same_atoms(const mol_shape_t *a, const mol_shape_t *b)
{
    uint32_t i, j, taken = 0;
    if (a->n != b->n)
        return 0;
    for (i = 1; i < a->n; i++) {
        for (j = 1; j < b->n; j++)
            if (!((taken >> j) & 1u) && a->node[i].kind == b->node[j].kind &&
                !memcmp(a->node[i].p, b->node[j].p, sizeof a->node[i].p) && a->node[i].lval == b->node[j].lval &&
                !memcmp(&a->node[i].lfo, &b->node[j].lfo, sizeof a->node[i].lfo))
                break;
        if (j == b->n)
            return 0;
        taken |= 1u << j;
    }
    return 1;
}

static void test_shapes(void)
{
    static mol_shape_t s, t;
    static mol_design_t d;
    uint32_t seed, i, hist[MOL_NODES + 1] = {0}, kinds[AT_N] = {0}, rewired = 0, single = 0, lfos = 0, atoms = 0;
    for (seed = 1; seed <= 3000u; seed++) {
        mol_draw_shape(&s, seed);
        hist[s.n]++;
        check_shape(&s, seed, "seed", kinds);
    }
    printf("shapes: 3000 drawn; atoms 1..5: %u %u %u %u %u; kinds:", hist[2], hist[3], hist[4], hist[5], hist[6]);
    for (i = 1; i < AT_N; i++) {
        printf(" %s %u", AT_SYM[i], kinds[i]);
        CHECK(kinds[i] > 0u, "kind %s never drawn", at_name(i));
    }
    printf("\n");
    for (seed = 1; seed <= 400u; seed++) {             /* rewired: the same atoms, every rule kept, bound anew */
        rnd_t r;
        memset(&d, 0, sizeof d);
        mol_draw_shape(&d.shape, seed);
        rnd_seed(&r, seed);
        for (i = 1; i < d.shape.n; i++) {
            fx_draw_values(&d.shape.node[i], &r);
            fx_draw_lfo(&d.shape.node[i], &r);
            atoms++;
            lfos += d.shape.node[i].lfo.depth != 0;
            if (fx_heavy(d.shape.node[i].kind))
                CHECK(!d.shape.node[i].lfo.depth, "seed %u: an LFO on DELAY or REVERB", seed);
            else if (d.shape.node[i].lfo.depth) {
                uint32_t kd = d.shape.node[i].kind, v = d.shape.node[i].lval;
                CHECK(v < 4u && ((FX_LFO_OK[d.shape.node[i].kind] >> v) & 1u), "seed %u: an LFO on a switch", seed);
                CHECK(v < 4u && d.shape.node[i].lfo.depth * 2 <= at_hi(kd, v) - at_lo(kd, v),
                      "seed %u: an atom's LFO wider than its value's bounds", seed);
            }
        }
        if (mol_rewire_shape(&t, &d.shape, seed * 31u, 0, 0)) {
            rewired++;
            check_shape(&t, seed, "rewired", 0);
            CHECK(same_atoms(&t, &d.shape), "seed %u: rewiring changed the atoms", seed);
        } else
            single++;
    }
    CHECK(rewired > 300u, "only %u of 400 molecules rewired", rewired);
    printf("rewire: 400 molecules, %u bound anew, %u with one wiring only; atoms' LFOs: %u of %u moving\n", rewired,
           single, lfos, atoms);
}

/* each of Norilsk's effects as an atom on its own, 8 draws of its values: heard with a sound in, within full scale,
 * and quiet after (Norilsk's host quiet: 1 s of noise, then 40 s of nothing; a sound still there must have fallen 6 dB
 * since the silence began, or be under 32; CLOUD not frozen, as there); and its time a block */
static void test_atoms(void)
{
    static int32_t in[CTL], out[CTL];
    static fx_state_t st;
    uint32_t kind, d, b, i, nb = (uint32_t)(41 * FS / CTL), one = (uint32_t)(FS / CTL);
    rnd_t nz;
    double worst_us = 0;
    const char *worst = "";
    for (kind = 1; kind < AT_N; kind++) {
        double ns = 0;
        uint32_t nblk = 0;
        if (fx_heavy(kind))
            continue;
        CHECK(fx_bytes(kind) <= FX_ARENA, "%s: %u B, over the arena", at_name(kind), fx_bytes(kind));
        for (d = 0; d < 8u; d++) {
            mol_node_t a;
            rnd_t r;
            double e = 0, first = 0, last = 0;
            int32_t pk = 0, tail = 0, held = 0;
            memset(&a, 0, sizeof a);
            a.kind = (uint8_t)kind;
            rnd_seed(&r, 0xA70Bu + kind * 97u + d);
            fx_draw_values(&a, &r);
            if (kind == AT_CLOUD && a.p[2] >= 50)
                a.p[2] = 20;                                     /* (frozen: held by design) */
            rack_root = 40 + (int32_t)(d % 12u);
            fx_start(&st, &a, 0);
            rnd_seed(&nz, 77u + d);
            for (b = 0; b < nb; b++) {
                struct timespec t0, t1;
                for (i = 0; i < CTL; i++)
                    in[i] = b < one ? mulq15(rnd_bi(&nz), 12000) + (sin_ph((b * CTL + i) * 21474836u) >> 3) : 0;
                clock_gettime(CLOCK_MONOTONIC, &t0);
                fx_block(&st, &a, in, out);
                clock_gettime(CLOCK_MONOTONIC, &t1);
                if (b < 2u * one) {
                    ns += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
                    nblk++;
                }
                for (i = 0; i < CTL; i++) {
                    int32_t v = absi(out[i]);
                    pk = maxi(pk, v);
                    if (b < one)
                        e += (double)out[i] * out[i];
                    else if (b >= 3u * one && b < 4u * one)
                        first += (double)v * v;
                    else if (b >= nb - one) {
                        if (b == nb - one && !i)
                            held = out[0];                       /* (a level held, not a sound: see below) */
                        last += (double)v * v;
                        tail = maxi(tail, absi(out[i] - held));
                    }
                }
            }
            CHECK(sqrt(e / FS) > 30.0, "%s draw %u: nothing heard (rms %.1f)", at_name(kind), d, sqrt(e / FS));
            CHECK(pk <= Q15, "%s draw %u: peak %d", at_name(kind), d, pk);
            CHECK(tail < 32 || 10.0 * log10((last + 1) / (first + 1)) <= -6.0,
                  "%s draw %u: holds a sound with nothing in (%d after 40 s)", at_name(kind), d, tail);
            CHECK(absi(held) < 64, "%s draw %u: holds a level of %d with nothing in", at_name(kind), d, held);
            /* (Norilsk's CHEB, some values, rests at a level of 30 .. 40, -60 dB: a rounding's fixed point, inaudible;
             * Norilsk's own quiet test, other values, does not meet it. The sound itself must still fall away) */
        }
        if (ns / nblk / 1000 > worst_us) {
            worst_us = ns / nblk / 1000;
            worst = at_name(kind);
        }
    }
    printf("atoms: all 44 each heard, within full scale, quiet after; the slowest, %s, %.2f us a block here\n",
           worst, worst_us);
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
            mol_shake_lfos(seed * 7u);
            mol_shake_fx(seed * 13u, 0);
        }
        mol_block(wav + b * 2u * CTL);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / nb;
}

/* every family the generator draws, alone (no atoms), 4 seeds each, 6 s: heard, within full scale; the struck ones
 * struck, more than once */
static void test_families(void)
{
    static gen_design_t g;
    static gen_state_t st;
    static int32_t buf[CTL];
    uint32_t i, seed, b;
    int heard = 0;
    for (i = 0; i < FAM_NPICK; i++) {
        uint32_t f = FAM_PICK[i].f;
        for (seed = 1; seed <= 4u; seed++) {
            gen_clock_t c;
            int32_t pk = 0, strikes = 0, was = 0;
            uint32_t sd = seed * 7919u + f;
            memset(&g, 0, sizeof g);
            gen_family(&g, &c, &FAM_PICK[i], sd);
            CHECK(g.patch.family == f, "family %s drawn as %s", fam_name(f), fam_name(g.patch.family));
            CHECK(fam_is_pm(f) || fam_is_struck(f) == (c.mode != GK_HOLD && !c.crackle),
                  "%s: struck and its clock disagree", fam_name(f));   /* (a physical family: either) */
            g.note[0] = 40, g.note[1] = 47, g.note[2] = 52;
            gen_start(&st, &g, &c, sd);
            for (b = 0; b < 6u * FS / CTL; b++) {
                uint32_t k;
                int32_t bp = 0;
                gen_block(&st, &g, &c, buf);
                for (k = 0; k < CTL; k++)
                    bp = maxi(bp, absi(buf[k]));
                pk = maxi(pk, bp);
                strikes += c.mode != GK_HOLD && !c.crackle && st.knext > was;   /* (the clock wound on: a strike) */
                was = st.knext;
            }
            CHECK(pk > 1500, "%s seed %u: never heard (peak %d)", fam_name(f), seed, pk);
            CHECK(pk < 8 * Q15, "%s seed %u: peak %d", fam_name(f), seed, pk);   /* (the generator's headroom: held,
                                                      * REVERSE reaches 5 x full scale; the atoms' guards and the
                                                      * leveller take it, as before 0.55) */
            if (c.mode != GK_HOLD && !c.crackle)
                CHECK(strikes >= 2, "%s seed %u: struck %d times in 6 s", fam_name(f), seed, strikes);
            heard += pk > 1500;
        }
    }
    printf("families: %u, each alone 4 x 6 s, %d of %u heard\n", (uint32_t)FAM_NPICK, heard, (uint32_t)FAM_NPICK * 4u);
}

static void test_sound(void)
{
    uint32_t seed, i, sec, struck = 0;
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
        if (mol_d.clock.mode == GK_HOLD || mol_d.clock.crackle) {
            CHECK(rms[4] > 0.005, "seed %u: silent after the swell (rms %.4f)", seed, rms[4]);
            CHECK(rms[SECS - 1u] < 6.0 * rms[4] + 0.02, "seed %u: running away (%.3f -> %.3f)", seed, rms[4],
                  rms[SECS - 1u]);
        } else {                                        /* struck: a clock may pause seconds; its loudest second */
            double hi = 0;
            for (sec = 1; sec < SECS; sec++)
                hi = rms[sec] > hi ? rms[sec] : hi;
            CHECK(hi > 0.005, "seed %u: struck, never heard (rms %.4f at most)", seed, hi);
            struck++;
        }
        CHECK(clip < FS / 100u, "seed %u: %u samples at the rails", seed, clip);
        if (seed <= 4u) {
            char path[64];
            snprintf(path, sizeof path, "build/molecule_%u.wav", seed);
            save_wav(path, wav, FS * SECS);
        }
        if (seed <= 12u)
            printf("  seed %2u: %-9s %u atoms  rms 1s %.3f  4s %.3f  9s %.3f  rails %u\n", seed,
                   fam_name(mol_d.gen.patch.family), mol_d.shape.n - 1u, rms[1], rms[4], rms[9], clip);
    }
    printf("sound: 40 molecules x %u s (%u struck); a block (%u samples) takes %.1f us on this host, %.2f %% of real "
           "time\n", SECS, struck, CTL, ns_sum / 40 / 1000, ns_sum / 40 / (1e9 * CTL / FS) * 100);
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
static void side_short(int b)
{
    b ? app_right(now_ms) : app_left(now_ms);
    run_ms(100);
    b ? app_right_up(now_ms) : app_left_up(now_ms);
}
static void side_long(int b)
{
    b ? app_right(now_ms) : app_left(now_ms);
    run_ms(900);
    b ? app_right_up(now_ms) : app_left_up(now_ms);
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
    {
        mol_shape_t before = mol_d.shape;
        side_short(0);                                  /* the left button: rewired, morphing (no fade: 0.60) */
        {
            uint32_t q;
            for (q = 0; q < 12u; q++) {
                run_ms(32);
                CHECK(ATOMIC_GET(mol_phase) == MP_RUN && mol_fade == Q15, "the left button faded the sound");
            }
        }
        CHECK(app.ov == OV_TEXT, "the left button showed nothing");
        CHECK(!strcmp(app.ov_text, "NO REWIRE") || memcmp(&before, &mol_d.shape, sizeof before),
              "the left button said REWIRE and changed nothing");
        CHECK(ATOMIC_GET(mol_phase) == MP_RUN, "not sounding after a rewire");
        shot("06_left_rewire");
    }
    side_short(1);
    run_ms(100);
    CHECK(app.ov == OV_TEXT && !strcmp(app.ov_text, "FX"), "the right button did not draw the values");
    shot("07_right_fx");
    {
        gen_lfo_t before = mol_d.gen.lfo[0];
        side_long(0);                                   /* the left button held: the LFOs anew */
        CHECK(app.ov == OV_TEXT && !strcmp(app.ov_text, "LFO"), "the left button held did not say LFO");
        CHECK(memcmp(&before, &mol_d.gen.lfo[0], sizeof before), "the left button held left the generator's LFOs");
        shot("07b_left_held_lfo");
    }
    app_left(now_ms);                                   /* both side buttons: neither acts */
    app_right(now_ms);
    run_ms(1000);
    app_left_up(now_ms);
    app_right_up(now_ms);
    run_ms(50);
    CHECK(!mot.on, "both side buttons held started MOTION");
    {
        uint32_t before = mol_d.seed;
        press_short();                                  /* a short press: a new molecule */
        run_ms(1500);
        CHECK(mol_d.seed != before, "a short press made no new molecule");
        shot("08_another_molecule");
    }
    press_long();                                       /* held: the menu, which stays after the release */
    CHECK(app.screen == SC_MENU && app.item == MI_SETTINGS, "a long press did not open the menu at SETTINGS");
    shot("09_menu_settings");
    press_short();                                      /* SETTINGS: MOTION RATE, 20 a minute to begin with */
    CHECK(app.screen == SC_SETTINGS && motion_rate() == 20u, "SETTINGS did not show MOTION RATE 20");
    shot("09a_settings_20");
    app_turn(1, now_ms);
    app_turn(1, now_ms);
    run_ms(50);
    CHECK(motion_rate() == 45u, "MOTION RATE %u after two turns up", motion_rate());
    app_turn(20, now_ms);
    run_ms(50);
    CHECK(motion_rate() == 120u, "MOTION RATE past its top");
    shot("09a2_settings_120");
    app_turn(-20, now_ms);
    run_ms(50);
    CHECK(motion_rate() == 6u, "MOTION RATE past its bottom");
    shot("09a3_settings_6");
    app_turn(4, now_ms);
    press_short();                                      /* the next page: MIX, 50 % to begin with */
    CHECK(app.screen == SC_SETTINGS && app.page == SET_MIX && mol_mix() == 50u, "the second page is not MIX at 50");
    shot("09a4_settings_mix");
    app_turn(-1, now_ms);
    app_turn(-1, now_ms);
    run_ms(50);
    CHECK(mol_mix() == 40u, "MIX %u after two turns down", mol_mix());
    app_turn(-20, now_ms);
    run_ms(50);
    CHECK(mol_mix() == 0u, "MIX below 0");
    shot("09a5_settings_mix_0");
    app_turn(8, now_ms);
    press_short();                                      /* the next page: FX MORPH, 0 s to begin with */
    CHECK(app.screen == SC_SETTINGS && app.page == SET_MORPH && mol_d.fx_morph == 0u, "the third page is not FX MORPH at 0");
    shot("09a6_settings_morph_0");
    app_turn(40, now_ms);
    run_ms(50);
    CHECK(mol_d.fx_morph == 30u, "FX MORPH past 30 s");
    shot("09a7_settings_morph_30");
    app_turn(-25, now_ms);
    press_short();                                      /* back to the molecule, all three kept */
    CHECK(app.screen == SC_MOL && mol_d.motion_rate == 30u && mol_mix() == 40u && mol_d.fx_morph == 5u,
          "SETTINGS did not keep 30 a minute, 40 %%, 5 s");
    {                                                   /* the right button now glides there over 5 s */
        mol_shape_t prev;
        uint32_t t, i, k, mid = 0;
        side_short(1);
        CHECK(mol_fxg.on, "FX MORPH 5 s: the right button did not glide");
        for (t = 0; t < 160u; t++) {                    /* 5.1 s, frame by frame */
            prev = mol_d.shape;
            run_ms(32);
            for (i = 1; i < mol_d.shape.n; i++)
                for (k = 0; k < 4u; k++) {
                    int32_t d = absi(prev.node[i].p[k] - mol_d.shape.node[i].p[k]);
                    uint32_t ok = fx_heavy(mol_d.shape.node[i].kind) ? 0xFu : FX_LFO_OK[mol_d.shape.node[i].kind];
                    if (ok >> k & 1u)
                        CHECK(d <= 2, "FX MORPH: atom %u value %u jumped %d in a frame", i, k, d);
                    if (t == 78u && mol_fxg.from[i][k] != mol_fxg.to[i][k] && (ok >> k & 1u) &&
                        mol_d.shape.node[i].p[k] != mol_fxg.from[i][k] && mol_d.shape.node[i].p[k] != mol_fxg.to[i][k])
                        mid = 1;
                }
        }
        CHECK(mid, "FX MORPH: halfway, no value was on its way");
        CHECK(!mol_fxg.on, "FX MORPH: still gliding after 5 s");
        for (i = 1; i < mol_d.shape.n; i++)
            CHECK(!memcmp(mol_d.shape.node[i].p, mol_fxg.to[i], sizeof mol_fxg.to[i]), "FX MORPH: atom %u not there", i);
        CHECK(ATOMIC_GET(mol_phase) == MP_RUN, "FX MORPH faded the sound");
    }
    {                                                   /* MOTION: the right button held */
        mol_shape_t before = mol_d.shape;
        uint32_t t, ev0 = mot.events, moved = 0, changed = 0, shapes0 = mol_shapes;
        side_long(1);
        CHECK(mot.on && !strcmp(app.ov_text, "MOTION"), "the right button held did not start MOTION");
        for (t = 0; t < 625u; t++) {                    /* 20 s at 30 a minute, frame by frame: nothing leaps */
            mol_shape_t prev = mol_d.shape;
            uint32_t i, j, sh = mol_shapes;
            run_ms(32);
            CHECK(ATOMIC_GET(mol_phase) == MP_RUN && mol_fade == Q15, "MOTION faded the sound (frame %u)", t);
            if (sh == mol_shapes && prev.n == mol_d.shape.n)   /* the values glide: a few units a frame at most */
                for (i = 1; i < prev.n; i++)
                    for (j = 0; j < 4u; j++)
                        if (prev.node[i].kind == mol_d.shape.node[i].kind)
                            CHECK(absi(prev.node[i].p[j] - mol_d.shape.node[i].p[j]) <= 3,
                                  "MOTION: atom %u value %u jumped %d -> %d", i, j, prev.node[i].p[j],
                                  mol_d.shape.node[i].p[j]);
            for (i = 0; i < mol_d.shape.n; i++) {       /* each disc within 4 px of where a disc was (or swelling) */
                int32_t best = 1 << 30;
                for (j = 0; j < prev.n; j++) {
                    int32_t ex = mol_d.shape.node[i].x - prev.node[j].x, ey = mol_d.shape.node[i].y - prev.node[j].y;
                    best = ex * ex + ey * ey < best ? ex * ex + ey * ey : best;
                }
                if (motion_size(i) == 256)
                    CHECK(best <= 16, "MOTION: disc %u leapt %d px in a frame", i, (int)isqrt32((uint32_t)best));
            }
            if (t % 16u == 0)
                check_shape(&mol_d.shape, t, "moving", 0);
            moved |= mol_d.shape.node[0].x != before.node[0].x || mol_d.shape.node[0].y != before.node[0].y;
            if (t == 120u)
                shot("09b_motion");
#ifdef MOTION_FRAMES
            if (t % 10u == 0) {
                char nm[32];
                snprintf(nm, sizeof nm, "mf_%03u", t);
                shot(nm);
            }
#endif
        }
        changed = mol_shapes - shapes0;
        CHECK(moved, "MOTION moved nothing");
        CHECK(mot.events - ev0 >= 7u && mot.events - ev0 <= 11u, "MOTION: %u events in 20 s at 30 a minute",
              mot.events - ev0);
        CHECK(changed >= 1u, "MOTION changed no shape in 20 s");
        motion_rate_step(10);                           /* 120 a minute, two minutes: the search runs on */
        for (t = 0; t < 240u; t++) {
            uint32_t q;
            for (q = 0; q < 15u; q++) {
                run_ms(32);
                CHECK(ATOMIC_GET(mol_phase) == MP_RUN && mol_fade == Q15, "MOTION at 120 faded the sound");
            }
            check_shape(&mol_d.shape, t, "moving fast", 0);
        }
        CHECK(mot.events - ev0 >= 180u, "MOTION at 120 a minute: %u events", mot.events - ev0);   /* (a prune's fade
                                                      * holds the next event back about a second) */
        run_ms(400);
        CHECK(ATOMIC_GET(mol_phase) == MP_RUN || mol_pending, "MOTION left the molecule silent");
        printf("motion: %u events, %u shapes, %u atoms now\n", mot.events - ev0, mol_shapes - shapes0,
               mol_d.shape.n - 1u);
        shot("09b2_motion_later");
        motion_rate_step(-10);
        side_long(1);
        CHECK(!mot.on && !strcmp(app.ov_text, "STILL"), "the right button held did not stop MOTION");
        before = mol_d.shape;
        run_ms(3000);
        CHECK(!memcmp(&before, &mol_d.shape, sizeof before), "stopped, MOTION still moved the molecule");
    }
    press_long();
    CHECK(app.item == MI_SETTINGS, "the menu did not open on SETTINGS, the item last gone into");
    app_turn(1, now_ms);
    run_ms(50);
    CHECK(app.item == MI_PATCHES, "turning did not reach PATCHES");
    shot("09c_menu_patches");
    press_short();                                      /* PATCHES: the slots, none filled yet */
    CHECK(app.screen == SC_SLOTS && app.slot == 0 && map_is(app.used, -1), "PATCHES did not show empty slots");
    shot("09d_slots_empty");
    press_short();                                      /* a short press on an empty slot: EMPTY, stays */
    CHECK(app.screen == SC_SLOTS && app.ov == OV_TEXT && !strcmp(app.ov_text, "EMPTY 0"), "an empty slot said %s",
          app.ov_text);
    shot("09d2_slot_empty_said");
    app_turn(5, now_ms);
    run_ms(50);
    CHECK(app.slot == 5, "slot %d after five turns", app.slot);
    press_long();                                       /* held on slot 5: saved */
    CHECK(app.screen == SC_MOL && !strcmp(app.ov_text, "SAVED 5"), "saving to slot 5 said %s", app.ov_text);
    CHECK(patch_used(5) && !memcmp(&patch_buf.d, &mol_d, sizeof mol_d), "slot 5 does not hold the molecule");
    shot("09e_saved");
    {
        static mol_design_t saved;
        memcpy(&saved, &mol_d, sizeof saved);
        press_short();                                  /* another molecule, then slot 6 loaded: the saved one back */
        run_ms(1500);
        CHECK(memcmp(&saved, &mol_d, sizeof saved), "a short press did not change the molecule");
        press_long();                                   /* the menu opens on PATCHES, the slots on slot 5 */
        CHECK(app.item == MI_PATCHES, "the menu did not open on PATCHES, the item last gone into");
        press_short();
        CHECK(map_is(app.used, 5), "the slots did not read back as slot 5 alone");
        CHECK(app.slot == 5, "the slots opened on %d, not on 5, the slot last chosen", app.slot);
        run_ms(50);
        shot("09f_slots_one_filled");
        press_short();
        run_ms(500);
        CHECK(app.screen == SC_MOL && !strcmp(app.ov_text, "LOADED 5"), "loading slot 5 said %s", app.ov_text);
        CHECK(!memcmp(&saved, &mol_d, sizeof saved) && ATOMIC_GET(mol_phase) == MP_RUN,
              "slot 5 did not bring the saved molecule back, sounding");
        shot("09f2_loaded");
    }
    press_long();
    press_short();
    app_turn(37, now_ms);
    run_ms(50);
    CHECK(app.slot == 42 && app.top == 8, "slot %d (row %d first) after 37 turns", app.slot, app.top);
    shot("09h_slots_42");
    app_turn(200, now_ms);
    run_ms(50);
    CHECK(app.slot == (int32_t)PATCH_SLOTS - 1 && app.slot == 99, "turning past the last slot");
    shot("09i_slots_99");
    app_right(now_ms);                                  /* a side button: back, nothing saved */
    run_ms(50);
    {
        static uint8_t m[PATCH_MAP_BYTES];
        patch_used_map(m);
        CHECK(app.screen == SC_MOL && map_is(m, 5), "a side button in the slots saved or stayed");
    }
    {                                                   /* an older record, shorter, still loads: the rest zero */
        static uint8_t keep[PATCH_REC_MAX];
        patch_rec_t *rec = (patch_rec_t *)(pf_mem + patch_off(5));
        memcpy(keep, rec, sizeof keep);
        rec->len = (uint16_t)(sizeof rec->d - 8u);
        rec->check = patch_check(&rec->d, rec->len);
        CHECK(patch_used(5), "a shorter record read as empty");
        memcpy(rec, keep, sizeof keep);
    }
    press_long();
    app_turn(5, now_ms);
    run_ms(50);
    CHECK(app.item == MI_POWER, "turning did not stop at POWER");
    shot("09g_menu_power");
    press_short();                                      /* POWER: the confirmation, NO first */
    CHECK(app.screen == SC_CONFIRM && !app.yes, "no confirmation, or YES chosen first");
    shot("10_power_off_no");
    press_short();                                      /* NO: back to the molecule, the sound untouched */
    CHECK(app.screen == SC_MOL && ATOMIC_GET(mol_phase) == MP_RUN, "NO did not return to the sounding molecule");
    press_long();
    press_long();                                       /* held in the menu: back */
    CHECK(app.screen == SC_MOL, "a long press in the menu did not go back");
    press_long();
    app_turn(2, now_ms);
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

/* the patches' store: 100 slots in 64 KB, the twelve of builds up to 74 where they were, the neighbours kept through
 * a save, and a save cut short by the power at any step finished or undone whole at the next start */
static void test_patches(void)
{
    static mol_design_t keep;
    static uint8_t m[PATCH_MAP_BYTES];
    uint32_t i, cut, cuts = 0;
    memcpy(&keep, &mol_d, sizeof keep);
    memset(pf_mem, 0xFF, sizeof pf_mem);
    for (i = 0; i < 12u; i++) {                         /* builds up to 74: slot i at sector i's start */
        patch_rec_t *rec = (patch_rec_t *)(pf_mem + i * 4096u);
        memset(rec, 0, sizeof *rec);
        rec->magic = PATCH_MAGIC;
        rec->ver = 2;
        rec->len = (uint16_t)sizeof rec->d;
        rec->d = keep;
        rec->d.seed = 1000u + i;
        rec->check = patch_check(&rec->d, rec->len);
    }
    for (i = 0; i < 12u; i++)
        CHECK(patch_used(i) && patch_buf.d.seed == 1000u + i, "an older build's slot %u lost", i);
    patch_used_map(m);
    for (i = 12; i < PATCH_SLOTS; i++)
        CHECK(!(m[i >> 3] >> (i & 7u) & 1u), "slot %u not empty", i);
    for (i = 0; i < PATCH_SLOTS; i++) {                 /* all 100 written, each read back as itself */
        mol_d.seed = 5000u + i;
        CHECK(patch_save(i), "slot %u not saved", i);
    }
    for (i = 0; i < PATCH_SLOTS; i++)
        CHECK(patch_used(i) && patch_buf.d.seed == 5000u + i, "slot %u lost after all 100 saved", i);
    CHECK(!patch_save(PATCH_SLOTS), "slot 100 saved");
    for (cut = 0; cut < 400u; cut++) {                  /* the power cut after each step of a save to slot 37 */
        static uint8_t before[sizeof pf_mem];
        int done;
        memcpy(before, pf_mem, sizeof pf_mem);
        mol_d.seed = 9000u + cut;
        pf_ops_left = (int32_t)cut;
        done = patch_save(37);
        if (pf_ops_left >= 0 && done)                   /* (the save finished before the cut: all steps tried) */
            break;
        pf_ops_left = -1;
        patch_recover();
        cuts++;
        for (i = 0; i < PATCH_SLOTS; i++)
            if (i != 37u)
                CHECK(patch_used(i) && patch_buf.d.seed == 5000u + i, "cut after %u steps: slot %u lost", cut, i);
        CHECK(patch_used(37) && (patch_buf.d.seed == 5000u + 37u || patch_buf.d.seed == 9000u + cut),
              "cut after %u steps: slot 37 neither the old patch nor the new", cut);
        if (patch_used(37) && patch_buf.d.seed != 5000u + 37u) {   /* (the new one kept: the old one back for the next) */
            mol_d.seed = 5000u + 37u;
            patch_save(37);
        }
    }
    pf_ops_left = -1;
    CHECK(cuts > 50u, "a save took only %u steps", cuts);
    printf("patches: 100 slots in 64 KB (%u B a record), the older twelve kept, a save cut short at any of %u steps "
           "kept every other slot\n", (unsigned)sizeof(patch_rec_t), cuts);
    memset(pf_mem, 0xFF, sizeof pf_mem);
    memcpy(&mol_d, &keep, sizeof keep);
}

static void test_font(void)
{
    gfx_clear(C_BLACK);
    gfx_text(2, 4, "ABCDEFGHIJKLM", 2, C_WHITE);
    gfx_text(2, 22, "NOPQRSTUVWXYZ", 2, C_WHITE);
    gfx_text(2, 40, "0123456789", 2, C_WHITE);
    gfx_text(2, 58, "? % - + . :", 2, C_WHITE);
    gfx_power(SCR_W / 2, 100, 20, C_WHITE);
    save_png("build/screens/00_font.png");
}

int main(void)
{
    memset(pf_mem, 0xFF, sizeof pf_mem);
    setvbuf(stdout, NULL, _IONBF, 0);   /* (each line as it happens: a hang shows where) */
    test_shapes();
    test_atoms();
    test_families();
    test_sound();
    test_flows();
    test_patches();
    test_font();
    printf("memory: design %zu B, generator %zu B, atoms %zu B, atoms' arena %zu B, delay %zu B, reverb %zu B, "
           "frame %zu B, a patch %zu B\n", sizeof mol_d, sizeof mol_gs, sizeof mol_fs, sizeof fx_arena, sizeof dl_a, sizeof rv_mem,
           sizeof gfx_fb, sizeof(patch_rec_t));
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
