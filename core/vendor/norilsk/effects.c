/* SPDX-License-Identifier: GPL-3.0-only
 * Norilsk's effects, copied unchanged from its firmware/src/rack.c (commit bafd91c 2026-10-29) by tools/vendor_norilsk.py:
 * lines 41 .. 872 and 886 .. 1371 of that file (its master REVERB include and effect slots' constants left out).
 * Molecular's fx.c runs them as atoms. Do not edit: change Norilsk and copy again. */

enum { RM_FLT, RM_FLD, RM_MU, RM_SHF, RM_MDL, RM_STR, RM_VOW, RM_DLY, RM_TAP, RM_RVS, RM_SPR, RM_REV, RM_GLT,
       RM_CLD, RM_SCR, RM_RNG, RM_STV, RM_CHB, RM_BNK, RM_SMR, RM_DIF, RM_SLW, RM_XOR, RM_DST,
       RM_WST, RM_XMD, RM_CHS, NKIND };   /* (build 45: WAVESET, XMOD, CHAOS) */
static const char *const RM_NAME[NKIND] = {"FILTER", "FOLD", "MULAW", "SHIFT", "MODAL", "STRING", "VOWEL", "DELAY",
                                           "TAPE", "REVERSE", "SPRING", "REVERB", "GLITCH",
                                           "CLOUD", "SCREAM", "RING", "STARVE", "CHEB", "BANK",
                                           "SMEAR", "DIFFUSE", "SLEW", "XOR", "DUST", "WAVESET", "XMOD", "CHAOS"};
/* the values an effect comes back to (a draw moves them, as Vascular draws a processor's) */
static const uint8_t RM_HOME[NKIND][4] = {
    {20, 55, 40, 30}, {45, 45, 20, 70}, {45, 60, 50, 30}, {40, 30, 70, 50}, {40, 55, 40, 50}, {35, 60, 40, 60},
    {50, 50, 50, 30}, {65, 40, 40, 60}, {50, 30, 45, 40}, {40, 60, 30, 50}, {50, 40, 50, 50}, {65, 60, 40, 55},
    {50, 40, 35, 45},
    {55, 45, 20, 50}, {50, 60, 45, 50}, {45, 50, 20, 60}, {60, 30, 35, 55}, {40, 50, 60, 60}, {40, 60, 30, 50},
    {60, 40, 70, 55}, {60, 55, 70, 60}, {35, 40, 70, 60}, {40, 15, 45, 45}, {40, 40, 45, 70},
    {50, 30, 50, 30}, {50, 50, 30, 60}, {50, 40, 40, 60}};
/* .. and the bounds a draw keeps them in, so that every effect is heard: no band-pass at 15 kHz, no spring tuned where
 * the sound has nothing */
static const uint8_t RM_LO[NKIND][4] = {
    {0, 35, 0, 0}, {10, 10, 0, 30}, {0, 30, 20, 0}, {0, 0, 0, 0}, {0, 15, 0, 20}, {0, 25, 0, 30},
    {0, 0, 10, 0}, {20, 0, 0, 40}, {10, 0, 0, 0}, {0, 30, 0, 0}, {0, 0, 35, 10}, {20, 20, 0, 40}, {15, 0, 0, 0},
    {10, 10, 0, 10}, {0, 25, 10, 10}, {0, 0, 0, 20}, {20, 0, 0, 20}, {0, 0, 20, 20}, {0, 25, 0, 10},
    {0, 0, 30, 20}, {0, 0, 30, 20}, {0, 0, 30, 15}, {0, 0, 0, 20}, {0, 0, 15, 30},
    {0, 0, 10, 0}, {0, 0, 0, 20}, {0, 0, 0, 20}};
static const uint8_t RM_HI[NKIND][4] = {
    {100, 85, 80, 100}, {90, 90, 80, 100}, {100, 100, 100, 80}, {100, 90, 100, 100}, {100, 85, 100, 100}, {100, 95, 100, 100},
    {100, 100, 90, 80}, {100, 85, 100, 100}, {100, 100, 90, 85}, {100, 100, 80, 100}, {100, 100, 92, 60}, {100, 95, 100, 100},
    {100, 90, 85, 100},
    {100, 100, 100, 100}, {100, 92, 90, 100}, {100, 100, 90, 100}, {100, 70, 100, 100}, {100, 100, 100, 100}, {100, 95, 100, 100},
    {100, 100, 100, 100}, {100, 100, 100, 100}, {85, 100, 100, 100}, {100, 100, 90, 85}, {100, 85, 90, 100},
    {100, 100, 100, 100}, {100, 100, 100, 100}, {100, 100, 100, 100}};
static volatile int32_t rack_root = 48;        /* the scale's root, MIDI: what MODAL and STRING are tuned to */

/* AINL helpers the effects share; then the effects */
/* an instance's place in the effect slots' memory (build 32: its random seed -- the same on the device and on a
 * computer, run after run; the address itself before) */
static uint32_t fx_place(const void *st);
/* ------------------------------------------------------------- shared --- */
/* a constant-peak-gain resonator (Smith and Angell): zeros at DC and Nyquist, poles at f, radius r. Q28 coefficients */
typedef struct { int32_t a1, a2, g; } res_c;
typedef struct { int32_t y1, y2, x1, x2, e; } res_s;   /* (y: Q8 above the sample; e: the fraction the last dropped) */
/* f in Hz (Q8), Q in Q8: 1 - r = pi f / (Q FS) (to the second order), cos 2 theta = 1 - 2 sin^2 theta, theta = pi f / FS */
static void res_coef(res_c *c, int32_t hz8, int32_t q8)
{
    int64_t x, s, r, c2, x2;
    hz8 = clampi(hz8, 20 << 8, 16000 << 8);
    q8 = clampi(q8, 1 << 8, 400 << 8);
    x = ((int64_t)hz8 * 76491) >> 8;                      /* theta = pi f / FS, Q30 */
    x2 = (x * x) >> 30;
    s = x - ((((x2 * x) >> 30) * 178956971) >> 30) + ((((((x2 * x2) >> 30) * x) >> 30) * 8947849) >> 30);   /* sin theta:
                                                          * x - x^3 / 6 + x^5 / 120, Q30 */
    c2 = (1LL << 30) - 2 * ((s * s) >> 30);               /* cos 2 theta, Q30 */
    {
        uint32_t inv = 0xFFFFFFFFu / (uint32_t)q8;        /* 1 / Q, Q24 */
        int64_t om = (x * inv) >> 24;                     /* 1 - r = theta / Q, Q30 */
        r = (1LL << 30) - om + ((om * om) >> 31);
    }
    c->a1 = (int32_t)(((2 * r * c2) >> 30) >> 2);         /* 2 r cos, Q28 */
    c->a2 = (int32_t)(-((r * r) >> 30) >> 2);             /* -r^2, Q28 */
    c->g = (int32_t)(((1LL << 30) - ((r * r) >> 30)) >> 3);   /* (1 - r^2) / 2, Q28 */
}
AINL int32_t res_run(res_s *s, const res_c *c, int32_t x)
{
    /* the fraction a result drops carried into the next one (error feedback): a truncated high-Q resonator otherwise
     * rings on by itself for ever, a small tone or a DC level, once its input has fallen silent. (build 17) Its state
     * 256 times finer than the sample, the result rounded towards zero: the last digit it may still hold is under the
     * sample's -- MODAL and BANK raise it x 600, and it sang on alone, nothing in */
    int64_t acc = (int64_t)c->a1 * s->y1 + (int64_t)c->a2 * s->y2 + (int64_t)c->g * ((int64_t)(x - s->x2) << 8) + s->e;
    int32_t y = (int32_t)(acc >> 28);
    s->e = (int32_t)(acc - ((int64_t)y << 28));
    s->y2 = s->y1;
    s->y1 = y;
    s->x2 = s->x1;
    s->x1 = x;
    return y < 0 ? -((-y) >> 8) : y >> 8;
}
/* a note's frequency, Hz Q8 (MIDI, + cents) */
static int32_t note_hz8(int32_t note, int32_t cents)
{
    return (int32_t)(((uint64_t)440u * 256u * exp2_q16(((note - 69) * 100 + cents) * 65536 / 1200)) >> 16);
}
AINL uint8_t mu_enc8(int32_t x) { return gr_enc(x); }
AINL int32_t mu_dec8(uint8_t b) { return gr_dec(b); }

/* ------------------------------------------------------------- FILTER --- */
/* (build 12: more drastic, kept off the eardrums) the sound driven (DRIVE, p3: x 1 .. x 12, tanh) into a state-variable
 * filter -- TYPE (p0): low-pass, band-pass, high-pass, notch; FREQ (p1: 30 Hz up 9 octaves); RES (p2: Q 0.7 .. 12,
 * squelchy, short of ringing on its own) -- and out through a soft ceiling. Its level comes down as the resonance goes
 * up, and above 3 kHz the resonance is pulled back with the frequency: it may squelch and screech, never pierce. Fixed
 * where it is set: nothing sweeps by itself */
/* (build 32: an effect's values from its knobs are worked out when the knobs move, not every block -- the same
 * numbers; key: the knobs packed, have: worked out once) */
AINL uint32_t p_key(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
typedef struct { svf_t f; uint32_t key, have; svf_c c; int32_t type, drive, comp; uint32_t still; } m_flt_t;
static void m_flt(const uint8_t *p, m_flt_t *s, int32_t *x)
{
    int32_t type, drive, comp;
    uint32_t i;
    if (!s->have || s->key != p_key(p)) {
        int32_t oct = 321600 + p[1] * (9 * 65536 / 100), hz, q;
        hz = clampi((int32_t)(exp2_q16(clampi(oct, 4 << 16, 14 << 16)) >> 16), 30, 16000);
        q = 2896 + p[2] * (12 * 4096 - 2896) / 100;         /* Q 0.7 .. 12, Q12 */
        if (hz > 3000)                                      /* (high up, less of it: no whistle at 8 kHz) */
            q = 2896 + (q - 2896) * 3000 / hz;          /* (32 bits: < 1.4e8) */
        s->type = p[0] < 25 ? 1 : p[0] < 50 ? 2 : p[0] < 75 ? 3 : 4;
        s->drive = 4096 + p[3] * (11 * 4096) / 100;
        s->comp = (int32_t)(((uint32_t)Q15 * 4096u) / (uint32_t)(4096 + (q - 2896) / 3));   /* (x 1 .. ~0.3 with the Q) */
        svf_coef(&s->c, hz, q);
        s->key = p_key(p);
        s->have = 1;
    }
    type = s->type, drive = s->drive, comp = s->comp;
    {   /* (build 17: nothing in and only a remainder left -- a high Q's rounding held one for ever: let it go) */
        uint32_t any = 0;
        for (i = 0; i < CTL; i++)
            any |= (uint32_t)x[i];
        if (!any && absi(s->f.ic1) < 128 && absi(s->f.ic2) < 128)
            s->f.ic1 = s->f.ic2 = 0;
        s->still = any ? 0u : s->still + 1u;            /* (build 47: nothing in for 1.5 s -- the longest ring, Q 12 at
                                                             * 30 Hz, 60 dB down in 0.9 s -- and a rounding's fixed point
                                                             * above that let go too) */
        if (s->still >= 2067u)
            s->f.ic1 = s->f.ic2 = 0;
    }
    for (i = 0; i < CTL; i++) {
        int32_t v = tanh_q15(mulq12(x[i], drive)) >> 1, y;
        if (type == 4) {                                    /* notch: the band taken out (low + high) */
            int32_t bp = svf_run(&s->f, &s->c, v, 2);
            y = v - mulr24(s->c.k, bp);
        } else
            y = svf_run(&s->f, &s->c, v, type) * (type == 2 ? 2 : 1);
        x[i] = mulq15(tanh_q15(mulq15(y, comp) * 2), LV(0.7));   /* (a soft ceiling under full scale) */
    }
}

/* --------------------------------------------------------------- FOLD --- */
typedef struct { int32_t lp; uint32_t key, have, rp; int32_t g1, w, g2, tone, back; } m_fld_t;
/* reflected into -w .. w: (x + w) mod 4w, by a reciprocal of 4w (rp = (2^32 - 1) / 4w, worked out with the values)
 * and a step or two of correction -- the remainder exactly as % gives it, without a division a sample */
AINL int32_t fold_into(int32_t x, int32_t w, uint32_t rp)
{
    uint32_t p = 4u * (uint32_t)w;
    if (x > w || x < -w) {
        int32_t u = x + w;
        uint32_t a = (uint32_t)(u < 0 ? -u : u), m = a - (uint32_t)(((uint64_t)a * rp) >> 32) * p;
        while (m >= p)
            m -= p;
        if (u < 0 && m)
            m = p - m;
        x = (int32_t)m < 2 * w ? (int32_t)m - w : 3 * w - (int32_t)m;
    }
    return x;
}
static void m_fld(const uint8_t *p, m_fld_t *s, int32_t *x)
{
    int32_t g1, w, g2, tone, back;
    uint32_t i, rp;
    if (!s->have || s->key != p_key(p)) {
        s->g1 = (int32_t)(exp2_q16((p[0] * (5 * 65536 / 100)) - 65536) >> 4);   /* x 0.5 .. x 16, Q12 */
        s->w = 2621 + p[1] * (19005 - 2621) / 100;                             /* 0.08 .. 0.58 */
        s->g2 = 4096 + p[2] * (4 * 4096) / 100;                                /* x 1 .. x 5, Q12 */
        s->tone = 1400 + p[3] * (29430 - 1400) / 100;                          /* 300 Hz .. 16 kHz */
        s->back = (int32_t)((Q15 * 6000u) / (uint32_t)s->w);                   /* 0.18 / w, Q15 */
        s->rp = 0xFFFFFFFFu / (4u * (uint32_t)s->w);
        s->key = p_key(p);
        s->have = 1;
    }
    g1 = s->g1, w = s->w, g2 = s->g2, tone = s->tone, back = s->back, rp = s->rp;
    for (i = 0; i < CTL; i++) {
        int32_t y = fold_into(mulq12(x[i], g1), w, rp);
        y = fold_into(mulq12(y, g2), w, rp);
        y = mulq15(y, back > 4 * Q15 ? 4 * Q15 : back);
        s->lp += mulz15(y - s->lp, tone);        /* (towards zero: nothing in, it settles at 0, not a step under it --
                                                  * build 47: a FOLD after a FOLD made that step heard) */
        x[i] = s->lp;
    }
}

/* -------------------------------------------------------------- MULAW --- */
/* (build 12: further) p0 the rate (FS down to FS / 32, fractional: to 1.4 kHz), p1 the bits (8 down to 2, sign
 * included), p2 the filter before and after (0.05 .. 1.0 of the lower rate: open, it aliases), p3 the drive into it
 * (x 1 .. x 8: mu-law lifts the quiet) */
typedef struct { uint32_t ph; int32_t hold, a1, a2, b1, b2; uint32_t key, have, inc, shift; int32_t k, drive, undo; } m_mu_t;
static void m_mu(const uint8_t *p, m_mu_t *s, int32_t *x)
{
    uint32_t inc, shift, i;
    int32_t k, drive, undo;
    if (!s->have || s->key != p_key(p)) {
        uint32_t div16 = 16u + (uint32_t)(((uint32_t)p[0] * p[0]) * 496u / 10000u);   /* 1 .. 32, Q4 (squared: the
                                                          * low rates at the top of the knob) */
        int32_t hz = (int32_t)((uint32_t)FS * 16u / div16) * (5 + p[2] * 95 / 100) / 100;
        s->inc = (uint32_t)(0xFFFFFFFFu / div16) << 4;
        s->shift = 6u - (uint32_t)(p[1] * 7 / 101);     /* code bits dropped: 0 .. 6 (2 bits left) */
        s->k = clampi((hz * 4781) >> 10, 150, 32000);   /* a one-pole's step: 2 pi f / FS, Q15 */
        s->drive = 4096 + p[3] * (7 * 4096) / 100;
        s->undo = (4096 * 4096) / s->drive;
        s->key = p_key(p);
        s->have = 1;
    }
    inc = s->inc, shift = s->shift, k = s->k, drive = s->drive, undo = s->undo;
    for (i = 0; i < CTL; i++) {
        uint32_t was = s->ph;
        int32_t v = clampi(mulq12(x[i], drive), -32767, 32767);
        s->a1 += mulq15(v - s->a1, k);                  /* two poles before */
        s->a2 += mulq15(s->a1 - s->a2, k);
        s->ph += inc;
        if (s->ph < was) {                              /* a sample taken, coded, decoded */
            uint8_t b = mu_enc8(s->a2);
            b = (uint8_t)((b & 0x80u) | (((b & 0x7Fu) >> shift) << shift));
            s->hold = mu_dec8(b);
        }
        s->b1 += mulq15(s->hold - s->b1, k);            /* two poles after */
        s->b2 += mulq15(s->b1 - s->b2, k);
        x[i] = mulq12(s->b2, undo);
    }
}

/* -------------------------------------------------------------- SHIFT --- */
/* Niemitalo's Hilbert pair: two chains of four second-order allpass sections in z^-2, one path a sample late, 90
 * degrees apart from ~20 Hz to ~20 kHz. (build 12: more drastic) p0 the shift, 40 .. 900 Hz (log: every partial moved
 * off the harmonics, not a chorus); p1 the drive into it (x 1 .. x 6, tanh: denser sidebands); p2 the direction: down
 * below 45, up above 55, both (ring modulation) between; p3 the tone after, 1 .. 16 kHz. No feedback (a shifter fed
 * back spirals up or down: a barber pole). The shift is fixed while it sounds: nothing sweeps */
static const int32_t HB_A[4] = {128688190, 235208111, 262153419, 267764168};   /* a^2, Q28 */
static const int32_t HB_B[4] = {43421716, 196770956, 253765378, 265911936};
typedef struct { int32_t ax[4][2], ay[4][2], bx[4][2], by[4][2], bd; uint32_t ph; int32_t lp; } m_shf_t;
AINL int32_t hb_sec(int32_t c, int32_t x, int32_t *xs, int32_t *ys)   /* y = c (x + y[n-2]) - x[n-2] */
{
    int64_t m = (int64_t)c * (x + ys[1]);
    int32_t y = (int32_t)(m < 0 ? -((-m) >> 28) : m >> 28) - xs[1];   /* (towards zero: no ringing on in silence) */
    xs[1] = xs[0];
    xs[0] = x;
    ys[1] = ys[0];
    ys[0] = y;
    return y;
}
static void m_shf(const uint8_t *p, m_shf_t *s, int32_t *x)
{
    int32_t oct = 348800 + p[0] * (294000 / 100), drive = 4096 + p[1] * (5 * 4096) / 100, hz8, dir;   /* log2 40 .. 900 Hz */
    int32_t tone = 2800 + p[3] * (31000 - 2800) / 100;
    uint32_t inc, i, k;
    hz8 = (int32_t)(exp2_q16(oct) >> 8);
    inc = hz8_inc(hz8);
    dir = p[2] < 45 ? -1 : p[2] > 55 ? 1 : 0;
    for (i = 0; i < CTL; i++) {
        int32_t in = tanh_q15(mulq12(x[i], drive)) >> 1, a = in, b = in, y, c, sn;
        for (k = 0; k < 4u; k++) {
            a = hb_sec(HB_A[k], a, s->ax[k], s->ay[k]);
            b = hb_sec(HB_B[k], b, s->bx[k], s->by[k]);
        }
        { int32_t t = s->bd; s->bd = b; b = t; }        /* (the second path a sample late) */
        s->ph += inc;
        c = sin_ph(s->ph + 0x40000000u);
        sn = sin_ph(s->ph);
        y = dir > 0 ? mulq15(a, c) - mulq15(b, sn) : dir < 0 ? mulq15(a, c) + mulq15(b, sn) : mulq15(a, c) * 2;
        s->lp += mulz15(y - s->lp, tone);
        x[i] = s->lp * 2;
    }
}

/* -------------------------------------------------------------- MODAL --- */
/* three resonators: the root in an octave (p0: 1 .. 4 octaves up from the root at C1 .. B1), its modes at f s and
 * f s^2 (p2: s 1 .. 4.2: further apart, inharmonic); Q 4 .. 325 (p1, log); p3 the upper modes' level. (build 12) the
 * sound driven into them, their ring brought up with the Q and held under a soft ceiling: a body that sings, not a
 * murmur. Tuned once a block (cheap) */
typedef struct { res_c c[3]; res_s r[3]; int32_t key, mk; } m_mdl_t;
static void m_mdl(const uint8_t *p, m_mdl_t *s, int32_t *x)
{
    int32_t key = p[0] | p[1] << 8 | p[2] << 16 | rack_root << 24, lvl = Q15 / 4 + p[3] * (3 * Q15 / 4 / 100), i, k;
    if (key != s->key) {
        int32_t note = 24 + (rack_root % 12) + 12 * (p[0] * 4 / 101), f = note_hz8(note, 0);
        int32_t sr = 65536 + p[2] * (int32_t)(209715 / 100);           /* s, Q16: 1 .. 4.2 */
        int32_t q8 = (int32_t)(exp2_q16(p[1] * (622 * 65536 / 10000)) >> 8) * 4;   /* 4 .. ~300 (2^6.2), Q8 */
        s->mk = 512 + q8 / 2;                                          /* (make-up, Q8: x 6 .. x 75 with the Q) */
        for (k = 0; k < 3; k++) {
            res_coef(&s->c[k], f, q8);
            f = (int32_t)(((int64_t)f * sr) >> 16);
        }
        s->key = key;
    }
    for (i = 0; i < CTL; i++) {
        int32_t in = tanh_q15(x[i] * 2) >> 1,
                y = res_run(&s->r[0], &s->c[0], in) + mulq15(res_run(&s->r[1], &s->c[1], in) + res_run(&s->r[2], &s->c[2], in), lvl);
        x[i] = mulq15(tanh_q15(clampi((int32_t)(((int64_t)y * s->mk) >> 8), -8 * Q15, 8 * Q15)), LV(0.6));
    }
}

/* ------------------------------------------------------------- STRING --- */
/* three Karplus strings: the root in an octave (p0), the ring time 0.08 .. 8 s (p1: the loop gain from it, by the
 * pitch), two more strings +0.9 % and -0.7 % off (p2: their level), the loop's low-pass 400 Hz .. 14 kHz (p3); the
 * input low-passed at ~4 kHz */
#define STR_N 1400u              /* C1, 32.7 Hz */
typedef struct { int16_t line[3][STR_N]; uint32_t w; int32_t lp[3], in; uint32_t key, have; int32_t d8[3], g, tone, lvl, root; } m_str_t;
static void m_str(const uint8_t *p, m_str_t *s, int32_t *x)
{
    int32_t g, tone, lvl, k, root = rack_root, pk = 0;
    uint32_t i, any = 0;
    if (!s->have || s->key != p_key(p) || s->root != root) {
        int32_t note = 24 + (root % 12) + 12 * (p[0] * 4 / 101), hz8 = note_hz8(note, 0), len8[3];
        int32_t t100 = (int32_t)((8u * exp2_q16(p[1] * (664 * 65536 / 10000))) >> 16);   /* 0.08 .. 8 s, x 100 (2^6.64 = 100) */
        len8[0] = (int32_t)(((uint32_t)FS << 16) / (uint32_t)hz8);   /* samples, Q8 */
        len8[1] = (int32_t)(((int64_t)len8[0] * 64936) >> 16);   /* +0.9 % */
        len8[2] = (int32_t)(((int64_t)len8[0] * 65995) >> 16);   /* -0.7 % */
        for (k = 0; k < 3; k++)
            s->d8[k] = clampi(len8[k], 4 << 8, (int32_t)(STR_N - 3u) << 8);
        /* the loop gain: 60 dB in T s, a trip each 1 / f: 2^(-9.966 / (T f)) */
        {   /* 9.966 / (T f), Q16 = 9.966 x 65536 x 100 x 256 / (t100 x hz8) */
            uint32_t den = ((uint32_t)t100 * (uint32_t)hz8) >> 8;
            g = (int32_t)(exp2_q16(-(int32_t)(65313178u / (den ? den : 1u))) >> 1);
        }
        s->g = clampi(g, 6554, 32751);
        s->tone = 2800 + p[3] * (30500 - 2800) / 100;
        s->lvl = p[2] * (Q15 / 100);
        s->key = p_key(p);
        s->root = root;
        s->have = 1;
    }
    g = s->g, tone = s->tone, lvl = s->lvl;
    for (i = 0; i < CTL; i++)
        any |= (uint32_t)x[i];
    for (i = 0; i < CTL; i++) {
        int32_t y = 0;
        s->in += mulz15(x[i] - s->in, 15700);           /* (~4 kHz; towards zero, so silence settles at 0, not -1) */
        for (k = 0; k < 3; k++) {
            int32_t v;
            uint32_t rp = (s->w << 8) + (STR_N << 8) - (uint32_t)s->d8[k], i0 = rp >> 8, i1;
            if (i0 >= STR_N)                            /* (w + N - d, d within 4 .. N - 3: under 2N, one step) */
                i0 -= STR_N;
            i1 = i0 + 1u >= STR_N ? 0u : i0 + 1u;
            v = s->line[k][i0] + (s->line[k][i1] - s->line[k][i0]) * (int32_t)(rp & 255u) / 256;   /* (towards zero:
                                                  * a shift leaned every trip one way, and a string held a whisper at
                                                  * some roots for ever -- build 36) */
            s->lp[k] += mulz15(v - s->lp[k], tone);
            s->line[k][s->w] = (int16_t)clampi(s->in / 4 + mulz15(s->lp[k], g), -32767, 32767);
            y += k ? mulq15(v, lvl) : v;
        }
        if (++s->w >= STR_N)
            s->w = 0;
        x[i] = y;
        pk = maxi(pk, absi(y));
    }
    if (!any && absi(s->in) < 8 && pk && pk < 128) {          /* nothing in and under -48 dB: let go at once (build 36: the
                                                      * roundings of a long, nearly lossless loop could keep a whisper
                                                      * going round for ever at some roots) */
        memset(s->line, 0, sizeof s->line);
        s->lp[0] = s->lp[1] = s->lp[2] = 0;
    }
}

/* -------------------------------------------------------------- VOWEL --- */
/* three formants (a e i o u: F1 F2 F3) morphing along p0, the whole moved by p1 (x 2^-0.5 .. x 2^0.5), their Q by p2
 * (build 12: 10 .. 60 -- a voice, not a tint), the drive into them by p3 (x 2 .. x 16, tanh); their ring brought up
 * with the Q, under a soft ceiling */
static const int16_t VF[5][3] = {{600, 1040, 2250}, {400, 1620, 2400}, {250, 1750, 2600}, {400, 750, 2400}, {350, 600, 2400}};
typedef struct { res_c c[3]; res_s r[3]; int32_t key, mk; } m_vow_t;
static void m_vow(const uint8_t *p, m_vow_t *s, int32_t *x)
{
    int32_t key = p[0] | p[1] << 8 | p[2] << 16, drive = 8192 + p[3] * (14 * 4096) / 100, i, k;
    if (key != s->key) {
        int32_t pos = p[0] * 4 * 256 / 100, seg = pos >> 8, fr = pos & 255;
        int32_t sc = (int32_t)exp2_q16((p[1] - 50) * (65536 / 100));   /* x 2^-0.5 .. 2^0.5, Q16 */
        int32_t q8 = (10 + p[2] * 50 / 100) << 8;
        s->mk = 256 + q8 / 4;                                          /* (make-up, Q8: x 3.5 .. x 16) */
        seg = seg > 3 ? 3 : seg;
        for (k = 0; k < 3; k++) {
            int32_t f = VF[seg][k] + (((VF[seg + 1][k] - VF[seg][k]) * fr) >> 8);
            res_coef(&s->c[k], (int32_t)(((int64_t)f * sc) >> 8), q8);
        }
        s->key = key;
    }
    for (i = 0; i < CTL; i++) {
        int32_t in = tanh_q15(mulq12(x[i], drive)) >> 1,
                y = res_run(&s->r[0], &s->c[0], in) + mulq15(res_run(&s->r[1], &s->c[1], in), 19661) +
                    mulq15(res_run(&s->r[2], &s->c[2], in), 13107);
        x[i] = mulq15(tanh_q15(clampi((int32_t)(((int64_t)y * s->mk) >> 8), -8 * Q15, 8 * Q15)), LV(0.6));
    }
}

/* -------------------------------------------------------------- DELAY --- */
/* an 8-bit mu-law line at FS / 2 (a sample at the half rate, read with linear interpolation) */
AINL int32_t mu_tap(const uint8_t *line, uint32_t n, uint32_t w, int32_t t8)   /* t8 samples ago, Q8 */
{
    uint32_t q = (w << 8) + (n << 8) - (uint32_t)t8, i0 = q >> 8, f = q & 255u;
    int32_t y;
    if (i0 >= n)                                 /* (build 32: the remainder by a step -- w + n - t is under 2n) */
        i0 -= n;
    if (i0 >= n)
        i0 %= n;
    y = mu_dec8(line[i0]);
    return y + (((mu_dec8(line[i0 + 1u >= n ? 0u : i0 + 1u]) - y) * (int32_t)f) >> 8);
}
#define XF_PAIRS 512u            /* a change of time: the old head fades out, the new in, over 23 ms */
/* --------------------------------------------------------------- TAPE --- */
/* an echo on an 8-bit mu-law line at FS / 2: time 25 .. 900 ms (p0), (build 12) DROPOUTS (p1: the tape failing --
 * the echoes gone for 20 .. 200 ms, now and then, up to twice a second), feedback 0 .. 0.94 (p2) through the head's
 * saturation, AGE (p3: the loop's low-pass from 12 kHz down to 3 kHz, the head driven harder, hiss under it). The
 * time's own wander is a breath (at most 0.1 %), never a bend. The echoes alone come out. A change of time cross-fades */
#define TAP_N 19900u             /* 0.9 s at FS / 2 */
typedef struct { uint8_t line[TAP_N]; uint32_t w; int32_t t, t0, xf, lp, prev, wn, wt, dg, drop, env; rnd_t r; } m_tap_t;
static void m_tap(const uint8_t *p, m_tap_t *s, int32_t *x)
{
    int32_t want = (25 + p[0] * 875 / 100) * 2205 / 100 * 256;   /* samples at FS / 2, Q8 */
    int32_t fb = p[2] * (30802 / 100), age = 31000 - p[3] * (31000 - 12000) / 100, sat = 6144 + p[3] * (10 * 1024) / 100,
            hiss = p[3] * 6;                              /* (the head x 1.5 .. 4; hiss up to ~-40 dB) */
    uint32_t i;
    if (!s->t)
        s->t = want;
    if (want != s->t && !s->xf) {
        s->t0 = s->t;
        s->t = want;
        s->xf = XF_PAIRS;
    }
    if (!(s->r.s))
        rnd_seed(&s->r, 0x7A9Eu);
    if ((rnd_next(&s->r) & 255u) < 2u)                 /* the breath: a new place every ~0.2 s, glided to */
        s->wt = mulq15(rnd_bi(&s->r), 33);               /* (Q15 of the time: at most 0.1 %) */
    s->wn += (s->wt - s->wn) >> 7;
    if (!s->drop && p[1] && rnd_below(&s->r, 68900) < p[1])   /* a dropout: up to twice a second (1378 blocks / 100 %) */
        s->drop = (int32_t)ms_blocks((uint32_t)rnd_in(&s->r, 20, 200));
    if (s->drop)
        s->drop--;
    for (i = 0; i + 1u < CTL; i += 2u) {
        int32_t in = (x[i] + x[i + 1u]) >> 1, y, o0, tt = s->t + (int32_t)(((int64_t)s->t * s->wn) >> 15);
        y = mu_tap(s->line, TAP_N, s->w, clampi(tt, 4 << 8, (int32_t)(TAP_N - 4u) << 8));
        if (s->xf) {
            int32_t yo = mu_tap(s->line, TAP_N, s->w, s->t0);
            y = yo + (((y - yo) * (int32_t)(XF_PAIRS - (uint32_t)s->xf)) >> 9);
            s->xf--;
        }
        s->lp += mulz15(y - s->lp, age);
        s->line[s->w] = mu_enc8(mulq12(tanh_q15(mulq12(in + mulz15(s->lp, fb), sat)), 4096 * 4096 / sat));
        if (++s->w >= TAP_N)
            s->w = 0;
        s->dg += ((s->drop ? 0 : Q15) - s->dg) >> 6;     /* (a dropout's edges: ~3 ms, no click) */
        {   /* (build 17: the hiss under what passes, as on a tape: it follows the input and the echoes, ~0.3 s out,
             * and is gone with them -- before, it hissed on alone and kept the effect running for ever) */
            int32_t a = maxi(absi(in), absi(s->lp));
            s->env += a > s->env ? (a - s->env) >> 3 : -((s->env >> 12) + (s->env > 0));
        }
        y = mulq15(s->lp, s->dg) + (hiss ? mulq15(rnd_bi(&s->r), mulq15(hiss, clampi(s->env * 16, 0, Q15))) : 0);
        o0 = (s->prev + y) >> 1;
        s->prev = y;
        x[i] = o0;
        x[i + 1u] = y;
    }
}

/* ------------------------------------------------------------ REVERSE --- */
/* windows of 60 .. 220 ms (p0) read backwards at their own pitch: two heads half a window apart, each a triangle, so
 * the sum stays level; a low-pass on it, 1.4 .. 13 kHz (p1); fed back into the line (p2: 0 .. 0.7: the reversed
 * reversed again, smeared); p3 the windows' spread, how far back they reach (one to three windows). Its line: 0.9 s
 * at FS / 2, 8-bit mu-law */
#define RVS_N 19845u
typedef struct { uint8_t line[RVS_N]; uint32_t w, ph; int32_t lp, prev; } m_rvs_t;
AINL int32_t rvs_head(const m_rvs_t *s, uint32_t ph, uint32_t span, uint32_t back)   /* a head at phase ph */
{
    uint32_t d = back + (uint32_t)(((uint64_t)ph * span) >> 32) + 4u, q = s->w + RVS_N - d;
    int32_t tri = (int32_t)(ph >> 16), y;
    q = q >= RVS_N ? q - RVS_N : q;
    tri = tri < 32768 ? tri : 65535 - tri;              /* 0 .. 32767 .. 0 */
    y = mu_dec8(s->line[q]);
    return mulq15(y, tri);
}
static void m_rvs(const uint8_t *p, m_rvs_t *s, int32_t *x)
{
    uint32_t win = (60u + (uint32_t)p[0] * 160u / 100u) * 2205u / 100u;   /* samples at FS / 2 */
    uint32_t span = 2u * win, back = win * (uint32_t)p[3] / 50u, inc = 0xFFFFFFFFu / (win ? win : 1u), i;
    int32_t tone = 6400 + p[1] * (31000 - 6400) / 100, fb = p[2] * (LV(0.7) / 100);
    if (span + back > RVS_N - 8u)
        back = RVS_N - 8u - span;
    for (i = 0; i + 1u < CTL; i += 2u) {
        int32_t y;
        s->line[s->w] = mu_enc8(clampi(((x[i] + x[i + 1u]) >> 1) + mulq15(s->lp, fb), -32767, 32767));
        if (++s->w >= RVS_N)
            s->w = 0;
        s->ph += inc;
        y = rvs_head(s, s->ph, span, back) + rvs_head(s, s->ph + 0x80000000u, span, back);
        s->lp += mulq15(y - s->lp, tone);
        x[i] = (s->prev + s->lp) >> 1;
        x[i + 1u] = s->lp;
        s->prev = s->lp;
    }
}

/* ------------------------------------------------------------- GLITCH --- */
/* Russolo's bent buffers (Tonitor's russolo.c BEND, JUMP, after Noisferatu) as an effect: the sound written into a
 * line of 0.74 s (8-bit mu-law at FS / 2) and read back at its own speed -- never another pitch -- from SPAN behind
 * (p3: 20 .. 700 ms), its address now and then bent (p0: how often and how far -- a bit of it flipped, a higher one the
 * further), on a clock (p1: 0.5 .. 16 Hz) that at each tick bends, straightens, jumps the read elsewhere in the span,
 * or (p2: the chance) stutters: a few rounds of a short loop where it is (10 .. 120 ms). Each change cross-faded over
 * 2 ms, so it glitches without clicking. The read alone comes out */
#define GLT_N 16384u             /* 0.74 s at FS / 2 */
typedef struct {
    uint8_t line[GLT_N];
    uint32_t w, clk, ph;
    uint32_t mask, omask;        /* the address's bent bits now, before the change */
    int32_t back, oback;         /* how far behind the read is (samples at FS / 2), now and before */
    uint32_t loop, loop_at, loop_left;   /* a stutter: its length, its start, samples to go */
    int32_t xf, prev;
    rnd_t r;
} m_glt_t;
AINL int32_t glt_read(const m_glt_t *s, int32_t back, uint32_t mask)
{
    uint32_t i = (s->w + GLT_N - (uint32_t)back) & (GLT_N - 1u);
    return mu_dec8(s->line[(i ^ mask) & (GLT_N - 1u)]);
}
static void m_glt(const uint8_t *p, m_glt_t *s, int32_t *x)
{
    int32_t span = ms_samples2(20u + p[3] * 680u / 100u), o0;
    uint32_t i, tick = 97391u * 2u * (50u + p[1] * 1550u / 100u) / 100u;   /* 0.5 .. 16 Hz at FS / 2 (2^32 / 22050 a Hz:
                                                                             * in 32 bits, at most 3.1e8 before / 100) */
    if (!s->r.s)
        rnd_seed(&s->r, 0x474C54u ^ fx_place(s));
    if (!s->back)
        s->back = s->oback = span;
    for (i = 0; i + 1u < CTL; i += 2u) {
        int32_t in = (x[i] + x[i + 1u]) >> 1, y;
        s->line[s->w] = mu_enc8(clampi(in, -32767, 32767));
        s->w = (s->w + 1u) & (GLT_N - 1u);
        s->ph += tick;
        if (s->ph < tick) {                      /* the clock: a change */
            s->omask = s->mask;
            s->oback = s->back;
            s->xf = 44;                          /* (2 ms at FS / 2) */
            if (rnd_below(&s->r, 100) < p[2] && !s->loop_left) {   /* a stutter where it is */
                s->loop = ms_samples2((uint32_t)rnd_in(&s->r, 10, 120));
                s->loop_at = (s->w + GLT_N - (uint32_t)s->back) & (GLT_N - 1u);
                s->loop_left = s->loop * (uint32_t)rnd_in(&s->r, 2, 6);
            } else if (rnd_below(&s->r, 100) < p[0]) {   /* bent: a bit flipped, the higher the further */
                s->mask = 1u << rnd_in(&s->r, 4, 4 + p[0] * 9 / 100);
            } else if (rnd_chance(&s->r, 35)) {  /* a jump in the span */
                s->mask = 0;
                s->back = rnd_in(&s->r, 64, span);
            } else
                s->mask = 0;
        }
        if (s->loop_left) {                      /* stuttering: round the loop */
            uint32_t k = s->loop - s->loop_left % s->loop;
            y = mu_dec8(s->line[(s->loop_at + k) & (GLT_N - 1u)]);
            s->loop_left--;
        } else
            y = glt_read(s, s->back, s->mask);
        if (s->xf) {                             /* (the change cross-faded) */
            int32_t yo = glt_read(s, s->oback, s->omask);
            y = yo + (((y - yo) * (44 - s->xf) * 745) >> 15);   /* (/ 44 as x 745 / 2^15: no divide a sample) */
            s->xf--;
        }
        o0 = (s->prev + y) >> 1;
        s->prev = y;
        x[i] = o0;
        x[i + 1u] = y;
    }
}

/* ---------------------------------------------- build 11: drastic ones --- */
/* CLOUD -- a granular freezer (after Mutable Instruments' Clouds and Beads, MIT: the idea, not the code): the sound
 * written into 0.74 s (8-bit mu-law at FS / 2) and read back as up to six Hann grains of SIZE (p0: 30 .. 250 ms),
 * DENSITY a second (p1: 3 .. 60), each from a place drawn at random up to SPREAD back (p3: 0.05 .. 0.7 s), at the
 * sound's own speed (never another pitch); FREEZE (p2 above its middle) stops the writing: the cloud turns over what it
 * holds (and the slot's gate still closes it once no note sounds); a fresh one (a new patch, a recall) fills its 0.74 s
 * with the sound first, then holds it. The grains alone come out */
#define CLD_N 16384u
#define CLD_G 6u
typedef struct {
    uint8_t line[CLD_N];
    uint32_t w, filled, gp[CLD_G], gw[CLD_G], gwinc[CLD_G];
    volatile uint32_t fresh;     /* (build 19) pairs still to be written anew, frozen or not: a morph's end (fx_refill) */
    int32_t next, prev;
    uint8_t gon[CLD_G];
    rnd_t r;
} m_cld_t;
static void m_cld(const uint8_t *p, m_cld_t *s, int32_t *x)
{
    uint32_t len = ms_samples2(30u + p[0] * 220u / 100u), rate = 3u + p[1] * 57u / 100u, iv = 22050u / rate;
    uint32_t spread = ms_samples2(50u + p[3] * 650u / 100u), i, k, k2;
    int frozen = p[2] >= 50 && s->filled >= CLD_N && !s->fresh;   /* (frozen only once it holds something: a fresh cloud
                                                     * fills first, with the sound now, then holds it -- never an empty,
                                                     * silent one; told to, it takes the sound now again, its grains
                                                     * going on over it) */
    int32_t norm, o0;
    if (!s->r.s)
        rnd_seed(&s->r, 0x434C44u ^ fx_place(s));
    k2 = (iv << 15) / (3u * len);                        /* (as the voices' cloud: each grain at sqrt(8 / 3 O)) */
    norm = clampi((int32_t)isqrt32((k2 > 65535u ? 65535u : k2) << 16) << 1, 4096, 2 * Q15);
    for (i = 0; i + 1u < CTL; i += 2u) {
        int32_t in = (x[i] + x[i + 1u]) >> 1, y = 0;
        if (!frozen) {
            s->line[s->w] = mu_enc8(clampi(in, -32767, 32767));
            s->w = (s->w + 1u) & (CLD_N - 1u);
            if (s->filled < CLD_N)
                s->filled++;
            if (s->fresh)
                s->fresh--;
        }
        if (--s->next <= 0 && s->filled > len + 64u) {   /* a grain: a free place, from somewhere behind */
            for (k = 0; k < CLD_G && s->gon[k]; k++)
                ;
            if (k < CLD_G) {
                uint32_t room = s->filled - len - 32u, back = len + 16u + (uint32_t)rnd_below(&s->r, (int32_t)(spread < room ? spread : room));
                s->gp[k] = (s->w + CLD_N - back) & (CLD_N - 1u);
                s->gw[k] = 0;
                s->gwinc[k] = 0xFFFFFFFFu / len;
                s->gon[k] = 1;
            }
            s->next = (int32_t)iv / 2 + rnd_below(&s->r, (int32_t)iv);
        }
        for (k = 0; k < CLD_G; k++)
            if (s->gon[k]) {
                uint32_t hi = s->gw[k] >> 24, was = s->gw[k];
                int32_t hf = (int32_t)((s->gw[k] >> 9) & 0x7FFFu), w = HANN[hi] + (((HANN[hi + 1u] - HANN[hi]) * hf) >> 15);
                y += mulq15(mu_dec8(s->line[s->gp[k]]), w);
                s->gp[k] = (s->gp[k] + 1u) & (CLD_N - 1u);
                s->gw[k] += s->gwinc[k];
                if (s->gw[k] < was)
                    s->gon[k] = 0;
            }
        y = mulq15(y, norm);
        o0 = (s->prev + y) >> 1;
        s->prev = y;
        x[i] = o0;
        x[i + 1u] = y;
    }
}

/* SCREAM -- a saturated feedback network (a no-input mixer's howl, a feedback delay network after Jot and Stautner):
 * four lines of prime lengths (SIZE, p0: 1 .. 23 ms, never gliding: a new size is a new length at once), mixed by a
 * Hadamard matrix, each through a one-pole (TONE, p2) and a tanh, fed back by FEEDBACK (p1: up to 1.05 -- the tanh holds
 * it) times the input's level followed (g x env / (env + k): silence lets it die). DRIVE (p3) into it. The lines out */
#define SCR_N 1024u
typedef struct { int16_t d[4][SCR_N]; uint32_t w; int32_t lp[4], env; } m_scr_t;
static void m_scr(const uint8_t *p, m_scr_t *s, int32_t *x)
{
    static const uint16_t BASE[4] = {1013, 761, 557, 409};   /* (primes: no two lines in step) */
    uint32_t i, k, len[4];
    int32_t gmax = LV(0.6) + p[1] * (LV(0.45) / 100), tone = 2000 + p[2] * (30000 - 2000) / 100,
            drive = 2048 + p[3] * (6 * 4096) / 100, g;
    for (k = 0; k < 4u; k++)
        len[k] = clampi((int32_t)(BASE[k] * (uint32_t)(4 + p[0] * 96 / 100) / 100u), 17, SCR_N - 1);
    g = (int32_t)((uint32_t)gmax * (uint32_t)s->env / (uint32_t)(s->env + 600));   /* (once a block, 32 bits: the loop's
                                                                                   * gain by the input's level) */
    for (i = 0; i < CTL; i++) {
        int32_t in = mulq12(x[i], drive), y[4], h[4], o;
        s->env += (absi(x[i]) - s->env) >> 7;
        for (k = 0; k < 4u; k++)
            y[k] = s->d[k][(s->w + SCR_N - len[k]) & (SCR_N - 1u)];
        h[0] = (y[0] + y[1] + y[2] + y[3]) >> 1;         /* (a 4 x 4 Hadamard, / 2: it keeps the energy) */
        h[1] = (y[0] - y[1] + y[2] - y[3]) >> 1;
        h[2] = (y[0] + y[1] - y[2] - y[3]) >> 1;
        h[3] = (y[0] - y[1] - y[2] + y[3]) >> 1;
        o = (y[0] + y[1] + y[2] + y[3]) >> 1;
        for (k = 0; k < 4u; k++) {
            s->lp[k] += mulz15(h[k] - s->lp[k], tone);
            s->d[k][s->w] = (int16_t)clampi(tanh_q15(((k & 1u) ? -in : in) + mulz15(s->lp[k], g)), -32767, 32767);
        }
        s->w = (s->w + 1u) & (SCR_N - 1u);
        x[i] = o;
    }
}

/* RING -- a diode ring modulator (after Julian Parker's model, DAFx-11, as Mutable Instruments' Warps has it): a carrier
 * fixed at FREQ (p0: 30 .. 1450 Hz), or (SELF, p2) the sound's own past (23 ms back) as its carrier; DIODE (p1) from the
 * ideal product (clean sidebands) to the diodes' half-wave square law (leakage, grit); TONE (p3) a low-pass after */
#define RNG_N 1024u
typedef struct { uint8_t line[RNG_N]; uint32_t w, ph; int32_t lp; uint8_t odd; } m_rng_t;
AINL int32_t rng_d(int32_t v) { return v > 0 ? (v * v) >> 15 : 0; }   /* a diode: square law above its knee, Q15 */
static void m_rng(const uint8_t *p, m_rng_t *s, int32_t *x)
{
    int32_t hz8 = (int32_t)((30u * 256u * exp2_q16(p[0] * (558 * 65536 / 10000))) >> 16), dio = p[1] * (Q15 / 100),
            self = p[2] * (Q15 / 100), tone = 3000 + p[3] * (31000 - 3000) / 100;
    uint32_t inc = hz8_inc(hz8), i;
    for (i = 0; i < CTL; i++) {
        int32_t xv = clampi(x[i], -32767, 32767), c = sin_ph(s->ph), a, b, ideal, dd, y;
        s->ph += inc;
        if (self) {                                      /* (its own past as the carrier, 23 ms back, lifted) */
            int32_t past = mu_dec8(s->line[(s->w + RNG_N - 507u) & (RNG_N - 1u)]);
            c += mulq15(clampi(past * 4, -32767, 32767) - c, self);
        }
        if (s->odd ^= 1u) {
            s->line[s->w] = mu_enc8(xv);
            s->w = (s->w + 1u) & (RNG_N - 1u);
        }
        ideal = mulq15(xv, c) * 2;
        a = (c + xv) >> 1;
        b = (c - xv) >> 1;
        dd = (rng_d(a) - rng_d(b) - rng_d(-b) + rng_d(-a)) * 4;
        y = ideal + mulq15(dd - ideal, dio);
        s->lp += mulz15(y - s->lp, tone);
        x[i] = s->lp;
    }
}

/* STARVE -- a starved, gated fuzz (after the Fuzz Factory's circuit): GAIN (p0: x 10 .. x 1000, 20 .. 60 dB) into an
 * asymmetric clipper (soft above, hard below), its BIAS (p2) pulled down as the sound gets louder (a starved supply);
 * a GATE (p1) on the sound's level with hysteresis, chopping it to nothing below -- the velcro sputter, the tearing;
 * TONE (p3) a low-pass after. Its level brought back to the sound's */
typedef struct { int32_t env, lp, open, dc; } m_stv_t;   /* (dc: Q10 above the sample) */
static void m_stv(const uint8_t *p, m_stv_t *s, int32_t *x)
{
    int32_t g = (int32_t)((10u * exp2_q16(p[0] * (664 * 65536 / 10000))) >> 6), thr = p[1] * 33,   /* (gain Q10; to ~-20 dB) */
            bias = p[2] * (LV(0.5) / 100), tone = 1400 + p[3] * (30000 - 1400) / 100;
    uint32_t i;
    for (i = 0; i < CTL; i++) {
        int32_t xv = x[i], v, y;
        s->env += (absi(xv) - s->env) >> (absi(xv) > s->env ? 4 : 9);
        if (s->open ? s->env < thr : s->env > thr + (thr >> 1) + 8)
            s->open ^= 1;                                /* (hysteresis: it sputters at the threshold, not chatters) */
        v = (int32_t)clampi(((int64_t)xv * g) >> 10, -4 * Q15, 4 * Q15) + bias - mulq15(s->env, LV(0.6));
        y = v > 0 ? tanh_q15(v) : clampi(v, -LV(0.6), 0);
        y = s->open ? mulq15(y, LV(0.22)) : 0;          /* (its level near the sound's) */
        s->dc += y - (s->dc >> 10);                      /* (the clipper's lean taken off: Q10, so it settles on the
                                                          * lean itself -- build 17: it stopped up to 1023 short and a
                                                          * GATE of 0 held that much out for ever, nothing in) */
        s->lp += mulz15(y - (s->dc >> 10) - s->lp, tone);
        x[i] = s->lp;
    }
}

/* CHEB -- a harmonic re-voicer (after Airwindows' Pafnuty, MIT: the idea): the sound brought to a level (DRIVE, p2:
 * 0.3 .. 0.98 of full), through a sum of Chebyshev polynomials T1 .. T8 -- each makes its harmonic of a sine, and of a
 * texture a dense, fixed intermodulation -- weighted by SHAPE (p0: four sets, crossed) and ODD (p1: the even ones down,
 * or the odd), then brought back to the sound's level; TONE (p3) a low-pass after */
typedef struct { int32_t env, lp; } m_chb_t;
static const int16_t CHB_W[4][8] = {   /* Q15 weights of T1 .. T8 */
    {16384, 9830, 6554, 4915, 3277, 3277, 1638, 1638},   /* rich: everything, falling */
    {19661, 0, 11469, 0, 8192, 0, 4915, 0},              /* hollow: the odd ones */
    {6554, 0, 0, 9830, 0, 9830, 0, 9830},                /* glass: high ones only */
    {9830, 13107, 0, 9830, 0, 6554, 6554, 0}};           /* growl: the low even ones */
static void m_chb(const uint8_t *p, m_chb_t *s, int32_t *x)
{
    int32_t w[8], a = p[0] * 3, set = a / 100, fr = (a % 100) * (Q15 / 100), odd = p[1] * (Q15 / 100), tgt, gain, inv;
    int32_t tone = 3000 + p[3] * (31000 - 3000) / 100;
    uint32_t i, k;
    for (k = 0; k < 8u; k++) {
        int32_t v = set >= 3 ? CHB_W[3][k] : CHB_W[set][k] + mulq15(CHB_W[set + 1][k] - CHB_W[set][k], fr);
        w[k] = (k & 1u) ? mulq15(v, Q15 - odd) : mulq15(v, Q15 / 2 + odd / 2);   /* (k odd: T2, T4 ..: the even harmonics) */
    }
    tgt = LV(0.3) + p[2] * (LV(0.68) / 100);
    gain = (int32_t)(((uint32_t)tgt << 12) / (uint32_t)(s->env > 64 ? s->env : 64));   /* (once a block: to the level, Q12) */
    gain = clampi(gain, 4096 / 4, 4096 * 64);
    inv = (int32_t)(((uint32_t)clampi(s->env, 64, 65535) << 15) / (uint32_t)tgt);   /* (and back) */
    for (i = 0; i < CTL; i++) {
        int32_t xn = clampi(mulq12(x[i], gain), -32767, 32767), t0 = Q15, t1 = xn, y = mulq15(w[0], t1), t2;
        s->env += (absi(x[i]) - s->env) >> 8;
        for (k = 1; k < 8u; k++) {                       /* T(k+1) = 2 x T(k) - T(k-1) */
            t2 = clampi(2 * mulq15(xn, t1) - t0, -32767, 32767);
            y += mulq15(w[k], t2);
            t0 = t1;
            t1 = t2;
        }
        y = mulq15(clampi(y, -2 * Q15, 2 * Q15), inv) * 3;   /* (x 3: the sum of weights under one is brought back up) */
        s->lp += mulz15(y - s->lp, tone);
        x[i] = s->lp;
    }
}

/* BANK -- an inharmonic resonator bank (after Mutable Instruments' Rings and Elements, MIT: the idea): eight modes of a
 * bar, a plate, a bell or a membrane (SET, p2), the lowest in an octave of the scale's root (PITCH, p0), ringing at Q
 * (p1: 30 .. 400), the upper ones as bright as BRIGHT (p3) lets them. A texture into it comes out a struck, a bowed, a
 * ringing body. Tuned once a block when a value moves */
static const uint16_t BNK_R[4][8] = {   /* the modes' ratios to the lowest, x 100 */
    {100, 276, 540, 893, 1334, 1863, 2481, 3188},       /* a free bar */
    {100, 159, 214, 230, 292, 316, 365, 409},           /* a plate */
    {100, 250, 300, 433, 500, 580, 667, 800},           /* a bell */
    {100, 159, 214, 230, 265, 292, 316, 352}};          /* a membrane */
typedef struct { res_c c[8]; res_s r[8]; int32_t key, lv[8], mk; uint8_t nm; } m_bnk_t;
static void m_bnk(const uint8_t *p, m_bnk_t *s, int32_t *x)
{
    int32_t key = p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24 | (rack_root & 127) << 25;
    uint32_t i, k;
    if (key != s->key || !s->nm) {
        int32_t note = 36 + (rack_root % 12) + 12 * (p[0] * 3 / 101), hz8 = note_hz8(note, 0), set = p[2] * 3 / 100;
        int32_t q8 = (int32_t)((30u * 256u * exp2_q16(p[1] * (374 * 65536 / 10000))) >> 16);   /* 30 .. 400 */
        s->nm = 0;
        s->mk = 256 + q8 * 3 / 2;                        /* (make-up, Q8, with the Q: a narrower band passes less of a
                                                          * texture -- x 46 at Q 30 .. x 600 at Q 400, then a tanh) */
        for (k = 0; k < 8u; k++) {
            int32_t f = (int32_t)((uint32_t)hz8 * BNK_R[set][k] / 100u);   /* (32 bits: < 9e8) */
            if (f > (15000 << 8))
                break;
            res_coef(&s->c[k], f, q8);
            s->lv[k] = k ? Q15 * 6 / 10 - mulq15(Q15 * 6 / 10, Q15 - p[3] * (Q15 / 100)) * (int32_t)k / 8 : Q15 * 7 / 10;
            s->nm = (uint8_t)(k + 1u);
        }
        s->key = key;
    }
    for (i = 0; i < CTL; i++) {
        int32_t in = x[i] >> 1, y = 0;
        for (k = 0; k < s->nm; k++)
            y += mulq15(res_run(&s->r[k], &s->c[k], in), s->lv[k]);
        x[i] = mulq15(tanh_q15(clampi((int32_t)(((int64_t)y * s->mk) >> 8), -8 * Q15, 8 * Q15)), LV(0.6));   /* (a ringing
                                                                                                              * peak held) */
    }
}

/* ------------------------------------------------------------- SPRING --- */
/* the input through a resonance (p3: 400 Hz .. 5.9 kHz, the spring's tone), into a tank of 30 .. 120 ms (p1) whose
 * loop carries a chain of six short allpasses (the chirps; p0: their gain, 0.3 .. 0.75) and a low-pass, its gain
 * 0.2 .. 0.93 (p2). Mono, at the full rate */
#define SPR_N 5300u              /* 120 ms */
static const uint16_t SPR_AP[6] = {37, 53, 71, 89, 113, 149};
#define SPR_AP_N 520u
typedef struct { int16_t tank[SPR_N]; int16_t ap[SPR_AP_N]; uint32_t w, aw[6]; int32_t lp; res_c rc; res_s rs; int32_t key; } m_spr_t;
static void m_spr(const uint8_t *p, m_spr_t *s, int32_t *x)
{
    int32_t g = 9830 + p[0] * (24576 - 9830) / 100, fb = 6554 + p[2] * (30474 - 6554) / 100, k;
    uint32_t len = (30u + (uint32_t)p[1] * 90u / 100u) * 441u / 10u, i, base;
    int32_t key = p[3];
    if (key != s->key || !s->rc.g) {
        res_coef(&s->rc, (400 + p[3] * 55) << 8, 180);   /* (Q 0.7: a broad colour) */
        s->key = key;
    }
    for (i = 0; i < CTL; i++) {
        uint32_t q = s->w + SPR_N - len;
        int32_t y, v = (x[i] >> 1) + res_run(&s->rs, &s->rc, x[i]);   /* (the sound itself and its colour, into the tank) */
        q = q >= SPR_N ? q - SPR_N : q;
        y = s->tank[q];
        base = 0;
        for (k = 0; k < 6; k++) {                       /* Schroeder allpasses: w = y + g d; out = d - g w */
            uint32_t n = SPR_AP[k], r = s->aw[k];
            int32_t d = s->ap[base + r], ww = y + mulq15(d, g);
            s->ap[base + r] = (int16_t)clampi(ww, -32767, 32767);
            y = d - mulq15(ww, g);
            s->aw[k] = r + 1u >= n ? 0u : r + 1u;
            base += n;
        }
        s->lp += mulq15(y - s->lp, 20000);
        s->tank[s->w] = (int16_t)clampi(v + mulq15(s->lp, fb), -32767, 32767);
        if (++s->w >= SPR_N)
            s->w = 0;
        x[i] = 2 * y;                                  /* (x 2: its loop loses what the allpasses spread) */
    }
}

/* ============================================================== build 32 ===
 * Five more, cheap -- each under ~40 operations a sample and a few KiB --, none sounding with nothing in, none bending
 * a pitch. (Their names are provisional.) */

/* -------------------------------------------------------------- SMEAR --- */
/* a velvet-noise smear (after Alary, Politis and Valimaki's velvet-noise decorrelator, DAFx 2017): the sound through
 * SMR_T taps at random places within SPAN (p0: 12 .. 46 ms), each + or -, their weights falling to TAIL (p1: the last
 * at 1.0 .. 0.1 of the first) -- smoke, not a room; p2 the mix (0.4 .. 1 of it), p3 the tone after (300 Hz .. 16 kHz).
 * No feedback: nothing in, nothing out 46 ms later. The taps are drawn from the instance's place: the same each time */
#define SMR_N 2048u              /* 46 ms */
#define SMR_T 20u
typedef struct { int16_t line[SMR_N]; uint16_t at[SMR_T]; int16_t g[SMR_T]; uint32_t w, key, have; int32_t lp, norm, mix, tone; } m_smr_t;
static void m_smr(const uint8_t *p, m_smr_t *s, int32_t *x)
{
    uint32_t i, k;
    int32_t norm, mix, tone;
    if (!s->have || s->key != p_key(p)) {
        rnd_t r;
        uint32_t span = 530u + (uint32_t)p[0] * (SMR_N - 8u - 530u) / 100u, seg = span / SMR_T, e2 = 0;
        int32_t gl = Q15 - p[1] * (Q15 * 9 / 10) / 100;   /* (the last tap's weight) */
        rnd_seed(&r, 0x534D52u ^ fx_place(s));
        for (k = 0; k < SMR_T; k++) {
            int32_t g = (Q15 + (gl - Q15) * (int32_t)k / (int32_t)(SMR_T - 1u)) >> 4;   /* Q11 */
            s->at[k] = (uint16_t)(1u + k * seg + (uint32_t)rnd_below(&r, (int32_t)seg));
            s->g[k] = (int16_t)(rnd_chance(&r, 50) ? g : -g);
            e2 += (uint32_t)(g * g);
        }
        s->norm = (int32_t)((2048u * 32768u) / (isqrt32(e2) + 1u));   /* (the sum's level as the sound's, Q15) */
        s->mix = LV(0.4) + p[2] * (Q15 - LV(0.4)) / 100;
        s->tone = 1400 + p[3] * (30000 - 1400) / 100;
        s->key = p_key(p);
        s->have = 1;
    }
    norm = s->norm, mix = s->mix, tone = s->tone;
    for (i = 0; i < CTL; i++) {
        int32_t acc = 0, y;
        s->line[s->w] = (int16_t)clampi(x[i], -32767, 32767);
        for (k = 0; k < SMR_T; k++)                     /* (Q15 x Q11, twenty: under 2^31) */
            acc += s->line[(s->w - s->at[k]) & (SMR_N - 1u)] * s->g[k];
        s->w = (s->w + 1u) & (SMR_N - 1u);
        y = mulq15(acc >> 11, norm);
        y = mulq15(x[i], Q15 - mix) + mulq15(y, mix);
        s->lp += mulz15(y - s->lp, tone);
        x[i] = s->lp;
    }
}

/* ------------------------------------------------------------ DIFFUSE --- */
/* four allpasses in a row (Schroeder's; the lengths of Mutable Instruments' Clouds diffuser, at 44.1 kHz): the edges
 * of the sound blurred, no tail of its own. p0 the size (0.4 .. 1 of the lengths), p1 the diffusion (0.4 .. 0.75), p2
 * the mix (0.5 .. 1), p3 the tone after. The lengths never slide: a new size starts each line's reading afresh */
#define DIF_N 1405u
static const uint16_t DIF_L[4] = {174, 248, 371, 612}, DIF_O[4] = {0, 174, 422, 793};
typedef struct { int16_t d[DIF_N]; uint16_t len[4], w[4]; uint32_t key, have; int32_t lp, kap, mix, tone; } m_dif_t;
static void m_dif(const uint8_t *p, m_dif_t *s, int32_t *x)
{
    uint32_t i, k;
    int32_t kap, mix, tone;
    if (!s->have || s->key != p_key(p)) {
        for (k = 0; k < 4u; k++) {
            s->len[k] = (uint16_t)maxi(8, DIF_L[k] * (40 + p[0] * 60 / 100) / 100);
            if (s->w[k] >= s->len[k])
                s->w[k] = 0;
        }
        s->kap = LV(0.4) + p[1] * (LV(0.75) - LV(0.4)) / 100;
        s->mix = LV(0.5) + p[2] * (Q15 - LV(0.5)) / 100;
        s->tone = 1400 + p[3] * (30000 - 1400) / 100;
        s->key = p_key(p);
        s->have = 1;
    }
    kap = s->kap, mix = s->mix, tone = s->tone;
    for (i = 0; i < CTL; i++) {
        int32_t v = x[i], y;
        for (k = 0; k < 4u; k++) {
            int16_t *line = &s->d[DIF_O[k]];
            int32_t d = line[s->w[k]], a = v + mulz15(d, kap);   /* (towards zero: it dies out to nothing) */
            line[s->w[k]] = (int16_t)clampi(a, -32767, 32767);
            v = d - mulz15(a, kap);
            if (++s->w[k] >= s->len[k])
                s->w[k] = 0;
        }
        y = mulq15(x[i], Q15 - mix) + mulq15(v, mix);
        s->lp += mulz15(y - s->lp, tone);
        x[i] = s->lp;
    }
}

/* --------------------------------------------------------------- SLEW --- */
/* a slew limiter (after Airwindows' Slew): the sound may move only so far a sample -- the highs turned to dark,
 * grinding ramps. p0 the rate (40 .. 4000 a sample: from treacle to a trace), p1 the drive into it (x 1 .. x 8),
 * p2 the mix (0.3 .. 1), p3 the tone after. With nothing in it walks to 0 and stays */
typedef struct { int32_t y, lp; uint32_t key, have; int32_t step, drive, mix, tone; } m_slw_t;
static void m_slw(const uint8_t *p, m_slw_t *s, int32_t *x)
{
    uint32_t i;
    int32_t step, drive, mix, tone;
    if (!s->have || s->key != p_key(p)) {
        s->step = (int32_t)((40u * exp2_q16(p[0] * (664 * 65536 / 10000))) >> 16);   /* 40 .. 4000 */
        s->drive = 4096 + p[1] * (7 * 4096) / 100;
        s->mix = LV(0.3) + p[2] * (Q15 - LV(0.3)) / 100;
        s->tone = 1400 + p[3] * (30000 - 1400) / 100;
        s->key = p_key(p);
        s->have = 1;
    }
    step = s->step, drive = s->drive, mix = s->mix, tone = s->tone;
    for (i = 0; i < CTL; i++) {
        int32_t v = clampi(mulq12(x[i], drive), -2 * Q15, 2 * Q15), y;
        s->y += clampi(v - s->y, -step, step);
        y = mulq15(x[i], Q15 - mix) + mulq15(tanh_q15(s->y), mix);
        s->lp += mulz15(y - s->lp, tone);
        x[i] = s->lp;
    }
}

/* ---------------------------------------------------------------- XOR --- */
/* the sound combined bit by bit with itself a moment before (after the Teensy Audio library's effect_combine):
 * broken, digital, following the sound's shape. p0 the moment (0.1 .. 23 ms), p1 the operation (XOR, AND, OR), p2 the
 * bits kept (3 .. 10: fewer, harsher), p3 the tone after. Both taken towards zero first, so near-silence is silence;
 * the lean the bits leave taken off */
#define XOR_N 1024u
typedef struct { int16_t line[XOR_N]; uint32_t w, key, have; int32_t dx, dy, lp, back, op, sh, tone; } m_xor_t;
AINL int32_t xor_q(int32_t v, int32_t sh) { return v < 0 ? -((-v) >> sh) : v >> sh; }
static void m_xor(const uint8_t *p, m_xor_t *s, int32_t *x)
{
    uint32_t i;
    int32_t back, op, sh, tone;
    if (!s->have || s->key != p_key(p)) {
        s->back = 4 + p[0] * (int32_t)(XOR_N - 8u) / 100;
        s->op = p[1] < 34 ? 0 : p[1] < 67 ? 1 : 2;
        s->sh = 13 - p[2] * 7 / 100;                    /* (16 bits: 3 .. 10 kept) */
        s->tone = 1400 + p[3] * (30000 - 1400) / 100;
        s->key = p_key(p);
        s->have = 1;
    }
    back = s->back, op = s->op, sh = s->sh, tone = s->tone;
    for (i = 0; i < CTL; i++) {
        int32_t a = clampi(x[i] * 2, -32767, 32767), b = s->line[(s->w - (uint32_t)back) & (XOR_N - 1u)], qa, qb, r, y;
        s->line[s->w] = (int16_t)a;
        s->w = (s->w + 1u) & (XOR_N - 1u);
        qa = xor_q(a, sh);
        qb = xor_q(b, sh);
        r = op == 0 ? qa ^ qb : op == 1 ? qa & qb : qa | qb;
        y = r * (1 << (sh - 1));                        /* (back to the sample, halved) */
        {   /* (the lean off: y = x - x1 + R y1, towards zero) */
            int32_t yq = y - s->dx + mulz15(s->dy, 32500);
            s->dx = y;
            s->dy = yq;
            y = yq;
        }
        s->lp += mulz15(y - s->lp, tone);
        x[i] = s->lp;
    }
}

/* --------------------------------------------------------------- DUST --- */
/* hiss and crackle that live where the sound moves (after Airwindows' TapeDust): noise scaled by how fast the sound is
 * changing, followed a little. p0 the grain (hiss .. crackle, the crackle dense: ~600 grains a second, never a
 * rhythm), p1 the amount (x 1 .. x 8), p2 the dust's tone (300 Hz .. 16 kHz), p3 the sound under it (0 .. 1).
 * Nothing moving, nothing in it */
typedef struct { int32_t prev, env, nlp, seeded; rnd_t r; uint32_t key, have; int32_t grain, amt, tone, dry; } m_dst_t;
static void m_dst(const uint8_t *p, m_dst_t *s, int32_t *x)
{
    uint32_t i;
    int32_t grain, amt, tone, dry;
    if (!s->seeded) {
        rnd_seed(&s->r, 0x445354u ^ fx_place(s));
        s->seeded = 1;
    }
    if (!s->have || s->key != p_key(p)) {
        s->grain = p[0] * (Q15 / 100);
        s->amt = 4096 + p[1] * (7 * 4096) / 100;
        s->tone = 1400 + p[2] * (30000 - 1400) / 100;
        s->dry = p[3] * (Q15 / 100);
        s->key = p_key(p);
        s->have = 1;
    }
    grain = s->grain, amt = s->amt, tone = s->tone, dry = s->dry;
    for (i = 0; i < CTL; i++) {
        int32_t sl = absi(x[i] - s->prev) * 4, n = rnd_bi(&s->r), hiss, crack = 0;
        s->prev = x[i];
        if (sl > s->env)                                /* (the slope followed: fast up, ~20 ms down, to 0) */
            s->env += (sl - s->env) >> 3;
        else
            s->env -= (s->env >> 8) + (s->env > 0);
        hiss = mulq15(n, mini(s->env, 2 * Q15));
        if (rnd_q15(&s->r) < 446)                       /* (~600 a second) */
            crack = hiss * 4;
        hiss = mulq15(hiss, Q15 - grain) + mulq15(crack, grain);
        s->nlp += mulz15(clampi(mulq12(hiss, amt), -2 * Q15, 2 * Q15) - s->nlp, tone);
        x[i] = mulq15(x[i], dry) + s->nlp;
    }
}

/* ============================================================== build 45 ===
 * Three that follow what goes in more than their own values: the same values on a drone, a cloud and a hiss give three
 * different things. None sounding with nothing in, none bending a pitch */

/* ------------------------------------------------------------ WAVESET --- */
/* Trevor Wishart's waveset distortion, in real time (after the Composers' Desktop Project's distort programs, LGPL-2.1,
 * and Csound's waveset: the idea): the sound cut at its upward zero crossings into wavesets -- its own pseudo-cycles,
 * as long as the sound makes them -- and played back rearranged, a GROUP at a time (p1: 1 .. 8 wavesets). MODE (p0):
 * REPEAT (each group 2 .. 6 times, the ones after it skipped: it keeps its place), REVERSE (AMOUNT of the groups
 * backwards), SHUFFLE (each group from up to 2 .. 12 ahead), OMIT (AMOUNT of the groups left out, never two in a
 * row). AMOUNT (p2); SHORTEST (p3: 0.1 .. 3 ms -- crossings closer than that merged, so a hiss is not cut to dust).
 * A waveset is cut at 5.8 ms at most and a repeat lasts 66 ms at most: grain, never a rhythm. Each piece starts and
 * ends at a crossing, faded over its first and last 8 samples where it was cut short. Nothing in, nothing out */
#define WST_N 4096u              /* 93 ms */
#define WST_Q 64u                /* crossings kept */
#define WST_MAXW 256u            /* the longest waveset (5.8 ms) */
#define WST_REP 2910u            /* the longest repeat (66 ms) */
enum { WM_REPEAT, WM_REVERSE, WM_SHUFFLE, WM_OMIT };
typedef struct {
    int16_t line[WST_N];
    uint32_t cross[WST_Q];
    uint32_t w, qw, qr, last;    /* samples written; crossings written; the next waveset to play; the last cut */
    uint32_t pos, left, len, seg;   /* the piece playing: where, samples left, its length; where it starts (repeats) */
    int32_t dir, gain, rep, skip, omitted, prev, dx, dy;
    uint32_t key, have, seeded;
    int32_t mode, grp, amt, minw;
    rnd_t r;
} m_wst_t;
static void wst_pick(m_wst_t *s)
{
    uint32_t avail = s->qw - s->qr - 1u, g, a, b, d0;
    s->left = 0;
    if (s->rep > 0) {                                /* (a repeat: the same piece again) */
        s->rep--;
        s->pos = s->seg;
        s->left = s->len;
        if (!s->rep) {
            s->qr += s->skip;
            if (s->qw - s->qr < 2u)
                s->qr = s->qw - 2u;
        }
        return;
    }
    if (s->qw < 2u || s->qw - s->qr < 2u)
        return;                                      /* (no waveset whole yet: a moment of nothing) */
    if (avail > 24u)                                 /* (fallen behind: to the newest) */
        s->qr = s->qw - 3u, avail = 2u;
    g = (uint32_t)s->grp < avail ? (uint32_t)s->grp : avail;
    a = s->cross[s->qr % WST_Q];
    b = s->cross[(s->qr + g) % WST_Q];
    s->gain = Q15;
    s->dir = 1;
    if (s->mode == WM_SHUFFLE && avail > g) {        /* (a group from further on) */
        uint32_t j = (uint32_t)rnd_below(&s->r, mini(s->amt, (int32_t)(avail - g + 1u)));
        a = s->cross[(s->qr + j) % WST_Q];
        b = s->cross[(s->qr + j + g) % WST_Q];
    }
    d0 = s->w - a;
    if (b - a > WST_N / 2u || d0 >= WST_N - 64u) {   /* (out of the line's reach: to the newest) */
        s->qr = s->qw - 2u;
        return;
    }
    s->seg = s->pos = a;
    s->len = s->left = b - a;
    if (s->mode == WM_REPEAT) {                      /* (as often as AMOUNT says, within 66 ms and the line) */
        int32_t r = s->amt, room = (int32_t)((WST_N - 64u - d0) / s->len) + 1;
        r = mini(r, mini((int32_t)(WST_REP / s->len), room));
        if (r >= 2) {
            s->rep = r - 1;
            s->skip = (int32_t)(g * (uint32_t)r);
            return;
        }
    } else if (s->mode == WM_REVERSE && rnd_below(&s->r, 100) < s->amt) {
        s->pos = b - 1u;
        s->dir = -1;
    } else if (s->mode == WM_OMIT) {
        s->omitted = !s->omitted && rnd_below(&s->r, 100) < s->amt;
        s->gain = s->omitted ? 0 : Q15;
    }
    s->qr += g;
}
static void m_wst(const uint8_t *p, m_wst_t *s, int32_t *x)
{
    uint32_t i;
    if (!s->seeded) {
        rnd_seed(&s->r, 0x575354u ^ fx_place(s));
        s->seeded = 1;
    }
    if (!s->have || s->key != p_key(p)) {
        s->mode = mini(p[0] / 25, 3);
        s->grp = 1 + p[1] * 7 / 100;
        s->amt = s->mode == WM_REPEAT ? 2 + p[2] * 4 / 100 : s->mode == WM_SHUFFLE ? 2 + p[2] * 10 / 100
               : s->mode == WM_OMIT ? 15 + p[2] * 51 / 100 : 30 + p[2] * 70 / 100;   /* (repeats; reach; % omitted;
                                                                                         * % reversed) */
        s->minw = 4 + p[3] * 128 / 100;
        s->key = p_key(p);
        s->have = 1;
    }
    for (i = 0; i < CTL; i++) {
        int32_t v, y = 0;
        {   /* (the lean off, ~8 Hz: the crossings are the sound's, not its offset's) */
            int32_t vq = x[i] - s->dx + mulz15(s->dy, 32730);
            s->dx = x[i];
            s->dy = vq;
            v = clampi(vq, -32767, 32767);
        }
        s->line[s->w & (WST_N - 1u)] = (int16_t)v;
        if ((s->prev < 0 && v >= 0 && s->w - s->last >= (uint32_t)s->minw) || s->w - s->last >= WST_MAXW) {
            s->cross[s->qw % WST_Q] = s->w;
            s->qw++;
            s->last = s->w;
            if (s->qw - s->qr > WST_Q - 4u)
                s->qr = s->qw - 3u;
        }
        s->prev = v;
        s->w++;
        if (!s->left)
            wst_pick(s);
        if (s->left) {
            uint32_t k = s->len - s->left, e = mini(mini((int32_t)k, (int32_t)s->left - 1), 8);
            y = s->line[s->pos & (WST_N - 1u)];
            if (e < 8u)                                  /* (the ends: 8 samples in, 8 out) */
                y = y * (int32_t)e / 8;
            y = mulq15(y, s->gain);
            s->pos += (uint32_t)s->dir;
            s->left--;
        }
        x[i] = y;
    }
}

/* --------------------------------------------------------------- XMOD --- */
/* the sound against itself a moment before (after Mutable Instruments' Warps, MIT: its cross-modulation, the idea):
 * the copy DELAY back (p2: 1 .. 40 ms, a new delay cross-faded over a block), the two combined by ALGO (p0): FOLD
 * (their sum, AMOUNT x 1 .. x 6, folded back into the window), RING (a diode ring: |a + b| - |a - b|, the smaller of
 * the two with their signs multiplied -- AMOUNT from it to the square law, levelled by the sound's own), COMPARE
 * (Warps' comparator: the lower of the two, the copy while the sound is up, whichever is larger, the larger's size
 * or the other's negative -- AMOUNT through the four). Which partials meet which depends on the sound and the delay:
 * the same values never come out the same twice. TONE (p3) a low-pass after; the lean taken off */
#define XMD_N 2048u
typedef struct { int16_t line[XMD_N]; uint32_t w, key, have; int32_t d, d_old, fade, algo, amt, tone, env, inv, dx, dy, lp; } m_xmd_t;
AINL int32_t xmd_fold(int32_t v) { int32_t t = (v + 16384) & 0xFFFF; return t < 32768 ? (t - 16384) * 2 : (49151 - t) * 2; }
static void m_xmd(const uint8_t *p, m_xmd_t *s, int32_t *x)
{
    uint32_t i;
    int32_t pk = 0;
    if (!s->have || s->key != p_key(p)) {
        int32_t d = 44 + p[2] * (1764 - 44) / 100;
        s->algo = mini(p[0] / 34, 2);
        s->amt = p[1];
        s->tone = 1400 + p[3] * (30000 - 1400) / 100;
        if (!s->have)
            s->d_old = d;
        else if (d != s->d) {
            s->d_old = s->d;
            s->fade = CTL;
        }
        s->d = d;
        s->key = p_key(p);
        s->have = 1;
    }
    for (i = 0; i < CTL; i++)                           /* (the sound's level, for the square law) */
        pk = maxi(pk, absi(x[i]));
    s->env = pk > s->env ? pk : s->env - (s->env >> 4);
    s->inv = (int32_t)(((uint32_t)Q15 << 15) / (uint32_t)maxi(s->env, 1500));
    for (i = 0; i < CTL; i++) {
        int32_t a = clampi(x[i], -32767, 32767), b, y;
        s->line[s->w & (XMD_N - 1u)] = (int16_t)a;
        b = s->line[(s->w - (uint32_t)s->d) & (XMD_N - 1u)];
        if (s->fade > 0) {                              /* (a new delay: the old copy out, the new in, over a block) */
            int32_t o = s->line[(s->w - (uint32_t)s->d_old) & (XMD_N - 1u)];
            b = o + (b - o) * (int32_t)(CTL - (uint32_t)s->fade) / (int32_t)CTL;
            s->fade--;
        }
        s->w++;
        if (s->algo == 0)
            y = xmd_fold(((a + b) * (4096 + s->amt * 205)) >> 13);
        else if (s->algo == 1) {
            int32_t dio = (absi(a + b) - absi(a - b)) / 2, sq = mulq15(mulq15(a, b), s->inv);
            y = dio + (sq - dio) * s->amt / 100;
        } else {
            int32_t seq[4], q = s->amt * 3, k = mini(q / 100, 2), f = q - k * 100;
            seq[0] = a < b ? a : b;
            seq[1] = a > 1638 ? b : a;
            seq[2] = absi(a) > absi(b) ? a : b;
            seq[3] = absi(a) > absi(b) ? absi(a) : -absi(b);
            y = seq[k] + (seq[k + 1] - seq[k]) * f / 100;
        }
        {   /* (the lean off) */
            int32_t yq = y - s->dx + mulz15(s->dy, 32600);
            s->dx = y;
            s->dy = yq;
            y = yq;
        }
        s->lp += mulz15(y - s->lp, s->tone);
        x[i] = s->lp;
    }
}

/* -------------------------------------------------------------- CHAOS --- */
/* a double-well oscillator (Duffing's: x'' = -d x' + x - x^3 + g in) pushed by the sound: lightly (DRIVE, p0, low) it
 * rings in one well, a resonance that colours; harder, it period-doubles, rough; hardest, it hops between the wells,
 * chaotic -- and which, for how long, the sound decides. Its push levelled by the sound's own level down to -24 dB
 * (quieter, quieter: lighter). DAMP (p1: 0.04 .. 0.9), SCALE (p2: the wells' own frequency, 60 .. 800 Hz -- fixed while
 * it sounds, nothing glides), TONE (p3) a low-pass after. Semi-implicit Euler, two steps a sample, Q24. At rest it sits
 * in a well: a level, taken off (~10 Hz). Its output is brought to the sound's level (x 1/4 .. x 512): lightly pushed,
 * it is heard as much as when it hops -- the push changes what it does, not how loud. With nothing in, nothing out */
#define CHS_ONE (1 << 24)
typedef struct { int32_t x, v, env, gate, dx, dy, lp, oenv; uint32_t key, have; int32_t g, d, h, tone, inv; } m_chs_t;
static void m_chs(const uint8_t *p, m_chs_t *s, int32_t *x)
{
    uint32_t i, k;
    int32_t pk = 0, opk = 0, g1, dg;
    if (!s->have || s->key != p_key(p)) {
        int32_t hz8 = (int32_t)(((uint32_t)60u * 256u * exp2_q16(p[2] * 244498 / 100)) >> 16);   /* (60 .. 800 Hz: 2^3.73) */
        s->g = (CHS_ONE / 100) * (25 + p[0] * 155 / 100);   /* (0.25 .. 1.8; 32-bit: no 64-bit division) */
        s->d = (CHS_ONE / 100) * (4 + p[1] * 86 / 100);
        s->h = (int32_t)(((int64_t)hz8 * 845) >> 8);   /* (h = f 2 pi / (2 sqrt 2 FS), Q24) */
        s->tone = 1400 + p[3] * (30000 - 1400) / 100;
        s->key = p_key(p);
        if (!s->have)
            s->x = CHS_ONE;                          /* (at rest, in a well) */
        s->have = 1;
    }
    for (i = 0; i < CTL; i++)
        pk = maxi(pk, absi(x[i]));
    s->env = pk > s->env ? pk : s->env - (s->env >> 4) - (s->env > 0);
    s->inv = (int32_t)(((uint32_t)Q15 << 15) / (uint32_t)maxi(s->env, 2000));
    {   /* (out at the sound's level, as the last block measured it: a gain, Q10, x 1/4 .. x 512; none in, none out) */
        uint32_t ec = (uint32_t)mini(s->env, 65535);
        int32_t lvl = (int32_t)((ec << 10) / (uint32_t)maxi((s->oenv + 32) >> 6, 1));   /* (its output 64 times finer) */
        g1 = mulq15(clampi(lvl, 256, 524288), (int32_t)((ec << 15) / (ec + 400u)));
    }
    dg = (g1 - s->gate) / (int32_t)CTL;
    for (i = 0; i < CTL; i++) {
        int32_t u = clampi((int32_t)(((int64_t)x[i] * s->inv) >> 15), -2 * Q15, 2 * Q15), y;
        int32_t f = (int32_t)(((int64_t)s->g * u) >> 15);
        for (k = 0; k < 2u; k++) {
            int32_t x3 = (int32_t)(((((int64_t)s->x * s->x) >> 24) * s->x) >> 24);
            int32_t a = -(int32_t)(((int64_t)s->d * s->v) >> 24) + s->x - x3 + f;
            s->v = clampi(s->v + (int32_t)(((int64_t)s->h * a) >> 24), -8 * CHS_ONE, 8 * CHS_ONE);
            s->x = clampi(s->x + (int32_t)(((int64_t)s->h * s->v) >> 24), -3 * CHS_ONE, 3 * CHS_ONE);
        }
        y = s->x >> 4;                               /* (1.0: 2^20 -- 64 times the sample's: a small motion kept fine) */
        {   /* (the well's level off, ~10 Hz) */
            int32_t yq = y - s->dx + mulz15(s->dy, 32721);
            s->dx = y;
            s->dy = yq;
            y = yq;
        }
        s->lp += mulz15(y - s->lp, s->tone);
        opk = maxi(opk, absi(s->lp));
        s->gate += dg;
        x[i] = (int32_t)(((int64_t)s->lp * s->gate) >> 16);
    }
    s->oenv = opk > s->oenv ? opk : s->oenv - (s->oenv >> 4);
    s->gate = g1;
}

/* the memory each effect takes, rounded up to 8 bytes */
static uint32_t rm_size(uint32_t k)
{
    uint32_t n;
    switch (k) {
    case RM_FLT: n = sizeof(m_flt_t); break;
    case RM_FLD: n = sizeof(m_fld_t); break;
    case RM_MU: n = sizeof(m_mu_t); break;
    case RM_SHF: n = sizeof(m_shf_t); break;
    case RM_MDL: n = sizeof(m_mdl_t); break;
    case RM_STR: n = sizeof(m_str_t); break;
    case RM_VOW: n = sizeof(m_vow_t); break;
    case RM_TAP: n = sizeof(m_tap_t); break;
    case RM_RVS: n = sizeof(m_rvs_t); break;
    case RM_SPR: n = sizeof(m_spr_t); break;
    case RM_GLT: n = sizeof(m_glt_t); break;
    case RM_CLD: n = sizeof(m_cld_t); break;
    case RM_SCR: n = sizeof(m_scr_t); break;
    case RM_RNG: n = sizeof(m_rng_t); break;
    case RM_STV: n = sizeof(m_stv_t); break;
    case RM_CHB: n = sizeof(m_chb_t); break;
    case RM_BNK: n = sizeof(m_bnk_t); break;
    case RM_SMR: n = sizeof(m_smr_t); break;
    case RM_DIF: n = sizeof(m_dif_t); break;
    case RM_SLW: n = sizeof(m_slw_t); break;
    case RM_XOR: n = sizeof(m_xor_t); break;
    case RM_DST: n = sizeof(m_dst_t); break;
    case RM_WST: n = sizeof(m_wst_t); break;
    case RM_XMD: n = sizeof(m_xmd_t); break;
    case RM_CHS: n = sizeof(m_chs_t); break;
    default: n = 8u; break;                     /* (DELAY, REVERB: the master's from build 30, nothing here) */
    }
    return (n + 7u) & ~7u;
}
