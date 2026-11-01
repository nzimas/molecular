#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Two lab populations (tests/lab.c's CSVs) on one scale: robust z-scores from their union, a Gaussian kernel of the
union's median distance; for each, the Vendi score (the effective number of distinct sounds), nearest-neighbour
distances and the spread of each descriptor. Usage: lab_compare.py before.csv after.csv"""
import csv, sys
import numpy as np

def load(path):
    rows = list(csv.DictReader(open(path)))
    keys = [k for k in rows[0] if k not in ("seed", "family", "atoms")]
    return keys, np.array([[float(r[k]) for k in keys] for r in rows])

def main(a, b):
    keys, A = load(a)
    _, B = load(b)
    U = np.vstack([A, B])
    med = np.median(U, 0)
    mad = np.median(np.abs(U - med), 0) * 1.4826 + 1e-9
    za, zb = np.clip((A - med) / mad, -6, 6), np.clip((B - med) / mad, -6, 6)
    du = np.sqrt(((np.vstack([za, zb])[:, None] - np.vstack([za, zb])[None]) ** 2).sum(-1))
    sig = np.median(du[np.triu_indices(len(du), 1)])
    for name, z in (("before", za), ("after", zb)):
        d = np.sqrt(((z[:, None] - z[None]) ** 2).sum(-1))
        K = np.exp(-d ** 2 / (2 * sig * sig)) / len(z)
        ev = np.linalg.eigvalsh(K)
        ev = ev[ev > 1e-12]
        vs = np.exp(-(ev * np.log(ev)).sum())
        np.fill_diagonal(d, np.inf)
        nn = d.min(1)
        print("%-6s n %d  Vendi %.2f  nearest neighbour median %.2f  10th %.2f" % (name, len(z), vs, np.median(nn),
              np.percentile(nn, 10)))
        print("        spread (IQR, z):", " ".join("%s %.2f" % (k, np.subtract(*np.percentile(z[:, i], [75, 25])))
                                                  for i, k in enumerate(keys)))

if __name__ == "__main__":
    main(*sys.argv[1:3])
