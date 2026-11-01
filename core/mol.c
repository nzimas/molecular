/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR: the molecule. A tree rooted at the generator (node 0) with one to five atoms: the generator binds up to
 * three, an atom up to three in all (its parent and two children), DELAY and REVERB only their parent. Each atom
 * takes its parent's output; the edge atoms (leaves) are summed into the stereo output, spread across it as they
 * are across the screen, each scaled by 1 / sqrt(leaves).
 *
 * Laid out as a skeletal formula: bonds BOND pixels long on a twelve-direction compass, an atom's children at 60
 * degrees either side of its own bond (a zigzag, or a branch), the generator's spread round it. A layout that leaves
 * the screen, crowds two atoms or crosses a bond through an atom is drawn again.
 *
 * Main loop and audio: the main loop asks for a new molecule (mol_new) or for silence (mol_off); the audio fades out
 * and holds (MP_HELD); the main loop (mol_poll) draws the molecule while nothing reads it and hands it back; the
 * audio fades in. Values (the LFOs, the atoms') change in place between blocks.
 *
 * 0.60: a shape changed on the same atoms (rewired, an atom grown or pruned) is not faded: it morphs. The main loop
 * prepares it (mol_morph_start: the new shape, which atom each node was, a new atom's state) and the audio takes it at
 * its next block (mol_morph_take): each atom keeps its state (its place in the arena with it), and over a second
 *   an atom bound to another parent crossfades its input from the old one to the new
 *   a grown atom comes in dry and turns wet; an atom to be pruned turns dry and leaves the mix first (mol_leave)
 *   every node's share of the output glides to what the new shape gives it (an edge atom's: 1 / sqrt(edge atoms))
 * so nothing is cut or cleared, and the sound is a continuum. */

#define BOND 44                  /* long enough to see the bond between the discs */
#define R_GEN 16
#define R_ATOM 13
#define MOL_FADE_STEP (Q15 / 40)  /* fades of ~30 ms (40 blocks) */
#define MOL_LEV_TARGET 18000     /* the leveller's peak, Q15 (~ -5 dB) */
#define MOL_LEV_MAX (2 * Q15)    /* .. and its most lift (+6 dB) for a quiet molecule */

enum { MP_HELD, MP_FADE_IN, MP_RUN, MP_FADE_OUT };

typedef struct {
    uint32_t seed;
    mol_shape_t shape;
    gen_design_t gen;
    int32_t leaf_q15;            /* 1 / sqrt(leaves) */
    int32_t pan_l[MOL_NODES], pan_r[MOL_NODES];   /* the edge atoms' gains, Q15 (equal power) */
    gen_clock_t clock;           /* (0.55, so last: an older patch loads with none) */
    uint8_t motion_rate;         /* 0.58: MOTION's events a minute (0: an older patch, MOTION_RATE_DEFAULT) */
    uint8_t mix;                 /* 0.64: the atoms' share of the sound, % + 1 (0: an older patch, 50 %) */
    uint8_t rsv[2];
} mol_design_t;

static mol_design_t mol_d;
static gen_state_t mol_gs;
static fx_state_t mol_fs[MOL_NODES];
static mol_meter_t mol_meter;
static int mol_phase = MP_HELD;  /* (both sides: atomic loads and stores) */
static int32_t mol_fade;         /* audio side: 0 .. Q15 */
static int32_t mol_vol;          /* audio side: the volume as it glides */
static int32_t mol_lev = Q15;    /* audio side: the leveller's gain, Q15 (up to MOL_LEV_MAX) */
static volatile int32_t mol_vol_want = Q15 * 7 / 10;
static uint32_t mol_pending_seed;
static volatile uint8_t mol_pending, mol_want_off;
static uint32_t mol_shapes;      /* 0.58: counts the shapes handed to the audio (motion.c follows it) */
/* 0.60, the morph: the main loop's request (mol_next_shape, the map: the old node each new one was, 0xFF: new, with
 * its state in mol_next_fs) and the audio's glides, per node, Q15, linear: a second end to end (MOL_GLIDE) */
#define MOL_GLIDE (Q15 / 1378)  /* (1378 blocks a second) */
static volatile uint8_t mol_morph, mol_leave;
static uint8_t mol_next_map[MOL_NODES];
static int32_t mol_gout[MOL_NODES];  /* its share of the output (an edge atom's 1 / sqrt(edge atoms), else 0) */
static int32_t mol_gwet[MOL_NODES];  /* how much of its effect is heard (0: its input passed through) */
static int32_t mol_gxf[MOL_NODES];   /* its input: from mol_src (0) to its parent (Q15) */
static int32_t mol_gl[MOL_NODES], mol_gr[MOL_NODES];   /* its stereo gains as they glide */
static uint8_t mol_src[MOL_NODES];

#define ATOMIC_GET(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define ATOMIC_SET(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)

/* the twelve directions, 30 degrees apart, Q8 */
static const int16_t DIR_X[12] = {256, 222, 128, 0, -128, -222, -256, -222, -128, 0, 128, 222};
static const int16_t DIR_Y[12] = {0, 128, 222, 256, 222, 128, 0, -128, -222, -256, -222, -128};

AINL int32_t node_r(const mol_node_t *n) { return n->kind == AT_GEN ? R_GEN : R_ATOM; }

/* ------------------------------------------------------------ the shape --- */
static const uint8_t MOL_KW[AT_N] = {0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                                    8, 10,                            /* the kinds' weights: DELAY, REVERB often */
                                    3, 3, 3, 3, 3, 3, 3, 3,           /* (atoms.c's) */
                                    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};
/* a kind for one more atom beside those in s: one DELAY, one REVERB at most, within the atoms' arena (AT_GEN: none) */
static uint32_t mol_draw_kind(const mol_shape_t *s, uint32_t upto, rnd_t *r)
{
    uint8_t w[AT_N];
    uint32_t j, used = 0, any = 0;
    for (j = 1; j < upto; j++)
        used += fx_bytes(s->node[j].kind);
    for (j = 0; j < AT_N; j++)
        w[j] = MOL_KW[j];
    for (j = 1; j < upto; j++)
        if (fx_heavy(s->node[j].kind))
            w[s->node[j].kind] = 0;
    for (j = 1; j < AT_N; j++) {
        if (used + fx_bytes(j) > FX_ARENA)
            w[j] = 0;
        any += w[j];
    }
    return any ? rnd_weighted(r, w, AT_N) : AT_GEN;
}
/* the atoms that may take one more child: the generator up to three, an atom up to two (DELAY, REVERB none) */
static uint32_t mol_parents(const mol_shape_t *s, uint32_t upto, uint8_t *cand)
{
    uint32_t j, nc = 0;
    for (j = 0; j < upto; j++) {
        const mol_node_t *p = &s->node[j];
        if (j == 0 ? p->nchild < 3u : (!fx_heavy(p->kind) && p->nchild < 2u))
            cand[nc++] = (uint8_t)j;
    }
    return nc;
}
static void mol_draw_topology(mol_shape_t *s, rnd_t *r)
{
    static const uint8_t NW[MOL_MAXATOM] = {1, 2, 4, 4, 3};            /* one .. five atoms */
    uint32_t n = 1u + rnd_weighted(r, NW, MOL_MAXATOM), a, j;
    memset(s, 0, sizeof *s);
    s->node[0].kind = AT_GEN;
    s->node[0].parent = NOPARENT;
    s->n = 1;
    for (a = 1; a <= n; a++) {
        uint8_t cand[MOL_NODES];
        uint32_t kind = mol_draw_kind(s, a, r), nc = mol_parents(s, a, cand);
        j = cand[rnd_below(r, (int32_t)nc)];
        s->node[a].kind = (uint8_t)kind;
        s->node[a].parent = (uint8_t)j;
        s->node[j].nchild++;
        s->n++;
    }
}

/* the squared distance from (px, py) to the segment (ax, ay)-(bx, by); screen coordinates: 32 bits suffice (the
 * products stay under 2^31 for anything within a few hundred pixels) */
static int32_t seg_d2(int32_t px, int32_t py, int32_t ax, int32_t ay, int32_t bx, int32_t by)
{
    int32_t dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, t, qx, qy;
    t = l2 ? ((px - ax) * dx + (py - ay) * dy) * 1024 : 0;
    t = l2 ? (t < 0 ? 0 : t > l2 * 1024 ? 1024 : t / l2) : 0;
    qx = ax + ((dx * t) >> 10) - px;
    qy = ay + ((dy * t) >> 10) - py;
    return qx * qx + qy * qy;
}

/* one try at placing the shape; 1 if it fits the screen with nothing crowded */
static int mol_try_layout(mol_shape_t *s, rnd_t *r)
{
    int32_t dir[MOL_NODES], used[MOL_NODES], x0 = 9999, x1 = -9999, y0 = 9999, y1 = -9999, dx, dy;
    uint32_t i, j, k;
    int32_t g0 = rnd_below(r, 12), gstep = s->node[0].nchild == 2u ? (rnd_chance(r, 50) ? 4 : 6) : 4;
    memset(used, 0, sizeof used);
    s->node[0].x = s->node[0].y = 0;
    for (i = 1; i < s->n; i++) {
        mol_node_t *n = &s->node[i];
        const mol_node_t *p = &s->node[n->parent];
        if (n->parent == 0)
            dir[i] = (g0 + gstep * used[0]) % 12;
        else if (p->nchild == 1u)
            dir[i] = (dir[n->parent] + (rnd_chance(r, 50) ? 2 : 10)) % 12;   /* a zigzag, one way or the other */
        else
            dir[i] = (dir[n->parent] + (used[n->parent] ? 10 : 2)) % 12;     /* a branch: 60 degrees either side */
        used[n->parent]++;
        n->x = (int16_t)(p->x + ((BOND * DIR_X[dir[i]]) >> 8));
        n->y = (int16_t)(p->y + ((BOND * DIR_Y[dir[i]]) >> 8));
    }
    for (i = 0; i < s->n; i++) {                                        /* the bounding box, discs included */
        int32_t rr = node_r(&s->node[i]) + 4;
        x0 = s->node[i].x - rr < x0 ? s->node[i].x - rr : x0;
        x1 = s->node[i].x + rr > x1 ? s->node[i].x + rr : x1;
        y0 = s->node[i].y - rr < y0 ? s->node[i].y - rr : y0;
        y1 = s->node[i].y + rr > y1 ? s->node[i].y + rr : y1;
    }
    if (x1 - x0 > SCR_W - 2 || y1 - y0 > SCR_H - 2)
        return 0;
    for (i = 0; i < s->n; i++)
        for (j = i + 1u; j < s->n; j++) {
            int32_t ex = s->node[i].x - s->node[j].x, ey = s->node[i].y - s->node[j].y;
            int32_t m = node_r(&s->node[i]) + node_r(&s->node[j]) + 10;   /* (room for the level rings) */
            if (ex * ex + ey * ey < m * m)
                return 0;
        }
    for (k = 1; k < s->n; k++) {                                        /* no bond through another atom */
        const mol_node_t *a = &s->node[k], *b = &s->node[a->parent];
        for (i = 0; i < s->n; i++) {
            int32_t m = node_r(&s->node[i]) + 3;
            if (i == k || i == a->parent)
                continue;
            if (seg_d2(s->node[i].x, s->node[i].y, a->x, a->y, b->x, b->y) < m * m)
                return 0;
        }
    }
    dx = (SCR_W - (x1 + x0)) / 2;                                       /* centred on the screen */
    dy = (SCR_H - (y1 + y0)) / 2;
    for (i = 0; i < s->n; i++) {
        s->node[i].x = (int16_t)(s->node[i].x + dx);
        s->node[i].y = (int16_t)(s->node[i].y + dy);
    }
    return 1;
}

/* a shape from a seed, always one that fits */
static void mol_draw_shape(mol_shape_t *s, uint32_t seed)
{
    rnd_t r;
    uint32_t t;
    rnd_seed(&r, seed ^ 0xA70Du);
    for (;;) {
        mol_draw_topology(s, &r);
        for (t = 0; t < 64u; t++)
            if (mol_try_layout(s, &r)) {
                s->seed = seed;
                return;
            }
    }
}

/* the edge atoms' stereo places, from left to right as they stand on the screen */
static void mol_pan_at(mol_design_t *d, uint32_t i, uint32_t pan)
{
    uint32_t a = pan * 10737418u;                    /* (2^30 / 100: a quarter turn across the field) */
    d->shape.node[i].pan = (uint8_t)pan;
    d->pan_l[i] = sin_ph(a + (1u << 30));
    d->pan_r[i] = sin_ph(a);
}
static void mol_pan(mol_design_t *d)
{
    const mol_shape_t *s = &d->shape;
    uint32_t i, j, nl = 0, order[MOL_NODES];
    for (i = 1; i < s->n; i++)                       /* (0.60: an inner atom too, where it stands: it may become an
                                                      * edge atom in a morph, and is heard from there) */
        mol_pan_at(d, i, (uint32_t)clampi(s->node[i].x * 100 / SCR_W, 10, 90));
    for (i = 1; i < s->n; i++)
        if (!s->node[i].nchild) {
            for (j = nl; j > 0 && s->node[order[j - 1u]].x > s->node[i].x; j--)
                order[j] = order[j - 1u];
            order[j] = i;
            nl++;
        }
    d->leaf_q15 = nl <= 1u ? Q15 : (int32_t)(((uint32_t)Q15 << 12) / isqrt32((uint32_t)nl << 24));   /* Q15 / sqrt */
    for (j = 0; j < nl; j++)
        mol_pan_at(d, order[j], nl == 1u ? 50u : 15u + j * 70u / (nl - 1u));
}

/* 0.58, while the atoms move (motion.c): each edge atom's stereo place where it stands across the screen */
static void mol_pan_x(mol_design_t *d)
{
    uint32_t i;
    for (i = 1; i < d->shape.n; i++)
        mol_pan_at(d, i, (uint32_t)clampi(d->shape.node[i].x * 100 / SCR_W, 10, 90));
}

/* the whole molecule from a seed (main loop: fm_measure renders the patch offline) */
static void mol_draw(mol_design_t *d, uint32_t seed)
{
    rnd_t r;
    uint32_t i;
    memset(d, 0, sizeof *d);
    d->seed = seed;
    mol_draw_shape(&d->shape, seed);
    rnd_seed(&r, seed ^ 0xF1A5u);
    for (i = 1; i < d->shape.n; i++)
        fx_draw_values(&d->shape.node[i], &r);
    rnd_seed(&r, seed ^ 0x1F0Fu);                    /* (their LFOs from a draw of their own: the values as before) */
    for (i = 1; i < d->shape.n; i++)
        fx_draw_lfo(&d->shape.node[i], &r);
    mol_pan(d);
    gen_draw(&d->gen, &d->clock, seed);
}

/* the same atoms (kinds, values, LFOs) bound anew: a new tree by the topology's rules (DELAY and REVERB last, so
 * at the edge) and a new layout (keep: none, each atom where it stands, for MOTION to draw them together). 1, into t,
 * if a wiring other than s's was found (some molecules have only one) */
static int mol_rewire_shape(mol_shape_t *t, const mol_shape_t *s, uint32_t seed, int keep, uint8_t *map)
{
    rnd_t r;
    uint32_t tries, i, j, n = s->n;
    rnd_seed(&r, seed);
    for (tries = 0; tries < 64u; tries++) {
        uint8_t ord[MOL_NODES], from[MOL_NODES], nl = 0, same = 1;
        for (i = 1; i < n; i++)                                         /* the atoms shuffled, the heavy ones last */
            ord[i] = (uint8_t)i;
        for (i = n - 1u; i > 1u; i--) {
            uint8_t x = ord[i];
            j = 1u + (uint32_t)rnd_below(&r, (int32_t)i);
            ord[i] = ord[j];
            ord[j] = x;
        }
        for (i = 1; i < n; i++)
            if (!fx_heavy(s->node[ord[i]].kind))
                from[1u + nl++] = ord[i];
        for (i = 1; i < n; i++)
            if (fx_heavy(s->node[ord[i]].kind))
                from[1u + nl++] = ord[i];
        from[0] = 0;
        memset(t, 0, sizeof *t);
        t->node[0] = s->node[0];
        t->node[0].nchild = 0;
        t->n = (uint8_t)n;
        for (i = 1; i < n; i++) {
            uint8_t cand[MOL_NODES], nc = 0;
            for (j = 0; j < i; j++) {
                const mol_node_t *p = &t->node[j];
                if (j == 0 ? p->nchild < 3u : (!fx_heavy(p->kind) && p->nchild < 2u))
                    cand[nc++] = (uint8_t)j;
            }
            j = cand[rnd_below(&r, nc)];
            t->node[i] = s->node[from[i]];
            t->node[i].parent = (uint8_t)j;
            t->node[i].nchild = 0;
            t->node[j].nchild++;
            if (from[j] != s->node[from[i]].parent)                     /* bound to another atom than before */
                same = 0;
        }
        if (same && tries < 63u)
            continue;
        if (map)                                     /* (which atom each node was: the morph's) */
            for (i = 0; i < n; i++)
                map[i] = i ? from[i] : 0;
        if (keep) {
            t->seed = s->seed;
            return !same;
        }
        for (i = 0; i < 64u; i++)
            if (mol_try_layout(t, &r)) {
                t->seed = s->seed;
                return !same;
            }
    }
    return 0;
}

/* 0.58, MOTION's growing: s and one atom more (its kind, values and LFO drawn), bound to an atom with room, placed a
 * bond from it (the rest where they stand: 0.59). 1, into t, if there was room (five atoms at most, the arena) */
static int mol_grow_shape(mol_shape_t *t, const mol_shape_t *s, rnd_t *r, uint8_t *map)
{
    uint8_t cand[MOL_NODES];
    uint32_t n = s->n, kind, nc, j, i;
    if (n >= MOL_NODES || (kind = mol_draw_kind(s, n, r)) == AT_GEN || !(nc = mol_parents(s, n, cand)))
        return 0;
    *t = *s;
    for (i = 0; i < n; i++)
        map[i] = (uint8_t)i;
    map[n] = 0xFF;
    j = cand[rnd_below(r, (int32_t)nc)];
    memset(&t->node[n], 0, sizeof t->node[n]);
    t->node[n].kind = (uint8_t)kind;
    t->node[n].parent = (uint8_t)j;
    t->node[j].nchild++;
    t->n = (uint8_t)(n + 1u);
    fx_draw_values(&t->node[n], r);
    fx_draw_lfo(&t->node[n], r);
    i = (uint32_t)rnd_below(r, 12);
    t->node[n].x = (int16_t)clampi(s->node[j].x + ((BOND * DIR_X[i]) >> 8), R_ATOM + 3, SCR_W - 1 - R_ATOM - 3);
    t->node[n].y = (int16_t)clampi(s->node[j].y + ((BOND * DIR_Y[i]) >> 8), R_ATOM + 3, SCR_H - 1 - R_ATOM - 3);
    return 1;
}
/* .. and its pruning: one edge atom (mol_prune_pick) taken away, the rest where they stand. 1, into t, if done */
static uint32_t mol_prune_pick(const mol_shape_t *s, rnd_t *r)   /* 0: none (never the last atom) */
{
    uint8_t leaf[MOL_NODES];
    uint32_t i, nl = 0;
    if (s->n <= 2u)
        return 0;
    for (i = 1; i < s->n; i++)
        if (!s->node[i].nchild)
            leaf[nl++] = (uint8_t)i;
    return leaf[rnd_below(r, (int32_t)nl)];
}
static int mol_prune_shape(mol_shape_t *t, const mol_shape_t *s, uint32_t k, uint8_t *map)
{
    uint32_t i;
    if (k < 1u || k >= s->n || s->node[k].nchild || s->n <= 2u)
        return 0;
    *t = *s;
    for (i = 0; i + 1u < s->n; i++)
        map[i] = (uint8_t)(i < k ? i : i + 1u);
    t->node[s->node[k].parent].nchild--;
    for (i = k; i + 1u < s->n; i++) {                /* the rest moved down one (a leaf: nobody's parent) */
        t->node[i] = t->node[i + 1u];
        if (t->node[i].parent > k)
            t->node[i].parent--;
    }
    t->n--;
    return 1;
}

/* --------------------------------------------------------- main loop --- */
static mol_shape_t mol_next_shape;
static mol_design_t mol_next_design;
static void mol_new(uint32_t seed)
{
    mol_pending_seed = seed;
    mol_pending = 1;
    if (ATOMIC_GET(mol_phase) != MP_HELD)
        ATOMIC_SET(mol_phase, MP_FADE_OUT);
}
static void mol_off(void)
{
    mol_want_off = 1;
    if (ATOMIC_GET(mol_phase) != MP_HELD)
        ATOMIC_SET(mol_phase, MP_FADE_OUT);
}
/* sound allowed again after mol_off (powered on again); mol_new then starts it */
static void mol_wake(void) { mol_want_off = 0; }
static void mol_glides_reset(void);
/* 1 once silent for good (after mol_off) */
static int mol_poll(void)
{
    uint32_t i, at = 0;
    if (mol_pending == 4u) {                         /* a morph: done once the audio took it */
        if (!ATOMIC_GET(mol_morph)) {
            mol_pending = 0;
            mol_shapes++;
        }
        return 0;
    }
    if (ATOMIC_GET(mol_phase) != MP_HELD)
        return 0;
    if (mol_want_off)
        return 1;
    if (!mol_pending)
        return 0;
    if (mol_pending == 3u) {                  /* a patch loaded: the molecule as saved, started afresh */
        memcpy(&mol_d, &mol_next_design, sizeof mol_d);
        dl_init();
        rv_init();
    } else {
        uint8_t rate = mol_d.motion_rate, mix = mol_d.mix;   /* (MOTION's rate and the mix carried on) */
        mol_draw(&mol_d, mol_pending_seed);
        mol_d.motion_rate = rate;
        mol_d.mix = mix;
        dl_init();
        rv_init();
    }
    rack_root = mol_d.gen.note[0];                   /* MODAL, STRING, BANK: tuned to the chord's root */
    for (i = 1; i < mol_d.shape.n; i++) {
        const mol_node_t *a = &mol_d.shape.node[i];
        fx_start(&mol_fs[i], a, at);                 /* each atom's state at its place in the arena, cleared */
        at += fx_bytes(a->kind);
        fx_apply_heavy(a);
    }
    gen_start(&mol_gs, &mol_d.gen, &mol_d.clock, mol_d.seed);
    mol_glides_reset();
    mol_pending = 0;
    mol_shapes++;
    memset(&mol_meter, 0, sizeof mol_meter);
    mol_lev = Q15;                                   /* (held: the audio does not read it now) */
    ATOMIC_SET(mol_phase, MP_FADE_IN);
    return 0;
}
/* the menu's LFO: the generator's LFOs and every atom's anew */
static void mol_shake_lfos(uint32_t seed)
{
    rnd_t r;
    uint32_t i;
    rnd_seed(&r, seed);
    gen_draw_lfos(&mol_d.gen, &r);
    for (i = 1; i < mol_d.shape.n; i++) {
        mol_node_t tmp = mol_d.shape.node[i];
        fx_draw_lfo(&tmp, &r);
        mol_d.shape.node[i].lfo.depth = 0;           /* (still while it changes: the audio reads it between blocks) */
        mol_d.shape.node[i].lval = tmp.lval;
        mol_d.shape.node[i].lfo = tmp.lfo;
    }
}
/* 0.60: where a new atom's state fits in the arena, between the states the current atoms hold (they keep theirs
 * through a morph). 1 and *at if it does */
static int mol_arena_fit(uint32_t bytes, uint32_t *at)
{
    const mol_shape_t *s = &mol_d.shape;
    uint32_t c, i, try_at;
    for (c = 0; c < s->n; c++) {                     /* the candidates: the start, and the end of each state */
        int ok = 1;
        try_at = c == 0 ? 0u : (mol_fs[c].at + fx_bytes(s->node[c].kind) + 7u) & ~7u;
        if (c && !fx_bytes(s->node[c].kind))
            continue;
        if (try_at + bytes > FX_ARENA)
            continue;
        for (i = 1; i < s->n && ok; i++) {
            uint32_t b = fx_bytes(s->node[i].kind);
            if (b && try_at < mol_fs[i].at + b && mol_fs[i].at < try_at + bytes)
                ok = 0;
        }
        if (ok) {
            *at = try_at;
            return 1;
        }
    }
    return 0;
}
/* the shape in mol_next_shape (mol_next_map: which atom each node was) handed to the audio as a morph (the left
 * button's rewiring, MOTION's). A new atom's state is set up here, in arena nobody uses; a DELAY or REVERB new to the
 * molecule is cleared here too (nothing sounds through it yet). 0 if a new atom's state did not fit */
static fx_state_t mol_next_fs[MOL_NODES];
static int mol_morph_start(void)
{
    uint32_t i, had = 0;
    for (i = 1; i < mol_d.shape.n; i++)
        had |= 1u << mol_d.shape.node[i].kind;
    for (i = 1; i < mol_next_shape.n; i++) {
        const mol_node_t *a = &mol_next_shape.node[i];
        uint32_t at = 0;
        if (mol_next_map[i] != 0xFFu)
            continue;
        if (fx_bytes(a->kind) && !mol_arena_fit(fx_bytes(a->kind), &at))
            return 0;
        fx_start(&mol_next_fs[i], a, at);
        if (!(had >> a->kind & 1u)) {
            if (a->kind == AT_DELAY)
                dl_init();
            else if (a->kind == AT_REVERB)
                rv_init();
        }
        fx_apply_heavy(a);
    }
    mol_pending = 4;
    ATOMIC_SET(mol_morph, 1);
    return 1;
}
/* the audio, at the start of a block: the morph taken. Each node's state and glides from the node it was; a node
 * now bound to another parent crossfades from that one (its new number; or its new parent, if it was pruned) */
static void mol_morph_take(void)
{
    static fx_state_t fs[MOL_NODES];
    static int32_t go[MOL_NODES], gw[MOL_NODES], gx[MOL_NODES], gl[MOL_NODES], gr[MOL_NODES];
    static uint8_t src[MOL_NODES], par[MOL_NODES];
    uint8_t inv[MOL_NODES];
    uint32_t i, n0 = mol_d.shape.n;
    memcpy(fs, mol_fs, sizeof fs);
    memcpy(go, mol_gout, sizeof go);
    memcpy(gw, mol_gwet, sizeof gw);
    memcpy(gx, mol_gxf, sizeof gx);
    memcpy(gl, mol_gl, sizeof gl);
    memcpy(gr, mol_gr, sizeof gr);
    memcpy(src, mol_src, sizeof src);
    for (i = 0; i < n0; i++)
        par[i] = mol_d.shape.node[i].parent;
    memset(inv, 0xFF, sizeof inv);
    for (i = 0; i < mol_next_shape.n; i++)
        if (mol_next_map[i] != 0xFFu)
            inv[mol_next_map[i]] = (uint8_t)i;
    for (i = 1; i < mol_next_shape.n; i++) {
        uint32_t o = mol_next_map[i], np = mol_next_shape.node[i].parent;
        if (o == 0xFFu) {                            /* grown: dry at first, out of the mix at first */
            mol_fs[i] = mol_next_fs[i];
            mol_gout[i] = 0;
            mol_gwet[i] = 0;
            mol_gxf[i] = Q15;
            mol_src[i] = (uint8_t)np;
            mol_gl[i] = mol_d.pan_l[i];
            mol_gr[i] = mol_d.pan_r[i];
            continue;
        }
        mol_fs[i] = fs[o];
        mol_gout[i] = go[o];
        mol_gwet[i] = gw[o];
        mol_gl[i] = gl[o];
        mol_gr[i] = gr[o];
        if (inv[par[o]] != np) {                     /* another parent: from the old one */
            mol_src[i] = inv[par[o]] != 0xFFu ? inv[par[o]] : (uint8_t)np;
            mol_gxf[i] = inv[par[o]] != 0xFFu ? 0 : Q15;
        } else {
            mol_src[i] = gx[o] < Q15 && inv[src[o]] != 0xFFu ? inv[src[o]] : (uint8_t)np;
            mol_gxf[i] = mol_src[i] == np ? Q15 : gx[o];
        }
    }
    mol_d.shape = mol_next_shape;
    mol_pan(&mol_d);
    mol_leave = 0;
    ATOMIC_SET(mol_morph, 0);
}
/* the glides as they are when nothing has sounded yet (a new molecule, a patch): each node where it belongs */
static void mol_glides_reset(void)
{
    uint32_t i;
    for (i = 0; i < MOL_NODES; i++) {
        mol_gout[i] = -1;                            /* (-1: the audio sets it at once, to what the shape gives it) */
        mol_gwet[i] = Q15;
        mol_gxf[i] = Q15;
        mol_src[i] = 0;
        mol_gl[i] = mol_d.pan_l[i];
        mol_gr[i] = mol_d.pan_r[i];
    }
    mol_leave = 0;
}
/* the left button: the molecule rewired (mol_rewire_shape), through a fade. 0: no other wiring, or busy */
static int mol_rewire(uint32_t seed, int keep)
{
    if (mol_pending || mol_want_off || !mol_rewire_shape(&mol_next_shape, &mol_d.shape, seed, keep, mol_next_map))
        return 0;
    return mol_morph_start();
}
/* MOTION: an atom grown or pruned, through a fade. 0: no room (or none to prune), or busy */
static int mol_grow(rnd_t *r)
{
    if (mol_pending || mol_want_off || !mol_grow_shape(&mol_next_shape, &mol_d.shape, r, mol_next_map))
        return 0;
    return mol_morph_start();
}
/* MOTION's pruning, in two steps: mol_prune_begin(k) turns atom k dry and out of the mix (over a second); once its
 * share is gone (mol_gone), mol_prune(k) takes it away */
static void mol_prune_begin(uint32_t k) { mol_leave = (uint8_t)k; }
static int mol_gone(uint32_t k) { return k < mol_d.shape.n && mol_gout[k] == 0 && mol_gwet[k] == 0; }
static int mol_prune(uint32_t k)
{
    if (mol_pending || mol_want_off || !mol_prune_shape(&mol_next_shape, &mol_d.shape, k, mol_next_map))
        return 0;
    return mol_morph_start();
}

/* the right button: every atom's values anew (kinds and places stay) */
static void mol_shake_fx(uint32_t seed)
{
    rnd_t r;
    uint32_t i;
    rnd_seed(&r, seed);
    for (i = 1; i < mol_d.shape.n; i++) {
        mol_node_t tmp = mol_d.shape.node[i];
        fx_draw_values(&tmp, &r);
        memcpy(mol_d.shape.node[i].p, tmp.p, sizeof tmp.p);
        fx_apply_heavy(&mol_d.shape.node[i]);
    }
}
/* a patch: this molecule (as saved, mol_d's whole design) through a fade, started afresh. 0: busy */
static int mol_load(const mol_design_t *d)
{
    if (mol_pending || mol_want_off)
        return 0;
    memcpy(&mol_next_design, d, sizeof mol_next_design);
    mol_pending = 3;
    if (ATOMIC_GET(mol_phase) != MP_HELD)
        ATOMIC_SET(mol_phase, MP_FADE_OUT);
    return 1;
}
static void mol_volume(int32_t q15) { mol_vol_want = clampi(q15, 0, Q15); }

/* ------------------------------------------------------------- audio --- */
AINL int32_t soft_limit(int32_t x)                   /* untouched below 0.75, then bent into the last quarter */
{
    int32_t a = x < 0 ? -x : x;
    if (a <= 24576)
        return x;
    a = 24576 + mulq15(tanh_q15((a - 24576) * 4), 8191);
    return x < 0 ? -a : a;
}

/* the atoms' share of the sound, Q15 (the patch's MIX, % + 1; 0: an older patch, half), and as it glides */
static uint32_t mol_mix(void) { return mol_d.mix ? mol_d.mix - 1u : 50u; }
static int32_t mol_wet = Q15 / 2;

/* one block (CTL frames) into lr, interleaved stereo int16 */
static FAST void mol_block(int16_t *lr)
{
    static const int32_t LEAF_G[MOL_MAXATOM] = {32767, 23170, 18919, 16384, 14654};   /* 1 / sqrt(1 .. 5) */
    static int32_t buf[MOL_NODES][CTL], in[MOL_NODES][CTL], l[CTL], r[CTL], wl[CTL], wr[CTL], scratch[CTL], dry[CTL];
    const mol_shape_t *s;
    int ph;
    int32_t g, want[MOL_NODES];
    uint32_t i, k, nl = 0, cnt[MOL_NODES], lv;
    if (ATOMIC_GET(mol_morph))                       /* 0.60: a morph waiting: taken now, between blocks */
        mol_morph_take();
    s = &mol_d.shape;
    ph = ATOMIC_GET(mol_phase);
    if (ph == MP_HELD) {
        for (i = 0; i < 2u * CTL; i++)
            lr[i] = 0;
        return;
    }
    gen_block(&mol_gs, &mol_d.gen, &mol_d.clock, buf[0]);
    lv = mol_leave;                                  /* each node's share of the output: an edge atom's (the one
                                                      * leaving counted gone), 1 / sqrt(edge atoms); else none */
    for (k = 0; k < s->n; k++)
        cnt[k] = s->node[k].nchild;
    if (lv && lv < s->n)
        cnt[s->node[lv].parent]--;
    for (k = 1; k < s->n; k++)
        nl += k != lv && !cnt[k];
    for (k = 1; k < s->n; k++)
        want[k] = k != lv && !cnt[k] ? LEAF_G[nl ? nl - 1u : 0] : 0;
    for (k = 1; k < s->n; k++) {
        const mol_node_t *a = &s->node[k];
        const int32_t *pb = buf[a->parent], *sb = buf[mol_src[k]];
        int32_t xf = mol_gxf[k], wet;
        if (mol_gout[k] < 0)                         /* (a new molecule: at once) */
            mol_gout[k] = want[k];
        mol_gout[k] += clampi(want[k] - mol_gout[k], -MOL_GLIDE, MOL_GLIDE);
        mol_gwet[k] += clampi((k == lv ? 0 : Q15) - mol_gwet[k], -MOL_GLIDE, MOL_GLIDE);
        mol_gxf[k] = xf = mini(xf + MOL_GLIDE, Q15);
        mol_gl[k] += (mol_d.pan_l[k] - mol_gl[k]) >> 3;   /* (~8 blocks: no step as an atom moves across) */
        mol_gr[k] += (mol_d.pan_r[k] - mol_gr[k]) >> 3;
        if (xf < Q15)                                /* its input: from the old parent to the new */
            for (i = 0; i < CTL; i++)
                in[k][i] = sb[i] + mulq15(pb[i] - sb[i], xf);
        else
            for (i = 0; i < CTL; i++)
                in[k][i] = pb[i];
        if (fx_heavy(a->kind))
            continue;
        fx_block(&mol_fs[k], a, in[k], buf[k]);
        if ((wet = mol_gwet[k]) < Q15)               /* coming in, or leaving: partly dry */
            for (i = 0; i < CTL; i++)
                buf[k][i] = in[k][i] + mulq15(buf[k][i] - in[k][i], wet);
    }
    for (i = 0; i < CTL; i++)
        l[i] = r[i] = scratch[i] = 0;
    for (k = 1; k < s->n; k++) {
        const mol_node_t *a = &s->node[k];
        const int32_t *src = fx_heavy(a->kind) ? in[k] : buf[k];
        int32_t go = mol_gout[k], gw = mol_gwet[k];
        if (!go)
            continue;
        for (i = 0; i < CTL; i++) {
            dry[i] = mulq15(src[i], go);
            l[i] += mulq15(dry[i], mol_gl[k]);
            r[i] += mulq15(dry[i], mol_gr[k]);
        }
        if (!fx_heavy(a->kind))
            continue;
        for (i = 0; i < CTL; i++)
            wl[i] = wr[i] = 0;
        if (a->kind == AT_DELAY)
            dl_block(dry, wl, wr, scratch);
        else
            rv_block(dry, wl, wr, CTL);
        for (i = 0; i < CTL; i++) {
            if (gw < Q15) {                          /* (coming in, or leaving) */
                wl[i] = mulq15(wl[i], gw);
                wr[i] = mulq15(wr[i], gw);
            }
            l[i] += wl[i];
            r[i] += wr[i];
            buf[k][i] = (wl[i] + wr[i]) >> 1;        /* the meter: what the heavy atom adds */
        }
    }
    mol_wet += ((int32_t)(mol_mix() * Q15 / 100u) - mol_wet) >> 6;   /* (0.64: MIX, gliding: no step) */
    for (i = 0; i < CTL; i++) {                      /* 0.53: the effects half wet (0.64: the patch's MIX): the
                                                      * engine's own sound, centred (equal power), and the atoms' tree */
        int32_t c = mulq15(buf[0][i], 23170);
        l[i] = mulq15(l[i], mol_wet) + mulq15(c, Q15 - mol_wet);
        r[i] = mulq15(r[i], mol_wet) + mulq15(c, Q15 - mol_wet);
    }
    for (k = 0; k < s->n; k++) {                     /* the meters: a peak that falls ~20 dB a second */
        int32_t pk = 0, m = mol_meter.level[k];
        for (i = 0; i < CTL; i++)
            pk = maxi(pk, absi(buf[k][i]));
        mol_meter.level[k] = pk > m ? pk : m - (m >> 7);
    }
    mol_meter.blocks++;
    if (ph == MP_FADE_IN && (mol_fade += MOL_FADE_STEP) >= Q15) {
        mol_fade = Q15;
        ATOMIC_SET(mol_phase, MP_RUN);
    } else if (ph == MP_FADE_OUT && (mol_fade -= MOL_FADE_STEP) <= 0) {
        mol_fade = 0;
        ATOMIC_SET(mol_phase, MP_HELD);
    }
    {   /* the leveller: random molecules differ by tens of dB. The block's peak is known before its gain is applied,
         * so a loud block is brought down in that same block (no overshoot); a quiet one is lifted over ~0.7 s */
        int32_t pk = 1, want;
        for (i = 0; i < CTL; i++)
            pk = maxi(pk, maxi(absi(l[i]), absi(r[i])));
        want = pk * 2 <= MOL_LEV_TARGET ? MOL_LEV_MAX : (int32_t)(((uint32_t)MOL_LEV_TARGET << 15) / (uint32_t)pk);
        want = want > MOL_LEV_MAX ? MOL_LEV_MAX : want;
        if (want < mol_lev)
            mol_lev = want;
        else
            mol_lev += (want - mol_lev) >> 10;
    }
    mol_vol += (mol_vol_want - mol_vol) >> 4;
    g = mulq12(mulq15(mol_fade, mol_vol), mol_lev >> 3);   /* (mol_lev Q15 >> 3 = Q12: it may reach 2.0) */
    {   /* one block of look-ahead: the block before this one goes out now, its gain gliding from the gain it began
         * with to this block's gain, so a loud block is met by a gain already down (no step at a block's edge: the
         * leveller's drop clicked there, the volume and the fade stepped too). 32 samples late */
        static int32_t g_last, pl[CTL], pr[CTL];
        int32_t gi = g_last;
        for (i = 0; i < CTL; i++) {
            gi += (g - gi) >> 3;                     /* one pole per sample: no corner at a block's edge */
            lr[2u * i] = (int16_t)clampi(soft_limit(mulq15(pl[i], gi)), -32767, 32767);
            lr[2u * i + 1u] = (int16_t)clampi(soft_limit(mulq15(pr[i], gi)), -32767, 32767);
            pl[i] = l[i];
            pr[i] = r[i];
        }
        g_last = gi;
    }
}
