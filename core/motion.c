/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR: MOTION (0.58), after Vascular's angiogenesis: the molecule searching by itself, fluidly. Held right
 * button: on or off. While on:
 *   the drift   every disc wanders, its heading turning smoothly (a random walk of its turn); the bonds pull like
 *               springs towards their length, discs too close push apart, the screen's edges turn them back. Nothing
 *               is placed: everything moves there (and a gentle pull towards the middle of the screen) (0.59; 0.58 laid the molecule out anew at each event, a leap)
 *   the events  MOTION RATE times a minute (the patch's, SETTINGS) one, drawn as Vascular draws them, eight ways:
 *                 2 in 8 rewired (the same atoms, each where it stands; the new bonds draw them together)
 *                 1 in 8 an atom grown (else pruned)        1 in 8 an atom grown (else rewired)
 *                 1 in 8 an atom pruned (never the last)    3 in 8 one atom's values anew
 *               a grown atom swells from its parent's side; a pruned one shrinks away first, then goes
 * And the sound moves as the picture does (0.60): a changed shape morphs (mol.c: an atom's input crossfades to its new
 * parent, a grown atom comes in dry and turns wet, a pruned one turns dry and leaves the mix before it goes, over a
 * second); one atom's values anew glide there, from where they are, over a whole interval between events (at least
 * 1.5 s), so the values are always on their way somewhere. Only the values an LFO may move glide (FX_LFO_OK: a switch
 * or a coarse step would jump), and DELAY's and REVERB's not at all. Never the generator: it is the voice (as
 * Vascular's angiogenesis never touches the generators). Each atom's stereo place follows it across. The drift's pace follows RATE too: RATE x MOT_PX_PER_EVENT tenths of a pixel a second, each disc 0.6 .. 1.4
 * of it. Main loop only. */

#define MOTION_RATE_DEFAULT 20u
#define MOT_PX_PER_EVENT 4       /* tenths of a pixel a second per event a minute: 20 a minute, 8 px/s */
#define MOT_PULL_MS 200          /* a bond's stretch, a crowding: about this long to even out */
#define MOT_PULL_MAX 30           /* .. but no faster than this, pixels a second each end (a rewiring draws its atoms
                                  * together over a second or two, not at once) */
#define MOT_HOME_MS 6000         /* the pull towards the screen's middle (no molecule left in a corner) */
#define MOT_SWELL_MS 1000        /* a grown atom's swelling, a pruned one's shrinking (as its sound: MOL_GLIDE) */
#define MOT_TURN_MAX 900000      /* the most a heading turns a millisecond (2^32 a turn: ~1.3 turns a second) */
static const uint8_t MOTION_RATES[] = {6, 10, 15, 20, 30, 45, 67, 90, 120};   /* Vascular's (67: its 60 / 0.9) */
#define MOTION_NRATES (sizeof MOTION_RATES / sizeof MOTION_RATES[0])

static struct {
    int on;
    uint32_t shapes;             /* the shape the drift follows (mol_shapes) */
    uint32_t last, next;         /* the last tick; the next event */
    int32_t x[MOL_NODES], y[MOL_NODES];          /* where each disc is, Q8 pixels */
    uint32_t hd[MOL_NODES];                      /* its heading (2^32 a turn) */
    int32_t turn[MOL_NODES];                     /* .. and how fast it turns, a millisecond */
    int32_t sp[MOL_NODES];                       /* its pace, Q8 (0.6 .. 1.4) */
    int32_t size[MOL_NODES];                     /* how much of it is drawn, Q8: swelling, shrinking */
    int16_t v0[MOL_NODES][4], v1[MOL_NODES][4];  /* its values gliding: from, to (FX_LFO_OK's only) */
    uint32_t vt0[MOL_NODES], vdur[MOL_NODES];    /* .. since, over (0: not gliding) */
    uint32_t leaving;            /* the atom shrinking before it is pruned (0: none) */
    uint32_t events;             /* (the tests count them) */
    rnd_t r;
} mot;

static uint32_t motion_rate(void) { return mol_d.motion_rate ? mol_d.motion_rate : MOTION_RATE_DEFAULT; }
static uint32_t motion_rate_index(void)
{
    uint32_t i, v = motion_rate();
    for (i = 0; i + 1u < MOTION_NRATES && MOTION_RATES[i] < v; i++)
        ;
    return i;
}
/* the settings' turn: the next rate up or down */
static void motion_rate_step(int32_t d)
{
    int32_t i = clampi((int32_t)motion_rate_index() + d, 0, (int32_t)MOTION_NRATES - 1);
    mol_d.motion_rate = MOTION_RATES[i];
}
/* how large node i is drawn, Q8 (all of it, unless MOTION swells or shrinks it) */
static int32_t motion_size(uint32_t i) { return mot.on ? mot.size[i] : 256; }

static void mot_new_disc(uint32_t i)
{
    mot.hd[i] = rnd_next(&mot.r);
    mot.turn[i] = 0;
    mot.sp[i] = 154 + rnd_below(&mot.r, 205);    /* 0.6 .. 1.4, Q8 */
}
/* a new shape: each disc that was already there (an atom stands where it stood: rewired, grown, pruned all keep
 * them; within a few pixels, as it went on moving through the fade) keeps its place, heading and size; one that was not swells from nothing. A shape with none in common (a new
 * molecule, a patch loaded) is taken whole, as drawn */
static void mot_follow(void)
{
    int32_t x[MOL_NODES], y[MOL_NODES], sp[MOL_NODES], sz[MOL_NODES], tn[MOL_NODES];
    int16_t v0[MOL_NODES][4], v1[MOL_NODES][4];
    uint32_t hd[MOL_NODES], vt0[MOL_NODES], vdur[MOL_NODES], i, j, n0 = 0, kept = 0, used = 0;
    int took[MOL_NODES];
    memcpy(x, mot.x, sizeof x);
    memcpy(y, mot.y, sizeof y);
    memcpy(hd, mot.hd, sizeof hd);
    memcpy(tn, mot.turn, sizeof tn);
    memcpy(sp, mot.sp, sizeof sp);
    memcpy(sz, mot.size, sizeof sz);
    memcpy(v0, mot.v0, sizeof v0);
    memcpy(v1, mot.v1, sizeof v1);
    memcpy(vt0, mot.vt0, sizeof vt0);
    memcpy(vdur, mot.vdur, sizeof vdur);
    for (i = 0; i < MOL_NODES; i++)
        n0 += sz[i] > 0;
    for (i = 0; i < mol_d.shape.n; i++) {
        const mol_node_t *a = &mol_d.shape.node[i];
        took[i] = 0;
        for (j = 0; j < MOL_NODES && !took[i]; j++)
            if (!(used >> j & 1u) && sz[j] > 0 && absi((x[j] >> 8) - a->x) <= 6 && absi((y[j] >> 8) - a->y) <= 6) {
                used |= 1u << j;
                took[i] = 1;
                mot.x[i] = x[j];
                mot.y[i] = y[j];
                mot.hd[i] = hd[j];
                mot.turn[i] = tn[j];
                mot.sp[i] = sp[j];
                mot.size[i] = sz[j];
                memcpy(mot.v0[i], v0[j], sizeof v0[j]);
                memcpy(mot.v1[i], v1[j], sizeof v1[j]);
                mot.vt0[i] = vt0[j];
                mot.vdur[i] = vdur[j];
                kept++;
            }
    }
    for (i = 0; i < mol_d.shape.n; i++)
        if (!took[i]) {
            mot.x[i] = mol_d.shape.node[i].x << 8;
            mot.y[i] = mol_d.shape.node[i].y << 8;
            mot_new_disc(i);
            mot.size[i] = kept ? 0 : 256;            /* (grown: from nothing; a whole new molecule: as it is) */
            mot.vdur[i] = 0;
        }
    for (i = mol_d.shape.n; i < MOL_NODES; i++)
        mot.size[i] = 0;
    (void)n0;
    mot.leaving = 0;
    mot.shapes = mol_shapes;
}

static void motion_start(uint32_t seed, uint32_t now)
{
    uint32_t i;
    rnd_seed(&mot.r, seed);
    mot.on = 1;
    mot.last = now;
    mot.next = now + 60000u / motion_rate();
    for (i = 0; i < MOL_NODES; i++)
        mot.size[i] = 0;                             /* (nothing in common: the molecule taken whole) */
    mot_follow();
}
/* the values gliding stop where they are (the right button's new values take over) */
static void motion_hold_values(void)
{
    uint32_t i;
    for (i = 0; i < MOL_NODES; i++)
        mot.vdur[i] = 0;
}
static void motion_stop(void)
{
    uint32_t i;
    mot.on = 0;
    if (mot.leaving)                                 /* (one leaving comes back) */
        mol_leave = 0;
    mot.leaving = 0;
    for (i = 0; i < MOL_NODES; i++)                  /* values gliding stop where they are */
        mot.vdur[i] = 0;
}

/* one atom's values anew, gliding there from where they are (the values an LFO may move; not DELAY's, REVERB's) */
static void mot_redraw(uint32_t now)
{
    uint32_t cand[MOL_NODES], nc = 0, i, k, ok;
    mol_node_t tmp;
    if (mol_fxg.on)                                  /* (build 75: FX MORPH's glide has them all) */
        return;
    for (i = 1; i < mol_d.shape.n; i++)
        if (FX_LFO_OK[mol_d.shape.node[i].kind])
            cand[nc++] = i;
    if (!nc)
        return;
    i = cand[rnd_below(&mot.r, (int32_t)nc)];
    tmp = mol_d.shape.node[i];
    fx_draw_values(&tmp, &mot.r);
    ok = FX_LFO_OK[tmp.kind];
    for (k = 0; k < 4u; k++) {
        mot.v0[i][k] = mol_d.shape.node[i].p[k];
        mot.v1[i][k] = (ok >> k & 1u) ? tmp.p[k] : mol_d.shape.node[i].p[k];
    }
    mot.vt0[i] = now;
    mot.vdur[i] = maxi((int32_t)(60000u / motion_rate()), 1500);
}
static void mot_event(uint32_t now)
{
    uint32_t w = (uint32_t)rnd_below(&mot.r, 8), k;
    int done = 0;
    if (w == 4u || (w == 2u && mol_d.shape.n >= MOL_NODES)) {   /* pruned: leaving first (motion_tick ends it) */
        if ((k = mol_prune_pick(&mol_d.shape, &mot.r)) != 0) {
            mot.leaving = k;
            mol_prune_begin(k);
            done = 1;
        } else
            done = mol_grow(&mot.r);
    } else if (w < 2u)
        done = mol_rewire(rnd_next(&mot.r), 1);
    else if (w <= 3u)
        done = mol_grow(&mot.r) || mol_rewire(rnd_next(&mot.r), 1);
    if (!done)                                       /* (and whatever found nothing to change) */
        mot_redraw(now);
    mot.events++;
}

/* one step towards a distance d (Q8) between discs i and j, each moved half the way by the share k (Q8), lim at most
 * (Q8 pixels) */
static void mot_toward(uint32_t i, uint32_t j, int32_t want, int32_t k, int32_t lim)
{
    int32_t ex = mot.x[j] - mot.x[i], ey = mot.y[j] - mot.y[i], d, m;
    d = (int32_t)isqrt32((uint32_t)(ex * ex) + (uint32_t)(ey * ey));   /* Q8 (under 2^32: the screen is small) */
    if (d < 16)
        return;
    m = clampi((int32_t)(((int64_t)(d - want) * k) >> 9), -lim, lim);   /* half the error, by the share, each */
    mot.x[i] += (int32_t)((int64_t)ex * m / d);
    mot.y[i] += (int32_t)((int64_t)ey * m / d);
    mot.x[j] -= (int32_t)((int64_t)ex * m / d);
    mot.y[j] -= (int32_t)((int64_t)ey * m / d);
}

/* from the main loop, every tick */
static void motion_tick(uint32_t now)
{
    mol_shape_t *s = &mol_d.shape;
    uint32_t dt, i, j, rate;
    int32_t px, k, grow, lim;
    if (!mot.on || now - mot.last < 20u)             /* (steps of 20 ms at least: the device's loop runs every
                                                      * millisecond, and a step that small rounds to nothing) */
        return;
    dt = now - mot.last;
    mot.last = now;
    if (mol_want_off)
        return;
    if (mot.shapes != mol_shapes)                    /* a new shape (rewired, grown, a new molecule, a patch) */
        mot_follow();
    dt = dt > 100u ? 100u : dt;
    rate = motion_rate();
    px = (int32_t)(rate * MOT_PX_PER_EVENT * 256u / 10u);   /* Q8 pixels a second */
    k = (int32_t)(dt * 256u / MOT_PULL_MS);          /* the share of the error made good this tick, Q8 */
    grow = (int32_t)(dt * 256u / MOT_SWELL_MS);
    lim = (int32_t)(dt * MOT_PULL_MAX * 256u / 1000u);
    for (i = 0; i < s->n; i++) {                     /* each disc's own way: forward, turning smoothly */
        int32_t v = (px * mot.sp[i]) >> 8, r = node_r(&s->node[i]) + 3;
        mot.turn[i] += (rnd_bi(&mot.r) >> 3) * (int32_t)dt;
        mot.turn[i] -= mot.turn[i] >> 7;
        mot.turn[i] = clampi(mot.turn[i], -MOT_TURN_MAX, MOT_TURN_MAX);
        mot.hd[i] += (uint32_t)(mot.turn[i] * (int32_t)dt);
        mot.x[i] += (int32_t)((int64_t)sin_ph(mot.hd[i] + (1u << 30)) * v * (int32_t)dt / (1000 << 15));
        mot.y[i] += (int32_t)((int64_t)sin_ph(mot.hd[i]) * v * (int32_t)dt / (1000 << 15));
        mot.x[i] += ((SCR_W << 7) - mot.x[i]) * (int32_t)dt / MOT_HOME_MS;
        mot.y[i] += ((SCR_H << 7) - mot.y[i]) * (int32_t)dt / MOT_HOME_MS;
        if (i == mot.leaving && i)
            mot.size[i] = maxi(mot.size[i] - grow, 0);
        else
            mot.size[i] = mini(mot.size[i] + grow, 256);
        (void)r;
    }
    for (i = 1; i < s->n; i++)                       /* the bonds: springs towards their length */
        mot_toward(i, s->node[i].parent, BOND << 8, k, lim);
    for (i = 0; i < s->n; i++)                       /* discs too close: apart */
        for (j = i + 1u; j < s->n; j++) {
            int32_t ex = (mot.x[j] - mot.x[i]) >> 8, ey = (mot.y[j] - mot.y[i]) >> 8;
            int32_t m = node_r(&s->node[i]) + node_r(&s->node[j]) + 8;
            if (ex * ex + ey * ey < m * m)
                mot_toward(i, j, m << 8, 2 * k, lim);
        }
    for (i = 0; i < s->n; i++) {                     /* the screen's edges: held inside, the heading turned back */
        int32_t r = node_r(&s->node[i]) + 3;
        if (mot.x[i] < r << 8 || mot.x[i] > (SCR_W - 1 - r) << 8) {
            mot.x[i] = clampi(mot.x[i], r << 8, (SCR_W - 1 - r) << 8);
            if ((mot.x[i] < SCR_W << 7) == (sin_ph(mot.hd[i] + (1u << 30)) < 0))
                mot.hd[i] = (1u << 31) - mot.hd[i];  /* (only if heading out: mirrored across) */
        }
        if (mot.y[i] < r << 8 || mot.y[i] > (SCR_H - 1 - r) << 8) {
            mot.y[i] = clampi(mot.y[i], r << 8, (SCR_H - 1 - r) << 8);
            if ((mot.y[i] < SCR_H << 7) == (sin_ph(mot.hd[i]) < 0))
                mot.hd[i] = 0u - mot.hd[i];
        }
        s->node[i].x = (int16_t)(mot.x[i] >> 8);
        s->node[i].y = (int16_t)(mot.y[i] >> 8);
    }
    mol_pan_x(&mol_d);
    for (i = 1; i < s->n; i++)                       /* the values gliding */
        if (mot.vdur[i]) {
            uint32_t e = now - mot.vt0[i], q;
            if (e >= mot.vdur[i]) {
                e = mot.vdur[i];
                mot.vdur[i] = 0;
            }
            for (q = 0; q < 4u; q++)
                s->node[i].p[q] = (int16_t)(mot.v0[i][q] + (int32_t)(mot.v1[i][q] - mot.v0[i][q]) * (int32_t)e /
                                            (int32_t)(mot.vdur[i] ? mot.vdur[i] : e ? e : 1));
        }
    if (mot.leaving && mot.size[mot.leaving] == 0 && mol_gone(mot.leaving)) {   /* shrunk away and silent: pruned */
        if (!mol_prune(mot.leaving) && !mol_pending) {
            mot.leaving = 0;                         /* (it could not go: back it swells, and sounds) */
            mol_leave = 0;
        }
    }
    if (mol_pending || mot.leaving || ATOMIC_GET(mol_phase) != MP_RUN) {
        if ((int32_t)(now - mot.next) >= 0)          /* (no event during a change: the next soon after) */
            mot.next = now + 60000u / rate / 4u;
        return;
    }
    if ((int32_t)(now - mot.next) >= 0) {
        mot.next += 60000u / rate;
        if ((int32_t)(now - mot.next) >= 0)
            mot.next = now + 60000u / rate;
        mot_event(now);
    }
}
