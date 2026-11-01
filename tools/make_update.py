#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Molecular's update for the Pocket Master's flash_boot, as the web installer sends it: only section 'b' (the
application, build/device/molecular.bin); Sonicake's other sections on the pedal are left as they are. One SysEx a
line, in hex, for tools/pmmidi ("pmmidi update FILE").

Usage: make_update.py molecular.bin out.txt"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sonicake_proto as proto
from htfw import Section


class _Only:
    def __init__(self, data):
        self.sections = [Section("b", 0x0, data)]


def main(app, out):
    msgs = [m for _, m in proto.update_messages(_Only(open(app, "rb").read()))]
    with open(out, "w") as f:
        f.write("".join(m.hex() + "\n" for m in msgs))
    print("%s: %d messages" % (out, len(msgs)))


if __name__ == "__main__":
    main(*sys.argv[1:3])
