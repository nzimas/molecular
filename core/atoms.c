/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR: the atoms of its own (0.62), beside Norilsk's 25. Each works on a mono block in place, with four values
 * 0 .. 100 and a state in the atoms' arena (cleared before it sounds), like Norilsk's (fx.c runs them; its guard rides
 * their output). Chosen from Vascular's processors, open-source DSP and the electroacoustic studio
 * (notes/EFFECTS_SHOPPING.md in the research tree); written here from the published ideas:
 *
 *   SPIRAL   a pitch shifter inside a feedback delay: every repeat transposed again, staircases climbing or falling
 *            (after the Eventide H910, GRM PitchAccum, Shepard-Risset; Vascular's spiral)
 *   SOURCE   LPC: the sound's body (its spectral envelope) kept, its source replaced by noise or a buzz on the root
 *            (after IRCAM's source-filter processing, Lansky, Dodge)
 *   ENVEL    the sound's shape changed, not its colour: attacks taken away (Schaeffer's cloche coupee) or sharpened,
 *            the envelope inverted (decays become swells)
 *   LPG      a Buchla low-pass gate struck by the sound's onsets, a source of uncertainty drawing each strike's decay
 *            and colour (after the Buchla 292 and 266)
 *   BARBER   four taps of a short delay sweeping in staggered ramps, each faded in and out: a comb rising or falling
 *            for ever (a Shepard-Risset flanger)
 *   STICK    stick-slip friction: the sound drags a sprung mass that sticks and slips (after Dan Stowell's Friction)
 *   MORPHO   six bands, each delayed and fed back on its own, the low ones stored decimated: attacks fanned out into
 *            chirps (after Poullin's Morphophone, GRM 1953; Vascular's disperse)
 *   DISINTEGRATE  short read heads in series, pushed back by the sound's own level: smeared and granulated by how hard
 *            it is hit (after Airwindows' Disintegrate, as Vascular uses it)
 *
 * The second wave (0.63): REED (the sound blowing a reed bore), NLF (Dobson's nonlinear filter), SUBHARM (undertones),
 * SPARK (sparks fired at the sound's zero crossings), CONTRAST and BLUR (a 12-band bank: spectral contrast and trace; the
 * sound's slowly moving spectral shadow), DOPPLER (a source on an orbit), GSM (the phone codec, core/gsm.c), GENDY
 * (Xenakis' stochastic redrawing), INVERT (the spectrum mirrored), SQUELCH (undersampled and rebuilt).
 *
 * Integer only, Q15; a divide only once a block (or a frame). */

enum { NX_SPIRAL, NX_SOURCE, NX_ENVEL, NX_LPG, NX_BARBER, NX_STICK, NX_MORPHO, NX_DISINT,
       NX_REED, NX_NLF, NX_SUBHARM, NX_SPARK, NX_CONTRAST, NX_BLUR, NX_DOPPLER, NX_GSM, NX_GENDY, NX_INVERT, NX_SQUELCH,
       NX_N };
static const char *const NX_NAME[NX_N] = {"SPIRAL", "SOURCE", "ENVEL", "LPG", "BARBER", "STICK", "MORPHO",
                                          "DISINTEGRATE", "REED", "NLF", "SUBHARM", "SPARK", "CONTRAST", "BLUR",
                                          "DOPPLER", "GSM", "GENDY", "INVERT", "SQUELCH"};
/* the bounds a draw keeps each value in, so that every atom is heard */
static const uint8_t NX_LO[NX_N][4] = {
    {0, 20, 10, 0},       /* SPIRAL: interval, feedback, time, splice */
    {0, 10, 0, 30},       /* SOURCE: excitation, buzz pitch, hold, order */
    {5, 10, 0, 25},       /* ENVEL: swell .. sharpen, time, invert, floor */
    {20, 10, 10, 0},      /* LPG: sensitivity, decay, colour, uncertainty */
    {0, 10, 15, 30},      /* BARBER: rate, range, feedback, depth */
    {15, 10, 10, 30},     /* STICK: friction, spring, damping, drive */
    {30, 0, 0, 0},        /* MORPHO: time, tilt, feedback, scatter */
    {20, 20, 10, 10},     /* DISINTEGRATE: drive, length, layers, smoothing */
    {10, 30, 20, 20},     /* REED: pitch, pressure, brightness, damping */
    {20, 0, 10, 0},       /* NLF: feedback, resonance, nonlinearity, lag */
    {0, 0, 0, 20},        /* SUBHARM: divisor A, divisor B, mix, tone */
    {0, 10, 0, 0},        /* SPARK: pitch, decay, grit, threshold */
    {0, 0, 10, 10},       /* CONTRAST: contrast, trace, resolution, speed */
    {10, 0, 0, 0},        /* BLUR: blur, freeze, excitation, drift */
    {10, 20, 20, 0},      /* DOPPLER: speed, size, pass, air */
    {30, 30, 20, 0},      /* GSM: rate, level, tone, stutter */
    {0, 10, 0, 0},        /* GENDY: breakpoints, amplitude walk, duration walk, anchor */
    {0, 20, 0, 30},       /* INVERT: mirror, shift, resonance, bandwidth */
    {15, 0, 0, 0},        /* SQUELCH: rate, meltdown, squelch, follow */
};
static const uint8_t NX_HI[NX_N][4] = {
    {100, 85, 100, 100},
    {100, 90, 100, 100},
    {95, 90, 85, 75},
    {90, 90, 100, 100},
    {100, 100, 85, 100},
    {90, 90, 80, 100},
    {100, 100, 80, 100},
    {100, 100, 100, 90},
    {90, 95, 90, 90},
    {95, 100, 90, 100},
    {100, 100, 100, 100},
    {100, 90, 100, 85},
    {100, 85, 80, 90},
    {95, 95, 100, 100},
    {90, 100, 100, 100},
    {100, 90, 100, 60},
    {100, 85, 85, 80},
    {100, 80, 70, 100},
    {85, 100, 90, 100},
};

/* a one-pole's share a sample for a time constant of n samples, Q15 (once a block: a divide) */
AINL int32_t nx_share(int32_t n) { return n <= 1 ? Q15 : Q15 / n; }
/* level in, level out: a soft ceiling under full scale */
AINL int32_t nx_soft(int32_t x) { return mulq15(tanh_q15(x), LV(0.92)); }
/* a fractional read from an int16 ring of len (a power of two), d samples Q16 behind w */
AINL int32_t nx_read(const int16_t *buf, uint32_t mask, uint32_t w, uint32_t d16)
{
    uint32_t di = d16 >> 16, i = (w - di) & mask, j = (i - 1u) & mask;
    int32_t f = (int32_t)((d16 >> 2) & 0x3FFFu);                   /* (14 bits: the product fits 32) */
    return buf[i] + (((buf[j] - buf[i]) * f) >> 14);
}
AINL int16_t nx_s16(int32_t x) { return (int16_t)clampi(x, -32767, 32767); }
/* 1 once nothing has come in for 300 blocks (0.22 s): a filter's last rounding, held for ever at a high Q or circling
 * in a cascade, let go then (Norilsk's FILTER does the same) */
AINL int nx_quiet(uint32_t *still, const int32_t *x)
{
    uint32_t i, any = 0;
    for (i = 0; i < CTL; i++)
        any |= (uint32_t)x[i];
    *still = any ? 0u : *still + 1u;
    return *still >= 300u;
}

/* ------------------------------------------------------------- SPIRAL --- */
/* One ring, two read heads half a window apart sweeping its last W samples at (1 - ratio) a sample, behind a delay T:
 * the shifted sound, each head faded in and out (smooth: a triangle; SPLICE towards a hard switch, the H910's glitch,
 * and a jitter of the window). Written back through a high-pass, a low-pass and a soft clip, so each repeat is
 * shifted again. INTERVAL -12 .. +12 semitones, FEEDBACK 0 .. 0.95, TIME 20 .. 110 ms, SPLICE. */
#define SP_LEN 8192u
#define SP_WIN 1024
typedef struct {
    int16_t buf[SP_LEN];
    uint32_t w, key, have, ph, inc;      /* the heads' phase across the window, 2^32 a window; its step a sample */
    int32_t fb, t16, sharp, jit, hp, lp, lpk, win;
    rnd_t r;
} nx_spiral_t;
static void nx_spiral(const uint8_t *p, nx_spiral_t *s, int32_t *x)
{
    uint32_t i;
    if (!s->have || s->key != p_key(p)) {
        int32_t semi16 = (p[0] - 50) * (24 * 65536 / 100) / 12;      /* octaves, Q16 */
        int32_t ratio16 = (int32_t)exp2_q16(semi16);                  /* Q16 */
        s->inc = (uint32_t)((int64_t)(Q16 - ratio16) * (int64_t)(0x100000000LL / SP_WIN) >> 16);
        s->fb = p[1] * (LV(0.95) / 100);
        s->t16 = (int32_t)((882 + p[2] * (4851 - 882) / 100) << 16);  /* 20 .. 110 ms */
        s->sharp = 1 + p[3] * p[3] / 600;                             /* 1 (a triangle) .. 17 (all but a switch) */
        s->jit = p[3] > 60 ? (p[3] - 60) * 12 : 0;
        s->lpk = nx_share(2);
        if (!s->have) {
            rnd_seed(&s->r, 0x5A1Au);
            s->win = SP_WIN;
        }
        s->key = p_key(p);
        s->have = 1;
    }
    for (i = 0; i < CTL; i++) {
        uint32_t ph2 = s->ph + 0x80000000u, a = s->ph >> 16, b = ph2 >> 16;
        int32_t t1 = (int32_t)(a < 32768u ? a : 65535u - a) * 2, t2 = (int32_t)(b < 32768u ? b : 65535u - b) * 2;
        int32_t g1, g2, y, wv;
        /* each head's fade: its triangle, made steeper by SPLICE; the two always add up to one */
        g1 = clampi(Q15 / 2 + (t1 / 2 - Q15 / 2) * s->sharp, 0, Q15);
        g2 = Q15 - g1;
        (void)t2;
        y = mulq15(nx_read(s->buf, SP_LEN - 1u, s->w, (uint32_t)s->t16 + (uint32_t)(((uint64_t)a * (uint32_t)s->win) )), g1)
          + mulq15(nx_read(s->buf, SP_LEN - 1u, s->w, (uint32_t)s->t16 + (uint32_t)(((uint64_t)b * (uint32_t)s->win) )), g2);
        s->ph += s->inc;
        if (s->jit && (s->ph >> 16) < 4u && s->inc)                  /* (a window's turn: its length jittered) */
            s->win = SP_WIN - (int32_t)rnd_below(&s->r, s->jit);
        /* the loop: high-pass (no DC builds up), low-pass (repeats darken), soft clip */
        s->hp += ((y << 8) - s->hp) >> 9;                            /* (8 bits more: no DC left by the rounding) */
        wv = y - (s->hp >> 8);
        s->lp += mulq15(wv - s->lp, LV(0.6));
        wv = nx_soft(x[i] + mulq15(s->lp, s->fb));
        s->w = (s->w + 1u) & (SP_LEN - 1u);
        s->buf[s->w] = nx_s16(wv);
        x[i] = nx_soft((y - (s->hp >> 8)) + ((y - (s->hp >> 8)) >> 1));
    }
}

/* ------------------------------------------------------------- SOURCE --- */
/* LPC: every 128 samples the last 256, windowed, give an autocorrelation; Levinson-Durbin its reflection coefficients
 * (ORDER 4 .. 16), held more or less (HOLD). The input through the inverse lattice is its residual; the excitation is
 * that residual, white noise or a sawtooth on the generator's root (BUZZ: -24 .. +12 semitones), EXCITATION going
 * residual -> noise -> buzz; the synthesis lattice puts the body back. The noise and the buzz at the residual's level. */
#define SR_N 256
#define SR_HOP 128
#define SR_P 16
typedef struct {
    int16_t hist[SR_N];
    uint32_t pos, n, key, have, ph, inc;
    int32_t k[SR_P], kw[SR_P];          /* reflection coefficients Q15: running, wanted */
    int32_t ba[SR_P + 1], bs[SR_P + 1]; /* the analysis and synthesis lattices' delays */
    int32_t rms, rms_w;                 /* the residual's level (its estimate at the last frame) */
    int32_t mix, hold, order;
    rnd_t r;
} nx_source_t;
static void nx_source_frame(nx_source_t *s)
{
    int64_t R[SR_P + 1], en;
    static int32_t w[SR_N];                          /* (build 69: static, not 1 KB of the device's 4 KB stack) */
    int32_t a[SR_P + 1], t[SR_P + 1], r30[SR_P + 1];
    int32_t e30, i, j, p = s->order, sh = 0;
    for (i = 0; i < SR_N; i++) {                     /* the frame, oldest first, through a Hann window */
        int32_t h = (Q15 - sin_ph((uint32_t)i * (0xFFFFFFFFu / SR_N) + (1u << 30))) >> 1;
        w[i] = mulq15(s->hist[(s->pos + (uint32_t)i) & (SR_N - 1)], h);
    }
    for (j = 0; j <= p; j++) {
        int64_t acc = 0;
        for (i = j; i < SR_N; i++)
            acc += (int64_t)w[i] * w[i - j];
        R[j] = acc;
    }
    if (R[0] < (int64_t)SR_N * 2500) {               /* (near silence: the body as it was, the level gone) */
        s->rms_w = 0;
        return;
    }
    while ((R[0] >> sh) > (1LL << 30))
        sh++;
    for (j = 0; j <= p; j++)
        r30[j] = (int32_t)(R[j] >> sh);
    r30[0] += r30[0] >> 12;                          /* (a little white noise: the recursion well behaved) */
    e30 = r30[0];
    for (j = 0; j <= SR_P; j++)
        a[j] = 0;
    for (i = 1; i <= p; i++) {                       /* Levinson-Durbin: a Q30, k Q30, the error in R's units */
        int64_t acc = (int64_t)r30[i] << 30, kk;
        for (j = 1; j < i; j++)
            acc += (int64_t)a[j] * r30[i - j];
        kk = -acc / e30;
        kk = kk > 1063004405 ? 1063004405 : kk < -1063004405 ? -1063004405 : kk;   /* |k| < 0.99 */
        for (j = 1; j < i; j++)
            t[j] = a[j] + (int32_t)((kk * a[i - j]) >> 30);
        for (j = 1; j < i; j++)
            a[j] = t[j];
        a[i] = (int32_t)kk;
        s->kw[i - 1] = (int32_t)(kk >> 15);          /* Q15 */
        e30 = (int32_t)(((int64_t)e30 * ((1LL << 30) - ((kk * kk) >> 30))) >> 30);
        if (e30 < 16)
            e30 = 16;
    }
    for (i = p; i < SR_P; i++)
        s->kw[i] = 0;
    /* the residual's level a sample: the error's energy over the frame, the Hann window's power (3/8) undone */
    en = ((int64_t)e30 << sh) / SR_N * 8 / 3;
    s->rms_w = (int32_t)isqrt32((uint32_t)(en > 0x7FFFFFFF ? 0x7FFFFFFF : en));
}
static void nx_source(const uint8_t *p, nx_source_t *s, int32_t *x)
{
    uint32_t i;
    int32_t j, glide;
    if (!s->have || s->key != (p_key(p) ^ (uint32_t)rack_root)) {
        int32_t note = rack_root % 12 + 36 + (p[1] - 66) * 36 / 100 + 12;   /* about the root, -24 .. +12 */
        s->inc = hz8_inc(note_hz8(clampi(note, 12, 96), 0));
        s->mix = p[0];
        s->hold = p[2];
        s->order = 4 + p[3] * 12 / 100;
        if (!s->have)
            rnd_seed(&s->r, 0x5C0Eu);
        s->key = p_key(p) ^ (uint32_t)rack_root;
        s->have = 1;
    }
    glide = s->hold >= 98 ? 0 : (Q15 / 8) * (100 - s->hold) / 100 + 64;   /* the body follows, or is held */
    for (j = 0; j < SR_P; j++)
        s->k[j] += mulq15(s->kw[j] - s->k[j], glide);
    s->rms += mulq15(s->rms_w - s->rms, Q15 / 4);
    for (i = 0; i < CTL; i++) {
        int32_t f, e, syn, res;
        s->hist[s->pos] = nx_s16(x[i]);
        s->pos = (s->pos + 1u) & (SR_N - 1);
        /* the analysis lattice: the input's residual */
        f = x[i];
        {
            int32_t b_prev = f;
            for (j = 0; j < s->order; j++) {
                int32_t bj = s->ba[j], fn = f + mulq15(s->k[j], bj), bn = bj + mulq15(s->k[j], f);
                s->ba[j] = b_prev;
                b_prev = bn;
                f = fn;
            }
        }
        res = f;
        /* the excitation: residual -> noise (0 .. 50) -> buzz (50 .. 100), the synthetic ones at the residual's level */
        {
            int32_t nz = mulq15(rnd_bi(&s->r), s->rms) * 7 / 4;      /* (uniform noise: rms 1/sqrt 3 of its peak) */
            int32_t bz = mulq15((int32_t)(s->ph >> 16) - 32768, s->rms);   /* (a saw's harmonics meet the body's peaks) */
            s->ph += s->inc;
            if (s->mix <= 50)
                e = res + (nz - res) * s->mix / 50;
            else
                e = nz + (bz - nz) * (s->mix - 50) / 50;
        }
        /* the synthesis lattice: the body put back */
        f = e;
        for (j = s->order - 1; j >= 0; j--) {
            f -= mulq15(s->k[j], s->bs[j]);
            s->bs[j + 1] = s->bs[j] + mulq15(s->k[j], f);
        }
        s->bs[0] = f;
        syn = clampi(f, -4 * Q15, 4 * Q15);
        x[i] = nx_soft(syn);
        if (++s->n >= SR_HOP) {
            s->n = 0;
            nx_source_frame(s);
        }
    }
}

/* -------------------------------------------------------------- ENVEL --- */
/* A fast follower (1 ms up, 5 ms down) and a slow one (TIME: 20 ms .. 2 s). The gain (slow / fast)^a: a > 0 (SHAPE
 * below 50) takes the attacks away, every onset swelling in over TIME; a < 0 sharpens them (up to +12 dB). INVERT: the
 * gain also (the loudest of late / slow), so decays rise and the loud falls back (up to +24 dB), gated under FLOOR. */
typedef struct {
    uint32_t key, have;
    int32_t ef, es, ref, g, a16, inv, floor;
    int32_t ks30, kref30;               /* the slow follower's and the loudest's shares a sample, Q30 */
    int64_t es12, ref12;                /* .. their states, 12 bits more */
} nx_envel_t;
static void nx_envel(const uint8_t *p, nx_envel_t *s, int32_t *x)
{
    uint32_t i;
    int32_t gi;
    if (!s->have || s->key != p_key(p)) {
        int32_t ms = (int32_t)(exp2_q16(p[1] * (65536 * 67 / 1000)) >> 16) * 20;   /* 20 ms .. ~2 s */
        s->ks30 = (1 << 30) / (ms * 44);
        s->kref30 = (1 << 30) / 88200;                               /* the loudest: forgotten over ~2 s */
        s->a16 = (50 - p[0]) * 65536 / 50;                           /* +1 .. -1, Q16 */
        s->inv = p[2] * Q15 / 100;
        s->floor = (int32_t)(exp2_q16((p[3] * 7 << 16) / 100) >> 16) + 8;   /* 9 .. 136 (about -70 .. -48 dB) */
        s->key = p_key(p);
        s->have = 1;
    }
    {   /* the inversion's gain, once a block: (the loudest of late / the slow level)^(2 INVERT), up to +36 dB, gated
         * under the floor -- so a decay comes back up as a swell */
        int32_t den = s->es > s->floor ? s->es : s->floor;
        int32_t lgi = log2_q16((uint32_t)(s->ref + 1)) - log2_q16((uint32_t)den);
        lgi = (int32_t)(((int64_t)clampi(lgi, 0, 8 << 16) * s->inv) >> 14);   /* x 2 INVERT */
        gi = (int32_t)(exp2_q16(mini(lgi, 6 << 16)) >> 4);                 /* Q12, up to +36 dB */
        if (s->es < s->floor)
            gi = gi * s->es / s->floor;
    }
    for (i = 0; i < CTL; i++) {
        int32_t v = absi(x[i]), g, lg;
        s->ef += v > s->ef ? (v - s->ef) * 3 / 4 : (v - s->ef) / 220;
        s->es12 += ((((int64_t)s->ef << 12) - s->es12) * s->ks30) >> 30;   /* (the fast one, smoothed: the same measure) */
        s->es = (int32_t)(s->es12 >> 12);
        if (s->es12 > s->ref12)
            s->ref12 = s->es12;
        else
            s->ref12 -= (s->ref12 * s->kref30 >> 30) + 1;
        if (s->ref12 < 0)
            s->ref12 = 0;
        s->ref = (int32_t)(s->ref12 >> 12);
        lg = log2_q16((uint32_t)(s->es + 1)) - log2_q16((uint32_t)(s->ef + 1));   /* log2 (slow / fast), Q16 */
        lg = (int32_t)(((int64_t)lg * s->a16) >> 16);
        g = (int32_t)(exp2_q16(clampi(lg, -(12 << 16), 2 << 16)) >> 4);           /* Q12 */
        if (s->inv)                                                  /* the envelope turned over */
            g = mulq12(g, gi);
        s->g += (g - s->g) >> 5;                                     /* (no zipper) */
        x[i] = nx_soft(mulq12(x[i], s->g));
    }
}

/* ---------------------------------------------------------------- LPG --- */
/* A fast and a slow follower; an onset when the fast one jumps over the slow one by SENSITIVITY's ratio (30 ms
 * apart at least). Each strike draws its own decay and colour (UNCERTAINTY); its control c jumps to 1 in ~2 ms and falls
 * like a vactrol, quick at the top, a long tail. c opens a low-pass (60 Hz .. 60 Hz x 2^(8 .. 9) x colour) and the
 * gain (c^1.5). Between strikes the gate also follows the level a little, low SENSITIVITY more (a photocell). */
typedef struct {
    svf_t f;
    svf_c c;
    uint32_t key, have, hold;
    int32_t ef, es, cv, cv_to, dec, dec_now, kdec, oct, oct_now, q, ratio, follow, unc;
    rnd_t r;
} nx_lpg_t;
static void nx_lpg(const uint8_t *p, nx_lpg_t *s, int32_t *x)
{
    uint32_t i;
    int32_t hz, cut;
    if (!s->have || s->key != p_key(p)) {
        s->ratio = 4096 + (100 - p[0]) * 40;                         /* the jump an onset needs, Q12: x 1.0 .. 2.0 */
        s->dec = 2205 + p[1] * p[1] * 13;                            /* 50 ms .. 3 s, as samples to fall by most */
        s->oct = (6 << 16) + p[2] * (3 * 65536 / 100);               /* the low-pass's reach, octaves over 60 Hz */
        s->q = 2896 + p[2] * 60;                                     /* Q 0.7 .. 2.2, Q12 */
        s->unc = p[3] * Q15 / 100;
        s->follow = (100 - p[0]) * Q15 / 250;
        s->kdec = (1 << 30) / (s->dec * 4);
        if (!s->have) {
            rnd_seed(&s->r, 0x1F6u);
            s->dec_now = s->dec;
            s->oct_now = s->oct;
            s->kdec = (1 << 30) / (s->dec_now * 4);
        }
        s->key = p_key(p);
        s->have = 1;
    }
    cut = 60 * (int32_t)(exp2_q16(mulq15(s->oct_now, s->cv)) >> 16);
    hz = clampi(cut, 40, 16000);
    svf_coef(&s->c, hz, s->q);
    for (i = 0; i < CTL; i++) {
        int32_t v = absi(x[i]), y, gain, fol;
        s->ef += v > s->ef ? (v - s->ef) >> 2 : (v - s->ef) >> 9;
        s->es += (s->ef - s->es) >> 11;                              /* (the fast one, smoothed: the same measure) */
        if (s->hold)
            s->hold--;
        else if (s->ef > 300 && mulq12(s->es, s->ratio) < s->ef) {   /* a strike: its own decay and colour */
            int32_t u1 = mulq15(rnd_bi(&s->r), s->unc), u2 = mulq15(rnd_bi(&s->r), s->unc);
            s->dec_now = (int32_t)(((int64_t)s->dec * exp2_q16(u1 * 3 / 2 * 2)) >> 16);
            s->oct_now = s->oct + u2 * 3 / 2 * 2;
            s->kdec = (1 << 30) / (s->dec_now * 4);               /* (a divide a strike) */
            s->cv_to = Q15;
            s->hold = 1323;
        }
        if (s->cv_to) {                                              /* the attack: ~2 ms */
            s->cv += (Q15 - s->cv) >> 6;
            if (s->cv > Q15 - 400)
                s->cv_to = 0;
        } else                                                       /* the fall: faster at the top */
            s->cv -= (int32_t)((((int64_t)s->cv * (Q15 + 3 * s->cv)) * s->kdec) >> 30) + (s->cv > 0);
        fol = mini(mulq15(s->es * 6, s->follow), Q15 / 2);
        if (s->cv < fol)
            s->cv += (fol - s->cv) >> 8;
        s->cv = clampi(s->cv, 0, Q15);
        y = svf_run(&s->f, &s->c, x[i], 1);
        gain = mulq15(s->cv, (s->cv + Q15) >> 1);                     /* ~ c^1.5 */
        x[i] = nx_soft(mulq15(y, gain) * 2);
    }
}

/* ------------------------------------------------------------- BARBER --- */
/* Four taps of a 23 ms ring sweep its first RANGE ms in staggered ramps, a quarter apart, each faded in and out
 * (sin^2: the four always add up to two), so the comb they make rises (RATE above 50) or falls (below) for ever;
 * at 50 it stands. FEEDBACK -0.85 .. +0.85 (50: none), DEPTH the comb against the sound. */
#define BB_LEN 1024u
typedef struct {
    int16_t buf[BB_LEN];
    uint32_t w, key, have, ph;
    int32_t inc, range16, fb, depth, last;
} nx_barber_t;
static void nx_barber(const uint8_t *p, nx_barber_t *s, int32_t *x)
{
    uint32_t i, k;
    if (!s->have || s->key != p_key(p)) {
        int32_t r = p[0] - 50, hz100 = r * r * 4 / (r < 0 ? -1 : 1);    /* the sweep's cycles a second x 100: +-100 */
        s->inc = (int32_t)((int64_t)hz100 * 973 / 1);                /* 2^32 / FS / 100 = 973.8 */
        s->range16 = (44 + p[1] * 9) << 16;                          /* 1 .. 10 ms */
        s->fb = (p[2] - 50) * (LV(0.85) / 50);
        s->depth = 8192 + p[3] * 245;                                /* 0.25 .. 1 */
        s->key = p_key(p);
        s->have = 1;
    }
    for (i = 0; i < CTL; i++) {
        int32_t sum = 0;
        for (k = 0; k < 4u; k++) {
            uint32_t ph = s->ph + k * 0x40000000u, d16 = (uint32_t)(((uint64_t)(ph >> 16) * (uint32_t)s->range16) >> 16) + 0x20000u;
            int32_t sn = sin_ph(ph >> 1), wgt = mulq15(sn, sn);     /* sin^2 over the ramp */
            sum += mulq15(nx_read(s->buf, BB_LEN - 1u, s->w, d16), wgt);
        }
        sum >>= 1;
        s->ph -= (uint32_t)s->inc;                                   /* (falling delay: rising notches) */
        s->w = (s->w + 1u) & (BB_LEN - 1u);
        s->buf[s->w] = nx_s16(nx_soft(x[i] + mulq15(sum, s->fb)));
        s->last = sum;
        x[i] = nx_soft((x[i] + mulq15(sum, s->depth)) * 3 / 4);
    }
}

/* -------------------------------------------------------------- STICK --- */
/* A mass on a spring rests on a belt; the belt's speed is the sound (and its level, a steady drag: DRIVE). Stuck, the
 * mass rides the belt while the spring's pull stays under the static friction (FRICTION); then it slips, pulled back
 * by the spring and damped (DAMPING), under the kinetic friction, until it rides again. Its position, without its DC,
 * is the sound: squeaks, creaks, bowed chatter. SPRING sets the slipping's pitch. State Q24. */
typedef struct {
    uint32_t key, have;
    int32_t pos, vel, stuck, k, d, mus, muk, drive, bias, env, dc, out_g;
} nx_stick_t;
static void nx_stick(const uint8_t *p, nx_stick_t *s, int32_t *x)
{
    uint32_t i;
    if (!s->have || s->key != p_key(p)) {
        int32_t hz = 80 * (int32_t)(exp2_q16(p[1] * (6 * 65536 / 100)) >> 16);   /* 80 Hz .. ~5 kHz */
        int32_t w = (int32_t)(((int64_t)hz * 9340) >> 16);           /* 2 pi f / FS, Q16 (2 pi / 44100 = 1.425e-4) */
        s->k = (int32_t)(((int64_t)w * w) >> 8);                     /* w^2, Q24 */
        s->d = (p[2] * p[2] + 40) * 40;                              /* damping a sample, Q24 */
        s->mus = (2 + p[0] * p[0] / 8) << 10;                        /* static friction, Q24 */
        s->muk = s->mus * 5 / 8;
        s->drive = 2 + p[3] / 6;                                     /* the belt: the sound x .. (Q15 -> Q24 >> 10) */
        s->bias = p[3] * 6;
        s->out_g = 4096 + (100 - p[1]) * 60;
        s->key = p_key(p);
        s->have = 1;
    }
    for (i = 0; i < CTL; i++) {
        int32_t vb, spring, y;
        s->env += (absi(x[i]) - s->env) >> 10;
        vb = (x[i] << 3) * s->drive / 8 + s->env * s->bias / 64;    /* the belt's speed, Q24 units a sample */
        spring = -(int32_t)(((int64_t)s->k * s->pos) >> 24) - (int32_t)(((int64_t)s->d * s->vel) >> 24);
        if (s->stuck) {
            if (absi(spring) <= s->mus) {
                s->vel = vb;
            } else
                s->stuck = 0;
        }
        if (!s->stuck) {
            int32_t rel = vb - s->vel, fr = rel > 0 ? s->muk : -s->muk, nv = s->vel + spring + fr;
            if ((vb - nv > 0) != (rel > 0) && absi(spring) <= s->mus) {   /* caught up with the belt: stuck again */
                nv = vb;
                s->stuck = 1;
            }
            s->vel = clampi(nv, -(1 << 22), 1 << 22);
        }
        s->pos = clampi(s->pos + s->vel, -(1 << 28), 1 << 28);
        y = s->pos >> 9;                                             /* Q24 -> Q15 */
        s->dc += ((y << 8) - s->dc) >> 8;                            /* (8 bits more: no DC left by the rounding) */
        x[i] = nx_soft(mulq12(y - (s->dc >> 8), s->out_g));
        if (!s->env && !x[i])                                        /* (silent and still: let go) */
            s->pos = s->dc = s->vel = 0;
    }
}

/* ------------------------------------------------------------- MORPHO --- */
/* Six bands by complementary low-passes at 345, 690, 1380, 2760 and 5520 Hz (they add back to the sound), each into
 * its own delay, stored at 1/16, 1/16, 1/8, 1/4, 1/2 and the full rate (its band the anti-alias), read back with
 * linear interpolation, fed back on its own. TIME how far the bands spread (up to 0.74 s), TILT which come late (0: the
 * lows; towards 100 the higher ones, as far as their lines reach: 280 ms for the middle, 12 ms for the top), FEEDBACK,
 * SCATTER each band's own random share. */
#define MO_B 6
static const uint16_t MO_LEN[MO_B] = {2048, 2048, 1536, 1024, 768, 512};
static const uint8_t MO_SH[MO_B] = {4, 4, 3, 2, 1, 0};   /* the decimation, as a shift */
static const int16_t MO_HZ[MO_B - 1] = {345, 690, 1380, 2760, 5520};
typedef struct {
    int16_t line[2048 + 2048 + 1536 + 1024 + 768 + 512];
    svf_t f[MO_B - 1];
    svf_c c[MO_B - 1];
    uint32_t key, have, w[MO_B], ph[MO_B];
    int32_t d16[MO_B], dw16[MO_B], acc[MO_B], prev[MO_B], fbv[MO_B], scat[MO_B], fb;
    rnd_t r;
} nx_morpho_t;
static void nx_morpho(const uint8_t *p, nx_morpho_t *s, int32_t *x)
{
    uint32_t i, b, off;
    if (!s->have || s->key != p_key(p)) {
        if (!s->have) {
            rnd_seed(&s->r, 0x3090u);
            for (b = 0; b < MO_B - 1; b++)
                svf_coef(&s->c[b], MO_HZ[b], 2896);
            for (b = 0; b < MO_B; b++)
                s->scat[b] = rnd_q15(&s->r);
        }
        for (b = 0; b < MO_B; b++) {   /* each band's time out of 0.74 s, as far as its line reaches */
            int32_t lean = p[1] <= 50 ? (MO_B - 1 - (int32_t)b) * (50 - p[1]) : (int32_t)b * (p[1] - 50);   /* 0 .. 250 */
            int32_t share = lean * Q15 / 250, cap = (MO_LEN[b] - 4) << MO_SH[b], d;
            share += mulq15(s->scat[b] - share, p[3] * Q15 / 100);
            d = (int32_t)(((int64_t)32704 * mulq15(share, p[0] * Q15 / 100)) >> 15);   /* samples at the full rate */
            s->dw16[b] = (mini(d, cap) << 16) + (2 << 16);
        }
        s->fb = p[2] * (LV(0.9) / 100);
        s->key = p_key(p);
        s->have = 1;
    }
    for (b = 0; b < MO_B; b++)                                       /* the times glide (a Doppler, not a zipper) */
        s->d16[b] += (s->dw16[b] - s->d16[b]) >> 6;
    for (i = 0; i < CTL; i++) {
        int32_t rest = x[i], out = 0;
        off = 0;
        for (b = 0; b < MO_B; b++) {
            int32_t band = b < MO_B - 1 ? svf_run(&s->f[b], &s->c[b], rest, 1) : rest, y, sh = MO_SH[b];
            int16_t *ln = s->line + off;
            uint32_t len = MO_LEN[b], ph;
            if (b < MO_B - 1)
                rest -= band;
            s->acc[b] += band;
            ph = s->ph[b] = (s->ph[b] + 1u) & ((1u << sh) - 1u);
            if (!ph) {                                               /* a stored sample: the band's mean, fed back */
                int32_t v = s->acc[b] >> sh;
                s->acc[b] = 0;
                s->w[b] = s->w[b] + 1u >= len ? 0u : s->w[b] + 1u;
                ln[s->w[b]] = nx_s16(v + mulq15(s->fbv[b], s->fb));
            }
            {   /* read back between stored samples (their phase within the decimation adds to the fraction) */
                int32_t pos16 = (s->d16[b] - ((int32_t)ph << 16)) >> sh;
                int32_t pi = clampi(pos16 >> 16, 0, (int32_t)len - 2), pf = (pos16 >> 1) & 0x7FFF;
                int32_t i0 = (int32_t)s->w[b] - pi, i1;
                if (i0 < 0)
                    i0 += (int32_t)len;
                i1 = i0 ? i0 - 1 : (int32_t)len - 1;
                y = ln[i0] + (int32_t)(((int64_t)(ln[i1] - ln[i0]) * pf) >> 15);
            }
            s->fbv[b] = y;
            out += y;
            off += len;
        }
        x[i] = nx_soft(out);
    }
}

/* ------------------------------------------------------- DISINTEGRATE --- */
/* LAYERS (1 .. 8) short rings in series, each its own read head LENGTH samples at most behind, its place the sound's
 * own level (|x (x / 4 - 1/2)|) smoothed (SMOOTHING), so the louder the sound the further back each head reads; each
 * layer clipped to a narrow window. DRIVE into the window, the output brought back up. No feedback. */
#define DI_L 8
#define DI_LEN 256u
typedef struct {
    int16_t buf[DI_L][DI_LEN];
    uint32_t key, have, w;
    int32_t pos[DI_L], len, layers, sm, drive;
} nx_disint_t;
static void nx_disint(const uint8_t *p, nx_disint_t *s, int32_t *x)
{
    uint32_t i;
    int32_t l;
    if (!s->have || s->key != p_key(p)) {
        s->drive = 4096 + p[0] * 160;                                /* x 1 .. 5, Q12 */
        s->len = 8 + p[1] * 240 / 100;                               /* 8 .. 248 samples */
        s->layers = 1 + p[2] * 7 / 100;
        s->sm = mini(Q15, 4 * Q15 / (2 + p[3] * p[3] / 20));         /* (a step every 4 samples) */
        s->key = p_key(p);
        s->have = 1;
    }
    for (i = 0; i < CTL; i++) {
        int32_t v = mulq12(x[i], s->drive);
        s->w = (s->w + 1u) & (DI_LEN - 1u);
        for (l = 0; l < s->layers; l++) {
            if (!(s->w & 3u)) {                                       /* the head's place, every 4 samples */
                int32_t vv = clampi(v, -Q15, Q15), t = absi((vv * ((vv >> 2) - Q15 / 2)) >> 13);
                int32_t want = (clampi(t, 0, Q15) * s->len) >> 7;
                s->pos[l] += (int32_t)(((int64_t)((want << 8) - s->pos[l]) * s->sm) >> 15);   /* Q16 samples */
            }
            s->buf[l][s->w] = nx_s16(v);
            v = nx_read(s->buf[l], DI_LEN - 1u, s->w, (uint32_t)clampi(s->pos[l], 0, (s->len << 16)));
            v = clampi(v, -LV(0.222), LV(0.222));
        }
        x[i] = nx_soft(v * 3);
    }
}

/* =============================================================== the second wave (0.63) === */

/* --------------------------------------------------------------- REED --- */
/* The sound blows a clarinet (after the Synthesis ToolKit's Clarinet, Cook and Scavone: the classic reed and bore):
 * a bore half the period long (PITCH about the generator's root, -24 .. +12 semitones; a closed tube: odd harmonics),
 * its return reflected inverted through a low-pass (BRIGHTNESS) and losing DAMPING; at the reed the pressure difference
 * d opens it by clamp(0.7 - 0.3 d, -1, 1). The breath is the sound's level (PRESSURE) with the sound itself on it, so the
 * reed speaks while the sound plays and rests when it stops: squawks, overblowing, multiphonics. Q12 inside. */
#define RD_LEN 2048u
typedef struct {
    int16_t buf[RD_LEN];
    uint32_t key, have, w;
    int32_t d16, refl, press, lpk, lp, env, dc;
} nx_reed_t;
static void nx_reed(const uint8_t *p, nx_reed_t *s, int32_t *x)
{
    uint32_t i;
    if (!s->have || s->key != (p_key(p) ^ (uint32_t)rack_root)) {
        int32_t note = 36 + rack_root % 12 + (p[0] - 66) * 36 / 100 + 12, hz8 = note_hz8(clampi(note, 24, 96), 0);
        int32_t d = (int32_t)(((int64_t)FS << 23) / hz8);            /* half the period, Q16 samples */
        while (d >= (int32_t)(RD_LEN - 4) << 16)
            d >>= 1;
        s->d16 = d;
        s->press = 2000 + p[1] * 25;                                 /* the breath at full level, Q12: 0.49 .. 1.1 */
        s->lpk = 6000 + p[2] * 250;                                  /* the bell's low-pass, Q15 */
        s->refl = 28000 + p[3] * 40;                                 /* 0.85 .. 0.98 */
        s->key = p_key(p) ^ (uint32_t)rack_root;
        s->have = 1;
    }
    for (i = 0; i < CTL; i++) {
        int32_t breath, bore, pd, reed, out;
        s->env += (absi(x[i]) - s->env) >> 8;
        breath = mulq15(mini(s->env * 4, Q15), s->press) + (x[i] >> 4);   /* Q12 */
        bore = nx_read(s->buf, RD_LEN - 1u, s->w, (uint32_t)s->d16);
        s->lp += mulq15(bore - s->lp, s->lpk);                       /* the bell: darker as it returns */
        pd = -mulq15(s->lp, s->refl) - breath;
        reed = clampi(2867 - ((pd * 1229) >> 12), -4096, 4096);      /* 0.7 - 0.3 d, within +-1 (Q12) */
        out = clampi(breath + ((pd * reed) >> 12), -4 * 4096, 4 * 4096);
        s->w = (s->w + 1u) & (RD_LEN - 1u);
        s->buf[s->w] = nx_s16(out);
        s->dc += ((out << 8) - s->dc) >> 11;
        x[i] = nx_soft((out - (s->dc >> 8)) * 6);
    }
}

/* ---------------------------------------------------------------- NLF --- */
/* Dobson's nonlinear filter (as Csound's nlfilt, LGPL: the equation): y = a y1 + b y2 + d y[n-L]^2 + x. FEEDBACK a,
 * RESONANCE b, NONLINEARITY d (scaled by the sound's own level, so silence lets it rest), LAG L 1 .. 600 samples (its
 * pitch). From a resonant filter into buzz, squeals and chaos; the output's DC taken off. */
#define NL_LEN 1024u
typedef struct {
    int16_t buf[NL_LEN];
    uint32_t key, have, w;
    int32_t a, b, d, lag, y1, y2, env, dc;
} nx_nlf_t;
static void nx_nlf(const uint8_t *p, nx_nlf_t *s, int32_t *x)
{
    uint32_t i;
    if (!s->have || s->key != p_key(p)) {
        s->a = p[0] * 290;                                           /* 0 .. 0.88 */
        s->b = (p[1] - 60) * 200;                                    /* -0.37 .. 0.24 */
        if (s->a + absi(s->b) > 31000)                               /* (inside the stable region) */
            s->b = (s->b < 0 ? -1 : 1) * (31000 - s->a);
        s->d = p[2] * 300;                                           /* 0 .. 0.9 */
        s->lag = 1 + p[3] * p[3] / 17;                               /* 1 .. ~590 */
        s->key = p_key(p);
        s->have = 1;
    }
    for (i = 0; i < CTL; i++) {
        int32_t yl = s->buf[(s->w - (uint32_t)s->lag) & (NL_LEN - 1u)], y, dd;
        s->env += (absi(x[i]) - s->env) >> 9;
        dd = mulq15(s->d, mini(s->env * 8, Q15));
        y = mulq15(s->y1, s->a) + mulq15(s->y2, s->b) + mulq15(mulq15(yl, yl), dd) + (x[i] >> 1);
        y = nx_soft(y);
        s->y2 = s->y1;
        s->y1 = y;
        s->w = (s->w + 1u) & (NL_LEN - 1u);
        s->buf[s->w] = nx_s16(y);
        s->dc += ((y << 8) - s->dc) >> 10;
        x[i] = nx_soft((y - (s->dc >> 8)) * 3 / 2);
    }
}

/* ------------------------------------------------------------ SUBHARM --- */
/* Undertones (after the Mixtur-Trautonium and octave dividers): the sound's zero crossings, low-passed at ~1 kHz and
 * with some hysteresis, counted; two squares flip every A and every B of them (A, B: 2 .. 8), at the sound's level.
 * MIX from the undertones to the sound ring-modulated by the first; TONE their low-pass. Erratic on noise. */
typedef struct {
    svf_t f, t;
    svf_c fc, tc;
    uint32_t key, have;
    int32_t na, nb, ca, cb, sa, sb, pos, env, mix, dc;
} nx_subharm_t;
static void nx_subharm(const uint8_t *p, nx_subharm_t *s, int32_t *x)
{
    uint32_t i;
    if (!s->have || s->key != p_key(p)) {
        s->na = 2 + p[0] * 6 / 100;
        s->nb = 2 + p[1] * 6 / 100;
        s->mix = p[2] * Q15 / 100;
        svf_coef(&s->fc, 900, 2896);
        svf_coef(&s->tc, 120 + p[3] * p[3] / 2, 2896);               /* 120 Hz .. 5 kHz */
        if (!s->have)
            s->sa = s->sb = 1;
        s->key = p_key(p);
        s->have = 1;
    }
    for (i = 0; i < CTL; i++) {
        int32_t l = svf_run(&s->f, &s->fc, x[i], 1), sub, ring, y;
        s->env += (absi(x[i]) - s->env) >> 8;
        if ((!s->pos && l > 200) || (s->pos && l < -200)) {          /* a crossing, either way (with hysteresis): a
                                                                      * square flipping every N of them is f / N */
            s->pos = !s->pos;
            if (++s->ca >= s->na) { s->ca = 0; s->sa = -s->sa; }
            if (++s->cb >= s->nb) { s->cb = 0; s->sb = -s->sb; }
        }
        sub = (s->sa + s->sb) * mini(s->env * 2, Q15) / 2;
        ring = x[i] * s->sa;
        y = sub + mulq15(ring - sub, s->mix);
        y = svf_run(&s->t, &s->tc, y, 1);
        s->dc += ((y << 8) - s->dc) >> 11;
        x[i] = nx_soft(y - (s->dc >> 8));
    }
}

/* -------------------------------------------------------------- SPARK --- */
/* Every rising zero crossing of the sound fires a spark (after Airwindows' Nikola, MIT: the idea): a damped burst at a
 * frequency falling as it dies (PITCH 400 Hz .. 8 kHz), DECAY 0.5 .. 25 ms, part tone, part noise (GRIT), at the sound's
 * level; THRESHOLD how loud a crossing must be. Pulse trains locked to the sound, crackling electric arcs. */
typedef struct {
    uint32_t key, have, ph, inc0;
    int32_t pos, env, amp, a, ka, grit, thr, inc;
    rnd_t r;
} nx_spark_t;
static void nx_spark(const uint8_t *p, nx_spark_t *s, int32_t *x)
{
    uint32_t i;
    if (!s->have || s->key != p_key(p)) {
        s->inc0 = hz8_inc(400 * 256 * (int32_t)(exp2_q16(p[0] * (43 * 65536 / 1000)) >> 16));   /* 400 Hz x 2^0 .. 2^4.3 */
        s->ka = nx_share(22 + p[1] * p[1] / 9);                      /* 0.5 .. 25 ms */
        s->grit = p[2] * Q15 / 100;
        s->thr = 200 + p[3] * 60;
        if (!s->have)
            rnd_seed(&s->r, 0x5A9Cu);
        s->key = p_key(p);
        s->have = 1;
    }
    for (i = 0; i < CTL; i++) {
        int32_t tone, nz, y;
        s->env += absi(x[i]) > s->env ? (absi(x[i]) - s->env) >> 2 : (absi(x[i]) - s->env) >> 10;
        if (!s->pos && x[i] > s->thr) {                              /* a spark */
            s->pos = 1;
            s->a = Q15;
            s->amp = mini(s->env * 2, Q15);
            s->inc = (int32_t)s->inc0;
            s->ph = 0;
        } else if (s->pos && x[i] < -s->thr)
            s->pos = 0;
        s->a -= mulq15(s->a, s->ka) + (s->a > 0);                    /* (to nothing, not to a rounding's rest) */                                       /* (its pitch falls as it dies) */
        s->inc -= s->inc >> 9;                                       /* (its pitch falls as it dies) */
        s->ph += (uint32_t)s->inc;
        tone = sin_ph(s->ph);
        nz = rnd_bi(&s->r);
        y = mulq15(tone + mulq15(nz - tone, s->grit), mulq15(s->a, s->amp));
        x[i] = nx_soft(y * 3 / 2);
    }
}

/* ---------------------------------------------------- the band bank --- */
/* CONTRAST's and BLUR's: 12 band-passes from 80 Hz to 10 kHz, 0.63 octave apart, each with its level */
#define BK_N 12
static const int16_t BK_HZ[BK_N] = {80, 124, 193, 300, 465, 722, 1121, 1740, 2702, 4195, 6513, 10000};
typedef struct {
    svf_t f[BK_N];
    svf_c c[BK_N];
    int32_t env[BK_N];
} bk_t;
static void bk_init(bk_t *b, int32_t q12)
{
    uint32_t k;
    for (k = 0; k < BK_N; k++)
        svf_coef(&b->c[k], BK_HZ[k], q12);
}

/* ----------------------------------------------------------- CONTRAST --- */
/* Spectral contrast (after GRM Contrast, CDP's trace): each band's gain (its level / the bands' mean, in the log)^c,
 * CONTRAST c from -1 (the weak raised: a tone turns to breath) through 0 to +2 (the strong raised: noise turns tonal),
 * up to +-18 dB; TRACE keeps only the K loudest (12 .. 1); RESOLUTION the bands' Q; SPEED their followers. The output
 * brought back to the sound's level. */
typedef struct {
    bk_t b;
    uint32_t key, have;
    int32_t c16, keep, ke, g[BK_N], gw[BK_N], in_l, out_l, lev;
    uint32_t still;
} nx_contrast_t;
static void nx_contrast(const uint8_t *p, nx_contrast_t *s, int32_t *x)
{
    uint32_t i, k;
    int32_t lg[BK_N], mean = 0;
    if (!s->have || s->key != p_key(p)) {
        s->c16 = (p[0] - 33) * 65536 / 33;                           /* -1 .. +2, Q16 */
        s->keep = BK_N - p[1] * (BK_N - 1) / 100;
        bk_init(&s->b, 4096 + p[2] * 160);                           /* Q 1 .. 5 */
        s->ke = nx_share(220 + p[3] * 80);                           /* 5 .. 180 ms */
        if (!s->have) {
            for (k = 0; k < BK_N; k++)
                s->g[k] = 4096;
            s->lev = 4096;
        }
        s->key = p_key(p);
        s->have = 1;
    }
    if (nx_quiet(&s->still, x)) {
        memset(s->b.f, 0, sizeof s->b.f);
        memset(s->b.env, 0, sizeof s->b.env);
        return;
    }
    for (k = 0; k < BK_N; k++) {                                     /* the gains, once a block; each band's level per
                                                                      * hertz (half an octave less each 0.63 octave up:
                                                                      * white noise reads flat) */
        lg[k] = log2_q16((uint32_t)(s->b.env[k] + (64 << 8))) - (int32_t)k * 20644;   /* (Q8; a floor: no band
                                                                      * near nothing drags the mean down) */
        mean += lg[k] / BK_N;
    }
    for (k = 0; k < BK_N; k++) {
        int32_t d = (int32_t)(((int64_t)(lg[k] - mean) * s->c16) >> 16), rank = 0;
        uint32_t j;
        for (j = 0; j < BK_N; j++)
            rank += lg[j] > lg[k] || (lg[j] == lg[k] && j < k);
        s->gw[k] = (int32_t)(exp2_q16(clampi(d, -(3 << 16), 3 << 16)) >> 4);   /* Q12 */
        if (rank >= s->keep)
            s->gw[k] >>= 4;
    }
    for (i = 0; i < CTL; i++) {
        int32_t y = 0, v = x[i];
        for (k = 0; k < BK_N; k++) {
            int32_t bp = svf_run(&s->b.f[k], &s->b.c[k], v, 2);
            s->b.env[k] += mulq15((absi(bp) << 8) - s->b.env[k], s->ke);   /* (Q8: a slow follower still moves) */
            s->g[k] += (s->gw[k] - s->g[k]) >> 5;
            y += mulq12(bp, s->g[k]);
        }
        s->in_l += (absi(v) - s->in_l) >> 10;
        s->out_l += (absi(y) - s->out_l) >> 10;
        x[i] = nx_soft(mulq12(y, s->lev));
    }
    if (s->out_l > 64)                                                /* the level matched, slowly */
        s->lev += (clampi((int32_t)(((int64_t)s->in_l << 12) / s->out_l), 256, 6 * 4096) - s->lev) >> 3;
}

/* --------------------------------------------------------------- BLUR --- */
/* The sound replaced by its slowly moving spectral shadow (after CDP's spectral blur, GRM Evolution and Freeze): the
 * bank's levels follow the sound slowly (BLUR: 10 ms .. 4 s), and slower still towards FREEZE (up to ~7 s more); each level
 * plays its band again, from a sine at its centre (drifting by DRIFT) through noise to dust (EXCITATION), through the
 * band's own filter. */
typedef struct {
    bk_t b;
    svf_t nf[BK_N];
    uint32_t key, have, ph[BK_N], inc[BK_N];
    int32_t k30, mixn, dust, drift, lev, quiet;
    uint32_t still;
    int64_t env12[BK_N];
    rnd_t r;
} nx_blur_t;
static void nx_blur(const uint8_t *p, nx_blur_t *s, int32_t *x)
{
    uint32_t i, k;
    if (!s->have || s->key != p_key(p)) {
        int32_t ms = 10 + p[0] * p[0] * 2 / 5 + p[1] * p[1] * p[1] / 125; /* 10 ms .. ~4 s, and up to ~7 s more */
        s->k30 = (int32_t)((1LL << 30) / ((int64_t)ms * 44));
        s->mixn = mini(p[2] * 2, 100) * Q15 / 100;                   /* sines -> noise */
        s->dust = p[2] > 50 ? (p[2] - 50) * 2 : 0;                   /* noise -> dust */
        s->drift = p[3];
        if (!s->have) {
            bk_init(&s->b, 4096 * 3);
            rnd_seed(&s->r, 0xB1F2u);
            for (k = 0; k < BK_N; k++)
                s->inc[k] = hz8_inc(BK_HZ[k] << 8);
        }
        s->key = p_key(p);
        s->have = 1;
    }
    if ((s->quiet = nx_quiet(&s->still, x)) != 0)                   /* (the analysis let go; the levels fade on) */
        memset(s->b.f, 0, sizeof s->b.f);
    for (k = 0; k < BK_N; k++) {                                     /* the sines drift a little, each its own way */
        int32_t base = (int32_t)hz8_inc(BK_HZ[k] << 8), w = mulq15(rnd_bi(&s->r), base / 400 * s->drift / 100);
        s->inc[k] = (uint32_t)clampi((int32_t)s->inc[k] + w, base - base / 8 * s->drift / 100 - 1, base + base / 8 * s->drift / 100 + 1);
    }
    for (i = 0; i < CTL; i++) {
        int32_t y = 0, v = x[i], nz = rnd_bi(&s->r) >> 1;
        if (s->dust && rnd_below(&s->r, 4000) >= 40)                 /* dust: only now and then a grain */
            nz = mulq15(nz, Q15 - s->dust * Q15 / 100);
        for (k = 0; k < BK_N; k++) {
            int32_t bp = s->quiet ? 0 : svf_run(&s->b.f[k], &s->b.c[k], v, 2), e, sn, nb;
            {   /* its level, slowly; falling at least a little each sample (no rest above nothing) */
                int64_t d = ((((int64_t)absi(bp) << 12) - s->env12[k]) * s->k30) >> 30;
                s->env12[k] += d < 0 ? d - 1 : d;
                if (s->env12[k] < 0)
                    s->env12[k] = 0;
            }
            e = (int32_t)(s->env12[k] >> 12);
            s->ph[k] += s->inc[k];
            sn = s->mixn < Q15 ? sin_ph(s->ph[k]) >> 1 : 0;           /* (only what the excitation uses) */
            nb = s->mixn ? svf_run(&s->nf[k], &s->b.c[k], nz, 2) * 2 : 0;
            y += mulq15(sn + mulq15(nb - sn, s->mixn), mini(e * 3, Q15));
        }
        x[i] = nx_soft(y);
    }
}

/* ------------------------------------------------------------ DOPPLER --- */
/* A virtual source on an orbit round a point d0 away (after GRM Doppler, IRCAM Spat, the Leslie): its distance d sets
 * the delay (d / 343 m/s, so the pitch bends as it nears and leaves), the level (1 / d) and the air's absorption (a
 * low-pass falling with d). SPEED 0.05 .. 1.5 orbits a second, SIZE 2 .. 14 m, PASS how close it comes (0: circling at a
 * fixed distance, no bend; 100: right past), AIR. */
#define DP_LEN 4096u
typedef struct {
    int16_t buf[DP_LEN];
    uint32_t key, have, w, th, inc;
    int32_t r_cm, c_cm, air, d16, d16w, g, gw, lp, lpk, lpkw;
} nx_doppler_t;
static void nx_doppler(const uint8_t *p, nx_doppler_t *s, int32_t *x)
{
    uint32_t i;
    int32_t dx, dy, dcm, dd;
    if (!s->have || s->key != p_key(p)) {
        s->inc = hz8_inc(13 + p[0] * p[0] * 371 / 10000) * CTL;      /* 0.05 .. 1.5 Hz (Q8), a block's step */
        s->r_cm = 200 + p[1] * 12;                                   /* the orbit, cm */
        s->c_cm = mulq15(s->r_cm, p[2] * Q15 / 100);                /* its centre, from the listener (0: round them) */
        s->air = p[3];
        s->key = p_key(p);
        s->have = 1;
    }
    s->th += s->inc;                                                 /* where the source is, once a block */
    dx = s->c_cm + mulq15(s->r_cm, sin_ph(s->th + (1u << 30)));
    dy = mulq15(s->r_cm, sin_ph(s->th));
    dcm = (int32_t)isqrt32((uint32_t)(dx * dx + dy * dy)) + 100;
    dd = (int32_t)(((int64_t)dcm * 44100 / 34300) << 16);            /* the delay, Q16 samples */
    s->d16w = mini(dd, (int32_t)(DP_LEN - 4) << 16);
    s->gw = mini(Q15, 150 * Q15 / dcm);                              /* 1 / d (full at 1.5 m) */
    s->lpkw = clampi(Q15 - dcm * s->air * 2 / 3, 2000, Q15);         /* the air: darker further off */
    if (!s->have || !s->d16) {
        s->d16 = s->d16w;
        s->g = s->gw;
    }
    for (i = 0; i < CTL; i++) {
        int32_t y;
        s->d16 += (s->d16w - s->d16) >> 7;                           /* (smooth: a bend, never a jump) */
        s->g += (s->gw - s->g) >> 7;
        s->lpk += (s->lpkw - s->lpk) >> 7;
        s->w = (s->w + 1u) & (DP_LEN - 1u);
        s->buf[s->w] = nx_s16(x[i]);
        y = nx_read(s->buf, DP_LEN - 1u, s->w, (uint32_t)s->d16);
        s->lp += mulq15(y - s->lp, s->lpk);
        x[i] = nx_soft(mulq15(s->lp, s->g) * 2);
    }
}

/* ---------------------------------------------------------------- GSM --- */
/* The 13 kbit/s phone codec of the first digital mobiles, fed music (after Vascular's gsm): the sound low-passed,
 * taken every RATE-th sample (3 .. 8: 14.7 .. 5.5 kHz; the codec assumes 8), coded and decoded 160 at a time, held and
 * low-passed again. LEVEL into the codec (-24 .. +12 dB, undone after: the codec changes with level), TONE the filters'
 * cutoff, STUTTER the chance a frame is played again instead of the new one. A silent frame is not coded (zeros). */
typedef struct {
    struct gsm_state enc, dec;
    gsm_signal in[160], out[160];
    gsm_byte frame[33];
    svf_t a1, a2, b1, b2;
    svf_c c;
    uint32_t key, have, take, n, fmax;
    int32_t fac, gin, gout, held, stut, dc;
    rnd_t r;
} nx_gsm_t;
static void nx_gsm(const uint8_t *p, nx_gsm_t *s, int32_t *x)
{
    uint32_t i;
    if (!s->have || s->key != p_key(p)) {
        int32_t db16 = (p[1] - 66) * 36 * 65536 / 100 / 6;          /* dB / 6: octaves of gain, Q16 */
        s->fac = 3 + p[0] * 5 / 100;
        s->gin = (int32_t)(exp2_q16(db16) >> 4);                      /* Q12 */
        s->gout = (int32_t)(exp2_q16(-db16) >> 4);
        svf_coef(&s->c, mini(FS / 2 / s->fac * (40 + p[2] * 50 / 100) / 100, 16000), 2896);
        s->stut = p[3] * 30;
        if (!s->have) {
            gsm_init(&s->enc);
            gsm_init(&s->dec);
            rnd_seed(&s->r, 0x65Au);
        }
        s->key = p_key(p);
        s->have = 1;
    }
    for (i = 0; i < CTL; i++) {
        int32_t v = svf_run(&s->a2, &s->c, svf_run(&s->a1, &s->c, mulq12(x[i], s->gin), 1), 1), y;
        if (++s->take >= (uint32_t)s->fac) {                         /* a sample for the codec */
            int32_t q = clampi(v >> 3, -4096, 4095);                 /* Q15 -> the codec's 13 bits */
            s->take = 0;
            s->in[s->n] = (gsm_signal)((q << 3) & 0xfff8);
            s->fmax = maxi((int32_t)s->fmax, absi(q));
            if (++s->n >= 160u) {                                    /* a frame: coded and decoded */
                s->n = 0;
                if (s->fmax <= 2)
                    memset(s->out, 0, sizeof s->out);
                else if (rnd_below(&s->r, 3000) >= s->stut) {        /* (else the last frame again: a stutter) */
                    gsm_encode(&s->enc, s->in, s->frame);
                    gsm_decode(&s->dec, s->frame, s->out);
                }
                s->fmax = 0;
            }
            s->held = (s->out[s->n] >> 3) << 3;                      /* back to Q15 */
        }
        y = svf_run(&s->b2, &s->c, svf_run(&s->b1, &s->c, s->held, 1), 1);
        y = mulq12(y, s->gout);
        s->dc += ((y << 8) - s->dc) >> 12;
        x[i] = nx_soft(y - (s->dc >> 8));
    }
}

/* -------------------------------------------------------------- GENDY --- */
/* Xenakis' dynamic stochastic synthesis applied to the sound: each period of it (from its zero crossings) redrawn as a
 * polygon of N corners (BREAKPOINTS 4 .. 16), each corner the sound where it falls plus a random walk of its own
 * (AMPLITUDE WALK), each side's length the period / N stretched by another walk (DURATION WALK), the walks pulled
 * back towards the sound by ANCHOR. Linear between the corners. */
#define GY_N 16
typedef struct {
    svf_t f;
    svf_c fc;
    uint32_t key, have, pos;
    int32_t n, aw, dw, anc, wa[GY_N], wd[GY_N], k, t, len, y0, y1, slope16, per, cnt, env, dc;
    rnd_t r;
} nx_gendy_t;
static void nx_gendy(const uint8_t *p, nx_gendy_t *s, int32_t *x)
{
    uint32_t i;
    if (!s->have || s->key != p_key(p)) {
        s->n = 4 + p[0] * 12 / 100;
        s->aw = p[1] * 60;
        s->dw = p[2] * 90;
        s->anc = Q15 - p[3] * 300;
        if (!s->have) {
            svf_coef(&s->fc, 1200, 2896);
            rnd_seed(&s->r, 0x6E7Du);
            s->per = 200;
            s->len = 20;
        }
        s->key = p_key(p);
        s->have = 1;
    }
    for (i = 0; i < CTL; i++) {
        int32_t l = svf_run(&s->f, &s->fc, x[i], 1), y;
        s->env += (absi(x[i]) - s->env) >> 8;
        s->cnt++;
        if (!s->pos && l > 300) {                                    /* the period, from rising crossings */
            s->pos = 1;
            if (s->cnt > 20 && s->cnt < 2000)
                s->per += (s->cnt - s->per) >> 2;
            s->cnt = 0;
        } else if (s->pos && l < -300)
            s->pos = 0;
        if (++s->t >= s->len) {                                      /* the next corner */
            int32_t k = s->k = (s->k + 1) % s->n;
            s->wa[k] = mulq15(clampi(s->wa[k] + mulq15(rnd_bi(&s->r), s->aw), -Q15, Q15), s->anc);
            s->wd[k] = mulq15(clampi(s->wd[k] + mulq15(rnd_bi(&s->r), s->dw), -Q15 / 2, Q15 * 2), s->anc);
            s->t = 0;
            s->len = maxi(2, (s->per / s->n) + mulq15(s->per / s->n, s->wd[k]));
            s->y0 = s->y1;
            s->y1 = clampi(x[i] + mulq15(s->wa[k], mini(s->env * 3, Q15)), -Q15, Q15);
            s->slope16 = ((s->y1 - s->y0) << 8) / s->len;            /* (a divide a corner) */
        }
        y = s->y0 + ((s->slope16 * s->t) >> 8);
        s->dc += ((y << 8) - s->dc) >> 10;
        x[i] = nx_soft(y - (s->dc >> 8));
    }
}

/* ------------------------------------------------------------- INVERT --- */
/* The spectrum mirrored about F (after the speech scramblers, CDP's mirroring): the sound high-passed at 60 Hz and
 * low-passed at F (four poles), multiplied by a cosine at F + SHIFT, low-passed at F again: what was f is now F - f (and
 * moved by SHIFT). MIRROR F 400 Hz .. 8 kHz, SHIFT -200 .. +200 Hz, RESONANCE the low-passes' Q, BANDWIDTH how much below
 * F is kept. */
typedef struct {
    svf_t h, a1, a2, b1, b2;
    svf_c hc, lc;
    uint32_t key, have, ph, inc, still;
} nx_invert_t;
static void nx_invert(const uint8_t *p, nx_invert_t *s, int32_t *x)
{
    uint32_t i;
    if (!s->have || s->key != p_key(p)) {
        int32_t f = 400 * (int32_t)(exp2_q16(p[0] * (43 * 65536 / 1000)) >> 16);   /* 400 Hz .. ~8 kHz */
        int32_t sh = (p[1] - 50) * 4;
        svf_coef(&s->hc, 60, 2896);
        svf_coef(&s->lc, f * (50 + p[3] / 2) / 100, 2896 + p[2] * 60);
        s->inc = hz8_inc((f + sh) << 8);
        s->key = p_key(p);
        s->have = 1;
    }
    if (nx_quiet(&s->still, x)) {
        memset(&s->h, 0, 5 * sizeof s->h);
        return;
    }
    for (i = 0; i < CTL; i++) {
        int32_t v = svf_run(&s->h, &s->hc, x[i], 3);
        v = svf_run(&s->a2, &s->lc, svf_run(&s->a1, &s->lc, v, 1), 1);
        s->ph += s->inc;
        v = mulq15(v, sin_ph(s->ph + (1u << 30))) * 2;
        v = svf_run(&s->b2, &s->lc, svf_run(&s->b1, &s->lc, v, 1), 1);
        x[i] = nx_soft(v * 3 / 2);
    }
}

/* ------------------------------------------------------------ SQUELCH --- */
/* Undersampled and rebuilt (after Airwindows' ZAcidLowpass, MIT: the idea): the sound taken at RATE (200 Hz .. 12 kHz,
 * opened further by its own level: FOLLOW) and drawn again between the takes by a quadratic curve through their
 * midpoints, its control point the take itself, or the raw sound breaking through (MELTDOWN); a resonance at half the
 * rate (SQUELCH). Rings, aliases, squelches. */
typedef struct {
    svf_t f;
    svf_c c;
    uint32_t key, have, ph, inc0, still;
    int32_t h0, h1, h2, env, melt, res, follow, rhz;
} nx_squelch_t;
static void nx_squelch(const uint8_t *p, nx_squelch_t *s, int32_t *x)
{
    uint32_t i, inc;
    int32_t hz;
    if (!s->have || s->key != p_key(p)) {
        s->rhz = 200 * (int32_t)(exp2_q16(p[0] * (59 * 65536 / 1000)) >> 16);   /* 200 Hz .. ~12 kHz */
        s->melt = p[1] * Q15 / 100;
        s->res = 2896 + p[2] * 300;                                   /* Q 0.7 .. 8 */
        s->follow = p[3];
        s->key = p_key(p);
        s->have = 1;
    }
    if (nx_quiet(&s->still, x)) {
        memset(&s->f, 0, sizeof s->f);
        s->h0 = s->h1 = s->h2 = 0;
        return;
    }
    hz = clampi(s->rhz + s->rhz * mini(s->env * s->follow / 4000, 400) / 100, 100, 16000);
    inc = hz8_inc(hz << 8);
    svf_coef(&s->c, clampi(hz / 2, 60, 12000), s->res);
    for (i = 0; i < CTL; i++) {
        uint32_t prev = s->ph;
        int32_t t, m01, m12, ctl, u, y;
        s->env += (absi(x[i]) - s->env) >> 9;
        s->ph += inc;
        if (s->ph < prev) {                                          /* a take */
            s->h0 = s->h1;
            s->h1 = s->h2;
            s->h2 = x[i];
        }
        t = (int32_t)(s->ph >> 17);                                  /* 0 .. 32767 */
        u = Q15 - t;
        m01 = (s->h0 + s->h1) >> 1;
        m12 = (s->h1 + s->h2) >> 1;
        ctl = s->h1 + mulq15(x[i] - s->h1, s->melt);
        y = mulq15(mulq15(u, u), m01) + 2 * mulq15(mulq15(u, t), ctl) + mulq15(mulq15(t, t), m12);
        y += svf_run(&s->f, &s->c, y, 2) * (s->res - 2896) / 2400;
        x[i] = nx_soft(y);
    }
}

/* ------------------------------------------------------------ the list --- */
static uint32_t nx_size(uint32_t k)
{
    switch (k) {
    case NX_SPIRAL: return sizeof(nx_spiral_t);
    case NX_SOURCE: return sizeof(nx_source_t);
    case NX_ENVEL: return sizeof(nx_envel_t);
    case NX_LPG: return sizeof(nx_lpg_t);
    case NX_BARBER: return sizeof(nx_barber_t);
    case NX_STICK: return sizeof(nx_stick_t);
    case NX_MORPHO: return sizeof(nx_morpho_t);
    case NX_DISINT: return sizeof(nx_disint_t);
    case NX_REED: return sizeof(nx_reed_t);
    case NX_NLF: return sizeof(nx_nlf_t);
    case NX_SUBHARM: return sizeof(nx_subharm_t);
    case NX_SPARK: return sizeof(nx_spark_t);
    case NX_CONTRAST: return sizeof(nx_contrast_t);
    case NX_BLUR: return sizeof(nx_blur_t);
    case NX_DOPPLER: return sizeof(nx_doppler_t);
    case NX_GSM: return sizeof(nx_gsm_t);
    case NX_GENDY: return sizeof(nx_gendy_t);
    case NX_INVERT: return sizeof(nx_invert_t);
    case NX_SQUELCH: return sizeof(nx_squelch_t);
    }
    return 8u;
}
static void nx_block(uint32_t k, const uint8_t *p, void *st, int32_t *x)
{
    switch (k) {
    case NX_SPIRAL: nx_spiral(p, (nx_spiral_t *)st, x); break;
    case NX_SOURCE: nx_source(p, (nx_source_t *)st, x); break;
    case NX_ENVEL: nx_envel(p, (nx_envel_t *)st, x); break;
    case NX_LPG: nx_lpg(p, (nx_lpg_t *)st, x); break;
    case NX_BARBER: nx_barber(p, (nx_barber_t *)st, x); break;
    case NX_STICK: nx_stick(p, (nx_stick_t *)st, x); break;
    case NX_MORPHO: nx_morpho(p, (nx_morpho_t *)st, x); break;
    case NX_DISINT: nx_disint(p, (nx_disint_t *)st, x); break;
    case NX_REED: nx_reed(p, (nx_reed_t *)st, x); break;
    case NX_NLF: nx_nlf(p, (nx_nlf_t *)st, x); break;
    case NX_SUBHARM: nx_subharm(p, (nx_subharm_t *)st, x); break;
    case NX_SPARK: nx_spark(p, (nx_spark_t *)st, x); break;
    case NX_CONTRAST: nx_contrast(p, (nx_contrast_t *)st, x); break;
    case NX_BLUR: nx_blur(p, (nx_blur_t *)st, x); break;
    case NX_DOPPLER: nx_doppler(p, (nx_doppler_t *)st, x); break;
    case NX_GSM: nx_gsm(p, (nx_gsm_t *)st, x); break;
    case NX_GENDY: nx_gendy(p, (nx_gendy_t *)st, x); break;
    case NX_INVERT: nx_invert(p, (nx_invert_t *)st, x); break;
    case NX_SQUELCH: nx_squelch(p, (nx_squelch_t *)st, x); break;
    }
}
