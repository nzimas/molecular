/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR: the generator's second engine (0.64), physical models beside the FM: Karplus-Strong strings and tubes,
 * banded waveguides, modal bars, bells and membranes, struck on the clocks (gen.c) or held by a dust of tiny strikes.
 * Written from Karplus & Strong 1983, Jaffe & Smith 1983, J. O. Smith's Physical Audio Signal Processing (CCRMA), Cook
 * and Scavone's Synthesis ToolKit (Plucked, StifKarp, BandedWG, ModalBar: its mode tables), Mutable Instruments' Rings
 * and Plaits (the curved bridge, the burst), Faust's physmodels.lib (bell and membrane modes) -- the ideas and the
 * published tables, no code copied.
 *
 * The models (pm_design_t.model):
 *   PM_STRING  a plucked or struck string: a delay line, a first-order allpass for the fraction of a sample, a
 *              three-tap linear-phase loss filter (its brightness never detunes it), a gain a round trip for its decay
 *   PM_STIFF   the same with four biquad allpasses in the loop (StifKarp): every partial stretched, piano-wire to
 *              gong-like
 *   PM_DRUM    Karplus-Strong's drum: the loop's sign flipped at random (blend), from snare to noisy membrane
 *   PM_TUBE    the reflection inverted: half the loop, odd harmonics, a stopped pipe or a bottle
 *   PM_BUZZ    a string whose length shortens with its own displacement (the curved bridge of a sitar or tanpura)
 *   PM_GONG    a stiff string whose tension follows its energy: the pitch falls as it dies
 *   PM_BANDED  banded waveguides (Essl & Cook): one short loop a mode, a band-pass inside each: bowls, glasses, bars
 *   PM_MODAL   a bank of up to 8 resonators at a family's mode ratios (bars, bells, membranes)
 * The excitations: a burst of noise a period long (its brightness the hardness, the strike's level), a mallet (a
 * raised-cosine pulse, shorter when harder), the pick or strike position (a comb on the burst); held, a dust of
 * small strikes. Struck, a strike adds to what still rings.
 *
 * Fixed point: the loop Q15 in int16 lines; the round trip's gain and every coefficient near 1 in Q30 (a Q15 step
 * there is a decay twice as long, a resonance a semitone off). Coefficients are worked out once a molecule (main
 * loop, double (float on the pedal), no libm). */

/* the set-up's arithmetic (build 73): double on the host, float on the pedal, whose FPU is single precision only
 * (d1088-spu: every double instruction there is a coprocessor exception, the crash on a new physical-model molecule
 * since build 64). Float costs the decays about 1 % (a coefficient near 1 in 24 bits), nothing audible. */
#ifdef __nds32__
typedef float pm_real;
#else
typedef double pm_real;
#endif
#define PM_LEN 1024u
#define PM_MODES 8
enum { PM_STRING, PM_STIFF, PM_DRUM, PM_TUBE, PM_BUZZ, PM_GONG, PM_BANDED, PM_MODAL };
enum { EX_BURST, EX_MALLET };

/* the design, kept in the patch's bytes (fm_patch_t.p: a physical family's patch is not an FM voice) */
typedef struct {
    uint8_t model, excite;
    uint8_t bright;              /* the loss filter's brightness, 0 .. 100 */
    uint8_t t60;                 /* decay, x 0.1 s (1 .. 250) */
    uint8_t pick;                /* pick / strike position, 0 .. 100 (0: none) */
    uint8_t hard;                /* the excitation's hardness, 0 .. 100 */
    uint8_t disp;                /* stiffness (PM_STIFF, PM_GONG), 0 .. 100 */
    uint8_t blend;               /* PM_DRUM: the chance the sign stays, 0 .. 100 */
    uint8_t buzz;                /* PM_BUZZ: how hard the bridge bites; PM_GONG: the glide */
    uint8_t dust;                /* held: the dust's density, 0 .. 100 */
    uint8_t nmode;               /* PM_MODAL, PM_BANDED: modes */
    uint8_t octave;              /* the chord moved up (0 .. 3 octaves) */
    int16_t ratio[PM_MODES];     /* the modes' ratios to the note, Q10 (negative: a fixed frequency, Hz) */
    uint8_t mdec[PM_MODES];      /* each mode's decay, % of t60 */
    uint8_t mgain[PM_MODES];     /* each mode's level, 0 .. 100 */
} pm_design_t;

/* one note's loop or modes, worked out from the design (main loop) */
typedef struct {
    int32_t n;                   /* the integer delay */
    int32_t eta;                 /* the fraction's allpass, Q15 */
    int32_t rho;                 /* the round trip's gain, Q30 */
    int32_t h0, h1;              /* the loss filter, Q15 */
    int32_t ap[4][2];            /* the stiffness's biquad allpasses: a1, a2, Q30 */
    int32_t d16;                 /* PM_BUZZ, PM_GONG: the nominal delay, Q16 (read interpolated) */
    int32_t seg[PM_MODES];       /* PM_BANDED: each mode's loop length */
    int32_t c1[PM_MODES], c2[PM_MODES], g[PM_MODES];   /* resonators: 2 r cos w, r^2, input gain, Q30 */
    int32_t width;               /* the mallet's pulse, samples */
} pm_note_t;

/* one voice as it sounds */
typedef struct {
    int16_t line[PM_LEN];
    pm_note_t nt;
    uint32_t w;
    int32_t apx, apy;            /* the fraction's allpass: last in, last out */
    int32_t f1, f2;              /* the loss filter's last two inputs */
    int32_t bq[4][4];            /* the stiffness allpasses: x1 x2 y1 y2 */
    int32_t ry1[PM_MODES], ry2[PM_MODES];   /* resonators (Q15 x 2^8) */
    int32_t bx1[PM_MODES], bx2[PM_MODES];   /* banded: each band-pass's last inputs */
    uint32_t sw[PM_MODES];       /* banded: each segment's write place */
    int32_t epos, elen, elev;    /* the mallet: where in its pulse, its length, its level */
    int32_t energy;              /* PM_GONG: the loop's energy, for the tension */
    int32_t dc;                  /* the loop's DC leak (Q8) */
    int32_t peak, quiet;         /* let go after long silence (a fixed point's last rounding) */
    uint32_t lfsr;
    uint8_t live;
} pm_voice_t;

/* --------------------------------------- double (pedal: float) maths, no libm (main loop) --- */
static pm_real pm_exp(pm_real x)
{
    int k = 0;
    pm_real s = 1, t = 1;
    int i;
    while (x > 0.5 || x < -0.5) {
        x *= 0.5;
        k++;
    }
    for (i = 1; i < 14; i++) {
        t *= x / i;
        s += t;
    }
    while (k--)
        s *= s;
    return s;
}
static pm_real pm_ln(pm_real x)                       /* x > 0 */
{
    pm_real y = 0, z;
    int i;
    while (x > 2) { x *= 0.5; y += 0.6931471805599453; }
    while (x < 0.5) { x *= 2; y -= 0.6931471805599453; }
    z = (x - 1) / (x + 1);
    {
        pm_real z2 = z * z, t = z, s = 0;
        for (i = 1; i < 40; i += 2) {
            s += t / i;
            t *= z2;
        }
        y += 2 * s;
    }
    return y;
}
static pm_real pm_sincos(pm_real w, int want_cos)     /* w in [-pi, pi] (reduced by halving) */
{
    pm_real s, c, w2;
    int k = 0;
    while (w > 0.2 || w < -0.2) {
        w *= 0.5;
        k++;
    }
    w2 = w * w;
    s = w * (1 - w2 / 6 * (1 - w2 / 20 * (1 - w2 / 42 * (1 - w2 / 72))));
    c = 1 - w2 / 2 * (1 - w2 / 12 * (1 - w2 / 30 * (1 - w2 / 56)));
    while (k--) {                                    /* the doubled angle */
        pm_real s2 = 2 * s * c, c2 = c * c - s * s;
        s = s2;
        c = c2;
    }
    return want_cos ? c : s;
}
static pm_real pm_atan2(pm_real y, pm_real x)
{
    pm_real a, z, s = 0, t, z2;
    int i, k = 0, flip = 0;
    if (x == 0)
        return y >= 0 ? 1.5707963267948966 : -1.5707963267948966;
    z = y / x;
    if (z < 0) { z = -z; flip = 1; }
    if (z > 1) { z = 1 / z; k = 1; }
    {                                                /* atan by halving the angle twice, then its series */
        int h;
        for (h = 0; h < 3; h++)
            z = z / (1 + pm_exp(0.5 * pm_ln(1 + z * z)));
        z2 = z * z;
        t = z;
        for (i = 1; i < 30; i += 2) {
            s += t / i * ((i / 2) % 2 ? -1 : 1);
            t *= z2;
        }
        a = s * 8;
    }
    if (k)
        a = 1.5707963267948966 - a;
    if (flip)
        a = -a;
    if (x < 0)
        a = y >= 0 ? a + 3.141592653589793 : a - 3.141592653589793;
    return a;
}
static int32_t pm_q30(pm_real v) { return (int32_t)(v * 1073741824.0 + (v >= 0 ? 0.5 : -0.5)); }

/* the MIDI note's frequency, pm_real */
static pm_real pm_hz(int32_t note) { return 440.0 * pm_exp((note - 69) * 0.05776226504666211); }

/* one note's loop worked out from the design */
static void pm_prepare(pm_note_t *nt, const pm_design_t *d, int32_t note)
{
    pm_real f0, L, t60 = clampi(d->t60, 1, 250) * 0.1, ph = 0, delta, B = clampi(d->bright, 0, 100) / 100.0;
    int32_t k;
    memset(nt, 0, sizeof *nt);
    note += 12 * d->octave;
    f0 = pm_hz(note);
    while (f0 < 44.0)
        f0 *= 2;
    nt->width = 6 + (100 - clampi(d->hard, 0, 100)) * 2;            /* 0.15 .. 4.7 ms */
    if (d->model == PM_MODAL) {                                      /* the resonators */
        for (k = 0; k < d->nmode && k < PM_MODES; k++) {
            pm_real f = d->ratio[k] < 0 ? -d->ratio[k] : f0 * d->ratio[k] / 1024.0, w, r, tm;
            if (f > 16000 || f < 20)
                continue;
            tm = t60 * clampi(d->mdec[k], 1, 200) / 100.0;
            w = 6.283185307179586 * f / FS;
            r = pm_exp(-6.907755278982137 / (tm * FS));             /* 60 dB in tm */
            nt->c1[k] = pm_q30(2 * r * pm_sincos(w, 1));
            nt->c2[k] = pm_q30(r * r);
            nt->g[k] = pm_q30(pm_sincos(w, 0) * 0.25 * d->mgain[k] / 100.0);   /* (an impulse rings at about its gain,
                                                                      * whatever its decay or frequency) */
        }
        return;
    }
    if (d->model == PM_BANDED) {                                     /* one loop a mode, each with its band-pass */
        int32_t used = 0;
        for (k = 0; k < d->nmode && k < 4; k++) {
            pm_real f = f0 * d->ratio[k] / 1024.0, w = 6.283185307179586 * f / FS, r = 1 - 3.141592653589793 * 40.0 / FS;
            int32_t len;
            if (f < 20 || f > 15000)                                 /* (build 71: before any divide by it) */
                break;
            len = (int32_t)(FS / f + 0.5);
            if (len < 4 || used + len > (int32_t)PM_LEN)
                break;
            nt->seg[k] = len;
            used += len;
            nt->c1[k] = pm_q30(2 * r * pm_sincos(w, 1));
            nt->c2[k] = pm_q30(r * r);
            nt->g[k] = pm_q30(pm_exp(-6.907755278982137 / (f * t60 * clampi(d->mdec[k], 1, 200) / 100.0)) *
                              (1 - r * r) * 0.5);                    /* (the band-pass peaks at 1: the loop's gain the
                                                                      * decay's) */
        }
        return;
    }
    L = FS / f0;                                                     /* the loop's length in samples */
    if (d->model == PM_TUBE)
        L *= 0.5;                                                    /* (inverted: a round trip is half a period) */
    if (d->model == PM_STIFF || d->model == PM_GONG) {               /* StifKarp: four allpasses up from 2 f0 */
        pm_real stretch = 0.9 + 0.0999 * clampi(d->disp, 0, 100) / 100.0, rr = 0.5 + 0.5 * stretch;
        pm_real step = (FS / 2.0 - 2 * f0) / 4, w0 = 6.283185307179586 * f0 / FS;
        if (rr > 0.99999)
            rr = 0.99999;
        for (k = 0; k < 4; k++) {
            pm_real fc = 2 * f0 + k * step, a1 = -2 * rr * pm_sincos(6.283185307179586 * fc / FS, 1), a2 = rr * rr;
            /* its phase delay at f0: an allpass's numerator is z^-2 times its denominator mirrored, so its phase is
             * -2 w - 2 arg D(e^jw), D = 1 + a1 e^-jw + a2 e^-2jw: the delay 2 + 2 arg D / w */
            pm_real cw = pm_sincos(w0, 1), sw = pm_sincos(w0, 0), c2w = pm_sincos(2 * w0, 1), s2w = pm_sincos(2 * w0, 0);
            pm_real dr = 1 + a1 * cw + a2 * c2w, di = -(a1 * sw + a2 * s2w), pd = 2 + 2 * pm_atan2(di, dr) / w0;
            ph += pd > 0 ? pd : 0;
            nt->ap[k][0] = pm_q30(a1);
            nt->ap[k][1] = pm_q30(a2);
        }
    }
    if (L - 1 - ph > (pm_real)PM_LEN - 4)                              /* too long for the line: an octave up */
        L *= 0.5;
    if (L - 1 - ph < 3) {                                            /* (the stiffness ate the loop: less of it) */
        ph = L - 4;
        if (ph < 0)
            ph = 0;
    }
    nt->d16 = (int32_t)((L - 1 - ph) * 65536.0);
    nt->n = (int32_t)(L - 1 - ph - 0.2);
    delta = L - 1 - ph - nt->n;                                     /* 0.2 .. 1.2 */
    if (nt->n < 2)
        nt->n = 2;
    nt->eta = (int32_t)((1 - delta) / (1 + delta) * Q15);
    nt->h0 = (int32_t)((1 + B) / 2 * Q15);
    nt->h1 = (int32_t)((1 - B) / 4 * Q15);
    nt->rho = pm_q30(pm_exp(-6.907755278982137 / ((d->model == PM_TUBE ? 2 : 1) * f0 * t60)));
}

/* a strike at level lev (Q15): a burst a period long, combed at the pick position, into what still rings; or the
 * mallet's pulse; the modal and banded ones fed through their input */
static void pm_strike(pm_voice_t *v, const pm_note_t *nt, const pm_design_t *d, int32_t lev, rnd_t *r)
{
    int32_t j, len, pick, lp = 0, k_lp = 2000 + clampi(d->hard, 0, 100) * 300 + mulq15(lev, 8000);
    v->nt = *nt;
    v->live = 1;
    v->quiet = 0;
    if (!v->lfsr)
        v->lfsr = 0xACE1u;
    if (d->model == PM_MODAL || (d->excite == EX_MALLET && d->model != PM_BANDED)) {   /* the mallet: a raised cosine
                                                                      * (a banded one takes its burst: through narrow
                                                                      * bands a pulse is nearly nothing) */
        v->epos = 0;
        v->elen = nt->width;
        v->elev = lev * 2 / nt->width;                               /* (the pulse's area: one strike's, whatever its
                                                                      * width; the low modes ring at the level, the
                                                                      * high ones less: a softer mallet darker) */
        if (d->model == PM_MODAL || d->model == PM_BANDED)
            return;
    }
    if (d->model == PM_BANDED) {                                     /* a burst into every segment */
        uint32_t off = 0, s;
        for (s = 0; s < PM_MODES && nt->seg[s]; s++) {
            for (j = 0; j < nt->seg[s]; j++) {
                lp += mulq15(rnd_bi(r) - lp, k_lp);
                v->line[off + (uint32_t)j] = nx_s16(v->line[off + (uint32_t)j] + mulq15(lp, lev) * 6);
            }
            off += (uint32_t)nt->seg[s];
        }
        return;
    }
    if (d->excite == EX_MALLET)
        return;
    len = mini(nt->n, (int32_t)PM_LEN - 2);
    pick = clampi(d->pick, 0, 100) * len / 200;                     /* (the comb: up to half way) */
    {
        static int16_t e[PM_LEN];                                    /* (build 69: static, not 2 KB of the 4 KB stack) */
        for (j = 0; j < len; j++) {
            lp += mulq15(rnd_bi(r) - lp, k_lp);
            e[j] = nx_s16(mulq15(lp, lev));
        }
        for (j = 0; j < len; j++) {                                  /* the next len samples read: the line's last len */
            int32_t x = e[j] - (pick && j >= pick ? e[j - pick] : 0);
            uint32_t at = (v->w + 1u - (uint32_t)len + (uint32_t)j) & (PM_LEN - 1u);
            v->line[at] = nx_s16(v->line[at] + x / 2);
        }
    }
}

/* one block of one voice into out (added), Q15; 0 if it is silent */
static int pm_render(pm_voice_t *v, const pm_design_t *d, int32_t *out, int held, rnd_t *r)
{
    uint32_t i, k;
    const pm_note_t *nt = &v->nt;
    int32_t pk = 0, dust_p = held ? clampi(d->dust, 0, 100) * clampi(d->dust, 0, 100) / 40 + 1 : 0;
    if (!v->live)
        return 0;
    for (i = 0; i < CTL; i++) {
        int32_t ex = 0, y;
        if (dust_p && rnd_below(r, 4000) < dust_p)                   /* held: a dust of small strikes */
            ex = mulq15(rnd_bi(r), d->model == PM_BANDED ? 30000 : 6000);   /* (a bowl's narrow bands take less) */
        if (v->epos < v->elen) {                                     /* the mallet's pulse */
            int32_t ph = (int32_t)(((uint32_t)v->epos << 16) / (uint32_t)v->elen);   /* (a divide: only while striking) */
            ex += mulq15(Q15 - sin_ph(((uint32_t)ph << 16) + (1u << 30)), v->elev) * (d->model == PM_MODAL ? 1 : 8);
            v->epos++;
        }
        if (d->model == PM_MODAL) {
            y = 0;
            for (k = 0; k < PM_MODES; k++) {
                int32_t yn;
                if (!nt->c1[k])
                    continue;
                yn = (int32_t)((((int64_t)nt->c1[k] * v->ry1[k]) - ((int64_t)nt->c2[k] * v->ry2[k]) +
                                ((int64_t)nt->g[k] * (ex << 8)) + (1 << 29)) >> 30);
                v->ry2[k] = v->ry1[k];
                v->ry1[k] = yn;
                y += yn >> 8;
            }
        } else if (d->model == PM_BANDED) {
            uint32_t off = 0;
            y = 0;
            for (k = 0; k < 4 && nt->seg[k]; k++) {
                int16_t *seg = v->line + off;
                uint32_t len = (uint32_t)nt->seg[k], rd = v->sw[k] + 1u >= len ? 0u : v->sw[k] + 1u;
                int32_t x = seg[rd], bp;
                bp = (int32_t)((((int64_t)nt->c1[k] * v->ry1[k]) - ((int64_t)nt->c2[k] * v->ry2[k]) +
                                ((int64_t)nt->g[k] * ((x - v->bx2[k]) << 8)) + (1 << 29)) >> 30);
                v->bx2[k] = v->bx1[k];
                v->bx1[k] = x;
                v->ry2[k] = v->ry1[k];
                v->ry1[k] = bp;
                v->sw[k] = rd;
                seg[rd] = nx_s16((bp >> 8) + ex / 2);
                y += bp >> 8;
                off += len;
            }
        } else {
            int32_t x, a, lf;
            v->w = (v->w + 1u) & (PM_LEN - 1u);
            if (d->model == PM_BUZZ || d->model == PM_GONG) {        /* the length moving: read interpolated */
                int32_t dd = nt->d16, s = v->f1;
                if (d->model == PM_BUZZ) {                           /* the curved bridge (Rings, Plaits) */
                    int32_t a2 = absi(s) - 819, cb = (absi(a2) + a2) * (s > 0 ? 2 : -3) / 2;
                    dd -= (int32_t)(((int64_t)dd * mulq15(cb, d->buzz * d->buzz / 3)) >> 15);
                } else                                               /* the tension: shorter when it rings hard */
                    dd -= (int32_t)(((int64_t)dd * mulq15(mini(v->energy, Q15), d->buzz * 3)) >> 15);
                x = nx_read(v->line, PM_LEN - 1u, v->w, (uint32_t)clampi(dd, 2 << 16, (int32_t)(PM_LEN - 3) << 16));
                a = x;
            } else {
                x = v->line[(v->w - (uint32_t)nt->n) & (PM_LEN - 1u)];
                a = mulq15(nt->eta, x - v->apy) + v->apx;            /* the fraction: (eta + z^-1) / (1 + eta z^-1) */
                v->apx = x;
                v->apy = a;
            }
            if (d->model == PM_STIFF || d->model == PM_GONG)
                for (k = 0; k < 4; k++) {                            /* the stiffness: biquad allpasses, Q30 */
                    int32_t *q = v->bq[k], a1 = nt->ap[k][0], a2 = nt->ap[k][1], o;
                    o = (int32_t)((((int64_t)a2 * a) + ((int64_t)a1 * q[0]) - ((int64_t)a1 * q[2]) - ((int64_t)a2 * q[3]) +
                                   (1 << 29)) >> 30) + q[1];          /* (rounded: near 2 f0 the recursion lifts a
                                                                      * floor's bias 300-fold, a DC that grew) */
                    q[1] = q[0];
                    q[0] = a;
                    q[3] = q[2];
                    q[2] = o;
                    a = o;
                }
            lf = (int32_t)(((int64_t)(mulq15(nt->h0, v->f1) + mulq15(nt->h1, a + v->f2)) * nt->rho + (1 << 29)) >> 30);
            v->dc += ((lf << 8) - v->dc) >> 13;                      /* (a leak at ~1 Hz: no DC builds up in the loop) */
            lf -= v->dc >> 8;
            v->f2 = v->f1;
            v->f1 = a;
            if (d->model == PM_DRUM) {                               /* the drum: the sign flipped at random */
                v->lfsr = (v->lfsr >> 1) ^ (-(v->lfsr & 1u) & 0xB400u);
                if ((int32_t)(v->lfsr & 127u) >= d->blend * 128 / 100)
                    lf = -lf;
            } else if (d->model == PM_TUBE)
                lf = -lf;
            v->energy += ((absi(lf) << 2) - v->energy) >> 9;
            v->line[v->w] = nx_s16(lf + ex);
            y = lf;
        }
        pk = maxi(pk, absi(y));
        out[i] += nx_soft(y);                                        /* (a soft ceiling: long rings pile up, strike on
                                                                      * strike) */
    }
    v->peak = pk;
    if (pk < 3 && !dust_p) {                                         /* (silent for 1.5 s: let go, the line cleared) */
        if (++v->quiet > 2067) {
            memset(v, 0, sizeof *v);
            return 0;
        }
    } else
        v->quiet = 0;
    return 1;
}

/* ------------------------------------------------------------ the families --- */
/* ten families, each struck on a clock most of the time (SCATTER or MACHINE, as Molecular's struck ones) or held by
 * its dust; the clock's own burst off (the excitation is the strike) */
static const int16_t PM_BAR[7][4] = {                /* STK ModalBar's mode ratios (Q10; negative: fixed Hz) */
    {1024, 4086, 10906, -2443},                      /* marimba */
    {1024, 2058, 3994, 14715},                       /* vibraphone */
    {1024, 4178, 6829, -3725},                       /* agogo */
    {1024, 2844, 7555, 15746},                       /* wood 1 */
    {1024, 1820, 2435, 3458},                        /* wood 2 */
    {1024, 1028, 1037, 2434},                        /* beats */
    {1024, 1246, 1510, 1770},                        /* clump */
};
static const int16_t PM_BAND[4][4] = {               /* banded waveguides (STK BandedWG) */
    {1024, 2822, 5534, 9147},                        /* uniform bar */
    {1024, 4116, 10976, 18503},                      /* tuned bar */
    {1024, 2376, 4352, 6789},                        /* glass harmonica */
    {1020, 1028, 3050, 3065},                        /* Tibetan bowl: doublets */
};
static const int16_t PM_BELLR[8] = {512, 1024, 1229, 1536, 2048, 2560, 2734, 3072};   /* hum .. (textbook partials) */
static const int16_t PM_SKINR[8] = {1024, 1632, 2187, 2351, 2717, 2988, 3232, 3585};  /* a membrane's Bessel ratios */

static void pm_clock(gen_clock_t *c, rnd_t *r, int struck_pct)
{
    memset(c, 0, sizeof *c);
    if (!rnd_chance(r, struck_pct)) {
        c->mode = GK_HOLD;
        return;
    }
    if (rnd_chance(r, 60))
        fam_scatter(c, rnd_below(r, 4), r);
    else {
        c->mode = GK_MACHINE;
        c->rate = (uint8_t)rnd_in(r, 2, 40), c->jit = (uint8_t)rnd_in(r, 2, 40), c->drift = (uint8_t)rnd_in(r, 3, 30);
        c->accn = (uint8_t)rnd_in(r, 2, 6), c->pat = (uint8_t)(1 | rnd_below(r, 256));
        c->soft = (uint8_t)rnd_in(r, 25, 70), c->flam = (uint8_t)rnd_in(r, 0, 40), c->flam_ms = (uint8_t)rnd_in(r, 15, 90);
    }
    c->spread = (uint8_t)rnd_in(r, 15, 70);
    c->level = 0;                                    /* (no FM burst: the strike is the excitation) */
}
static void pm_generate(fm_patch_t *pt, gen_clock_t *c, uint32_t f, uint32_t seed)
{
    rnd_t rr, *r = &rr;
    pm_design_t d;
    int32_t k;
    rnd_seed(r, seed ^ 0x504Du);
    memset(&d, 0, sizeof d);
    d.dust = (uint8_t)rnd_in(r, 15, 60);
    d.hard = (uint8_t)rnd_in(r, 20, 95);
    d.octave = (uint8_t)rnd_in(r, 1, 2);
    switch (f) {
    case PF_HARP:
        d.model = PM_STRING, d.excite = EX_BURST;
        d.bright = (uint8_t)rnd_in(r, 25, 90), d.t60 = (uint8_t)rnd_in(r, 15, 70), d.pick = (uint8_t)rnd_in(r, 5, 45);
        pm_clock(c, r, 75);
        break;
    case PF_WIRE:
        d.model = PM_STIFF, d.excite = rnd_chance(r, 50) ? EX_BURST : EX_MALLET;
        d.disp = (uint8_t)rnd_in(r, 40, 100), d.bright = (uint8_t)rnd_in(r, 40, 95), d.t60 = (uint8_t)rnd_in(r, 20, 90);
        d.pick = (uint8_t)rnd_in(r, 0, 30), d.octave = (uint8_t)rnd_in(r, 0, 2);
        pm_clock(c, r, 75);
        break;
    case PF_SNARE:
        d.model = PM_DRUM, d.excite = EX_BURST;
        d.blend = (uint8_t)rnd_in(r, 30, 75), d.bright = (uint8_t)rnd_in(r, 50, 100), d.t60 = (uint8_t)rnd_in(r, 3, 18);
        d.hard = (uint8_t)rnd_in(r, 60, 100), d.octave = (uint8_t)rnd_in(r, 1, 3);
        pm_clock(c, r, 90);
        break;
    case PF_PIPE:
        d.model = PM_TUBE, d.excite = rnd_chance(r, 60) ? EX_MALLET : EX_BURST;
        d.bright = (uint8_t)rnd_in(r, 15, 70), d.t60 = (uint8_t)rnd_in(r, 5, 35);
        pm_clock(c, r, 70);
        break;
    case PF_SITAR:
        d.model = PM_BUZZ, d.excite = EX_BURST;
        d.buzz = (uint8_t)rnd_in(r, 30, 90), d.bright = (uint8_t)rnd_in(r, 40, 90), d.t60 = (uint8_t)rnd_in(r, 20, 80);
        d.pick = (uint8_t)rnd_in(r, 5, 30);
        pm_clock(c, r, 70);
        break;
    case PF_GONG:
        d.model = PM_GONG, d.excite = EX_MALLET;
        d.disp = (uint8_t)rnd_in(r, 70, 100), d.buzz = (uint8_t)rnd_in(r, 10, 60), d.bright = (uint8_t)rnd_in(r, 30, 80);
        d.t60 = (uint8_t)rnd_in(r, 40, 200), d.hard = (uint8_t)rnd_in(r, 10, 60), d.octave = (uint8_t)rnd_in(r, 0, 1);
        pm_clock(c, r, 65);
        break;
    case PF_BOWL: {
        int set = rnd_below(r, 4);
        d.model = PM_BANDED, d.excite = EX_MALLET, d.nmode = 4;
        for (k = 0; k < 4; k++) {
            d.ratio[k] = PM_BAND[set][k];
            d.mdec[k] = (uint8_t)(100 - k * rnd_in(r, 5, 20));
        }
        d.t60 = (uint8_t)rnd_in(r, 20, 120), d.hard = (uint8_t)rnd_in(r, 10, 70);
        pm_clock(c, r, 65);
        break;
    }
    case PF_BAR: {
        int set = rnd_below(r, 7);
        d.model = PM_MODAL, d.nmode = 4;
        for (k = 0; k < 4; k++) {
            d.ratio[k] = PM_BAR[set][k];
            d.mdec[k] = (uint8_t)(100 - k * rnd_in(r, 10, 25));
            d.mgain[k] = (uint8_t)rnd_in(r, k ? 25 : 70, k ? 80 : 100);
        }
        d.t60 = (uint8_t)rnd_in(r, set == 1 ? 25 : 4, set == 1 ? 80 : 30);
        pm_clock(c, r, 85);
        break;
    }
    case PF_BELL:
        d.model = PM_MODAL, d.nmode = 8;
        for (k = 0; k < 8; k++) {                    /* the partials, each a little off: doublets beat */
            d.ratio[k] = (int16_t)(PM_BELLR[k] + rnd_in(r, -4, 4));
            d.mdec[k] = (uint8_t)clampi(130 - k * rnd_in(r, 8, 14), 10, 150);
            d.mgain[k] = (uint8_t)rnd_in(r, 30, 100);
        }
        d.t60 = (uint8_t)rnd_in(r, 40, 200), d.hard = (uint8_t)rnd_in(r, 40, 100);
        pm_clock(c, r, 75);
        break;
    default:                                         /* PF_SKIN */
        d.model = PM_MODAL, d.nmode = 8;
        for (k = 0; k < 8; k++) {
            d.ratio[k] = PM_SKINR[k];
            d.mdec[k] = (uint8_t)clampi(100 - k * 10, 20, 100);
            d.mgain[k] = (uint8_t)rnd_in(r, k ? 10 : 60, k ? 70 : 100);
        }
        d.t60 = (uint8_t)rnd_in(r, 2, 15), d.hard = (uint8_t)rnd_in(r, 30, 90), d.octave = (uint8_t)rnd_in(r, 0, 2);
        pm_clock(c, r, 90);
        break;
    }
    memset(pt, 0, sizeof *pt);
    memcpy(pt->p, &d, sizeof d);
    pt->family = (uint8_t)f;
    pt->gain = 4096;
    pt->seed = seed;
}
