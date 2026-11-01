/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR: the atoms' effects. Norilsk's 25 (vendor/norilsk/effects.c, unchanged: FILTER .. CHAOS, each with its
 * four values 0 .. 100 and the bounds a draw keeps them in) and Tonitor's DELAY and REVERB (vendor/tonitor).
 *
 * Norilsk's effects work on a mono block in place, each with a state of its own: here every atom's state is a piece
 * of one arena (FX_ARENA), handed out as the molecule is drawn (a molecule is drawn to fit: fx_bytes) and cleared
 * before it sounds. Each atom's output passes Norilsk's guard (a peak rider to half scale, rack.c rm_guard), then
 * full scale at most.
 * MODAL, STRING and BANK are tuned to the generator's root (rack_root).
 *
 * DELAY and REVERB are the heavy atoms: one of each at most (their lines are single, 44 and 43 KB) and only at the
 * molecule's edge, where they take a mono input and add a stereo output. The others are mono in, mono out. */

#define FX_ARENA 52224u          /* the atoms' memory: the largest atoms (TAPE, REVERSE) take 20 KB each. Build 69: 51
                                  * KB, not 64: on the device all data must end below 0x2003C000 (device/app.ld) */
static uint8_t fx_arena[FX_ARENA] POOL __attribute__((aligned(8)));
static uint32_t fx_place(const void *st) { return (uint32_t)((const uint8_t *)st - fx_arena); }

static const char *const AT_SYM[AT_N] = {"FM", "FL", "FO", "MU", "SH", "MO", "ST", "VO", "TA", "RS", "SP", "GL",
                                         "CL", "SC", "RI", "SV", "CB", "BK", "SM", "DF", "SW", "XO", "DS", "WS",
                                         "XM", "CS", "DL", "RV",
                                         "SR", "SO", "EN", "LG", "BA", "SK", "MP", "DI",    /* (atoms.c's) */
                                         "RD", "NL", "SU", "PK", "CT", "BL", "DO", "GS", "GY", "IN", "SQ"};
/* each kind's effect in Norilsk's list */
static const uint8_t AT_RM[AT_N] = {0, RM_FLT, RM_FLD, RM_MU, RM_SHF, RM_MDL, RM_STR, RM_VOW, RM_TAP, RM_RVS, RM_SPR,
                                    RM_GLT, RM_CLD, RM_SCR, RM_RNG, RM_STV, RM_CHB, RM_BNK, RM_SMR, RM_DIF, RM_SLW,
                                    RM_XOR, RM_DST, RM_WST, RM_XMD, RM_CHS, RM_DLY, RM_REV};   /* (atoms.c's: none) */
AINL int at_own(uint32_t kind) { return kind >= AT_SPIRAL; }                 /* one of atoms.c's */
AINL const char *at_name(uint32_t kind) { return kind == AT_GEN ? "FM" : at_own(kind) ? NX_NAME[kind - AT_SPIRAL] : RM_NAME[AT_RM[kind]]; }
/* the bounds a draw keeps value v in */
AINL int32_t at_lo(uint32_t kind, uint32_t v) { return at_own(kind) ? NX_LO[kind - AT_SPIRAL][v] : RM_LO[AT_RM[kind]][v]; }
AINL int32_t at_hi(uint32_t kind, uint32_t v) { return at_own(kind) ? NX_HI[kind - AT_SPIRAL][v] : RM_HI[AT_RM[kind]][v]; }
AINL int fx_heavy(uint32_t kind) { return kind == AT_DELAY || kind == AT_REVERB; }
/* the arena an atom takes (the heavy ones keep their own lines: none) */
AINL uint32_t fx_bytes(uint32_t kind)
{
    return kind == AT_GEN || fx_heavy(kind) ? 0u : at_own(kind) ? (nx_size(kind - AT_SPIRAL) + 7u) & ~7u : rm_size(AT_RM[kind]);
}

typedef struct {
    uint32_t at;                 /* its state's place in the arena */
    int32_t env, g;              /* the guard: the peak it follows, its gain (Q15) */
    lfo_run_t lr;                /* its LFO */
    rnd_t r;                     /* .. and that LFO's smooth random draws */
} fx_state_t;

/* an LFO's rate, drawn: 0.02 .. 1.5 Hz on a log scale, as a phase step a block (Hz x 2^32 / (FS / CTL)) */
static uint32_t lfo_draw_inc(rnd_t *r)
{
    int32_t chz = (int32_t)((2u * exp2_q16(rnd_in(r, 0, 408289))) >> 16);   /* centi-hertz: 2 x 2^(0 .. 6.23) */
    if (rnd_chance(r, 15))                           /* 0.64: an allure now and then (Schaeffer's vibrato, Wishart's
                                                      * tremolo): 3 .. 12 Hz, a living wobble rather than a drift */
        chz = (int32_t)((300u * exp2_q16(rnd_in(r, 0, 131072))) >> 16);
    return (uint32_t)chz * 31165u;                                         /* 2^32 / (100 x 1378.125) */
}

/* an LFO's value this block, Q15 -32768 .. 32767 (the generator's and the atoms') */
AINL int32_t lfo_val(lfo_run_t *s, const gen_lfo_t *l, rnd_t *r)
{
    uint32_t ph = s->ph, prev = ph;
    s->ph = ph += l->inc;
    if (l->shape == 0)
        return sin_ph(ph);
    if (l->shape == 1) {
        int32_t t = (int32_t)(ph >> 16) - 32768;     /* a saw, -32768 .. 32767, folded into a triangle */
        return (t < 0 ? -t : t) * 2 - 32768;
    }
    if (ph < prev)                                   /* a new cycle: a new target */
        s->to = rnd_bi(r);
    s->now += (s->to - s->now) >> 7;
    return s->now;
}

/* the values an LFO may move, a bit each: not the switches and coarse steps (FILTER's type, MULAW's bits, SHIFT's
 * direction, MODAL's, STRING's and BANK's octave, BANK's set, GLITCH's slice range, CLOUD's freeze, XOR's operation
 * and bits, WAVESET's mode, group and amount, XMOD's algorithm): moved, they jump (BANK's set clicked 2000 times in
 * 10 s, host count), and none of DELAY's and REVERB's (cooked in the main loop) */
static const uint8_t FX_LFO_OK[AT_N] = {
    0x0, 0xE, 0xF, 0xD, 0xB, 0xE, 0xE, 0xF, 0xF, 0xF, 0xF,   /* FM FILTER FOLD MULAW SHIFT MODAL STRING VOWEL TAPE REVERSE SPRING */
    0xE, 0xB, 0xF, 0xF, 0xF, 0xF, 0xA, 0xF, 0xF, 0xF, 0x9,   /* GLITCH CLOUD SCREAM RING STARVE CHEB BANK SMEAR DIFFUSE SLEW XOR */
    0xF, 0x8, 0xE, 0xF, 0x0, 0x0,                            /* DUST WAVESET XMOD CHAOS DELAY REVERB */
    0xF, 0x7, 0xF, 0xF, 0xF, 0xF, 0xF, 0xB,                  /* SPIRAL SOURCE (not its order) ENVEL LPG BARBER STICK MORPHO
                                                              * DISINTEGRATE (not its layers) */
    0xF, 0xF, 0xC, 0xF, 0xD, 0xF, 0xF, 0xE, 0xE, 0xF, 0xF,   /* REED NLF SUBHARM (not its divisors) SPARK CONTRAST (not
                                                              * its trace) BLUR DOPPLER GSM (not its rate) GENDY (not its
                                                              * corners) INVERT SQUELCH */
};

/* an atom's LFO drawn anew: one of the values it may move, swung up to half its bounds' span either way, at the
 * generator's rates; a quarter of them still */
static void fx_draw_lfo(mol_node_t *a, rnd_t *r)
{
    uint32_t ok = FX_LFO_OK[a->kind], n = 0, k, pick;
    int32_t span;
    memset(&a->lfo, 0, sizeof a->lfo);
    a->lval = 0;
    for (k = 0; k < 4u; k++)
        n += (ok >> k) & 1u;
    if (!n)
        return;
    pick = (uint32_t)rnd_below(r, (int32_t)n);
    for (k = 0; k < 4u; k++)
        if (((ok >> k) & 1u) && !pick--)
            a->lval = (uint8_t)k;
    span = at_hi(a->kind, a->lval) - at_lo(a->kind, a->lval);
    a->lfo.inc = lfo_draw_inc(r);
    a->lfo.shape = (uint8_t)rnd_below(r, 3);
    a->lfo.depth = (int8_t)(span < 2 ? 0 : rnd_in(r, span / 6, span / 2));
    if (rnd_chance(r, 25))
        a->lfo.depth = 0;
}

/* each kind's values drawn anew: Norilsk's bounds for its effects (so that every effect is heard); DELAY and REVERB
 * as before (Tonitor's parameters) */
static void fx_draw_values(mol_node_t *a, rnd_t *r)
{
    static const int8_t HEAVY[2][4][2] = {
        {{20, 90}, {20, 75}, {20, 80}, {30, 70}},     /* DELAY: time, feedback, damping, mix */
        {{40, 100}, {40, 95}, {20, 70}, {40, 85}},    /* REVERB: size, decay, damping, mix */
    };
    uint32_t k;
    for (k = 0; k < 4u; k++)
        if (fx_heavy(a->kind))
            a->p[k] = (int16_t)rnd_in(r, HEAVY[a->kind == AT_REVERB][k][0], HEAVY[a->kind == AT_REVERB][k][1]);
        else if ((FX_LFO_OK[a->kind] >> k & 1u) && rnd_chance(r, 40)) {
            /* 0.64: two times in five a continuous value drawn towards its bounds' ends (an arcsine draw: where an
             * effect's character is, rather than its middle ground); a switch or a coarse step stays uniform */
            int32_t lo = at_lo(a->kind, k), hi = at_hi(a->kind, k), u = sin_ph((uint32_t)rnd_below(r, 32768) << 16);
            a->p[k] = (int16_t)(lo + ((hi - lo) * ((Q15 - u) >> 1) >> 15));   /* (1 - cos(pi t)) / 2 of the span */
        } else
            a->p[k] = (int16_t)rnd_in(r, at_lo(a->kind, k), at_hi(a->kind, k));
}

/* the heavy atoms' values passed on (main loop: their own parameter functions cook them) */
static void fx_apply_heavy(const mol_node_t *a)
{
    if (a->kind == AT_DELAY)
        dl_param(a->p[0], a->p[1], a->p[2], a->p[3]);
    else if (a->kind == AT_REVERB)
        rv_param(a->p[0], a->p[1], a->p[2], a->p[3]);
}

/* an atom's state set up at its place in the arena, cleared (main loop, while nothing sounds) */
static void fx_start(fx_state_t *s, const mol_node_t *a, uint32_t at)
{
    s->at = at;
    s->env = 0;
    s->g = Q15;
    memset(&s->lr, 0, sizeof s->lr);
    rnd_seed(&s->r, at * 2654435761u + a->kind);
    if (fx_bytes(a->kind))
        memset(fx_arena + at, 0, fx_bytes(a->kind));
    if (a->kind == AT_CHAOS) {   /* one block of silence first: CHAOS starts at rest in a well, and its 10 Hz level
                                  * filter, starting at 0, passed that as one sample 60 times full scale (it is gated
                                  * to nothing with nothing in, so this block makes no sound) */
        int32_t x[CTL];
        uint8_t p[4];
        uint32_t i;
        for (i = 0; i < CTL; i++)
            x[i] = 0;
        for (i = 0; i < 4u; i++)
            p[i] = (uint8_t)clampi(a->p[i], 0, 100);
        m_chs(p, (m_chs_t *)(fx_arena + at), x);
    }
}

/* Norilsk's guard (rack.c rm_guard): a peak rider -- instant attack, ~200 ms release -- to half scale */
static void fx_guard(fx_state_t *s, int32_t *x)
{
    int32_t pk = 0, want, g0 = s->g, dg;
    uint32_t i;
    for (i = 0; i < CTL; i++)
        pk = maxi(pk, absi(x[i]));
    if (pk > s->env)
        s->env = pk;
    else
        s->env -= s->env >> 8;
    want = s->env > 16384 ? (int32_t)((uint32_t)16384 * 32768u / (uint32_t)s->env) : Q15;
    if (want > g0 && g0 < Q15)
        want = g0 + ((want - g0) >> 3);
    dg = (want - g0) / (int32_t)CTL;
    for (i = 0; i < CTL; i++) {
        g0 += dg;
        x[i] = clampi(mulq15(x[i], g0), -Q15, Q15);  /* (and full scale at most: the guard's gain comes a block late) */
    }
    s->g = want;
}

/* one block of an inner atom, in -> out (CTL samples, Q15) */
static FAST void fx_block(fx_state_t *s, const mol_node_t *a, const int32_t *in, int32_t *out)
{
    uint8_t p[4];
    void *st = fx_arena + s->at;
    uint32_t i;
    for (i = 0; i < CTL; i++)
        out[i] = in[i];
    for (i = 0; i < 4u; i++)
        p[i] = (uint8_t)clampi(a->p[i], 0, 100);
    if (a->lfo.depth) {                              /* its LFO, within the bounds its effect keeps the value in */
        uint32_t v = a->lval & 3u;
        int32_t m = a->p[v] + ((lfo_val(&s->lr, &a->lfo, &s->r) * a->lfo.depth) >> 15);
        p[v] = (uint8_t)clampi(m, at_lo(a->kind, v), at_hi(a->kind, v));
    }
    switch (a->kind) {
    case AT_FILTER: m_flt(p, (m_flt_t *)st, out); break;
    case AT_FOLD: m_fld(p, (m_fld_t *)st, out); break;
    case AT_MULAW: m_mu(p, (m_mu_t *)st, out); break;
    case AT_SHIFT: m_shf(p, (m_shf_t *)st, out); break;
    case AT_MODAL: m_mdl(p, (m_mdl_t *)st, out); break;
    case AT_STRING: m_str(p, (m_str_t *)st, out); break;
    case AT_VOWEL: m_vow(p, (m_vow_t *)st, out); break;
    case AT_TAPE: m_tap(p, (m_tap_t *)st, out); break;
    case AT_REVERSE: m_rvs(p, (m_rvs_t *)st, out); break;
    case AT_SPRING: m_spr(p, (m_spr_t *)st, out); break;
    case AT_GLITCH: m_glt(p, (m_glt_t *)st, out); break;
    case AT_CLOUD: m_cld(p, (m_cld_t *)st, out); break;
    case AT_SCREAM: m_scr(p, (m_scr_t *)st, out); break;
    case AT_RING: m_rng(p, (m_rng_t *)st, out); break;
    case AT_STARVE: m_stv(p, (m_stv_t *)st, out); break;
    case AT_CHEB: m_chb(p, (m_chb_t *)st, out); break;
    case AT_BANK: m_bnk(p, (m_bnk_t *)st, out); break;
    case AT_SMEAR: m_smr(p, (m_smr_t *)st, out); break;
    case AT_DIFFUSE: m_dif(p, (m_dif_t *)st, out); break;
    case AT_SLEW: m_slw(p, (m_slw_t *)st, out); break;
    case AT_XOR: m_xor(p, (m_xor_t *)st, out); break;
    case AT_DUST: m_dst(p, (m_dst_t *)st, out); break;
    case AT_WAVESET: m_wst(p, (m_wst_t *)st, out); break;
    case AT_XMOD: m_xmd(p, (m_xmd_t *)st, out); break;
    case AT_CHAOS: m_chs(p, (m_chs_t *)st, out); break;
    default:
        if (!at_own(a->kind))
            return;
        nx_block(a->kind - AT_SPIRAL, p, st, out);           /* (atoms.c's) */
        break;
    }
    fx_guard(s, out);
}
