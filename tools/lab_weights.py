#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Family weights from a lab population (tests/lab.c CSV, generators alone): each render's rarity is the distance to
its 8th nearest neighbour in robust z-space; a family's new weight follows its renders' mean rarity, damped half way
to the old weight, within 2 .. 8 (the research: weight families towards the rare regions they reach).
Usage: lab_weights.py lab.csv  (prints family, renders, rarity, suggested weight)"""
import csv, sys
import numpy as np

rows = list(csv.DictReader(open(sys.argv[1])))
keys = [k for k in rows[0] if k not in ("seed", "family", "atoms", "width")]
X = np.array([[float(r[k]) for k in keys] for r in rows])
med = np.median(X, 0); mad = np.median(np.abs(X - med), 0) * 1.4826 + 1e-9
Z = np.clip((X - med) / mad, -6, 6)
D = np.sqrt(((Z[:, None] - Z[None]) ** 2).sum(-1))
np.fill_diagonal(D, np.inf)
rar = np.sort(D, 1)[:, 7]
fams = sorted(set(r["family"] for r in rows), key=lambda f: -np.mean([rar[i] for i, r in enumerate(rows) if r["family"] == f]))
m = np.median(rar)
for f in fams:
    idx = [i for i, r in enumerate(rows) if r["family"] == f]
    print("%-9s %3d  rarity %.2f  weight x %.2f" % (f, len(idx), np.mean(rar[idx]), np.sqrt(np.mean(rar[idx]) / m)))
