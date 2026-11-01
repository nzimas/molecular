Copied unchanged from Norilsk (github.com/nzimas/norilsk, firmware/src/rack.c; the commit is in effects.c's first
lines) by tools/vendor_norilsk.py: its 25 effects (FILTER .. CHAOS), their names, value bounds and sizes.
GPL-3.0-only, the same author. Keep effects.c as it is, so fixes can be compared with Norilsk's: Molecular's fx.c
runs the effects as atoms, and core/molecular.c supplies the few helpers Norilsk keeps elsewhere (mulz15, mini,
ms_samples2: its dsp.h and mol.c).
