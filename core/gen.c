/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR: the generator, the molecule's nucleus. Tonitor's six-operator FM (vendor/tonitor/fm.c) as a drone:
 * one patch drawn on the AMBIENT path, a chord of GEN_NV notes held for as long as the molecule lives, each voice
 * swelling in, and four LFOs moving the patch's macros (BRIGHTNESS, DETUNE, SATURATION, MOTION). */

#define GEN_NV 3u
#define GEN_SWELL 4096           /* blocks for a voice to swell in: ~3 s */
#define GEN_ROOT_LO 28           /* the chord's root, MIDI: E1 .. E3 */
#define GEN_ROOT_HI 52

/* the chords, as intervals above the root: open fifths and octaves, fourths, a dark minor seventh, clusters */
static const int8_t GEN_CHORDS[][GEN_NV] = {
    {0, 7, 12}, {0, 12, 19}, {0, 5, 12}, {0, 7, 19}, {0, 3, 10}, {0, 2, 7}, {0, 6, 13}, {0, 1, 12}, {0, 12, 24},
};
#define GEN_NCHORD (sizeof GEN_CHORDS / sizeof GEN_CHORDS[0])

typedef struct {
    fm_patch_t patch;
    int32_t note[GEN_NV];
    gen_lfo_t lfo[NMAC];
} gen_design_t;

typedef struct {
    fm_voice_t v[GEN_NV];
    int32_t swell[GEN_NV];       /* 0 .. GEN_SWELL blocks */
    uint32_t ph[NMAC];           /* the LFOs' phases */
    int32_t rnd_now[NMAC], rnd_to[NMAC];   /* the smooth random shape: where it is, where it goes */
    rnd_t r;                     /* the smooth random's draws (audio side only) */
    int8_t mac[NMAC];            /* the macros this block, for the screens */
} gen_state_t;

/* an LFO's rate, drawn: 0.02 .. 1.5 Hz on a log scale, as a phase step a block (Hz x 2^32 / (FS / CTL)) */
static uint32_t gen_lfo_inc(rnd_t *r)
{
    int32_t chz = (int32_t)((2u * exp2_q16(rnd_in(r, 0, 408289))) >> 16);   /* centi-hertz: 2 x 2^(0 .. 6.23) */
    return (uint32_t)chz * 31165u;                                         /* 2^32 / (100 x 1378.125) */
}

/* the four LFOs drawn anew (the left button); BRIGHTNESS and MOTION move most, SATURATION least */
static void gen_draw_lfos(gen_design_t *g, rnd_t *r)
{
    static const int8_t DEPTH_MAX[NMAC] = {MAC_MAX, MAC_MAX * 3 / 4, MAC_MAX / 2, MAC_MAX};
    uint32_t k;
    for (k = 0; k < NMAC; k++) {
        gen_lfo_t *l = &g->lfo[k];
        l->inc = gen_lfo_inc(r);
        l->depth = (int8_t)rnd_in(r, 0, DEPTH_MAX[k]);
        l->centre = (int8_t)rnd_in(r, -MAC_MAX / 3, MAC_MAX / 3);
        l->shape = (uint8_t)rnd_below(r, 3);
        if (rnd_chance(r, 25))                       /* a quarter of them still: the sound as drawn there */
            l->depth = 0;
    }
}

/* the whole generator from a seed: patch (measured, so slow: the main loop only), chord, LFOs */
static void gen_draw(gen_design_t *g, uint32_t seed)
{
    rnd_t rr;
    const int8_t *ch;
    int32_t root;
    uint32_t i;
    fm_generate(&g->patch, -1, seed, PATH_AMBIENT, GEN_NOW);
    fm_measure(&g->patch);
    rnd_seed(&rr, seed ^ 0x6E4Eu);
    root = rnd_in(&rr, GEN_ROOT_LO, GEN_ROOT_HI);
    ch = GEN_CHORDS[rnd_below(&rr, (int32_t)GEN_NCHORD)];
    for (i = 0; i < GEN_NV; i++)
        g->note[i] = root + ch[i];
    gen_draw_lfos(g, &rr);
}

/* the voices started (main loop, while the audio is held silent) */
static void gen_start(gen_state_t *s, const gen_design_t *g, uint32_t seed)
{
    uint32_t i;
    memset(s, 0, sizeof *s);
    rnd_seed(&s->r, seed ^ 0x1F0u);
    for (i = 0; i < GEN_NV; i++) {
        fm_note_on(&s->v[i], &g->patch, g->note[i], 1);
        fm_hold(&s->v[i], &g->patch);                /* held at full: the drone never decays */
    }
}

/* an LFO's value this block, Q15 -32768 .. 32767 */
AINL int32_t gen_lfo_val(gen_state_t *s, const gen_lfo_t *l, uint32_t k)
{
    uint32_t ph = s->ph[k], prev = ph;
    s->ph[k] = ph += l->inc;
    if (l->shape == 0)
        return sin_ph(ph);
    if (l->shape == 1) {
        int32_t t = (int32_t)(ph >> 16) - 32768;     /* a saw, -32768 .. 32767, folded into a triangle */
        return (t < 0 ? -t : t) * 2 - 32768;
    }
    if (ph < prev)                                   /* a new cycle: a new target */
        s->rnd_to[k] = rnd_bi(&s->r);
    s->rnd_now[k] += (s->rnd_to[k] - s->rnd_now[k]) >> 7;
    return s->rnd_now[k];
}

/* one block of the drone, Q15 into out (CTL samples) */
static FAST void gen_block(gen_state_t *s, const gen_design_t *g, int32_t *out)
{
    int32_t buf[CTL];
    uint32_t i, k;
    for (k = 0; k < NMAC; k++) {
        const gen_lfo_t *l = &g->lfo[k];
        int32_t m = l->centre + ((gen_lfo_val(s, l, k) * l->depth) >> 15);
        s->mac[k] = (int8_t)clampi(m, -MAC_MAX, MAC_MAX);
    }
    for (i = 0; i < CTL; i++)
        out[i] = 0;
    for (k = 0; k < GEN_NV; k++) {
        int32_t sw = s->swell[k], gain;
        if (!fm_render(&s->v[k], &g->patch, buf, s->mac))
            continue;
        if (sw < GEN_SWELL)
            s->swell[k] = ++sw;
        gain = (sw * sw) >> 9;                       /* the swell, squared: 4096^2 >> 9 = Q15 */
        gain >>= 1;                                  /* three voices: -6 dB each keeps the sum in its headroom */
        for (i = 0; i < CTL; i++)
            out[i] += mulq15(buf[i], gain);
    }
}
