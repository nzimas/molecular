#!/usr/bin/env python3
"""Packages the Molecular application as a Sonicake HTFW update: the stock v1.3.3 image with its section 'b' (the
application, written by flash_boot at 0x10000) replaced and every other section (the effects processor's code and
data, the presets) kept as Sonicake ships them. Header version and product as stock.

Usage: pack.py molecular.bin stock_v1.3.3.bin out.bin"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
from htfw import Image, Section, rebuild

def main(app_path, stock_path, out_path):
    app = open(app_path, "rb").read()
    stock = Image(open(stock_path, "rb").read())
    assert not stock.check(), "the stock image fails its own checks"
    secs = [Section(s.tag, s.addr, app if s.tag == "b" else s.data) for s in stock.sections]
    raw = rebuild(stock, secs)
    img = Image(raw)
    problems = img.check()
    if problems:
        sys.exit("packaged image fails: " + "; ".join(problems))
    open(out_path, "wb").write(raw)
    print(f"{out_path}: {len(raw)} bytes, {img.product!r} v{img.version_str}, image CRC {img.checksum:#06x}")
    for s, o in zip(img.sections, stock.sections):
        print(f"   {s.tag} {s.addr:#08x} {s.length:8d} bytes  crc {s.crc:#06x}  {'NEW (Molecular)' if s.tag == 'b' else 'stock, unchanged' if s.data == o.data else '??'}")

if __name__ == "__main__":
    main(*sys.argv[1:4])
