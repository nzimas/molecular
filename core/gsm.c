/* SPDX-License-Identifier: GPL-3.0-only (this wrapper; the codec: vendor/gsm/COPYRIGHT, Degener / Bormann, permissive)
 * The GSM 06.10 full-rate codec for the GSM atom (atoms.c), from Vascular's vendored copy (every symbol prefixed
 * vascular_, gsm_prefix.h), its files unchanged and compiled here as part of the one translation unit. Its encoder and
 * decoder states live in the atom's arena (no gsm_create: no allocation); asserts off; the macros it leaves behind
 * undone. */
#define NDEBUG 1
#include "vendor/gsm/add.c"
#include "vendor/gsm/code.c"
#include "vendor/gsm/decode.c"
#include "vendor/gsm/gsm_decode.c"
#include "vendor/gsm/gsm_encode.c"
#include "vendor/gsm/long_term.c"
#undef STEP
#include "vendor/gsm/lpc.c"
#undef STEP
#include "vendor/gsm/preprocess.c"
#include "vendor/gsm/rpe.c"
#include "vendor/gsm/short_term.c"
#include "vendor/gsm/table.c"
#undef STEP
#undef SASR
#undef saturate
#undef GSM_ADD
#undef GSM_MULT_R

/* a codec state as gsm_create leaves it, in place */
static void gsm_init(struct gsm_state *g)
{
    memset(g, 0, sizeof *g);
    g->nrp = 40;
}
