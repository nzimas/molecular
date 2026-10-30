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
 * audio fades in. Values (the LFOs, the atoms') change in place between blocks. */

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

#define ATOMIC_GET(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define ATOMIC_SET(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)

/* the twelve directions, 30 degrees apart, Q8 */
static const int16_t DIR_X[12] = {256, 222, 128, 0, -128, -222, -256, -222, -128, 0, 128, 222};
static const int16_t DIR_Y[12] = {0, 128, 222, 256, 222, 128, 0, -128, -222, -256, -222, -128};

AINL int32_t node_r(const mol_node_t *n) { return n->kind == AT_GEN ? R_GEN : R_ATOM; }

/* ------------------------------------------------------------ the shape --- */
static void mol_draw_topology(mol_shape_t *s, rnd_t *r)
{
    static const uint8_t NW[MOL_MAXATOM] = {1, 2, 4, 4, 3};            /* one .. five atoms */
    static const uint8_t KW[AT_N] = {0, 4, 3, 2, 2, 3, 3, 4};          /* the kinds' weights */
    uint32_t n = 1u + rnd_weighted(r, NW, MOL_MAXATOM), a, j, nchorus = 0;
    memset(s, 0, sizeof *s);
    s->node[0].kind = AT_GEN;
    s->node[0].parent = NOPARENT;
    s->n = 1;
    for (a = 1; a <= n; a++) {
        uint8_t w[AT_N], cand[MOL_NODES], nc = 0;
        uint32_t kind;
        for (j = 0; j < AT_N; j++)
            w[j] = KW[j];
        for (j = 1; j < a; j++)
            if (fx_heavy(s->node[j].kind))
                w[s->node[j].kind] = 0;                                /* one DELAY, one REVERB at most */
        if (nchorus >= FX_CH_MAX)
            w[AT_CHORUS] = 0;
        kind = rnd_weighted(r, w, AT_N);
        for (j = 0; j < a; j++) {
            const mol_node_t *p = &s->node[j];
            if (j == 0 ? p->nchild < 3u : (!fx_heavy(p->kind) && p->nchild < 2u))
                cand[nc++] = (uint8_t)j;
        }
        j = cand[rnd_below(r, nc)];
        s->node[a].kind = (uint8_t)kind;
        s->node[a].parent = (uint8_t)j;
        s->node[j].nchild++;
        if (kind == AT_CHORUS)
            nchorus++;
        s->n++;
    }
}

/* the squared distance from (px, py) to the segment (ax, ay)-(bx, by) */
static int64_t seg_d2(int32_t px, int32_t py, int32_t ax, int32_t ay, int32_t bx, int32_t by)
{
    int64_t dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, t, qx, qy;
    t = l2 ? ((int64_t)(px - ax) * dx + (int64_t)(py - ay) * dy) * 1024 : 0;
    t = l2 ? (t < 0 ? 0 : t > l2 * 1024 ? 1024 : t / l2) : 0;          /* (main loop only: a 64-bit divide) */
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
            if (seg_d2(s->node[i].x, s->node[i].y, a->x, a->y, b->x, b->y) < (int64_t)m * m)
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
static void mol_pan(mol_design_t *d)
{
    const mol_shape_t *s = &d->shape;
    uint32_t i, j, nl = 0, order[MOL_NODES];
    for (i = 1; i < s->n; i++)
        if (!s->node[i].nchild) {
            for (j = nl; j > 0 && s->node[order[j - 1u]].x > s->node[i].x; j--)
                order[j] = order[j - 1u];
            order[j] = i;
            nl++;
        }
    d->leaf_q15 = nl <= 1u ? Q15 : (int32_t)(((uint32_t)Q15 << 12) / isqrt32((uint32_t)nl << 24));   /* Q15 / sqrt */
    for (j = 0; j < nl; j++) {
        mol_node_t *n = &d->shape.node[order[j]];
        uint32_t a;
        n->pan = (uint8_t)(nl == 1u ? 50u : 15u + j * 70u / (nl - 1u));
        a = (uint32_t)n->pan * 10737418u;                               /* (2^30 / 100: a quarter turn across the field) */
        d->pan_l[order[j]] = sin_ph(a + (1u << 30));
        d->pan_r[order[j]] = sin_ph(a);
    }
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
    mol_pan(d);
    gen_draw(&d->gen, seed);
}

/* --------------------------------------------------------- main loop --- */
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
/* 1 once silent for good (after mol_off) */
static int mol_poll(void)
{
    uint32_t i, nch = 0;
    if (ATOMIC_GET(mol_phase) != MP_HELD)
        return 0;
    if (mol_want_off)
        return 1;
    if (!mol_pending)
        return 0;
    mol_pending = 0;
    mol_draw(&mol_d, mol_pending_seed);
    dl_init();
    rv_init();
    for (i = 1; i < mol_d.shape.n; i++) {
        const mol_node_t *a = &mol_d.shape.node[i];
        fx_start(&mol_fs[i], a, nch);
        if (a->kind == AT_CHORUS)
            nch++;
        fx_apply_heavy(a);
    }
    gen_start(&mol_gs, &mol_d.gen, mol_d.seed);
    memset(&mol_meter, 0, sizeof mol_meter);
    mol_lev = Q15;                                   /* (held: the audio does not read it now) */
    ATOMIC_SET(mol_phase, MP_FADE_IN);
    return 0;
}
/* the left button: the generator's LFOs anew */
static void mol_shake_gen(uint32_t seed)
{
    rnd_t r;
    rnd_seed(&r, seed);
    gen_draw_lfos(&mol_d.gen, &r);
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

/* one block (CTL frames) into lr, interleaved stereo int16 */
static FAST void mol_block(int16_t *lr)
{
    static int32_t buf[MOL_NODES][CTL], l[CTL], r[CTL], wl[CTL], wr[CTL], scratch[CTL], dry[CTL];
    const mol_shape_t *s = &mol_d.shape;
    int ph = ATOMIC_GET(mol_phase);
    int32_t g;
    uint32_t i, k;
    if (ph == MP_HELD) {
        for (i = 0; i < 2u * CTL; i++)
            lr[i] = 0;
        return;
    }
    gen_block(&mol_gs, &mol_d.gen, buf[0]);
    for (k = 1; k < s->n; k++)
        if (!fx_heavy(s->node[k].kind))
            fx_block(&mol_fs[k], &s->node[k], buf[s->node[k].parent], buf[k]);
    for (i = 0; i < CTL; i++)
        l[i] = r[i] = scratch[i] = 0;
    for (k = 1; k < s->n; k++) {
        const mol_node_t *a = &s->node[k];
        const int32_t *src = fx_heavy(a->kind) ? buf[a->parent] : buf[k];
        if (a->nchild)
            continue;
        for (i = 0; i < CTL; i++) {
            dry[i] = mulq15(src[i], mol_d.leaf_q15);
            l[i] += mulq15(dry[i], mol_d.pan_l[k]);
            r[i] += mulq15(dry[i], mol_d.pan_r[k]);
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
            l[i] += wl[i];
            r[i] += wr[i];
            buf[k][i] = (wl[i] + wr[i]) >> 1;        /* the meter: what the heavy atom adds */
        }
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
    for (i = 0; i < CTL; i++) {
        lr[2u * i] = (int16_t)clampi(soft_limit(mulq15(l[i], g)), -32767, 32767);
        lr[2u * i + 1u] = (int16_t)clampi(soft_limit(mulq15(r[i], g)), -32767, 32767);
    }
}
