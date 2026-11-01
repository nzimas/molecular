/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR: the shared types. A molecule is a seed: the generator's patch, chord and LFOs, and the atoms' kinds,
 * places and values are all drawn from it. */
#pragma once
#include <stdint.h>

/* the generator, Norilsk's 25 effects (fx.c, vendor/norilsk), Tonitor's DELAY and REVERB */
enum { AT_GEN, AT_FILTER, AT_FOLD, AT_MULAW, AT_SHIFT, AT_MODAL, AT_STRING, AT_VOWEL, AT_TAPE, AT_REVERSE, AT_SPRING,
       AT_GLITCH, AT_CLOUD, AT_SCREAM, AT_RING, AT_STARVE, AT_CHEB, AT_BANK, AT_SMEAR, AT_DIFFUSE, AT_SLEW, AT_XOR,
       AT_DUST, AT_WAVESET, AT_XMOD, AT_CHAOS, AT_DELAY, AT_REVERB, AT_N };
#define MOL_MAXATOM 5u
#define MOL_NODES (MOL_MAXATOM + 1u)   /* the generator is node 0; a parent always comes before its children */
#define NOPARENT 0xFFu

/* the screen: the Pocket Master's 1.77" ST7735S, driven in landscape by the stock firmware (MADCTL 0x68) */
#define SCR_W 160
#define SCR_H 128

/* one LFO on one of the patch's four macros, or on one of an atom's values */
typedef struct {
    uint32_t inc;                /* phase step a block (2^32 = one cycle) */
    int8_t centre, depth;        /* -MAC_MAX .. MAC_MAX, 0 .. MAC_MAX (an atom's: centre unused, depth in its value's
                                  * units, 0 .. 50) */
    uint8_t shape;               /* 0 sine, 1 triangle, 2 smooth random */
} gen_lfo_t;

/* an LFO as it runs (audio side) */
typedef struct {
    uint32_t ph;                 /* its phase */
    int32_t now, to;             /* the smooth random shape: where it is, where it goes */
} lfo_run_t;

typedef struct {
    uint8_t kind;                /* AT_* */
    uint8_t parent;              /* its parent's index (NOPARENT: the generator) */
    uint8_t nchild;
    uint8_t pan;                 /* an edge atom's place in the stereo field, 0 left .. 100 right */
    int16_t x, y;                /* its centre on the screen */
    int16_t p[4];                /* its values, 0 .. 100 each (fx.c gives each kind's meaning) */
    uint8_t lval;                /* the value its LFO moves (none: lfo.depth 0; DELAY, REVERB: none) */
    gen_lfo_t lfo;
} mol_node_t;

typedef struct {
    uint32_t seed;               /* what drew the shape */
    uint8_t n;                   /* nodes: 2 .. MOL_NODES */
    mol_node_t node[MOL_NODES];
} mol_shape_t;


/* the clock (0.55, gen.c): GK_HOLD, the chord held, as the drone always was; or the voices struck on an unstable clock, each
 * strike a note of the chord, its operators from the top (a burst of coloured noise with it), let go after GATE.
 * After Norilsk's struck families (mol.c mo_strikes, the same author's):
 *   MACHINE  a machine running: strikes at RATE, each early or late by JIT % of the period, the pace wandering by
 *            DRIFT; a pattern of ACCN strikes, those whose bit is set in PAT full, the others at SOFT; now and then a
 *            bounce (FLAM %: a second strike, quieter, FLAM_MS later)
 *   SCATTER  no pulse to count: each interval the mean (RATE, wandering by DRIFT) up or down by as much as JIT tenths
 *            of an octave; clusters of up to ACCN quick strikes FLAM_MS apart (FLAM % of the time); a long pause PAT %
 *            of the time; each strike's level at random, soft ones more often, a softer one darker
 * MACHINE may let out a hiss of steam halfway between strikes (OFF: its level %, OFF_MS x 4 ms long). CRACKLE: the
 * strikes are bursts only, over the chord held (Norilsk's ASH: a dusty crackle over a drone).
 * Kept in mol_design_t after the rest (a patch saved before it has none: zero, GK_HOLD). */
enum { GK_HOLD, GK_MACHINE, GK_SCATTER };
typedef struct {
    uint8_t mode;                /* GK_* */
    uint8_t rate;                /* strikes a second x 4: 1 .. 80 */
    uint8_t jit, drift;
    uint8_t accn, pat, soft;     /* (soft: %) */
    uint8_t flam, flam_ms;
    uint8_t gate;                /* a strike's note held this long, x 4 ms, then let go */
    uint8_t spread;              /* the chance a strike takes another note of the chord than the root, % */
    uint8_t click, color, level; /* the burst: 60 dB down in CLICK ms; dark 0 .. bright 100; its level, % */
    uint8_t off, off_ms;         /* MACHINE: the hiss */
    uint8_t crackle;             /* 1: bursts only, the chord held */
    uint8_t rsv;
} gen_clock_t;

/* the screens' view of the sound: written by the audio, read by the drawing (single values: no lock needed) */
typedef struct {
    int32_t level[MOL_NODES];    /* each node's output level, Q15, a slow peak follower */
    uint32_t blocks;             /* blocks rendered: the drawing's clock for the pulses */
} mol_meter_t;
