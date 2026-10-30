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
#include "mol.h"
#include "fx.c"
#include "gen.c"
#include "mol.c"
#include "gfx.c"
#include "app.c"
