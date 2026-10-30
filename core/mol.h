/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR: the shared types. A molecule is a seed: the generator's patch, chord and LFOs, and the atoms' kinds,
 * places and values are all drawn from it. */
#pragma once
#include <stdint.h>

enum { AT_GEN, AT_FILTER, AT_FOLD, AT_RING, AT_CRUSH, AT_CHORUS, AT_DELAY, AT_REVERB, AT_N };
#define MOL_MAXATOM 5u
#define MOL_NODES (MOL_MAXATOM + 1u)   /* the generator is node 0; a parent always comes before its children */
#define NOPARENT 0xFFu

/* the screen (the Pocket Master's 1.77" LCD, taken as 128 x 160 until the board confirms it) */
#define SCR_W 128
#define SCR_H 160

typedef struct {
    uint8_t kind;                /* AT_* */
    uint8_t parent;              /* its parent's index (NOPARENT: the generator) */
    uint8_t nchild;
    uint8_t pan;                 /* an edge atom's place in the stereo field, 0 left .. 100 right */
    int16_t x, y;                /* its centre on the screen */
    int16_t p[4];                /* its values, 0 .. 100 each (fx.c gives each kind's meaning) */
} mol_node_t;

typedef struct {
    uint32_t seed;               /* what drew the shape */
    uint8_t n;                   /* nodes: 2 .. MOL_NODES */
    mol_node_t node[MOL_NODES];
} mol_shape_t;

/* one LFO on one of the patch's four macros */
typedef struct {
    uint32_t inc;                /* phase step a block (2^32 = one cycle) */
    int8_t centre, depth;        /* -MAC_MAX .. MAC_MAX, 0 .. MAC_MAX */
    uint8_t shape;               /* 0 sine, 1 triangle, 2 smooth random */
} gen_lfo_t;

/* the screens' view of the sound: written by the audio, read by the drawing (single values: no lock needed) */
typedef struct {
    int32_t level[MOL_NODES];    /* each node's output level, Q15, a slow peak follower */
    uint32_t blocks;             /* blocks rendered: the drawing's clock for the pulses */
} mol_meter_t;
