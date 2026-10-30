#!/bin/sh
# Molecular: the tables, the host tests, and the simulator (SDL2). Run from anywhere.
set -e
cd "$(dirname "$0")"
mkdir -p build/screens
python3 -I tools/gen_tables.py build/tonitor_tables.h >/dev/null
CFLAGS="-std=gnu99 -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-sign-compare -Ibuild -Icore"
cc $CFLAGS tests/host.c -o build/host -lm
if command -v sdl2-config >/dev/null; then
    cc $CFLAGS $(sdl2-config --cflags) sim/sim.c -o build/molecular-sim $(sdl2-config --libs) -lm
    echo "simulator: build/molecular-sim"
fi
