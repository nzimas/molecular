/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR: the generator's families. Tonitor's 27 (vendor/tonitor/fm.c) and sixteen more after Norilsk's
 * (github.com/nzimas/norilsk, the same author's: mol.c), recipes for the same six sine operators -- Norilsk's waves,
 * grains and bodies stood in for by operators (a body's modes as carriers on its ratios, a noise burst as an operator
 * fed back hard, a square as a modulator at twice the carrier).
 *
 * Held (the chord sounds for as long as the molecule lives):
 *   HUM      a machine room: two carriers nailed to the mains' region (46 .. 63 Hz, 100 .. 158 Hz) under a bell's
 *            inharmonic partials
 *   SWARM    many high carriers (harmonics 3 .. 11) a little apart, beating: a buzzing mass
 *   ORGAN    an organ in a ruin: six carriers on drawbar ratios
 *   MODES    a bar's, a plate's, a bell's or a hull's modes as carriers, each smeared into a band by one operator
 *            fed back to noise: metal in the wind
 *   STEAM    a closed pipe's odd partials, hissing: the same, the noise up
 *   SCRAPE   every operator on high bell ratios, chopped by a sample-and-hold on the level
 *   CORONA   high carriers on 2, 3, 4, 6, barely modulated, shimmering
 *   ASH      a dark drone an octave down under a sparse dusty crackle (the clock's bursts only)
 * Struck (Norilsk's "metal struck, never a drum", on the clock: gen.c):
 *   ANVIL    a hammer on an anvil: bright inharmonic partials on a bar's or a plate's modes    SCATTER
 *   STAMP    a press coming down: a low dark thud                                               SCATTER
 *   RIVET    a rivet gun: high, small, very short                                               SCATTER
 *   SHEET    a metal sheet: a hull's crowded modes, wobbling, longer                            SCATTER
 *   GEARS    quick ticks, a pattern of teeth, the pace wandering a little                      MACHINE
 *   PISTON   a slow thump and a hiss of steam between                                           MACHINE
 *   RATCHET  irregular clatter on a bar or a bell, bouncing                                     MACHINE
 *   FORGE    a drop hammer: slow heavy strikes, each bouncing, ringing long                     MACHINE
 * And Tonitor's harsher families (BRUIT, FRICTION, SHARDS, STATIC, GLITCH), drawn on its open path: the ambient path
 * the rest are drawn on softens everything.
 *
 * 0.64, from the FM literature (Truax 1977, Schottstaedt 1977, Chowning's bell in CCRMA's CLM, Plaits' ratios, a count
 * of 9,606 DX7 voices; notes/DIVERSITY_RESEARCH.md in the research tree):
 *   TRUAX    one set of partials, the energy moving through it: three pairs, each a member of one of Truax's normal
 *            forms (2:5 -> carriers 1, 1.5, 3.5, 4 over 2.5 ...), their envelopes crossing           held
 *   HAMMER   Schottstaedt's piano: modulators 1:1 and 4:1, stretched half a percent, the index spiking at the strike
 *   TAM      tam-tams and membranes: modulators near the carrier (x 1.15 .. 1.8, irrational), fed back hard
 *   CHIME    Chowning's tubular bell: three pairs, 2:5, 0.6:4.8, 0.22:0.83 (the last a hum under the fundamental)
 *   FORMANT  carriers fixed where formants are (300 Hz .. 3 kHz), the modulators following the chord: the three notes
 *            fused in one resonance                                                                  held
 *   GROWL    the loop algorithms (DX7 4 and 6: feedback through two or three operators) on inharmonic ratios: rasp held
 *   GLOW     against nature: the index rising as the sound dies (struck slowly)
 *   ALLURE   Schaeffer's allure: a timbral tremolo at 3 .. 12 Hz, on the modulators only (the brightness pulses)  held */

enum { MF_HUM = FF_N, MF_SWARM, MF_ORGAN, MF_MODES, MF_STEAM, MF_SCRAPE, MF_CORONA, MF_ASH,
       MF_ANVIL, MF_STAMP, MF_RIVET, MF_SHEET, MF_GEARS, MF_PISTON, MF_RATCHET, MF_FORGE,
       MF_TRUAX, MF_HAMMER, MF_TAM, MF_CHIME, MF_FORMANT, MF_GROWL, MF_GLOW, MF_ALLURE,   /* 0.64 */
       MF_END };
static const char *const MF_NAME[MF_END - FF_N] = {"HUM", "SWARM", "ORGAN", "MODES", "STEAM", "SCRAPE", "CORONA", "ASH",
                                                   "ANVIL", "STAMP", "RIVET", "SHEET", "GEARS", "PISTON", "RATCHET",
                                                   "FORGE", "TRUAX", "HAMMER", "TAM", "CHIME", "FORMANT", "GROWL",
                                                   "GLOW", "ALLURE"};
/* 0.64: the physical families (pm.c), after these */
enum { PF_HARP = MF_END, PF_WIRE, PF_SNARE, PF_PIPE, PF_SITAR, PF_GONG, PF_BOWL, PF_BAR, PF_BELL, PF_SKIN, PF_END };
static const char *const PF_NAME[PF_END - MF_END] = {"HARP", "WIRE", "SNARE", "PIPE", "SITAR", "GONG", "BOWL", "BAR",
                                                     "BELL", "SKIN"};
static const char *fam_name(uint32_t f)
{
    return f < FF_N ? FF_NAME[f] : f < MF_END ? MF_NAME[f - FF_N] : f < PF_END ? PF_NAME[f - MF_END] : "FM";
}
AINL int fam_is_pm(uint32_t f) { return f >= MF_END && f < PF_END; }
AINL int fam_is_struck(uint32_t f)
{
    return (f >= MF_ANVIL && f <= MF_FORGE) || f == MF_HAMMER || f == MF_TAM || f == MF_CHIME || f == MF_GLOW;
}

/* the draw: a family and the path it is drawn on (Tonitor's), by weight. Tonitor's ambient families as Molecular drew
 * them before (fm.c FF_W6's ambient row), its harsher ones on the open path, the new held ones, the struck ones */
typedef struct { uint8_t f, path, w; } fam_pick_t;
/* (0.64: the weights moved towards the families that reach the rarer regions of timbre: tests/lab.c over 1000
 * generators, tools/lab_weights.py, damped half way) */
static const fam_pick_t FAM_PICK[] = {
    {FF_METAL, PATH_AMBIENT, 3}, {FF_DRONE, PATH_AMBIENT, 3}, {FF_ITERATIF, PATH_AMBIENT, 1},
    {FF_SPECTRE, PATH_AMBIENT, 4}, {FF_ABYSS, PATH_AMBIENT, 3}, {FF_CHOIR, PATH_AMBIENT, 4},
    {FF_BOWED, PATH_AMBIENT, 3}, {FF_HOLLOW, PATH_AMBIENT, 3}, {FF_BLOOM, PATH_AMBIENT, 3}, {FF_TIDE, PATH_AMBIENT, 3},
    {FF_WIND, PATH_AMBIENT, 2}, {FF_PLUCK, PATH_AMBIENT, 2}, {FF_MALLET, PATH_AMBIENT, 3}, {FF_WARP, PATH_AMBIENT, 1},
    {FF_SHAPER, PATH_AMBIENT, 3}, {FF_LADDER, PATH_AMBIENT, 4}, {FF_REVERSE, PATH_AMBIENT, 4},
    {FF_CREEP, PATH_AMBIENT, 4}, {FF_CLANG, PATH_AMBIENT, 2},
    {FF_BRUIT, PATH_OPEN, 3}, {FF_FRICTION, PATH_OPEN, 3}, {FF_SHARDS, PATH_OPEN, 4}, {FF_STATIC, PATH_OPEN, 4},
    {FF_GLITCH, PATH_OPEN, 3},
    {MF_HUM, PATH_OPEN, 4}, {MF_SWARM, PATH_OPEN, 4}, {MF_ORGAN, PATH_OPEN, 4}, {MF_MODES, PATH_OPEN, 4},
    {MF_STEAM, PATH_OPEN, 4}, {MF_SCRAPE, PATH_OPEN, 4}, {MF_CORONA, PATH_OPEN, 4}, {MF_ASH, PATH_OPEN, 4},
    {MF_ANVIL, PATH_OPEN, 7}, {MF_STAMP, PATH_OPEN, 7}, {MF_RIVET, PATH_OPEN, 7}, {MF_SHEET, PATH_OPEN, 6},
    {MF_GEARS, PATH_OPEN, 7}, {MF_PISTON, PATH_OPEN, 7}, {MF_RATCHET, PATH_OPEN, 6}, {MF_FORGE, PATH_OPEN, 7},
    {MF_TRUAX, PATH_OPEN, 4}, {MF_HAMMER, PATH_OPEN, 6}, {MF_TAM, PATH_OPEN, 6}, {MF_CHIME, PATH_OPEN, 5},   /* 0.64 */
    {MF_FORMANT, PATH_OPEN, 4}, {MF_GROWL, PATH_OPEN, 4}, {MF_GLOW, PATH_OPEN, 6}, {MF_ALLURE, PATH_OPEN, 4},
    {PF_HARP, 0, 5}, {PF_WIRE, 0, 5}, {PF_SNARE, 0, 6}, {PF_PIPE, 0, 4}, {PF_SITAR, 0, 5},     /* 0.64: pm.c */
    {PF_GONG, 0, 4}, {PF_BOWL, 0, 4}, {PF_BAR, 0, 4}, {PF_BELL, 0, 4}, {PF_SKIN, 0, 5},
};
#define FAM_NPICK (sizeof FAM_PICK / sizeof FAM_PICK[0])

/* ------------------------------------------------------------ helpers --- */
/* an algorithm with lo .. hi carriers (Norilsk's mo_alg) */
static uint8_t fam_alg(rnd_t *r, int lo, int hi)
{
    uint8_t ok[32];
    uint32_t a, n = 0;
    for (a = 0; a < 32u; a++) {
        uint32_t c = fm6_carriers(a), k = 0;
        for (; c; c &= c - 1u)
            k++;
        if ((int)k >= lo && (int)k <= hi)
            ok[n++] = (uint8_t)a;
    }
    return ok[rnd_below(r, (int32_t)n)];
}
/* Norilsk's bell ratios, x 100 */
static const int16_t FAM_INHARM[12] = {100, 141, 173, 224, 283, 337, 397, 449, 527, 561, 679, 741};
/* a body's modes, x 1000 (Norilsk's BS_RATIO): bar, plate, bell, closed pipe, hull */
enum { BS_BAR, BS_PLATE, BS_BELL, BS_PIPE, BS_HULL };
static const int16_t FAM_MODES[5][6] = {
    {1000, 2756, 5404, 8933, 13344, 18640}, {1000, 1594, 2136, 2296, 2653, 2918},
    {500, 1000, 1183, 1506, 2000, 2514}, {1000, 3000, 5000, 7000, 9000, 11000},
    {1000, 1412, 1890, 2312, 2770, 3195},
};
/* the index falling up the keyboard, high modulators turned down (Norilsk's mo_care) */
static void fam_care(uint8_t *p, rnd_t *r)
{
    int n;
    for (n = 1; n <= 6; n++) {
        uint8_t *op = fm_op(p, n);
        op[FP_BP] = 39;
        if (fm_is_carrier(p, n))
            op[FP_RS] = (uint8_t)rnd_in(r, 1, 3);
        else {
            int32_t x = fm_op_ratio100(op);
            if (op[FP_MODE] == 0u && x >= 800)
                op[FP_OL] = (uint8_t)clampi(op[FP_OL] - (x - 800) / 50, 0, 99);
            op[FP_RC] = 0;
            op[FP_RD] = (uint8_t)rnd_in(r, 15, 35);
        }
    }
}
/* a held sound that keeps moving (Norilsk's mo_gen2): the index opening and closing over seconds, a slow LFO on the
 * level, no pitch envelope. (The carriers are held at full by the generator, which swells them in itself.) */
static void fam_held(uint8_t *p, rnd_t *r)
{
    static const uint8_t LW[3] = {3, 4, 2}, LFW[3] = {0, 4, 5};
    int n, i;
    for (i = 0; i < 4; i++) {
        p[FP_PR1 + i] = 99;
        p[FP_PL1 + i] = 50;
    }
    for (n = 1; n <= 6; n++) {
        uint8_t *op = fm_op(p, n);
        if (fm_is_carrier(p, n)) {
            op[FP_AMS] = 1;
            op[FP_DET] = (uint8_t)clampi(7 + (n & 1 ? 1 : -1) * rnd_in(r, 2, 5), 0, 14);
        } else {
            fm_env(op, rnd_in(r, 6, 35), rnd_in(r, 4, 22), rnd_in(r, 3, 18), rnd_in(r, 25, 50), rnd_in(r, 70, 99),
                   rnd_in(r, 35, 99), rnd_in(r, 45, 95), 0);
            op[FP_AMS] = (uint8_t)rnd_in(r, 2, 3);
            if (op[FP_MODE] == 0u && fm_op_ratio100(op) < 800)
                op[FP_OL] = (uint8_t)maxi(op[FP_OL], rnd_in(r, 52, 68));
        }
    }
    p[FP_LFW] = LFW[rnd_weighted(r, LW, 3)];
    p[FP_LFS] = (uint8_t)rnd_in(r, 4, 26);
    p[FP_LAMD] = (uint8_t)rnd_in(r, 30, 70);
    p[FP_LFD] = (uint8_t)rnd_in(r, 0, 70);
    p[FP_LPMD] = (uint8_t)mini(p[FP_LPMD], 2);
    p[FP_LPMS] = (uint8_t)mini(p[FP_LPMS], 2);
}
/* Norilsk's METAL: four to six carriers on bell ratios, the high ones quieter and shorter */
static void fam_metal(uint8_t *p, rnd_t *r)
{
    int n, base = rnd_below(r, 3), ol = rnd_in(r, 85, 99);
    p[FP_ALG] = fam_alg(r, 4, 6);
    for (n = 1; n <= 6; n++) {
        uint8_t *op = fm_op(p, n);
        if (fm_is_carrier(p, n)) {
            int32_t x = FAM_INHARM[clampi(base + rnd_below(r, 6), 0, 11)] * (rnd_chance(r, 30) ? 2 : 1), hi = x > 300;
            fm_ratio(op, x);
            op[FP_OL] = (uint8_t)clampi(ol - (x - 100) / 25 - rnd_below(r, 8), 40, 99);
            fm_env(op, 99, rnd_in(r, 25, 45) + hi * 15, rnd_in(r, 15, 35) + hi * 10, rnd_in(r, 35, 60), 99,
                   rnd_in(r, 60, 85), rnd_in(r, 0, 30), 0);
            op[FP_DET] = (uint8_t)rnd_in(r, 3, 11);
        } else {
            fm_ratio(op, rnd_chance(r, 50) ? 141 : rnd_in(r, 300, 700));
            op[FP_OL] = (uint8_t)rnd_in(r, 45, 70);
            fm_env(op, 99, rnd_in(r, 50, 80), 30, 60, 99, rnd_in(r, 0, 40), 0, 0);
        }
    }
    p[FP_FB] = (uint8_t)rnd_in(r, 0, 3);
}
/* carriers on a body's modes (x mul / 1000), the lowest loudest; the modulators one operator fed back to noise (or
 * a plain one) at a low index: each mode smeared into a band. ALG 24 (0-based 23): five carriers, the sixth fed back
 * into three of them */
static void fam_modes(uint8_t *p, rnd_t *r, int set, int32_t mul, int noise_lo, int noise_hi)
{
    int n, k = 0;
    p[FP_ALG] = 23;
    p[FP_FB] = 7;
    for (n = 1; n <= 6; n++) {
        uint8_t *op = fm_op(p, n);
        if (fm_is_carrier(p, n)) {
            int32_t x = (int32_t)FAM_MODES[set][k] * mul / 10000;    /* (x 100) */
            fm_ratio(op, clampi(x + rnd_in(r, -2, 2), 50, 3100));
            op[FP_OL] = (uint8_t)clampi(99 - k * 6 - rnd_below(r, 6), 50, 99);
            op[FP_DET] = (uint8_t)rnd_in(r, 4, 10);
            k++;
        } else {                                     /* the noise: high, fed back hard, a low index */
            if (rnd_chance(r, 60))
                fm_fixed(op, 3, rnd_in(r, 0, 50));    /* 1 .. 3 kHz */
            else
                fm_ratio(op, rnd_in(r, 700, 1500));
            op[FP_OL] = (uint8_t)rnd_in(r, noise_lo, noise_hi);
        }
    }
}

/* ------------------------------------------------------- held families --- */
static void fam_hum(uint8_t *p, rnd_t *r)
{
    int n, k = 0;
    fam_metal(p, r);
    for (n = 1; n <= 6; n++)
        if (fm_is_carrier(p, n)) {                   /* the first two carriers nailed to the mains' region */
            if (k == 0)
                fm_fixed(fm_op(p, n), 1, rnd_in(r, 66, 80));
            else if (k == 1)
                fm_fixed(fm_op(p, n), 2, rnd_in(r, 0, 20));
            k++;
        }
}
static void fam_swarm(uint8_t *p, rnd_t *r)
{
    static const int16_t SW[6] = {300, 400, 500, 700, 900, 1100};
    int n;
    p[FP_ALG] = fam_alg(r, 3, 4);
    for (n = 1; n <= 6; n++) {
        uint8_t *op = fm_op(p, n);
        if (fm_is_carrier(p, n)) {
            fm_ratio(op, SW[rnd_below(r, 6)]);
            op[FP_OL] = (uint8_t)rnd_in(r, 78, 95);
            op[FP_DET] = (uint8_t)(rnd_chance(r, 50) ? rnd_in(r, 0, 3) : rnd_in(r, 11, 14));   /* the extremes: beating */
        } else {
            fm_ratio(op, rnd_in(r, 100, 200));
            op[FP_OL] = (uint8_t)rnd_in(r, 50, 70);
        }
    }
    p[FP_FB] = (uint8_t)rnd_in(r, 2, 5);
}
static void fam_organ(uint8_t *p, rnd_t *r)
{
    static const int16_t DRAW[8] = {50, 100, 150, 200, 300, 400, 600, 800};   /* 16' 8' 5 1/3' 4' 2 2/3' 2' 1 1/3' 1' */
    int n;
    p[FP_ALG] = 31;
    for (n = 1; n <= 6; n++) {
        uint8_t *op = fm_op(p, n);
        fm_ratio(op, n == 1 ? 100 : DRAW[rnd_below(r, 8)]);
        op[FP_OL] = (uint8_t)(n == 1 ? 99 : rnd_in(r, 55, 92));
        op[FP_DET] = (uint8_t)rnd_in(r, 5, 9);
    }
    p[FP_FB] = (uint8_t)rnd_in(r, 0, 3);             /* (a little saw on the sixth drawbar) */
}
static void fam_scrape(uint8_t *p, rnd_t *r)
{
    int n;
    fam_metal(p, r);
    for (n = 1; n <= 6; n++) {
        uint8_t *op = fm_op(p, n);
        fm_ratio(op, FAM_INHARM[rnd_in(r, 4, 11)] * (rnd_chance(r, 30) ? 2 : 1));
        if (fm_is_carrier(p, n))
            op[FP_AMS] = 3;
    }
    p[FP_LFW] = (uint8_t)(rnd_chance(r, 70) ? 5 : 3);   /* chopped: a sample-and-hold or a square on the level */
    p[FP_LFS] = (uint8_t)rnd_in(r, 60, 90);
    p[FP_LAMD] = (uint8_t)rnd_in(r, 40, 80);
}
static void fam_corona(uint8_t *p, rnd_t *r)
{
    static const int16_t CO[4] = {200, 300, 400, 600};
    int n;
    p[FP_ALG] = fam_alg(r, 3, 4);
    for (n = 1; n <= 6; n++) {
        uint8_t *op = fm_op(p, n);
        if (fm_is_carrier(p, n)) {
            fm_ratio(op, CO[rnd_below(r, 4)]);
            op[FP_OL] = (uint8_t)rnd_in(r, 78, 95);
            op[FP_DET] = (uint8_t)rnd_in(r, 3, 12);
        } else {
            fm_ratio(op, rnd_in(r, 100, 300));
            op[FP_OL] = (uint8_t)rnd_in(r, 25, 50);
        }
    }
    p[FP_FB] = (uint8_t)rnd_in(r, 0, 2);
}

/* ----------------------------------------------------- struck families --- */
/* Norilsk's mo_ops7: lo .. hi carriers on bell ratios -- or, half the time, on the body's modes the family rings --
 * x 2 at BRIGHT %, struck (falling to nothing even held), the index dying first; then the clock */
static void fam_struck_ops(uint8_t *p, rnd_t *r, int lo, int hi, int bright, int r2, int r3, int idx, int set,
                           int32_t mul)
{
    int n, k = 0, modes = set >= 0 && rnd_chance(r, 50);
    p[FP_ALG] = fam_alg(r, lo, hi);
    for (n = 1; n <= 6; n++) {
        uint8_t *op = fm_op(p, n);
        if (fm_is_carrier(p, n)) {
            int32_t x = modes ? (int32_t)FAM_MODES[set][k % 6] * mul / 10000 : FAM_INHARM[rnd_below(r, 12)];
            x *= rnd_chance(r, bright) ? 2 : 1;
            fm_ratio(op, clampi(x, 50, 3100));
            op[FP_OL] = (uint8_t)clampi(rnd_in(r, 86, 99) - (x - 100) / 40, 50, 99);
            fm_env(op, 99, clampi(r2 + rnd_in(r, -8, 8) + (x > 300 ? 10 : 0), 1, 99), clampi(r3 + rnd_in(r, -8, 8), 1, 99),
                   rnd_in(r, 40, 70), 99, rnd_in(r, 20, 60), 0, 0);
            op[FP_DET] = (uint8_t)rnd_in(r, 3, 11);
            k++;
        } else {
            fm_ratio(op, rnd_chance(r, 50) ? FAM_INHARM[rnd_below(r, 12)] : rnd_in(r, 150, 900));
            op[FP_OL] = (uint8_t)clampi(idx + rnd_in(r, -10, 10), 0, 99);
            fm_env(op, 99, clampi(r2 + 20 + rnd_in(r, 0, 15), 1, 99), 60, 70, 99, rnd_in(r, 0, 30), 0, 0);
        }
    }
    p[FP_FB] = (uint8_t)rnd_in(r, 1, 5);
    p[FP_LAMD] = 0;
    p[FP_LPMD] = 0;
}
/* the low ones (STAMP, PISTON): the first operator at 0.5 or 1, the tube's odd partials from a modulator at twice it */
/* ------------------------------------------------------- 0.64 families --- */
static const int16_t FAM_IRR[16] = {71, 79, 100, 141, 157, 175, 225, 275, 283, 314, 346, 424, 471, 500, 566, 800};
                                                     /* Plaits' irrational ratios (x 100): sqrt2/2, pi/4 .. 4 sqrt2, 8 */
static void fam_truax(uint8_t *p, rnd_t *r)
{
    static const uint8_t NF[8][2] = {{2, 5}, {3, 7}, {4, 9}, {3, 8}, {2, 7}, {2, 9}, {1, 2}, {1, 3}};
    const uint8_t *f = NF[rnd_below(r, 8)];
    int32_t c = f[0], m = f[1], used = 0, k, n;
    p[FP_ALG] = 4;                                   /* (DX7 5: three pairs, 1-2, 3-4, 5-6) */
    for (k = 0; k < 3; k++) {
        int32_t N, x;
        do {                                         /* a member of the family: (c + N m) or |c - N m|, over c */
            N = rnd_below(r, 4);
            x = (rnd_chance(r, 30) && N ? absi(c - N * m) : c + N * m) * 100 / c;
        } while (((used >> N) & 1) && used != 15);
        used |= 1 << N;
        n = 1 + 2 * k;
        fm_ratio(fm_op(p, n), clampi(x, 50, 3100));
        fm_op(p, n)[FP_OL] = (uint8_t)rnd_in(r, k ? 78 : 90, 99);
        fm_op(p, n)[FP_DET] = (uint8_t)rnd_in(r, 5, 9);
        fm_ratio(fm_op(p, n + 1), m * 100 / c);
        fm_op(p, n + 1)[FP_OL] = (uint8_t)rnd_in(r, 60, 82);
    }
    p[FP_FB] = (uint8_t)rnd_in(r, 0, 2);
}
static void fam_hammer(uint8_t *p, rnd_t *r)
{
    int n;
    p[FP_ALG] = 15;                                  /* (DX7 16: one carrier, three branches of modulators) */
    for (n = 1; n <= 6; n++) {
        uint8_t *op = fm_op(p, n);
        switch (n) {
        case 1: fm_ratio(op, 100); op[FP_OL] = 99;
            fm_env(op, 99, rnd_in(r, 30, 45), rnd_in(r, 18, 32), rnd_in(r, 40, 60), 99, rnd_in(r, 75, 90), 0, 0); break;
        case 2: fm_ratio(op, 101); op[FP_OL] = (uint8_t)rnd_in(r, 72, 86); break;   /* 1:1, stretched */
        case 3: fm_ratio(op, 402); op[FP_OL] = (uint8_t)rnd_in(r, 60, 78); break;   /* 4:1, stretched */
        case 4: fm_ratio(op, rnd_in(r, 300, 900)); op[FP_OL] = (uint8_t)rnd_in(r, 30, 55); break;
        default: fm_ratio(op, n == 5 ? 100 : rnd_in(r, 500, 1200)); op[FP_OL] = (uint8_t)rnd_in(r, 35, 60); break;
        }
        if (n > 1)                                   /* the index spiking at the strike, then settling, then gone */
            fm_env(op, 99, rnd_in(r, 60, 75), rnd_in(r, 25, 40), 50, 99, rnd_in(r, 55, 75), 0, 0);
    }
    p[FP_FB] = (uint8_t)rnd_in(r, 3, 6);
}
static void fam_tam(uint8_t *p, rnd_t *r)
{
    int n;
    int32_t base = rnd_chance(r, 50) ? 50 : 100;
    p[FP_ALG] = rnd_chance(r, 60) ? 15 : 14;          /* (DX7 16 or 15) */
    for (n = 1; n <= 6; n++) {
        uint8_t *op = fm_op(p, n);
        if (fm_is_carrier(p, n)) {
            fm_ratio(op, base);
            op[FP_OL] = 99;
            fm_env(op, 99, rnd_in(r, 20, 40), rnd_in(r, 15, 30), 40, 99, rnd_in(r, 70, 90), 0, 0);
        } else {
            fm_ratio(op, base * rnd_in(r, 115, 180) / 100 + rnd_in(r, -3, 3));
            if (rnd_chance(r, 25))
                fm_ratio(op, FAM_IRR[rnd_below(r, 16)] * base / 100);
            op[FP_OL] = (uint8_t)rnd_in(r, 70, 92);
            fm_env(op, 99, rnd_in(r, 45, 70), rnd_in(r, 30, 50), 50, 99, rnd_in(r, 40, 70), 0, 0);
        }
    }
    p[FP_FB] = (uint8_t)rnd_in(r, 5, 7);
}
static void fam_chime(uint8_t *p, rnd_t *r)
{
    static const int16_t CR[3] = {800, 240, 88}, MR[3] = {2000, 1920, 332};   /* x 4: 2:5, 0.6:4.8, 0.22:0.83 */
    int k;
    p[FP_ALG] = 4;
    p[FP_TRNSP] = 0;                                 /* two octaves down: the ratios x 4 (the hum's 0.22 in reach) */
    for (k = 0; k < 3; k++) {
        uint8_t *c = fm_op(p, 1 + 2 * k), *m = fm_op(p, 2 + 2 * k);
        fm_ratio(c, CR[k] + rnd_in(r, -4, 4));
        fm_ratio(m, MR[k] + rnd_in(r, -6, 6));
        c[FP_OL] = (uint8_t)(k == 2 ? rnd_in(r, 75, 90) : 99);
        m[FP_OL] = (uint8_t)(k == 0 ? rnd_in(r, 74, 86) : k == 1 ? rnd_in(r, 66, 80) : rnd_in(r, 55, 70));
        fm_env(c, 99, rnd_in(r, 18, 30), rnd_in(r, 12, 22), 30, 99, rnd_in(r, 80, 92), 0, 0);
        fm_env(m, 99, rnd_in(r, 35, 55), rnd_in(r, 25, 40), 40, 99, rnd_in(r, 40, 60), 0, 0);
        m[FP_RD] = (uint8_t)rnd_in(r, 50, 90);       /* (the index falling steeply up the keyboard) */
        m[FP_RC] = 2;
    }
    p[FP_FB] = 0;
}
static void fam_formant(uint8_t *p, rnd_t *r)
{
    static const int16_t F1[2] = {48, 95}, F2[2] = {0, 38}, F3[2] = {34, 50};   /* fixed: 300 .. 900 Hz; 1 .. 2.4 kHz;
                                                                              * 2.2 .. 3.2 kHz */
    int k;
    p[FP_ALG] = 4;
    for (k = 0; k < 3; k++) {
        uint8_t *c = fm_op(p, 1 + 2 * k), *m = fm_op(p, 2 + 2 * k);
        if (k == 0)
            fm_fixed(c, 2, rnd_in(r, F1[0], F1[1]));
        else if (k == 1)
            fm_fixed(c, 3, rnd_in(r, F2[0], F2[1]));
        else
            fm_fixed(c, 3, rnd_in(r, F3[0], F3[1]));
        c[FP_OL] = (uint8_t)(99 - k * rnd_in(r, 4, 9));
        fm_ratio(m, rnd_chance(r, 70) ? 100 : 50);
        m[FP_OL] = (uint8_t)rnd_in(r, 55, 78);
    }
    p[FP_FB] = (uint8_t)rnd_in(r, 0, 3);
}
static void fam_growl(uint8_t *p, rnd_t *r)
{
    int n;
    p[FP_ALG] = rnd_chance(r, 50) ? 3 : 5;           /* (DX7 4 and 6: feedback through a chain of operators) */
    for (n = 1; n <= 6; n++) {
        uint8_t *op = fm_op(p, n);
        fm_ratio(op, FAM_IRR[rnd_below(r, 16)] * (rnd_chance(r, 30) ? 2 : 1));
        op[FP_OL] = (uint8_t)(fm_is_carrier(p, n) ? rnd_in(r, 85, 99) : rnd_in(r, 60, 85));
        op[FP_DET] = (uint8_t)rnd_in(r, 2, 12);
    }
    p[FP_FB] = (uint8_t)rnd_in(r, 4, 7);
}
static void fam_glow(uint8_t *p, rnd_t *r)
{
    int n;
    p[FP_ALG] = fam_alg(r, 1, 3);
    for (n = 1; n <= 6; n++) {
        uint8_t *op = fm_op(p, n);
        if (fm_is_carrier(p, n)) {
            fm_ratio(op, rnd_chance(r, 50) ? 100 : FAM_IRR[rnd_below(r, 16)]);
            op[FP_OL] = 99;
            fm_env(op, 99, rnd_in(r, 12, 25), rnd_in(r, 10, 20), 30, 99, rnd_in(r, 70, 90), 0, 0);   /* long decay */
        } else {                                     /* the index low at the strike, rising as it dies */
            fm_ratio(op, FAM_IRR[rnd_below(r, 16)]);
            op[FP_OL] = (uint8_t)rnd_in(r, 70, 90);
            fm_env(op, rnd_in(r, 50, 80), rnd_in(r, 10, 20), rnd_in(r, 8, 16), 40, rnd_in(r, 20, 40), rnd_in(r, 70, 90),
                   rnd_in(r, 85, 99), 0);
        }
    }
    p[FP_FB] = (uint8_t)rnd_in(r, 0, 4);
}
/* ALLURE: after fam_held -- the timbre pulsing at 3 .. 12 Hz, the level still (the modulators alone on the LFO) */
static void fam_allure_post(uint8_t *p, rnd_t *r)
{
    int n;
    for (n = 1; n <= 6; n++)
        fm_op(p, n)[FP_AMS] = (uint8_t)(fm_is_carrier(p, n) ? 0 : rnd_in(r, 2, 3));
    p[FP_LFW] = (uint8_t)(rnd_chance(r, 70) ? 1 : 5);   /* (a triangle, or random steps) */
    p[FP_LFS] = (uint8_t)rnd_in(r, 48, 72);
    p[FP_LAMD] = (uint8_t)rnd_in(r, 25, 60);
    p[FP_LFD] = 0;
}

static void fam_low(uint8_t *p, rnd_t *r)
{
    fm_ratio(fm_op(p, 1), rnd_chance(r, 50) ? 50 : 100);
    if (!fm_is_carrier(p, 2))
        fm_ratio(fm_op(p, 2), fm_op_ratio100(fm_op(p, 1)) * 2);
}
/* SCATTER for ANVIL STAMP RIVET SHEET (Norilsk's mo_scatter8), k their order */
static void fam_scatter(gen_clock_t *c, int k, rnd_t *r)
{
    static const uint8_t RT[4][2] = {{5, 14}, {2, 7}, {10, 30}, {3, 9}};
    static const uint8_t SP[4][2] = {{10, 16}, {8, 14}, {12, 20}, {10, 16}};
    static const uint8_t CL[4][2] = {{15, 35}, {8, 20}, {30, 55}, {10, 25}};
    static const uint8_t CM[4][2] = {{40, 110}, {70, 160}, {25, 70}, {60, 140}};
    c->mode = GK_SCATTER;
    c->rate = (uint8_t)rnd_in(r, RT[k][0], RT[k][1]);
    c->jit = (uint8_t)rnd_in(r, SP[k][0], SP[k][1]);
    c->drift = (uint8_t)rnd_in(r, 15, 45);
    c->soft = (uint8_t)rnd_in(r, 12, 40);
    c->flam = (uint8_t)rnd_in(r, CL[k][0], CL[k][1]);
    c->flam_ms = (uint8_t)rnd_in(r, CM[k][0], CM[k][1]);
    c->accn = (uint8_t)rnd_in(r, 1, k == 2 ? 7 : 4);
    c->pat = (uint8_t)rnd_in(r, 5, 20);
}
static void fam_struck(uint8_t *p, gen_clock_t *c, uint32_t f, rnd_t *r)
{
    c->gate = (uint8_t)rnd_in(r, 5, 15);             /* 20 .. 60 ms: the envelopes fall to nothing while held */
    c->spread = (uint8_t)rnd_in(r, 10, 60);
    switch (f) {
    case MF_ANVIL:
        fam_struck_ops(p, r, 2, 4, 50, 35, 30, 70, rnd_chance(r, 50) ? BS_BAR : BS_PLATE, rnd_in(r, 1000, 2000));
        fam_scatter(c, 0, r);
        c->click = (uint8_t)rnd_in(r, 2, 6), c->color = (uint8_t)rnd_in(r, 70, 100), c->level = (uint8_t)rnd_in(r, 50, 90);
        break;
    case MF_STAMP:
        fam_struck_ops(p, r, 1, 2, 0, 55, 45, 60, -1, 0);
        fam_low(p, r);
        fam_scatter(c, 1, r);
        c->click = (uint8_t)rnd_in(r, 10, 30), c->color = (uint8_t)rnd_in(r, 5, 40), c->level = (uint8_t)rnd_in(r, 70, 100);
        break;
    case MF_RIVET:
        fam_struck_ops(p, r, 2, 3, 80, 65, 55, 55, BS_PLATE, rnd_in(r, 1500, 2000));
        fam_scatter(c, 2, r);
        c->click = (uint8_t)rnd_in(r, 1, 3), c->color = (uint8_t)rnd_in(r, 80, 100), c->level = (uint8_t)rnd_in(r, 60, 100);
        break;
    case MF_SHEET: {
        int n;
        fam_struck_ops(p, r, 3, 5, 30, 30, 32, 50, BS_HULL, rnd_in(r, 750, 1500));
        p[FP_LFW] = 0;                               /* the sheet wobbling: a triangle on the level, 3 .. 7 Hz */
        p[FP_LFS] = (uint8_t)rnd_in(r, 45, 65);
        p[FP_LAMD] = (uint8_t)rnd_in(r, 20, 50);
        p[FP_LFD] = 0;
        for (n = 1; n <= 6; n++)
            if (fm_is_carrier(p, n))
                fm_op(p, n)[FP_AMS] = 2;
        fam_scatter(c, 3, r);
        c->click = (uint8_t)rnd_in(r, 5, 15), c->color = (uint8_t)rnd_in(r, 40, 80), c->level = (uint8_t)rnd_in(r, 50, 90);
        break;
    }
    case MF_GEARS:
        fam_struck_ops(p, r, 2, 3, 70, 75, 60, 50, rnd_chance(r, 60) ? BS_PLATE : BS_BAR, rnd_in(r, 1250, 2000));
        c->mode = GK_MACHINE;
        c->rate = (uint8_t)rnd_in(r, 24, 72), c->jit = (uint8_t)rnd_in(r, 2, 8), c->drift = (uint8_t)rnd_in(r, 3, 12);
        c->accn = (uint8_t)rnd_in(r, 3, 7), c->pat = (uint8_t)(rnd_chance(r, 50) ? 1 : 1 | rnd_below(r, 256));
        c->soft = (uint8_t)rnd_in(r, 30, 65);
        c->click = (uint8_t)rnd_in(r, 1, 4), c->color = (uint8_t)rnd_in(r, 60, 100), c->level = (uint8_t)rnd_in(r, 50, 90);
        break;
    case MF_PISTON:
        fam_struck_ops(p, r, 1, 2, 0, 55, 45, 55, -1, 0);
        fam_low(p, r);
        c->mode = GK_MACHINE;
        c->rate = (uint8_t)rnd_in(r, 3, 12), c->jit = (uint8_t)rnd_in(r, 2, 6), c->drift = (uint8_t)rnd_in(r, 2, 8);
        c->accn = 2, c->pat = (uint8_t)(rnd_chance(r, 50) ? 3 : 1), c->soft = (uint8_t)rnd_in(r, 50, 80);
        c->off = (uint8_t)rnd_in(r, 35, 80), c->off_ms = (uint8_t)rnd_in(r, 20, 60);
        c->click = (uint8_t)rnd_in(r, 15, 40), c->color = (uint8_t)rnd_in(r, 5, 35), c->level = (uint8_t)rnd_in(r, 70, 100);
        break;
    case MF_RATCHET:
        fam_struck_ops(p, r, 2, 4, 50, 65, 55, 60, rnd_chance(r, 50) ? BS_BAR : BS_BELL, rnd_in(r, 1000, 1750));
        c->mode = GK_MACHINE;
        c->rate = (uint8_t)rnd_in(r, 12, 48), c->jit = (uint8_t)rnd_in(r, 20, 45), c->drift = (uint8_t)rnd_in(r, 15, 35);
        c->accn = (uint8_t)rnd_in(r, 2, 5), c->pat = (uint8_t)(rnd_chance(r, 50) ? 1 : 1 | rnd_below(r, 256));
        c->soft = (uint8_t)rnd_in(r, 25, 60), c->flam = (uint8_t)rnd_in(r, 10, 35), c->flam_ms = (uint8_t)rnd_in(r, 15, 40);
        c->click = (uint8_t)rnd_in(r, 2, 8), c->color = (uint8_t)rnd_in(r, 50, 90), c->level = (uint8_t)rnd_in(r, 50, 90);
        break;
    default:                                         /* MF_FORGE */
        fam_struck_ops(p, r, 2, 4, 40, 30, 25, 70, rnd_chance(r, 50) ? BS_BAR : BS_PLATE, rnd_in(r, 750, 1500));
        c->mode = GK_MACHINE;
        c->rate = (uint8_t)rnd_in(r, 1, 5), c->jit = (uint8_t)rnd_in(r, 5, 25), c->drift = (uint8_t)rnd_in(r, 5, 20);
        c->accn = 2, c->pat = (uint8_t)(rnd_chance(r, 50) ? 3 : 1), c->soft = (uint8_t)rnd_in(r, 55, 85);
        c->flam = (uint8_t)rnd_in(r, 30, 60), c->flam_ms = (uint8_t)rnd_in(r, 60, 120);
        c->click = (uint8_t)rnd_in(r, 3, 8), c->color = (uint8_t)rnd_in(r, 60, 95), c->level = (uint8_t)rnd_in(r, 60, 95);
        break;
    }
}

/* ---------------------------------------------------------------- draw --- */
/* a patch of one of Molecular's own families (f >= FF_N) and its clock, from a seed (then fm_measure) */
static void fam_generate(fm_patch_t *pt, gen_clock_t *c, uint32_t f, uint32_t seed)
{
    rnd_t rr, *r = &rr;
    uint8_t *p = pt->p;
    rnd_seed(r, seed ^ 0x4E6F72u);
    fm_gen_cur = GEN_NOW;                            /* (fm_ratio as meant: generation 2 on) */
    fm_voice_defaults(p);
    switch (f) {
    case MF_HUM: fam_hum(p, r); break;
    case MF_SWARM: fam_swarm(p, r); break;
    case MF_ORGAN: fam_organ(p, r); break;
    case MF_MODES: {
        static const uint8_t SETS[4] = {BS_BAR, BS_PLATE, BS_BELL, BS_HULL};
        int set = SETS[rnd_below(r, 4)];
        fam_modes(p, r, set, set == BS_BELL ? 2000 : rnd_chance(r, 50) ? 500 : 1000, 40, 60);
        break;
    }
    case MF_STEAM: fam_modes(p, r, BS_PIPE, rnd_chance(r, 50) ? 500 : 1000, 55, 72); break;
    case MF_SCRAPE: fam_scrape(p, r); break;
    case MF_CORONA: fam_corona(p, r); break;
    case MF_TRUAX: fam_truax(p, r); break;
    case MF_FORMANT: fam_formant(p, r); break;
    case MF_GROWL: fam_growl(p, r); break;
    case MF_ALLURE:
        if (rnd_chance(r, 50))
            fam_truax(p, r);
        else
            fam_metal(p, r);
        break;
    case MF_HAMMER: case MF_TAM: case MF_CHIME: case MF_GLOW:
        if (f == MF_HAMMER)
            fam_hammer(p, r);
        else if (f == MF_TAM)
            fam_tam(p, r);
        else if (f == MF_CHIME)
            fam_chime(p, r);
        else
            fam_glow(p, r);
        c->gate = (uint8_t)rnd_in(r, 10, 40);
        c->spread = (uint8_t)rnd_in(r, 10, 60);
        if (f == MF_GLOW) {                          /* (slow: each strike given time to glow) */
            c->mode = GK_SCATTER;
            c->rate = (uint8_t)rnd_in(r, 1, 3), c->jit = (uint8_t)rnd_in(r, 5, 15), c->drift = (uint8_t)rnd_in(r, 10, 30);
            c->soft = (uint8_t)rnd_in(r, 40, 80), c->accn = 1, c->pat = (uint8_t)rnd_in(r, 5, 20);
            c->gate = (uint8_t)rnd_in(r, 120, 250);
        } else
            fam_scatter(c, rnd_below(r, 4), r);
        c->click = (uint8_t)rnd_in(r, 2, 10), c->color = (uint8_t)rnd_in(r, 30, 90), c->level = (uint8_t)rnd_in(r, 0, 40);
        break;
    case MF_ASH:
        fm_gen_drone(p, r);
        p[FP_TRNSP] = 12;                            /* an octave down */
        c->mode = GK_SCATTER;                        /* the crackle: tiny dark bursts, sparse, no pulse */
        c->crackle = 1;
        c->rate = (uint8_t)rnd_in(r, 12, 48), c->jit = (uint8_t)rnd_in(r, 15, 30), c->drift = (uint8_t)rnd_in(r, 20, 40);
        c->soft = (uint8_t)rnd_in(r, 10, 30), c->flam = (uint8_t)rnd_in(r, 10, 30), c->flam_ms = (uint8_t)rnd_in(r, 20, 60);
        c->accn = 3, c->pat = 10;
        c->click = (uint8_t)rnd_in(r, 1, 3), c->color = (uint8_t)rnd_in(r, 40, 70), c->level = (uint8_t)rnd_in(r, 15, 35);
        break;
    default:
        fam_struck(p, c, f, r);
        break;
    }
    if (!fam_is_struck(f))
        fam_held(p, r);
    if (f == MF_ALLURE)
        fam_allure_post(p, r);
    fam_care(p, r);
    fm_shape1(p, PATH_OPEN, r);                      /* (Tonitor's care: no screaming sidebands) */
    fm_name(p, fam_name(f));
    fm6_sanitize(p);
    pt->family = (uint8_t)f;
    pt->path = PATH_OPEN;
    pt->gen = GEN_NOW;
    pt->gain = 4096;
    pt->seed = seed;
}
