#!/usr/bin/env python3
"""Assemble a dock from a panel-background SVG's nine frame pieces.

Usage: preview.py <panel-background.svg> <out.svg> [width height]
Render the result with e.g.  rsvg-convert -z 1.5 out.svg -o out.png
"""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
W = int(sys.argv[3]) if len(sys.argv) > 3 else 300
H = int(sys.argv[4]) if len(sys.argv) > 4 else 50

svg = open(src).read()
# Piece geometry from the generator: corners R x R, edge tiles S long, laid out
# top-left at (0,0), top at (R,0), top-right at (R+S,0), etc.
m = re.search(r'id="top">\s*<rect[^>]*x="([0-9.]+)"[^>]*width="([0-9.]+)"', svg)
R, S = float(m.group(1)), float(m.group(2))
inner = svg.split('\n', 2)[2].rsplit('</svg>', 1)[0]


def use(i, x, y, sx, sy, px, py):
    return f'<g transform="translate({x},{y}) scale({sx},{sy}) translate({-px},{-py})"><use href="#{i}"/></g>'


pieces = [
    ('topleft', 0, 0, 1, 1, 0, 0),
    ('top', R, 0, (W - 2 * R) / S, 1, R, 0),
    ('topright', W - R, 0, 1, 1, R + S, 0),
    ('left', 0, R, 1, (H - 2 * R) / S, 0, R),
    ('center', R, R, (W - 2 * R) / S, (H - 2 * R) / S, R, R),
    ('right', W - R, R, 1, (H - 2 * R) / S, R + S, R),
    ('bottomleft', 0, H - R, 1, 1, 0, R + S),
    ('bottom', R, H - R, (W - 2 * R) / S, 1, R, R + S),
    ('bottomright', W - R, H - R, 1, 1, R + S, R + S),
]
body = ''.join(use(*p) for p in pieces)
open(dst, 'w').write(
    f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
    f'width="{W + 40}" height="{H + 40}">'
    f'<defs><linearGradient id="bg" x1="0" x2="1"><stop offset="0" stop-color="#1b2a4a"/>'
    f'<stop offset="1" stop-color="#6a3a5a"/></linearGradient>{inner}</defs>'
    f'<rect width="{W + 40}" height="{H + 40}" fill="url(#bg)"/>'
    f'<g transform="translate(20,20)">{body}</g></svg>')
