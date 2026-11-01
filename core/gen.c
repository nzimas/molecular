/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR: the generator, the molecule's nucleus. Tonitor's six-operator FM (vendor/tonitor/fm.c): one patch of
 * one of the families (families.c), a chord of GEN_NV notes -- held for as long as the molecule lives, each voice
 * swelling in, or struck on the clock (mol.h gen_clock_t) -- and four LFOs moving the patch's macros (BRIGHTNESS,
 * DETUNE, SATURATION, MOTION). */

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
    /* the clock's (GK_MACHINE, GK_SCATTER) */
    int32_t vg[GEN_NV];          /* each voice's strike level, Q15 */
    int32_t gate[GEN_NV];        /* blocks until it is let go */
    uint32_t next_v;             /* the voice the next strike takes */
    int32_t knext, kper, kwand;  /* samples to the next strike; the mean period; its wander, Q15 */
    int32_t kvel, kflam, kflev;  /* (SCATTER: the hand tiring); samples to a bounce, its level */
    uint8_t kstep;               /* MACHINE: the place in the pattern; SCATTER: strikes left in a cluster */
    int32_t kenv, kdec, kcol, kcol0, klp;   /* the burst: its level, its fall a sample, its colour, its low-pass */
    int32_t khiss, henv, hdec, hlp;         /* the hiss: samples until it starts; its level, fall, low-pass */
    rnd_t kr;
    lfo_run_t lr[NMAC];          /* the LFOs as they run */
    rnd_t r;                     /* the smooth random's draws (audio side only) */
    int8_t mac[NMAC];            /* the macros this block, for the screens */
} gen_state_t;

/* the four LFOs drawn anew (the menu's LFO); BRIGHTNESS and MOTION move most, SATURATION least */
static void gen_draw_lfos(gen_design_t *g, rnd_t *r)
{
    static const int8_t DEPTH_MAX[NMAC] = {MAC_MAX, MAC_MAX * 3 / 4, MAC_MAX / 2, MAC_MAX};
    uint32_t k;
    for (k = 0; k < NMAC; k++) {
        gen_lfo_t *l = &g->lfo[k];
        l->inc = lfo_draw_inc(r);
        l->depth = (int8_t)rnd_in(r, 0, DEPTH_MAX[k]);
        l->centre = (int8_t)rnd_in(r, -MAC_MAX / 3, MAC_MAX / 3);
        l->shape = (uint8_t)rnd_below(r, 3);
        if (rnd_chance(r, 25))                       /* a quarter of them still: the sound as drawn there */
            l->depth = 0;
    }
}

/* the whole generator from a seed: a family (families.c), its patch (measured, so slow: the main loop only) and
 * clock, the chord, the LFOs */
static void gen_draw(gen_design_t *g, gen_clock_t *c, uint32_t seed)
{
    rnd_t rr;
    const int8_t *ch;
    int32_t root;
    uint32_t i;
    uint8_t w[FAM_NPICK];
    const fam_pick_t *fp;
    rnd_seed(&rr, seed ^ 0xFA3Du);
    for (i = 0; i < FAM_NPICK; i++)
        w[i] = FAM_PICK[i].w;
    fp = &FAM_PICK[rnd_weighted(&rr, w, FAM_NPICK)];
    memset(c, 0, sizeof *c);
    if (fp->f < FF_N)
        fm_generate(&g->patch, fp->f, seed, fp->path, GEN_NOW);
    else
        fam_generate(&g->patch, c, fp->f, seed);
    fm_measure(&g->patch);
    rnd_seed(&rr, seed ^ 0x6E4Eu);
    root = rnd_in(&rr, GEN_ROOT_LO, GEN_ROOT_HI);
    ch = GEN_CHORDS[rnd_below(&rr, (int32_t)GEN_NCHORD)];
    for (i = 0; i < GEN_NV; i++)
        g->note[i] = root + ch[i];
    gen_draw_lfos(g, &rr);
}

/* the voices started (main loop, while the audio is held silent): held, or the clock set going (its first strike
 * at once) */
static void gen_start(gen_state_t *s, const gen_design_t *g, const gen_clock_t *c, uint32_t seed)
{
    uint32_t i;
    int32_t ms;
    memset(s, 0, sizeof *s);
    rnd_seed(&s->r, seed ^ 0x1F0u);
    if (c->mode != GK_HOLD) {
        rnd_seed(&s->kr, seed ^ 0x4B4E4F43u);
        ms = clampi(c->click, 1, 60);
        s->kdec = (int32_t)(exp2_q16(-(int32_t)(6531317u / (uint32_t)(ms * 441))) >> 1);   /* 60 dB in ms, a sample */
        s->kcol = s->kcol0 = 1500 + clampi(c->color, 0, 100) * 285;
        s->kper = (int32_t)(FS * 4u / (uint32_t)clampi(c->rate, 1, 80));
        ms = clampi(c->off_ms, 1, 125) * 4;
        s->hdec = (int32_t)(exp2_q16(-(int32_t)(6531317u / (uint32_t)(ms * 441))) >> 1);
        s->kwand = s->kvel = Q15;
        s->knext = 0;
        if (!c->crackle)
            return;
    }
    for (i = 0; i < GEN_NV; i++) {
        fm_note_on(&s->v[i], &g->patch, g->note[i], 1);
        fm_hold(&s->v[i], &g->patch);                /* held at full: the drone never decays */
    }
}

/* macro 2 sets the grit (fine steps) and adds a whole step to the feedback (0 .. 7): (8f +- 32) / 64 of its felt
 * value f (the engine's square root, fm.c). An LFO crossing a feedback step once a block clicked (host count over
 * 40 molecules: 112 clicks, 1 with macro 2 held still). The LFO keeps m where the step is the centre's. */
static int32_t gen_fb_step(int32_t m)
{
    int32_t a = m < 0 ? -m : m, f = (int32_t)isqrt32((uint32_t)(a * MAC_MAX));
    f = m < 0 ? -f : f;
    return (f * 8 + (f < 0 ? -32 : 32)) / 64;
}
static int32_t gen_fb_band(int32_t centre, int32_t m)
{
    int32_t want = gen_fb_step(centre);
    while (m != centre && gen_fb_step(m) != want)
        m += m > centre ? -1 : 1;
    return m;
}
/* a voice struck again: its envelopes from stage 1 from where they are (Norilsk's retrig: a voice still ringing does
 * not drop to nothing first, as a fresh note-on's envelopes would), its pitch the new note's */
static void gen_retrig(fm_voice_t *v, const fm_patch_t *pt, int32_t note)
{
    const uint8_t *p = pt->p;
    uint32_t k;
    if (!v->live) {
        fm_note_on(v, pt, note, 1);
        return;
    }
    v->note = clampi(note + p[FP_TRNSP] - 24, 0, 127);
    for (k = 0; k < 6u; k++) {
        v->n.base[k] = fm6_osc_base(p + k * FP_OP, v->note, 0);
        v->n.env[k].down = 1;
        fm6_env_advance(&v->n.env[k], p + k * FP_OP, 0);
    }
    v->n.down = 1;
}
/* a strike at level lev (Q15): the next voice, a note of the chord, from the top; the burst (CRACKLE: the burst only) */
static void gen_strike(gen_state_t *s, const gen_design_t *g, const gen_clock_t *c, int32_t lev)
{
    uint32_t k = s->next_v, n = rnd_chance(&s->kr, c->spread) ? 1u + (uint32_t)rnd_below(&s->kr, GEN_NV - 1u) : 0u;
    s->kenv = mulq15(lev, c->level * 327);
    if (c->crackle)
        return;
    s->next_v = (k + 1u) % GEN_NV;
    gen_retrig(&s->v[k], &g->patch, g->note[n]);
    s->vg[k] = lev;
    s->gate[k] = maxi(1, (int32_t)c->gate * 4 * 1378 / 1000);
}
/* this block's strikes, on the clock */
static FAST void gen_clock(gen_state_t *s, const gen_design_t *g, const gen_clock_t *c)
{
    uint32_t k;
    for (k = 0; k < GEN_NV; k++)                     /* each struck note let go after its gate */
        if (s->gate[k] > 0 && --s->gate[k] == 0)
            fm_note_off(&s->v[k], &g->patch);
    if (s->khiss > 0 && (s->khiss -= (int32_t)CTL) <= 0)
        s->henv = c->off * 327;
    if ((s->knext -= (int32_t)CTL) > 0) {
        if (s->kflam > 0 && (s->kflam -= (int32_t)CTL) <= 0)
            gen_strike(s, g, c, s->kflev);
        return;
    }
    if (c->mode == GK_MACHINE) {
        int32_t per = mulq15(s->kper, s->kwand), lev, dr = c->drift * 328;
        if (++s->kstep >= maxi(c->accn, 1))
            s->kstep = 0;
        lev = (c->pat >> s->kstep) & 1u ? Q15 : c->soft * 327;
        lev -= mulq15(lev, rnd_q15(&s->kr) >> 3);    /* (no two strikes alike: up to 12 % under) */
        gen_strike(s, g, c, lev);
        s->kwand = clampi(s->kwand + mulq15(rnd_bi(&s->kr), c->drift * 82), Q15 - dr, Q15 + dr);
        s->knext = maxi(s->knext + per + mulq15(per * c->jit / 100, rnd_bi(&s->kr)), (int32_t)CTL);
        if (c->off)
            s->khiss = per / 2;
        if (c->flam && rnd_chance(&s->kr, c->flam)) {
            s->kflam = c->flam_ms * 44;
            s->kflev = lev * 5 / 8;
        }
    } else {
        int32_t u = rnd_q15(&s->kr), soft = c->soft * 327, lev, per;
        s->kvel = clampi(s->kvel + mulq15(rnd_bi(&s->kr), Q15 / 5), Q15 / 2, Q15);   /* (the hand tiring, pressing) */
        lev = mulq15(soft + mulq15(Q15 - soft, mulq15(u, u)), s->kvel);   /* (soft ones more often than hard) */
        s->kcol = mulq15(s->kcol0, Q15 / 2 + lev / 2);   /* (a softer strike darker) */
        gen_strike(s, g, c, lev);
        s->kwand = clampi(s->kwand + mulq15(rnd_bi(&s->kr), c->drift * 328), Q15 / 3, Q15 * 5 / 2);
        if (s->kstep) {                              /* (in a cluster) */
            s->kstep--;
            per = c->flam_ms * 44 + mulq15(c->flam_ms * 22, rnd_bi(&s->kr));
        } else if (rnd_chance(&s->kr, c->flam)) {
            s->kstep = (uint8_t)(1 + rnd_below(&s->kr, maxi(c->accn, 1)));
            per = c->flam_ms * 44;
        } else {                                     /* (an interval: the mean, wandering, up or down by octaves) */
            int32_t oct16 = rnd_bi(&s->kr) * (int32_t)c->jit / 5;
            per = (int32_t)(((int64_t)mulq15(s->kper, s->kwand) * (int32_t)exp2_q16(oct16)) >> 16);
            if (rnd_chance(&s->kr, c->pat))
                per *= 3 + rnd_below(&s->kr, 3);     /* (a long pause) */
        }
        s->knext += clampi(per, 1764, 110250);       /* (40 ms .. 2.5 s) */
    }
}

/* one block of the generator, Q15 into out (CTL samples) */
static FAST void gen_block(gen_state_t *s, const gen_design_t *g, const gen_clock_t *c, int32_t *out)
{
    int32_t buf[CTL];
    uint32_t i, k;
    for (k = 0; k < NMAC; k++) {
        const gen_lfo_t *l = &g->lfo[k];
        int32_t m = l->centre + ((lfo_val(&s->lr[k], l, &s->r) * l->depth) >> 15);
        if (k == 2u)                                 /* the grit moves, the feedback step does not */
            m = gen_fb_band(l->centre, clampi(m, -MAC_MAX, MAC_MAX));
        s->mac[k] = (int8_t)clampi(m, -MAC_MAX, MAC_MAX);
    }
    if (c->mode != GK_HOLD)
        gen_clock(s, g, c);
    for (i = 0; i < CTL; i++)
        out[i] = 0;
    for (k = 0; k < GEN_NV; k++) {
        int32_t sw = s->swell[k], gain;
        if (!fm_render(&s->v[k], &g->patch, buf, s->mac))
            continue;
        if (c->mode != GK_HOLD && !c->crackle)
            gain = s->vg[k] + (s->vg[k] >> 1);       /* struck: at its strike's level, x 1.5 (a strike's peak is
                                                      * brief: at the drones' x 0.5 struck molecules peaked 2 .. 11 dB
                                                      * under them, the leveller's lift all used) */
        else {
            if (sw < GEN_SWELL)
                s->swell[k] = ++sw;
            gain = (sw * sw) >> 9;                   /* the swell, squared: 4096^2 >> 9 = Q15 */
            gain >>= 1;                              /* three voices: -6 dB each keeps the sum in its headroom */
        }
        for (i = 0; i < CTL; i++)
            out[i] += mulq15(buf[i], gain);
    }
    if (s->kenv > 8)                                 /* the burst: coloured noise, falling */
        for (i = 0; i < CTL; i++) {
            s->klp += mulz15(rnd_bi(&s->kr) - s->klp, s->kcol);
            out[i] += mulq15(s->klp, s->kenv) >> 1;
            s->kenv = mulq15(s->kenv, s->kdec);
        }
    if (s->henv > 8)                                 /* the hiss: bright noise (its lows taken out), falling */
        for (i = 0; i < CTL; i++) {
            int32_t n = rnd_bi(&s->kr);
            out[i] += mulq15(n - s->hlp, s->henv) >> 3;
            s->hlp += (n - s->hlp) >> 3;
            s->henv = mulq15(s->henv, s->hdec);
        }
}
