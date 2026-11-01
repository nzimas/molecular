/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR's diversity lab (host only): renders molecules, measures each with timbre descriptors after IRCAM's
 * Timbre Toolbox (Peeters et al. 2011: median and IQR of frame descriptors), and reports how diverse the population
 * is -- the Vendi score (the effective number of distinct sounds), nearest-neighbour distances, near-duplicates -- and,
 * by generator family and by atom kind, where the sounds fall: which families sound alike, which are spread, which
 * descriptors each covers.
 *
 *   build/lab N [first_seed] [mode] [ref]   mode: mol (default: whole molecules), gen (the generator alone, dry);
 *                                         ref: save this population's scaling (medians, MADs, the kernel's width) as
 *                                         the yardstick (build/lab_ref_<mode>.txt); without it, a saved yardstick is
 *                                         used, so populations compare on one scale
 *
 * Descriptors (12, mono unless noted; 1024-point frames, hop 256; frames 60 dB under the loudest skipped):
 *   0 brightness     spectral centroid, log2 Hz, median          1 brightness motion  its IQR, octaves
 *   2 noisiness      spectral flatness 50 Hz .. 16 kHz, dB, median  3 variation  1 - spectral correlation of
 *                                                                  successive frames, median
 *   4 periodicity    normalised autocorrelation peak 40 .. 2000 Hz, median
 *   5 roughness      Vassilakis over the 30 strongest peaks, log, median
 *   6 dynamics       frame RMS in dB, IQR                        7 onsets   log(1 + onsets a second)
 *   8 attack         log10 of the median 10 .. 90 % rise at onsets  9 mod rate  log2 centroid of the envelope's
 *                                                                  0.5 .. 32 Hz modulation
 *  10 mod depth      that band's share of the envelope's variance  11 width   1 - corr(L, R), median */
#define HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../core/molecular.c"

static int plat_patch_read(uint32_t slot, void *buf, uint32_t n) { return -1; }
static int plat_patch_write(uint32_t slot, const void *buf, uint32_t n) { return -1; }

#define SECS 6
#define NS (FS * SECS)
#define ND 12
#define NF 1024
#define HOP 256
static const char *const DN[ND] = {"bright", "brmove", "noise", "vary", "period", "rough", "dyn", "onsets", "attack",
                                   "modrate", "moddep", "width"};
static int16_t wav[NS * 2];
static double mono[NS];

static void fft(double *re, double *im, int n)
{
    int i, j, k, m;
    for (i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (m = 2; m <= n; m <<= 1) {
        double a = -2 * M_PI / m;
        for (k = 0; k < n; k += m)
            for (j = 0; j < m / 2; j++) {
                double wr = cos(a * j), wi = sin(a * j);
                double xr = re[k + j + m / 2] * wr - im[k + j + m / 2] * wi, xi = re[k + j + m / 2] * wi + im[k + j + m / 2] * wr;
                re[k + j + m / 2] = re[k + j] - xr; im[k + j + m / 2] = im[k + j] - xi;
                re[k + j] += xr; im[k + j] += xi;
            }
    }
}
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static double pct(double *v, int n, double q)
{
    if (n <= 0)
        return 0;
    qsort(v, (size_t)n, sizeof *v, cmpd);
    return v[(int)(q * (n - 1) + 0.5)];
}

/* the descriptors of one render (wav, stereo int16) */
static void describe(double *d)
{
    static double cen[NS / HOP], flat[NS / HOP], var[NS / HOP], per[NS / HOP], rgh[NS / HOP], rms[NS / HOP], flux[NS / HOP];
    static double re[4096], im[4096], prev[NF / 2], mag[NF / 2], env[NS / HOP], wid[64];
    int nfr = 0, f, k, i, nv = 0, nr = 0, nw = 0;
    double peak = 0, gate;
    for (i = 0; i < NS; i++)
        mono[i] = (wav[2 * i] + wav[2 * i + 1]) / 65536.0;
    for (f = 0; f + NF <= NS; f += HOP) {                       /* frame energies first: the gate */
        double e = 0;
        for (i = 0; i < NF; i++)
            e += mono[f + i] * mono[f + i];
        rms[nfr] = 10 * log10(e / NF + 1e-12);
        env[nfr] = sqrt(e / NF);
        if (rms[nfr] > peak || !nfr)
            peak = rms[nfr] > peak || !nfr ? rms[nfr] : peak;
        nfr++;
    }
    gate = peak - 60;
    memset(prev, 0, sizeof prev);
    {
        int nc = 0, nfl = 0;
        for (f = 0; f < nfr; f++) {
            double sp = 0, s = 0, lg = 0, ar = 0, num = 0, d1 = 0, d2 = 0, fx = 0;
            int lo = (int)(50.0 * NF / FS), hi = (int)(16000.0 * NF / FS), nb = 0;
            for (i = 0; i < NF; i++) {
                double w = 0.54 - 0.46 * cos(2 * M_PI * i / (NF - 1));
                re[i] = mono[f * HOP + i] * w;
                im[i] = 0;
            }
            fft(re, im, NF);
            {   /* the flux: positive log growth, over the bins within 40 dB of the frame's loudest, averaged */
                double mx = 0;
                int nfx = 0;
                for (k = 1; k < NF / 2; k++) {
                    mag[k] = sqrt(re[k] * re[k] + im[k] * im[k]);
                    if (mag[k] > mx)
                        mx = mag[k];
                }
                for (k = 1; k < NF / 2; k++)
                    if (mag[k] > mx * 0.01) {
                        fx += mag[k] > prev[k] ? log((mag[k] + 1e-9) / (prev[k] + mx * 1e-3)) : 0;
                        nfx++;
                    }
                flux[f] = nfx ? fx / nfx : 0;
            }
            if (rms[f] < gate) {
                memcpy(prev, mag, sizeof prev);
                continue;
            }
            for (k = 1; k < NF / 2; k++) {
                double p2 = mag[k] * mag[k], fk = (double)k * FS / NF;
                sp += p2 * fk;
                s += p2;
                if (k >= lo && k <= hi) {
                    lg += log(p2 + 1e-18);
                    ar += p2;
                    nb++;
                }
                num += prev[k] * mag[k];
                d1 += prev[k] * prev[k];
                d2 += mag[k] * mag[k];
            }
            if (s > 0)
                cen[nc++] = log2(sp / s + 1);
            if (ar > 0)
                flat[nfl++] = 10 * log10(exp(lg / nb) / (ar / nb) + 1e-12);
            if (d1 > 0 && d2 > 0)
                var[nv++] = 1 - num / sqrt(d1 * d2);
            memcpy(prev, mag, sizeof prev);
        }
        {
            double t[NS / HOP];
            memcpy(t, cen, sizeof(double) * nc);
            d[0] = pct(t, nc, 0.5);
            d[1] = pct(t, nc, 0.75) - pct(t, nc, 0.25);
            d[2] = pct(flat, nfl, 0.5);
            d[3] = pct(var, nv, 0.5);
        }
    }
    {   /* periodicity: autocorrelation peaks on 2048-sample frames, every 8th hop */
        int np = 0;
        for (f = 0; f + 2048 + 1103 <= NS; f += 2048) {
            double best = 0, e0 = 0;
            int tau;
            for (i = 0; i < 2048; i++)
                e0 += mono[f + i] * mono[f + i];
            if (10 * log10(e0 / 2048 + 1e-12) < gate)
                continue;
            for (tau = FS / 2000; tau <= FS / 40; tau += 1) {
                double c = 0, e1 = 0;
                for (i = 0; i < 2048; i += 2) {
                    c += mono[f + i] * mono[f + i + tau];
                    e1 += mono[f + i + tau] * mono[f + i + tau];
                }
                c /= sqrt((e0 / 2) * e1 + 1e-18);
                if (c > best)
                    best = c;
            }
            per[np++] = best;
        }
        d[4] = pct(per, np, 0.5);
    }
    {   /* roughness (Vassilakis) over spectral peaks, 4096-point frames */
        for (f = 0; f + 4096 <= NS; f += 2048 * 2) {
            double pk[30], pa[30], mx = 0, R = 0;
            int npk = 0, j;
            for (i = 0; i < 4096; i++) {
                double w = 0.54 - 0.46 * cos(2 * M_PI * i / 4095);
                re[i] = mono[f + i] * w;
                im[i] = 0;
            }
            fft(re, im, 4096);
            for (k = 1; k < 2048; k++)
                re[k] = sqrt(re[k] * re[k] + im[k] * im[k]);
            for (k = 1; k < 2048; k++)
                if (re[k] > mx)
                    mx = re[k];
            if (mx <= 0)
                continue;
            for (k = 2; k < 2047 && npk < 30; k++)
                if (re[k] > re[k - 1] && re[k] >= re[k + 1] && re[k] > mx * 0.00316) {
                    pk[npk] = (double)k * FS / 4096;
                    pa[npk] = re[k] / mx;
                    npk++;
                }
            for (i = 0; i < npk; i++)
                for (j = i + 1; j < npk; j++) {
                    double amin = fmin(pa[i], pa[j]), amax = fmax(pa[i], pa[j]), fmn = fmin(pk[i], pk[j]);
                    double df = fabs(pk[i] - pk[j]), sv = 0.24 / (0.0207 * fmn + 18.96);
                    double Y = 2 * amin / (amin + amax), Z = exp(-3.5 * sv * df) - exp(-5.75 * sv * df);
                    R += pow(pa[i] * pa[j], 0.1) * 0.5 * pow(Y, 3.11) * Z;
                }
            rgh[nr++] = log(R + 1e-4);
        }
        d[5] = pct(rgh, nr, 0.5);
    }
    {   /* dynamics, onsets, attack, modulation */
        double t[NS / HOP], dyn[NS / HOP], att[256];
        int n2 = 0, ons = 0, na = 0, last = -1000;
        for (f = 0; f < nfr; f++)
            if (rms[f] >= gate)
                dyn[n2++] = rms[f];
        d[6] = pct(dyn, n2, 0.75) - pct(dyn, n2, 0.25);
        for (f = 2; f < nfr - 2; f++) {                          /* onsets: flux over its local median + 1.5 */
            int a = f - 43 < 0 ? 0 : f - 43, b = f + 43 > nfr ? nfr : f + 43, m = 0, j;
            for (j = a; j < b; j++)
                t[m++] = flux[j];
            if (flux[f] > pct(t, m, 0.5) + 0.35 && flux[f] >= flux[f - 1] && flux[f] >= flux[f + 1] &&
                f - last >= 9 && rms[f] >= gate) {
                ons++;
                last = f;
                if (na < 256) {                                  /* its rise: 10 .. 90 % of the peak after it */
                    int g, pkf = f;
                    double pv = env[f], a10, a90, t10 = -1, t90 = -1;
                    for (g = f; g < f + 40 && g < nfr; g++)
                        if (env[g] > pv) { pv = env[g]; pkf = g; }
                    a10 = pv * 0.1, a90 = pv * 0.9;
                    for (g = f - 8 < 0 ? 0 : f - 8; g <= pkf; g++) {
                        if (t10 < 0 && env[g] >= a10) t10 = g;
                        if (t90 < 0 && env[g] >= a90) t90 = g;
                    }
                    if (t10 >= 0 && t90 >= t10)
                        att[na++] = log10((t90 - t10 + 1) * (double)HOP / FS);
                }
            }
        }
        d[7] = log(1 + ons / (double)SECS);
        d[8] = na ? pct(att, na, 0.5) : log10(0.5);
        {   /* the envelope's modulation spectrum (frame rate FS/HOP = 172 Hz) */
            int n = 1024, j;
            double mean = 0, sw = 0, swf = 0, tot = 0;
            for (j = 0; j < n; j++) {
                double v = j < nfr ? 20 * log10(env[j] + 1e-6) : 0;
                re[j] = v;
                im[j] = 0;
                mean += v / n;
            }
            for (j = 0; j < n; j++) {
                re[j] = (re[j] - mean) * (j < nfr ? 1 : 0);
                tot += re[j] * re[j];
            }
            fft(re, im, n);
            for (j = 1; j < n / 2; j++) {
                double fr = (double)j * FS / HOP / n, p = re[j] * re[j] + im[j] * im[j];
                if (fr >= 0.5 && fr <= 32) {
                    sw += p;
                    swf += p * log2(fr);
                }
            }
            d[9] = sw > 0 ? swf / sw : 0;
            d[10] = tot > 0 ? fmin(1, 2 * sw / (n * tot)) : 0;
        }
    }
    for (i = 0; i + FS / 2 <= NS; i += FS / 2) {                 /* width */
        double sl = 0, sr = 0, slr = 0, ll = 0, rr = 0;
        int j;
        for (j = i; j < i + FS / 2; j++) {
            double l = wav[2 * j], r = wav[2 * j + 1];
            sl += l; sr += r; slr += l * r; ll += l * l; rr += r * r;
        }
        if (ll > 1 && rr > 1)
            wid[nw++] = 1 - slr / sqrt(ll * rr);
    }
    d[11] = pct(wid, nw, 0.5);
}

/* a molecule from its seed, rendered (the generator alone with gen) */
static int render(uint32_t seed, int gen_only)
{
    uint32_t b, i;
    mol_volume(Q15);
    mol_new(seed);
    while (ATOMIC_GET(mol_phase) != MP_HELD) {
        static int16_t j[2 * CTL];
        mol_block(j);
    }
    mol_poll();
    if (gen_only) {                                              /* (the atoms out of the mix: MIX 0) */
        mol_d.mix = 1;
        mol_wet = 0;
    } else {
        mol_d.mix = 0;
        mol_wet = Q15 / 2;
    }
    for (b = 0; b < NS / CTL; b++)
        mol_block(wav + b * 2u * CTL);
    for (i = 0; i < NS * 2; i++)
        if (wav[i])
            return 1;
    return 0;
}

#define MAXN 3000
static double D[MAXN][ND], Z[MAXN][ND];
static int fam[MAXN], natom[MAXN];
static uint32_t kinds[MAXN];
static double dist(int a, int b)
{
    double s = 0;
    int k;
    for (k = 0; k < ND; k++)
        s += (Z[a][k] - Z[b][k]) * (Z[a][k] - Z[b][k]);
    return sqrt(s);
}
/* the Vendi score of the population: exp(entropy of the eigenvalues of K / n), K a Gaussian kernel on the distances
 * (sigma the median distance); eigenvalues by Jacobi rotations */
static double ref_sigma;
static double vendi(int n)
{
    static double K[MAXN][MAXN];
    double *ds = malloc(sizeof(double) * (size_t)n * (n - 1) / 2), sig, H = 0;
    int i, j, m = 0, sweep;
    for (i = 0; i < n; i++)
        for (j = i + 1; j < n; j++)
            ds[m++] = dist(i, j);
    sig = ref_sigma > 0 ? ref_sigma : pct(ds, m, 0.5);
    if (ref_sigma <= 0)
        ref_sigma = sig;
    free(ds);
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++)
            K[i][j] = exp(-pow(dist(i, j), 2) / (2 * sig * sig)) / n;
    for (sweep = 0; sweep < 12; sweep++) {
        double off = 0;
        int p, q;
        for (p = 0; p < n; p++)
            for (q = p + 1; q < n; q++)
                off += K[p][q] * K[p][q];
        if (off < 1e-14)
            break;
        for (p = 0; p < n; p++)
            for (q = p + 1; q < n; q++) {
                double app = K[p][p], aqq = K[q][q], apq = K[p][q], th, c, s;
                int r;
                if (fabs(apq) < 1e-12)
                    continue;
                th = 0.5 * atan2(2 * apq, aqq - app);
                c = cos(th), s = sin(th);
                for (r = 0; r < n; r++) {
                    double kp = K[r][p], kq = K[r][q];
                    K[r][p] = c * kp - s * kq;
                    K[r][q] = s * kp + c * kq;
                }
                for (r = 0; r < n; r++) {
                    double kp = K[p][r], kq = K[q][r];
                    K[p][r] = c * kp - s * kq;
                    K[q][r] = s * kp + c * kq;
                }
            }
    }
    for (i = 0; i < n; i++)
        if (K[i][i] > 1e-12)
            H -= K[i][i] * log(K[i][i]);
    return exp(H);
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 300, gen_only = argc > 3 && !strncmp(argv[3], "gen", 3), i, k, j;
    int no_pm = argc > 3 && strstr(argv[3], "nopm") != NULL;     /* (genopm, molnopm: seeds drawing a physical family
                                                                  * skipped -- the families as they were before 0.64) */
    int save_ref = argc > 4 && !strcmp(argv[4], "ref"), have_ref = 0;
    char refpath[64];
    FILE *rf;
    uint32_t first = argc > 2 ? (uint32_t)atoi(argv[2]) : 1, silent = 0;
    double med[ND], mad[ND], *nn;
    FILE *csv = fopen("build/lab.csv", "w");
    if (n > MAXN)
        n = MAXN;
    fprintf(csv, "seed,family,atoms");
    for (k = 0; k < ND; k++)
        fprintf(csv, ",%s", DN[k]);
    fprintf(csv, "\n");
    for (i = 0; i < n; i++) {
        static uint32_t skip;
        uint32_t seed = first + (uint32_t)i + skip;
        if (!render(seed, gen_only))
            silent++;
        while (no_pm && gen_is_pm(&mol_d.gen)) {                 /* (a physical family: the next seed instead) */
            seed = first + (uint32_t)i + ++skip;
            render(seed, gen_only);
        }
        describe(D[i]);
        fam[i] = mol_d.gen.patch.family;
        natom[i] = mol_d.shape.n - 1;
        kinds[i] = 0;
        fprintf(csv, "%u,%s,", seed, fam_name(fam[i]));
        for (k = 1; k < mol_d.shape.n; k++)
            fprintf(csv, "%s%s", k > 1 ? "+" : "", at_name(mol_d.shape.node[k].kind));
        for (k = 0; k < ND; k++)
            fprintf(csv, ",%.4f", D[i][k]);
        fprintf(csv, "\n");
        if (i % 50 == 49)
            fprintf(stderr, "  %d / %d\n", i + 1, n);
    }
    fclose(csv);
    snprintf(refpath, sizeof refpath, "build/lab_ref_%s.txt", gen_only ? "gen" : "mol");
    if (!save_ref && (rf = fopen(refpath, "r")) != NULL) {      /* the saved yardstick */
        have_ref = 1;
        for (k = 0; k < ND; k++)
            have_ref &= fscanf(rf, "%lf %lf", &med[k], &mad[k]) == 2;
        have_ref &= fscanf(rf, "%lf", &ref_sigma) == 1;
        fclose(rf);
    }
    for (k = 0; k < ND; k++) {                                   /* robust z-scores */
        double t[MAXN];
        if (!have_ref) {
            for (i = 0; i < n; i++)
                t[i] = D[i][k];
            med[k] = pct(t, n, 0.5);
            for (i = 0; i < n; i++)
                t[i] = fabs(D[i][k] - med[k]);
            mad[k] = pct(t, n, 0.5) * 1.4826 + 1e-9;
        }
        for (i = 0; i < n; i++)
            Z[i][k] = fmax(-6, fmin(6, (D[i][k] - med[k]) / mad[k]));
    }
    if (!have_ref)
        ref_sigma = 0;
    printf("lab: %d %s, seeds %u .. %u, %u silent\n", n, gen_only ? "generators alone" : "molecules", first,
           first + (uint32_t)n - 1u, silent);
    printf("descriptor medians:");
    for (k = 0; k < ND; k++)
        printf(" %s %.2f", DN[k], med[k]);
    printf("\n");
    nn = malloc(sizeof(double) * (size_t)n);
    for (i = 0; i < n; i++) {
        nn[i] = 1e9;
        for (j = 0; j < n; j++)
            if (j != i && dist(i, j) < nn[i])
                nn[i] = dist(i, j);
    }
    {
        double t[MAXN], vs = vendi(n);
        if (save_ref && (rf = fopen(refpath, "w")) != NULL) {
            for (k = 0; k < ND; k++)
                fprintf(rf, "%.6f %.6f\n", med[k], mad[k]);
            fprintf(rf, "%.6f\n", ref_sigma);
            fclose(rf);
            printf("(this population's scaling saved as the yardstick: %s)\n", refpath);
        }
        memcpy(t, nn, sizeof(double) * n);
        printf("Vendi %.1f (%.1f %% of %d); nearest neighbour median %.2f, 10th percentile %.2f\n", vs, 100 * vs / n, n,
               pct(t, n, 0.5), pct(t, n, 0.1));
    }
    {   /* by family: count, spread, nearest other family, kNN self-recognition */
        int f;
        printf("%-9s %4s %6s %6s %-9s %5s  centroid (z)\n", "family", "n", "spread", "near", "nearest", "knn%");
        for (f = 0; f < 64; f++) {
            int cnt = 0, hit = 0, a, best = -1;
            double c[ND] = {0}, sp = 0, bestd = 1e9;
            for (i = 0; i < n; i++)
                if (fam[i] == f) {
                    cnt++;
                    for (k = 0; k < ND; k++)
                        c[k] += Z[i][k];
                }
            if (!cnt)
                continue;
            for (k = 0; k < ND; k++)
                c[k] /= cnt;
            for (i = 0; i < n; i++)
                if (fam[i] == f) {
                    double s = 0;
                    int nb[5], m;
                    double nd[5];
                    for (k = 0; k < ND; k++)
                        s += (Z[i][k] - c[k]) * (Z[i][k] - c[k]);
                    sp += sqrt(s) / cnt;
                    for (m = 0; m < 5; m++) { nd[m] = 1e9; nb[m] = -1; }
                    for (j = 0; j < n; j++) {
                        double dd;
                        if (j == i)
                            continue;
                        dd = dist(i, j);
                        for (m = 0; m < 5; m++)
                            if (dd < nd[m]) {
                                int q;
                                for (q = 4; q > m; q--) { nd[q] = nd[q - 1]; nb[q] = nb[q - 1]; }
                                nd[m] = dd;
                                nb[m] = j;
                                break;
                            }
                    }
                    for (m = 0, a = 0; m < 5; m++)
                        a += nb[m] >= 0 && fam[nb[m]] == f;
                    hit += a >= 3;
                }
            for (a = 0; a < 64; a++) {                           /* the nearest other family's centroid */
                double c2[ND] = {0}, s = 0;
                int c2n = 0;
                if (a == f)
                    continue;
                for (i = 0; i < n; i++)
                    if (fam[i] == a) {
                        c2n++;
                        for (k = 0; k < ND; k++)
                            c2[k] += Z[i][k];
                    }
                if (!c2n)
                    continue;
                for (k = 0; k < ND; k++)
                    s += pow(c2[k] / c2n - c[k], 2);
                if (sqrt(s) < bestd) {
                    bestd = sqrt(s);
                    best = a;
                }
            }
            printf("%-9s %4d %6.2f %6.2f %-9s %5.0f ", fam_name((uint32_t)f), cnt, sp, bestd,
                   best >= 0 ? fam_name((uint32_t)best) : "-", 100.0 * hit / cnt);
            for (k = 0; k < ND; k++)
                printf(" %+.1f", c[k]);
            printf("\n");
        }
    }
    {   /* the most alike pairs */
        int shown = 0, a, b;
        double t[MAXN];
        memcpy(t, nn, sizeof(double) * n);
        double tau = pct(t, n, 0.05);
        printf("closest pairs (under %.2f):\n", tau);
        for (a = 0; a < n && shown < 12; a++)
            for (b = a + 1; b < n && shown < 12; b++)
                if (dist(a, b) < tau) {
                    printf("  %u %s / %u %s  %.2f\n", first + a, fam_name(fam[a]), first + b, fam_name(fam[b]), dist(a, b));
                    shown++;
                }
    }
    return 0;
}
