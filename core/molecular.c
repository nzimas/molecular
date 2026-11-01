/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR: the core in one unit, as Tonitor builds: the vendored engine and effects, then Molecular's own. The
 * platform defines HOST (or not) and puts the generated tonitor_tables.h on the include path. */
#include <stdint.h>
#include <string.h>
#include "tonitor_tables.h"
#include "vendor/tonitor/dsp.h"
#include "vendor/tonitor/fm.c"

/* the mu-law codes Tonitor's delay keeps its line in (as Tonitor's granulator has them, gran.c) */
static inline int32_t gr_dec(uint8_t b)
{
    int32_t m = MULAW_DEC[b & 0x7Fu];
    return b & 0x80u ? -m : m;
}
static inline uint8_t gr_enc(int32_t x)
{
    uint32_t a = (uint32_t)(x < 0 ? -x : x);
    if (a > 32767u)
        a = 32767u;
    return (uint8_t)((x < 0 ? 0x80u : 0u) | MULAW_ENC[a >> 4]);
}

#define LV(x) ((int32_t)((x) * 32768.0))     /* a level as Q15, folded at compile time (Tonitor's anima.c) */
#include "vendor/tonitor/reverb.c"
#include "vendor/tonitor/delay.c"

/* what Norilsk's effects take from Norilsk's dsp.h and mol.c, not in Tonitor's dsp.h (the same author's) */
AINL int32_t mulz15(int32_t a, int32_t b)          /* a Q15 product rounded towards zero */
{
    int64_t m = (int64_t)a * b;
    return (int32_t)(m < 0 ? -((-m) >> 15) : m >> 15);
}
AINL int32_t mini(int32_t a, int32_t b) { return a < b ? a : b; }
AINL uint32_t ms_samples2(uint32_t ms) { return ms * 2205u / 100u; }   /* ms -> samples at FS / 2 */
AINL uint32_t ms_blocks(uint32_t ms) { return (ms * 1411u) >> 10; }      /* ms -> blocks (1378 a second) */
#include "vendor/norilsk/tables.h"
#include "vendor/norilsk/effects.c"
#include "mol.h"
#include "gsm.c"
#include "atoms.c"
#include "fx.c"
#include "families.c"
#include "pm.c"
#include "gen.c"
#include "mol.c"
#include "patch.c"
#include "motion.c"
#include "gfx.c"
#include "app.c"
