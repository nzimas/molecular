#!/usr/bin/env python3
"""Checks a Molecular device image against what the Pocket Master's ROM and Sonicake's flash_boot see in the stock
application, before it may be packaged. Usage: verify.py molecular.bin stock_b_section.bin"""
import struct, sys

BASE = 0x10000                 # where flash_boot runs the application

def main(ours_path, stock_path):
    ours, stock = open(ours_path, "rb").read(), open(stock_path, "rb").read()
    fails = []
    # 1. the ROM header (the SDK's stub, 0xA4 .. 0x105): byte for byte as stock
    if ours[0xA4:0x106] != stock[0xA4:0x106]:
        diff = [hex(i) for i in range(0xA4, 0x106) if ours[i] != stock[i]]
        fails.append("header differs from stock at " + ", ".join(diff))
    # 2. the vector table: 41 jumps (j imm24, big-endian instructions) into the image
    for k in range(0, 0xA4, 4):
        w = struct.unpack_from(">I", ours, k)[0]
        if w >> 24 != 0x48:
            fails.append(f"vector {k // 4} is not a jump: {w:08x}")
            continue
        off = (w & 0xFFFFFF) * 2
        if off >= len(ours):
            fails.append(f"vector {k // 4} jumps outside the image (+{off:#x})")
    # 3. the image fits below the patches' flash (0x170000, hal.h HAL_PATCH_BASE; stock's presets at 0x190000)
    if BASE + len(ours) > 0x170000:
        fails.append(f"image ends at {BASE + len(ours):#x}, past the patches at 0x170000")
    print(f"{ours_path}: {len(ours)} bytes, runs at {BASE:#x}..{BASE + len(ours):#x}")
    for f in fails:
        print("  FAIL:", f)
    if not fails:
        print("  header identical to stock, 41 vectors in range, size within the application area")
    return not fails

if __name__ == "__main__":
    sys.exit(0 if main(sys.argv[1], sys.argv[2]) else 1)
