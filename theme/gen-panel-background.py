# Generates a macOS Tahoe-style (clear Liquid Glass) Plasma panel-background.svg
#
# Dock: nearly clear body, specular rim that is bright along the top edge and
# fades down the sides to almost nothing at the bottom, framed by a thin dark
# outline. Corners are Apple-style continuous curves (superellipse), and every
# edge layer is one device pixel wide so straight edges stay sharp at 150%.
# Top bar (north-*): fm's glass colour as it appears on screen with fm's thin light
# edge along the bottom, 1 device pixel wide.
import math
import sys

R = 22            # corner extent (px): where the curve meets the straight edge.
                  # Keep R * DEVICE_SCALE whole so corner tiles stay pixel-aligned.
N = 5             # superellipse exponent: 2 = circle, ~5 = Apple continuous corner
S = 4             # tile size for edges/center
BODY = 0.03       # white body opacity (clear glass)
RIM_TOP = 0.60    # specular highlight strength at the top edge
RIM_SIDE = 0.14   # ... at the vertical midpoint of the sides
RIM_BOTTOM = 0.05 # ... at the bottom edge
OUTER = 0.30      # dark outline opacity (1 device px, crisp)
NORTH = 0.92      # top bar opacity: nearly solid, there is no blur behind panels
NORTH_TINT = '#282d3a'   # fm's glass as it shows on screen (rgb(20,22,29) @ .62 over the blurred
                         # wallpaper measures ~rgb(39,44,56)); fm's raw tint looks far too dark unblurred
NORTH_EDGE = 0.14        # bottom edge line (white), like the edge of fm's glass sheet
MARGIN = 6        # dock content margin
MASK_INSET = 2 / 1.5  # blur mask inset (2 device px), hides KWin's aliased blur edge under the outline
NORTH_MARGIN = 2  # top bar content margin

# Edge layers from the outside in: (width px, kind, opacity factor).
# kind "dark" = black outline, "rim" = white highlight scaled by the rim strength.
# Widths are whole *device* pixels at the display scale (DEVICE_SCALE), so the
# straight edges land exactly on the pixel grid and stay razor sharp.
DEVICE_SCALE = 1.5           # display scale factor (150%)
PX = 1 / DEVICE_SCALE        # one device pixel in logical px
LAYERS = [
    (1 * PX, 'dark', 1.0),   # crisp 1-device-px outline
    (1 * PX, 'rim', 1.0),    # crisp 1-device-px specular highlight
    (1 * PX, 'rim', 0.30),   # 1-device-px inner sheen
]
EDGE = sum(w for w, _, _ in LAYERS)  # total edge thickness

defs = []
out = []


def grad(gid, top, bottom, factor):
    defs.append(
        f'<linearGradient id="{gid}" x1="0" y1="0" x2="0" y2="1">'
        f'<stop offset="0" stop-color="#ffffff" stop-opacity="{top * factor:.4f}"/>'
        f'<stop offset="1" stop-color="#ffffff" stop-opacity="{bottom * factor:.4f}"/></linearGradient>')


def squircle(extent, steps=64):
    """Points of an Apple-style continuous corner (superellipse, exponent N)
    centred at (R,R) with the given extent, from the left edge (x=R-extent,
    y=R) to the top edge (x=R, y=R-extent)."""
    pts = []
    for i in range(steps + 1):
        t = (math.pi / 2) * i / steps
        c, s = math.cos(t), math.sin(t)
        pts.append((R - extent * c ** (2 / N), R - extent * s ** (2 / N)))
    return pts


def ring(e_out, e_in, style):
    """Quarter band between two squircle extents, top-left orientation."""
    outer = squircle(e_out)
    inner = squircle(e_in)[::-1] if e_in > 0.001 else [(R, R)]
    pts = outer + inner
    d = 'M ' + ' L '.join(f'{x:.4f},{y:.4f}' for x, y in pts) + ' Z'
    return f'<path style="{style}" d="{d}"/>'


def layer_styles(rim_kind):
    """Style per layer for a piece. rim_kind: ('flat', opacity) or ('grad', gradient id prefix)."""
    styles = []
    for i, (_, kind, f) in enumerate(LAYERS):
        if kind == 'dark':
            styles.append(f'fill:#000000;fill-opacity:{OUTER * f:.4f}')
        elif rim_kind[0] == 'flat':
            styles.append(f'fill:#ffffff;fill-opacity:{rim_kind[1] * f:.4f}')
        else:
            styles.append(f'fill:url(#{rim_kind[1]}-{i})')
    return styles


BODY_STYLE = f'fill:#ffffff;fill-opacity:{BODY}'

# Gradients per rim layer: top corners (top -> side), sides, bottom corners
# (drawn flipped, so local top = screen bottom).
for i, (_, kind, f) in enumerate(LAYERS):
    if kind == 'rim':
        grad(f'rim-tl-{i}', RIM_TOP, RIM_SIDE, f)
        grad(f'rim-side-{i}', RIM_SIDE, RIM_SIDE, f)
        grad(f'rim-bl-{i}', RIM_BOTTOM, RIM_SIDE, f)


def corner(eid, x, y, sx, sy, rim_kind, mask=False):
    tx = x + (R if sx < 0 else 0)
    ty = y + (R if sy < 0 else 0)
    g = f'<g id="{eid}" transform="translate({tx},{ty}) scale({sx},{sy})">'
    if mask:
        g += ring(R - MASK_INSET, 0.0001, 'fill:#000000')
    else:
        r = R
        for (w, _, _), st in zip(LAYERS, layer_styles(rim_kind)):
            g += ring(r, r - w, st)
            r -= w
        g += ring(r, 0.0001, BODY_STYLE)
    out.append(g + '</g>')


def straight(eid, x, y, horizontal, outer_first, rim_kind, mask=False):
    """Edge tile. horizontal: S wide x R tall (top/bottom); else R wide x S tall."""
    g = f'<g id="{eid}">'
    if mask:
        o = MASK_INSET if outer_first else 0
        if horizontal:
            g += f'<rect style="fill:#000000" x="{x}" y="{y + o}" width="{S}" height="{R - MASK_INSET}"/>'
        else:
            g += f'<rect style="fill:#000000" x="{x + o}" y="{y}" width="{R - MASK_INSET}" height="{S}"/>'
        out.append(g + '</g>')
        return
    pos = 0.0
    bands = []
    for (w, _, _), st in zip(LAYERS, layer_styles(rim_kind)):
        bands.append((pos, w, st))
        pos += w
    bands.append((pos, R - pos, BODY_STYLE))
    for off, w, st in bands:
        o = off if outer_first else R - off - w
        if horizontal:
            g += f'<rect style="{st}" x="{x}" y="{y + o}" width="{S}" height="{w}"/>'
        else:
            g += f'<rect style="{st}" x="{x + o}" y="{y}" width="{w}" height="{S}"/>'
    out.append(g + '</g>')


def frame(prefix, oy, mask=False):
    c = lambda n: prefix + n
    corner(c('topleft'), 0, oy, 1, 1, ('grad', 'rim-tl'), mask)
    straight(c('top'), R, oy, True, True, ('flat', RIM_TOP), mask)
    corner(c('topright'), R + S, oy, -1, 1, ('grad', 'rim-tl'), mask)
    straight(c('left'), 0, oy + R, False, True, ('grad', 'rim-side'), mask)
    st = 'fill:#000000' if mask else BODY_STYLE
    out.append(f'<rect id="{c("center")}" style="{st}" x="{R}" y="{oy + R}" width="{S}" height="{S}"/>')
    straight(c('right'), R + S, oy + R, False, False, ('grad', 'rim-side'), mask)
    corner(c('bottomleft'), 0, oy + R + S, 1, -1, ('grad', 'rim-bl'), mask)
    straight(c('bottom'), R, oy + R + S, True, False, ('flat', RIM_BOTTOM), mask)
    corner(c('bottomright'), R + S, oy + R + S, -1, -1, ('grad', 'rim-bl'), mask)


frame('', 0)
frame('mask-', 100, mask=True)

# Top bar (north): fm glass tint, no corners; a 1-device-px light line at the bottom.
ny = 200
tint = f'fill:{NORTH_TINT};fill-opacity:{NORTH}'
edge = f'fill:#ffffff;fill-opacity:{NORTH_EDGE}'
for i, n in enumerate(['topleft', 'top', 'topright', 'left', 'center', 'right',
                       'bottomleft', 'bottom', 'bottomright']):
    x, y = (i % 3) * 2, ny + (i // 3) * 2
    if n.startswith('bottom'):   # one device pixel high: tint with the light line on top of it
        out.append(f'<g id="north-{n}"><rect style="{tint}" x="{x}" y="{y}" width="1" height="{PX:.6f}"/>'
                   f'<rect style="{edge}" x="{x}" y="{y}" width="1" height="{PX:.6f}"/></g>')
    else:
        h = PX if n.startswith('top') else 1
        out.append(f'<rect id="north-{n}" style="{tint}" x="{x}" y="{y}" width="1" height="{h:.6f}"/>')

# Content margins
for i, side in enumerate(['top', 'bottom', 'left', 'right']):
    out.append(f'<rect id="hint-{side}-margin" style="fill:#ff00ff" '
               f'x="{100 + i * 10}" y="0" width="{MARGIN}" height="{MARGIN}"/>')
    out.append(f'<rect id="north-hint-{side}-margin" style="fill:#ff00ff" '
               f'x="{150 + i * 10}" y="0" width="{NORTH_MARGIN}" height="{NORTH_MARGIN}"/>')

svg = ('<?xml version="1.0" encoding="UTF-8"?>\n'
       '<svg xmlns="http://www.w3.org/2000/svg" width="200" height="210" viewBox="0 0 200 210">\n'
       '<defs>' + ''.join(defs) + '</defs>\n'
       + '\n'.join(out) + '\n</svg>\n')
sys.stdout.write(svg)
