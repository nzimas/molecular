/* SPDX-License-Identifier: GPL-3.0-only
 * Clicks in the output, on the host: 40 molecules x 10 s (with an argument, a MOTION RATE: MOTION running at it); a click = a second difference over 12x the molecule's median second difference (and over 2000). */
#define HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../core/molecular.c"
#define SECS 10u
static int16_t wav[FS * SECS * 2u];
static int plat_patch_read(uint32_t slot, void *buf, uint32_t n) { return -1; }
static int plat_patch_write(uint32_t slot, const void *buf, uint32_t n) { return -1; }
static int cmpu(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return x < y ? -1 : x > y; }
static uint32_t d2[FS * SECS], phase_hist[CTL];
int main(int argc, char **argv)
{
    uint32_t seed, pass, rate = argc > 1 ? (uint32_t)atoi(argv[1]) : 0;
    mol_volume(Q15);
    for (pass = 0; pass < 1u; pass++) {
        uint32_t total = 0, with = 0;
        for (seed = 1; seed <= 40u; seed++) {
            uint32_t b, nb = FS * SECS / CTL, i, n = 0, clicks = 0, med;
            mol_new(seed);
            while (ATOMIC_GET(mol_phase) != MP_HELD) { static int16_t j[2 * CTL]; mol_block(j); }
            mol_poll();
            if (rate) {
                mol_d.motion_rate = (uint8_t)rate;
                motion_start(seed, 0);
            }
            for (b = 0; b < nb; b++) {
                mol_block(wav + b * 2u * CTL);
                if (rate) {                         /* the main loop: every block's millisecond */
                    uint32_t ms = (uint32_t)((uint64_t)b * CTL * 1000u / FS);
                    mol_poll();
                    motion_tick(ms);
                }
            }
            motion_stop();
            for (i = 2; i < FS * SECS; i++) {      /* left channel */
                int32_t v = wav[2 * i] - 2 * wav[2 * i - 2] + wav[2 * i - 4];
                d2[n++] = (uint32_t)(v < 0 ? -v : v);
            }
            {
                static uint32_t s[FS * SECS];
                memcpy(s, d2, n * sizeof *s);
                qsort(s, n, sizeof *s, cmpu);
                med = s[n / 2] + 1u;
            }
            for (i = FS; i + 40u < n; i++) {       /* after the first second: an isolated jump */
                uint32_t k, around = 0;
                if (d2[i] < 1500u || d2[i] < 4u * med)
                    continue;
                for (k = 3; k <= 32u; k++) {
                    if (d2[i - k] > around) around = d2[i - k];
                    if (d2[i + k] > around) around = d2[i + k];
                }
                if (d2[i] > 6u * around) {
                    clicks++;
                    phase_hist[(i - 1u) % CTL]++;
                    i += 32u;
                }
            }
            total += clicks;
            with += clicks > 0;
            if (clicks)
            {
                uint32_t q;
                printf("  seed %2u: %-9s %5u clicks, atoms", seed, fam_name(mol_d.gen.patch.family), clicks);
                for (q = 1; q < mol_d.shape.n; q++)
                    printf(" %s", at_name(mol_d.shape.node[q].kind));
                printf("\n");
            }
        }
        printf("%s: %u of 40 molecules click, %u clicks\n", rate ? "moving" : "molecules", with, total);
        {
            uint32_t k;
            printf("  position in the %u-sample block:", CTL);
            for (k = 0; k < CTL; k++) printf(" %u", phase_hist[k]);
            printf("\n");
            memset(phase_hist, 0, sizeof phase_hist);
        }
    }
    return 0;
}
