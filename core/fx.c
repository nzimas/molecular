/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR: the atoms' effects. Each atom has four values, 0 .. 100 (mol_node_t.p), read by the audio every block:
 *
 *   FILTER  cutoff (40 Hz .. 12 kHz), resonance, type (low / band / high-pass), sweep depth (a slow sweep, ~0.03 Hz, up to 1.5 octaves)
 *   FOLD    drive (1 .. 8 times), symmetry, mix
 *   RING    carrier frequency (20 Hz .. 2 kHz), mix
 *   CRUSH   bit reduction (16 .. 3 bits), sample-rate reduction (hold 1 .. 32 samples), mix
 *   CHORUS  rate (0.1 .. 3 Hz), depth (0.5 .. 15 ms), mix, feedback
 *   DELAY   time, feedback, damping, mix   (Tonitor's master delay: vendor/tonitor/delay.c)
 *   REVERB  size, decay, damping, mix      (Tonitor's plate: vendor/tonitor/reverb.c)
 *
 * DELAY and REVERB are the heavy atoms: one of each at most (their lines are single, 44 and 43 KB) and only at the
 * molecule's edge, where they take a mono input and add a stereo output. The others are mono in, mono out. */

#define FX_CH_N 2048u            /* the chorus's line: 46 ms */
#define FX_CH_MAX 2u             /* two choruses at most (their lines are in the pool) */
static int16_t fx_line[FX_CH_MAX][FX_CH_N] POOL;

static const char *const AT_SYM[AT_N] = {"FM", "FL", "FO", "RG", "CR", "CH", "DL", "RV"};
static const char *const AT_NAME[AT_N] = {"FM", "FILTER", "FOLD", "RING", "CRUSH", "CHORUS", "DELAY", "REVERB"};
AINL int fx_heavy(uint32_t kind) { return kind == AT_DELAY || kind == AT_REVERB; }

typedef struct {
    svf_t svf;
    svf_c c;
    uint32_t ph;                 /* FILTER's sweep, RING's carrier, CHORUS's LFO */
    int32_t dcx, dcy;            /* FOLD's DC blocker */
    int32_t hold, hold_n;        /* CRUSH's held sample and the samples left */
    uint32_t w;                  /* CHORUS's write index */
    uint8_t line;                /* CHORUS's line */
} fx_state_t;

/* each kind's values drawn anew: the ranges keep every atom musical (no runaway feedback, no ear-piercing rings) */
static void fx_draw_values(mol_node_t *a, rnd_t *r)
{
    static const int8_t RANGE[AT_N][4][2] = {
        {{0, 0}, {0, 0}, {0, 0}, {0, 0}},
        {{20, 85}, {0, 80}, {0, 100}, {0, 60}},       /* FILTER */
        {{10, 80}, {20, 80}, {40, 100}, {0, 0}},      /* FOLD */
        {{5, 95}, {30, 90}, {0, 0}, {0, 0}},          /* RING */
        {{20, 85}, {0, 70}, {40, 100}, {0, 0}},       /* CRUSH */
        {{5, 60}, {20, 90}, {30, 70}, {0, 60}},       /* CHORUS */
        {{20, 90}, {20, 75}, {20, 80}, {30, 70}},     /* DELAY */
        {{40, 100}, {40, 95}, {20, 70}, {40, 85}},    /* REVERB */
    };
    uint32_t k;
    for (k = 0; k < 4u; k++)
        a->p[k] = (int16_t)rnd_in(r, RANGE[a->kind][k][0], RANGE[a->kind][k][1]);
}

/* the heavy atoms' values passed on (main loop: their own parameter functions cook them) */
static void fx_apply_heavy(const mol_node_t *a)
{
    if (a->kind == AT_DELAY)
        dl_param(a->p[0], a->p[1], a->p[2], a->p[3]);
    else if (a->kind == AT_REVERB)
        rv_param(a->p[0], a->p[1], a->p[2], a->p[3]);
}

static void fx_start(fx_state_t *s, const mol_node_t *a, uint32_t chorus_ix)
{
    memset(s, 0, sizeof *s);
    if (a->kind == AT_CHORUS) {
        s->line = (uint8_t)chorus_ix;
        memset(fx_line[chorus_ix], 0, sizeof fx_line[0]);
    }
}

AINL int32_t fx_mix(int32_t dry, int32_t wet, int32_t pct) { return dry + (((wet - dry) * pct * 328) >> 15); }

/* one block of an inner atom, in -> out (CTL samples, Q15) */
static FAST void fx_block(fx_state_t *s, const mol_node_t *a, const int32_t *in, int32_t *out)
{
    const int16_t *p = a->p;
    uint32_t i;
    switch (a->kind) {
    case AT_FILTER: {
        int32_t oct = p[0] * 5374, hz;                         /* 8.2 octaves above 40 Hz, Q16 */
        int type = p[2] < 34 ? 1 : p[2] < 67 ? 2 : 3;
        s->ph += 3000u * CTL;                                  /* the sweep: ~0.03 Hz */
        oct += ((sin_ph(s->ph) * 983) >> 15) * p[3];           /* up to +-1.5 octaves (Q16) */
        hz = (int32_t)((40u * exp2_q16(clampi(oct, 0, 540000))) >> 16);
        svf_coef(&s->c, hz, 2458 + p[1] * 221);                /* q 0.6 .. 6 */
        for (i = 0; i < CTL; i++)
            out[i] = svf_run(&s->svf, &s->c, in[i], type);
        break;
    }
    case AT_FOLD: {
        int32_t drive = 256 + p[0] * 1792 / 100, bias = (p[1] - 50) * 327;
        for (i = 0; i < CTL; i++) {
            int32_t x = ((in[i] * drive) >> 8) + bias, n = 16, y;
            while ((x > Q15 || x < -Q15) && n--)               /* reflected back inside +-1, as often as it takes */
                x = x > Q15 ? 2 * Q15 - x : -2 * Q15 - x;
            y = x - s->dcx + s->dcy - (s->dcy >> 9);           /* a DC blocker: the symmetry leaves an offset */
            s->dcx = x;
            s->dcy = y;
            out[i] = fx_mix(in[i], y, p[2]);
        }
        break;
    }
    case AT_RING: {
        int32_t hz = (int32_t)((20u * exp2_q16(p[0] * 4354)) >> 16);   /* 20 Hz x 2^(0 .. 6.64) */
        uint32_t inc = hz8_inc(hz << 8);
        for (i = 0; i < CTL; i++) {
            s->ph += inc;
            out[i] = fx_mix(in[i], mulq15(in[i], sin_ph(s->ph)), p[1]);
        }
        break;
    }
    case AT_CRUSH: {
        int32_t drop = p[0] * 13 / 100, hold = 1 + p[1] * 31 / 100;
        int32_t mask = ~((1 << drop) - 1);
        for (i = 0; i < CTL; i++) {
            if (--s->hold_n <= 0) {
                s->hold = clampi(in[i], -Q15, Q15 - 1) & mask;
                s->hold_n = hold;
            }
            out[i] = fx_mix(in[i], s->hold, p[2]);
        }
        break;
    }
    case AT_CHORUS: {
        int16_t *ln = fx_line[s->line];
        uint32_t inc = hz8_inc((10 + p[0] * 290 / 100) * 256 / 100);   /* 0.1 .. 3 Hz, as Hz Q8 */
        int32_t depth = 22 + p[1] * 640 / 100, base = depth + 44, fb = p[3] * 229;   /* samples; feedback Q15 <= 0.7 */
        for (i = 0; i < CTL; i++) {
            int32_t d8, y0, y1, y, fr;
            uint32_t ri;
            s->ph += inc;
            d8 = (base << 8) + ((sin_ph(s->ph) * depth) >> 7);  /* the delay, Q8 samples */
            ri = (s->w + FX_CH_N * 2u - (uint32_t)(d8 >> 8)) & (FX_CH_N - 1u);
            fr = d8 & 255;
            y0 = ln[ri];
            y1 = ln[(ri - 1u) & (FX_CH_N - 1u)];
            y = y0 + (((y1 - y0) * fr) >> 8);
            ln[s->w] = (int16_t)clampi(in[i] + mulq15(y, fb), -32767, 32767);
            s->w = (s->w + 1u) & (FX_CH_N - 1u);
            out[i] = fx_mix(in[i], y, p[2]);
        }
        break;
    }
    default:
        for (i = 0; i < CTL; i++)
            out[i] = in[i];
    }
}
