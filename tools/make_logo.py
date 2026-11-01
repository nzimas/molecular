#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Molecular's mark and lettering, written as SVG into docs/assets (mark.svg, logo-dark.svg, logo-light.svg).

The mark: a molecule as the pedal draws one, a skeletal formula -- the generator (yellow) and its atoms in the
pedal's colours, bonded at 60 degrees -- inside a ring; one atom only outlined, arriving (MOTION: the molecule
rearranging itself), its way there a dotted arc. The lettering: geometric, built of 16-unit strokes like the
other firmwares' (Tonitor, Norilsk, Bigorna); its O an atom, its nucleus the generator's yellow.

Usage: make_logo.py [out_dir]"""
import math, os, sys

BG, FG = "#0E0D0D", "#EDEAE4"
GEN, PINK, CYAN, GREEN, VIOLET = "#FFE63C", "#FF64D2", "#50DCFF", "#78FF78", "#B4A0FF"


def mark_body(fg):
    """the mark's drawing on a 200 x 200 field, centred at (100, 100)"""
    g = (70, 114)                                    # the generator
    a1 = (116, 88)                                   # bonded to it, up and right
    a2 = (116, 36)                                   # a branch from a1, straight up
    a3 = (162, 114)                                  # a zigzag from a1
    a4 = (139, 154)                                  # arriving, bonded to a3 (dashed)
    out = []
    out.append('<circle cx="100" cy="100" r="88" fill="none" stroke="%s" stroke-width="8"/>' % fg)
    for (x0, y0), (x1, y1) in ((g, a1), (a1, a2), (a1, a3)):
        out.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="%s" stroke-width="7"/>' % (x0, y0, x1, y1, fg))
    d = math.hypot(a4[0] - a3[0], a4[1] - a3[1])     # (dashed up to the arriving atom's ring, not through it)
    e = (a4[0] - (a4[0] - a3[0]) * 14 / d, a4[1] - (a4[1] - a3[1]) * 14 / d)
    out.append('<line x1="%d" y1="%d" x2="%g" y2="%g" stroke="%s" stroke-width="5" stroke-dasharray="4 5"/>'
               % (a3[0], a3[1], e[0], e[1], fg))
    for k, (dx, dy, r, o) in enumerate(((-17, 9, 4.5, 0.7), (-29, 15, 3.5, 0.45), (-39, 20, 2.5, 0.25))):
        out.append('<circle cx="%g" cy="%g" r="%g" fill="%s" opacity="%g"/>' % (a4[0] + dx, a4[1] + dy, r, VIOLET, o))
    out.append('<circle cx="%d" cy="%d" r="24" fill="%s"/>' % (g[0], g[1], GEN))
    for (x, y), c in ((a1, PINK), (a2, GREEN), (a3, CYAN)):
        out.append('<circle cx="%d" cy="%d" r="16" fill="%s"/>' % (x, y, c))
    out.append('<circle cx="%d" cy="%d" r="11" fill="none" stroke="%s" stroke-width="5"/>' % (a4[0], a4[1], VIOLET))
    return "\n".join(out)


def letters(fg, x0):
    """MOLECULAR, 100 units tall (y 50 .. 150), 16-unit strokes; returns (svg, right edge)"""
    S, T, B = 16, 50, 150
    h = S / 2
    out, x = [], x0
    gap = 22

    def stroke(d, join="miter"):                     # (bevel: M's and A's points, cut at the letters' height)
        out.append('<path d="%s" fill="none" stroke="%s" stroke-width="%d" stroke-linejoin="%s"/>' % (d, fg, S, join))

    def M(w=88):
        nonlocal x
        stroke("M %g %g V %g L %g %g L %g %g V %g" % (x + h, B, T + 2, x + w / 2, T + 64, x + w - h, T + 2, B), "bevel")
        x += w + gap

    def O(nucleus=True):
        nonlocal x
        r = 50 - h
        cx = x + 50
        out.append('<circle cx="%g" cy="100" r="%g" fill="none" stroke="%s" stroke-width="%d"/>' % (cx, r, fg, S))
        if nucleus:
            out.append('<circle cx="%g" cy="100" r="11" fill="%s"/>' % (cx, GEN))
        x += 100 + gap

    def L(w=62):
        nonlocal x
        stroke("M %g %g V %g H %g" % (x + h, T, B - h, x + w))
        x += w + gap

    def E(w=66):
        nonlocal x
        stroke("M %g %g H %g V %g H %g" % (x + w, T + h, x + h, B - h, x + w))
        stroke("M %g 100 H %g" % (x + h, x + w - 8))
        x += w + gap

    def C():
        nonlocal x
        r = 50 - h
        cx = x + 50
        a0, a1 = math.radians(40), math.radians(320)
        stroke("M %g %g A %g %g 0 1 0 %g %g" % (cx + r * math.cos(a0), 100 - r * math.sin(a0), r, r,
                                                  cx + r * math.cos(a1), 100 - r * math.sin(a1)))
        x += 92 + gap

    def U(w=80):
        nonlocal x
        r = w / 2 - h
        stroke("M %g %g V %g A %g %g 0 0 0 %g %g V %g" % (x + h, T, B - h - r, r, r, x + w - h, B - h - r, T))
        x += w + gap

    def A(w=88):
        nonlocal x
        stroke("M %g %g L %g %g L %g %g" % (x + 4, B, x + w / 2, T + 4, x + w - 4, B), "bevel")
        stroke("M %g %g H %g" % (x + 22, 116, x + w - 22))
        x += w + gap

    def R(w=72):
        nonlocal x
        bowl = 27
        stroke("M %g %g V %g H %g A %g %g 0 0 1 %g %g H %g" % (x + h, B, T + h, x + w - bowl - h, bowl - h + 4,
                                                             bowl - h + 4, x + w - bowl - h, T + h + 2 * (bowl - h + 4),
                                                             x + h))
        stroke("M %g %g L %g %g" % (x + w - 34, T + 2 * (bowl + 4) - 4, x + w, B))
        x += w + gap

    for f in (M, O, L, E, C, U, L, A, R):
        f()
    return "\n".join(out), x - gap


def svg(w, h, body, label="Molecular"):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" role="img" aria-label="%s">\n'
            '<title>%s</title>\n%s\n</svg>\n' % (w, h, label, label, body))


def main(out):
    os.makedirs(out, exist_ok=True)
    mark = ('<rect width="200" height="200" rx="36" fill="%s"/>\n<g transform="translate(10 10) scale(0.9)">\n%s\n</g>'
            % (BG, mark_body(FG)))
    open(os.path.join(out, "mark.svg"), "w").write(svg(200, 200, mark))
    for name, fg in (("logo-dark.svg", FG), ("logo-light.svg", BG)):
        body, right = letters(fg, 250)
        open(os.path.join(out, name), "w").write(svg(int(right + 10), 200, mark_body(fg) + "\n" + body))
    print("wrote mark.svg, logo-dark.svg, logo-light.svg in", out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "docs", "assets"))
