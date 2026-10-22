# Generates the "IrixGlass" Aurorae window decoration: every window gets
# the look of fm (~/CODE/irixfm), the IRIX file manager with the Tahoe glass.
#
# Values are taken from fm's QML (qml/Main.qml, EdgeRing.qml, TrafficLights.qml):
#   corner radius   26 * 0.85 = 22 px (circular, like fm's PathRectangle)
#   glass tint      rgba(20, 22, 29, .62)  (fm's bodyTint at "Standard")
#   edge ring       white .65 at the top -> .10 at 25 % of the height -> .10 at 75 % -> .55
#                   at the bottom (fm's EdgeRing with bottomAlpha .55)
#   traffic lights  13.3 px circles, 26 px apart, #ff5f57 #febc2e #28c840,
#                   glyphs (x - +) on hover of the group
# The client area behind the title bar is left clear: Kvantum (IrixGlass) paints
# the same tint there, so title bar and window body read as one glass sheet.
#
# Usage: gen-decoration.py <output dir>   (writes decoration.svg, buttons, rc)
#
# How Aurorae v2 (KWin 6.8, aurorae/v2/decoration.cpp) uses this file:
# - "decoration[-inactive]-<piece>": a 9-piece KSvg frame around window + padding.
#   The padding (PaddingTop/...) is cut out into KWin's shadow; the part of the
#   padding inside the window rectangle (outside the rounded corners) is painted
#   by the decoration itself, so both come from the same SVG.
# - "decoration-maximized[-inactive]-center": maximized windows use this ONE
#   element (FrameSvg NoBorder), stretched over the whole window. No shadow.
# - "mask-<piece>": the blur region (thresholded at 1 logical px, inset by 1 px).
#   Maximized windows are blurred as a full rectangle. ("X-KDE-PluginInfo-blur"
#   in metadata.json is not read by v2; the mask alone switches blur on.)
# - "hint-stretch-borders": side pieces are stretched instead of tiled, so the
#   ring's vertical gradient follows the window height like fm's.
# - Buttons come from kwinrc ButtonsOnLeft/Right (not from LeftButtons here).
#   Button states: active, hover, pressed, deactivated (+ "-inactive"); with
#   ButtonGroupHover all lights show their glyph when the group is hovered.
#
# Borders: BorderLeft/Right/Bottom are clamped to KWin's border-size range
# (Tiny 1-4, Normal 4-6, Large 6-8, VeryLarge 8-12). install.sh sets
# BorderSize=Large, so the 8 px frame below is used as is.
# KWin keeps a theme's rc in memory while any window uses it: rc changes take
# effect after switching all windows to another theme and back (or relogin).
#
# Crisp at 150 %: every edge (piece sizes, window edge, ring width, frame,
# title) is a whole number of device pixels (multiples of 2/3 logical px).
# Curves are SVG arcs and the shadow is gradients, so the file stays small.
import os
import sys

R = 22                  # window corner radius (fm: 26 * u, u = 0.85)
TINT = (20, 22, 29)     # fm bodyTint rgb
GLASS = 0.62            # fm glassAlpha ("Standard")
SCALE = 1.5             # display scale: sizes below are whole device pixels at it
RING = 2 / SCALE        # edge ring: 2 device px (fm draws 1.2 px = 1.8 device px, anti-aliased)
RING_ALPHA = 1.2 / RING  # same light as fm's 1.2 px ring
RING_TOP, RING_MID, RING_BOTTOM = 0.65, 0.10, 0.55   # fm EdgeRing stops (0, .25-.75, 1)
H_REF = 500             # window height the fixed corner pieces assume for fm's 25 % / 75 % stops
INACTIVE_RING = 0.6     # inactive windows: ring dimmed by this factor
TITLE = 62              # title bar height = fm's toolbar row (72 × 0.85 ≈ 61), so the lights sit where fm's do
PAD_L = PAD_R = 40      # shadow padding
PAD_T, PAD_B = 26, 54   # less above, more below: the shadow falls downwards
SHADOW = 0.13           # shadow opacity at the window edge (active)
INACTIVE_SHADOW = 0.08
SHADOW_FALLOFF = 2.4    # Gaussian fall-off over the padding
BORDER = 8              # glass frame at the sides and bottom. KWin doesn't clip the client to the
                        # rounded shape, so the client's square corners must sit inside the 22 px
                        # curve: (R - B)^2 * 2 <= R^2  ->  B >= 6.5
LIGHTS = [('close', '#ff5f57', 'x'), ('minimize', '#febc2e', '-'),
          ('maximize', '#28c840', '+'), ('restore', '#28c840', '+')]
INACTIVE_LIGHT = 0.22    # the lights are grey (white .22 over the glass) in every window and state
DIAMETER = 40 / 3       # fm TrafficLights.diameter
BUTTON = 16             # button box (16 + 10 spacing = 26 px centre to centre)
LIGHT_X = 30            # centre of the first light from the window's left edge (fm: 27.5 × 0.85 + 6.7)

CL = PAD_L + R          # corner column width
RT = PAD_T + TITLE      # top row height
RB = PAD_B + R          # bottom row height
M = 8                   # size of the stretchable middle pieces
W, H = 2 * CL + M, RT + M + RB   # model window incl. padding
X0, Y0, X1, Y1 = PAD_L, PAD_T, W - PAD_R, H - PAD_B   # the window itself
RGB = 'rgb({},{},{})'.format(*TINT)


def n(v):
    """Short number: 3 decimals at most, no trailing zeros."""
    s = f'{v:.3f}'.rstrip('0').rstrip('.')
    return '0' if s == '-0' else s


def rr(x0, y0, x1, y1, r, box):
    """Path of a rounded rectangle cut to a box. The box must not cut through a
    corner arc (true for all pieces and zones here), so the cut is exact."""
    bx0, by0, bx1, by1 = box
    ix0, iy0, ix1, iy1 = max(x0, bx0), max(y0, by0), min(x1, bx1), min(y1, by1)
    if ix1 - ix0 < 1e-6 or iy1 - iy0 < 1e-6:
        return ''
    rad = {}
    for k, (cx, cy, sx, sy) in {'tl': (x0, y0, 1, 1), 'tr': (x1, y0, -1, 1),
                                'br': (x1, y1, -1, -1), 'bl': (x0, y1, 1, -1)}.items():
        # corner square of the arc, and how much of it lies in the box
        qx0, qx1 = sorted((cx, cx + sx * r))
        qy0, qy1 = sorted((cy, cy + sy * r))
        ox = min(qx1, bx1) - max(qx0, bx0)
        oy = min(qy1, by1) - max(qy0, by0)
        if r <= 0 or ox <= 1e-6 or oy <= 1e-6:
            rad[k] = 0
        elif abs(ox - r) < 1e-6 and abs(oy - r) < 1e-6:
            rad[k] = r
        else:
            raise ValueError(f'box {box} cuts the {k} arc')
    tl, tr, br, bl = rad['tl'], rad['tr'], rad['br'], rad['bl']

    def arc(rr_, x, y):
        return f'A{n(rr_)} {n(rr_)} 0 0 1 {n(x)} {n(y)}' if rr_ else f'L{n(x)} {n(y)}'
    return (f'M{n(ix0 + tl)} {n(iy0)}H{n(ix1 - tr)}' + (arc(tr, ix1, iy0 + tr) if tr else '') +
            f'V{n(iy1 - br)}' + (arc(br, ix1 - br, iy1) if br else '') +
            f'H{n(ix0 + bl)}' + (arc(bl, ix0, iy1 - bl) if bl else '') +
            f'V{n(iy0 + tl)}' + (arc(tl, ix0 + tl, iy0) if tl else '') + 'Z')


def rect(box, clip):
    return rr(*box, 0, clip)


PIECES = {  # name: box in the model window
    'topleft': (0, 0, CL, RT), 'top': (CL, 0, CL + M, RT), 'topright': (CL + M, 0, W, RT),
    'left': (0, RT, CL, RT + M), 'center': (CL, RT, CL + M, RT + M), 'right': (CL + M, RT, W, RT + M),
    'bottomleft': (0, RT + M, CL, H), 'bottom': (CL, RT + M, CL + M, H), 'bottomright': (CL + M, RT + M, W, H),
}
WINDOW = (X0, Y0, X1, Y1)

defs = []


def stops(pairs):
    return ''.join(f'<stop offset="{n(o)}" stop-opacity="{n(a)}"/>' for o, a in pairs)


# ---- shadow ---------------------------------------------------------------------
# A rounded-rect drop shadow from gradients: an elliptical radial gradient in each
# corner (centred on the corner arc) and a linear one along each side, all with the
# same stops, so they meet seamlessly. The ellipse is taller below the window
# (PAD_B) than above (PAD_T), which shifts the shadow downwards like macOS's.
EXT = R + PAD_L          # gradient length in x (arc centre -> padding edge)
EXT_T, EXT_B = R + PAD_T, R + PAD_B


def shadow_stops(strength):
    t0 = R / EXT          # the window edge, measured along x
    pts = [(0, strength)]
    for i in range(9):
        t = i / 8
        pts.append((t0 + (1 - t0) * t, strength * 2.718 ** (-(t * SHADOW_FALLOFF) ** 2) * (1 - t)))
    return pts


def shadow_defs(tag, strength):
    s = stops(shadow_stops(strength))
    for k, (cx, cy, ry) in {'tl': (X0 + R, Y0 + R, EXT_T), 'tr': (X1 - R, Y0 + R, EXT_T),
                            'bl': (X0 + R, Y1 - R, EXT_B), 'br': (X1 - R, Y1 - R, EXT_B)}.items():
        defs.append(f'<radialGradient id="s{tag}{k}" gradientUnits="userSpaceOnUse" cx="0" cy="0" r="1" '
                    f'gradientTransform="matrix({EXT} 0 0 {ry} {n(cx)} {n(cy)})">{s}</radialGradient>')
    for k, (x1, y1, x2, y2) in {'l': (X0 + R, 0, X0 + R - EXT, 0), 'r': (X1 - R, 0, X1 - R + EXT, 0),
                                't': (0, Y0 + R, 0, Y0 + R - EXT_T), 'b': (0, Y1 - R, 0, Y1 - R + EXT_B)}.items():
        defs.append(f'<linearGradient id="s{tag}{k}" gradientUnits="userSpaceOnUse" '
                    f'x1="{n(x1)}" y1="{n(y1)}" x2="{n(x2)}" y2="{n(y2)}">{s}</linearGradient>')


SHADOW_ZONES = {  # zone box: gradient
    'tl': (0, 0, X0 + R, Y0 + R), 'tr': (X1 - R, 0, W, Y0 + R),
    'bl': (0, Y1 - R, X0 + R, H), 'br': (X1 - R, Y1 - R, W, H),
    'l': (0, Y0 + R, X0, Y1 - R), 'r': (X1, Y0 + R, W, Y1 - R),
    't': (X0 + R, 0, X1 - R, Y0), 'b': (X0 + R, Y1, X1 - R, H),
}


# ---- edge ring ------------------------------------------------------------------
def ring_alpha(y):
    """fm's ring gradient at height y of a window H_REF tall."""
    q = y / H_REF
    if q < .25:
        return RING_TOP + (RING_MID - RING_TOP) * q / .25
    if q <= .75:
        return RING_MID
    return RING_MID + (RING_BOTTOM - RING_MID) * (q - .75) / .25


def ring_defs(tag, factor):
    f = factor * RING_ALPHA
    a_title, a_low = ring_alpha(TITLE), ring_alpha(H_REF - R)
    mid_len = H_REF - TITLE - R          # the stretched middle row, in a H_REF window
    rows = {
        'top': (Y0, RT, [(0, RING_TOP), (1, a_title)]),
        # stretched with the window, so its stops are fractions of the side length
        'mid': (RT, RT + M, [(0, a_title), ((.25 * H_REF - TITLE) / mid_len, RING_MID),
                             ((.75 * H_REF - TITLE) / mid_len, RING_MID), (1, a_low)]),
        'bottom': (RT + M, Y1, [(0, a_low), (1, RING_BOTTOM)]),
    }
    for row, (y0, y1, st) in rows.items():
        s = ''.join(f'<stop offset="{n(o)}" stop-color="#fff" stop-opacity="{n(a * f)}"/>' for o, a in st)
        defs.append(f'<linearGradient id="r{tag}{row}" gradientUnits="userSpaceOnUse" '
                    f'x1="0" y1="{n(y0)}" x2="0" y2="{n(y1)}">{s}</linearGradient>')


def bounds(box):
    bx0, by0, bx1, by1 = box   # an invisible rect fixes the element's bounds to the piece
    return (f'<rect x="{n(bx0)}" y="{n(by0)}" width="{n(bx1 - bx0)}" height="{n(by1 - by0)}" '
            f'opacity="0.001"/>')


def piece(box, tag):
    row = 'top' if box[1] == 0 else 'mid' if box[1] == RT else 'bottom'
    out = [bounds(box)]
    # Shadow: only outside the window (evenodd cut-out), never under the glass.
    for z, zone in SHADOW_ZONES.items():
        zb = (max(zone[0], box[0]), max(zone[1], box[1]), min(zone[2], box[2]), min(zone[3], box[3]))
        if zb[2] - zb[0] < 1e-6 or zb[3] - zb[1] < 1e-6:
            continue
        out.append(f'<path d="{rect(zb, zb)}{rr(*WINDOW, R, zb)}" fill="url(#s{tag}{z})" fill-rule="evenodd"/>')
    # Glass tint: the title bar and the frame around the client, never under the client.
    shape = rr(*WINDOW, R, box)
    if shape:
        client = rect((X0 + BORDER, Y0 + TITLE, X1 - BORDER, Y1 - BORDER), box)
        out.append(f'<path d="{shape}{client}" fill="{RGB}" fill-opacity="{GLASS}" fill-rule="evenodd"/>')
        inner = rr(X0 + RING, Y0 + RING, X1 - RING, Y1 - RING, R - RING, box)
        out.append(f'<path d="{shape}{inner}" fill="url(#r{tag}{row})" fill-rule="evenodd"/>')
    return ''.join(out)


def decoration_svg():
    body = []
    for active in (True, False):
        tag = 'a' if active else 'i'
        shadow_defs(tag, SHADOW if active else INACTIVE_SHADOW)
        ring_defs(tag, 1 if active else INACTIVE_RING)
        prefix = 'decoration' + ('' if active else '-inactive')
        for name, box in PIECES.items():
            body.append(f'<g id="{prefix}-{name}">{piece(box, tag)}</g>')
        # Maximized: one element, stretched over the window: square glass, no ring.
        body.append(f'<rect id="decoration-maximized{"" if active else "-inactive"}-center" '
                    f'width="{M}" height="{M}" fill="{RGB}" fill-opacity="{GLASS}"/>')
    for name, box in PIECES.items():   # window shape: the blur region
        body.append(f'<g id="mask-{name}">{bounds(box)}<path d="{rr(*WINDOW, R, box)}"/></g>')
    body.append('<rect id="hint-stretch-borders" width="1" height="1" fill="none"/>')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">'
            f'<defs>{"".join(defs)}</defs>{"".join(body)}</svg>\n')


def glyph(kind, c):
    """fm's hover glyphs (x - +, black .6), drawn as strokes."""
    s = DIAMETER * .22
    st = 'stroke="#000" stroke-opacity="0.6" stroke-width="1.3" stroke-linecap="round" fill="none"'
    if kind == 'x':
        return f'<path d="M{n(c - s)} {n(c - s)}L{n(c + s)} {n(c + s)}M{n(c + s)} {n(c - s)}L{n(c - s)} {n(c + s)}" {st}/>'
    s *= 1.2
    d = f'M{n(c - s)} {n(c)}H{n(c + s)}' + ('' if kind == '-' else f'M{n(c)} {n(c - s)}V{n(c + s)}')
    return f'<path d="{d}" {st}/>'


def button_svg(color, kind):
    c = BUTTON / 2
    r = n(DIAMETER / 2)
    box = f'<rect width="{BUTTON}" height="{BUTTON}" opacity="0.001"/>'
    edge = 'stroke="#000" stroke-opacity="0.25" stroke-width="0.5"'   # fm: border .5, black .25

    def light(fill, opacity=1, g=False, darken=0):
        s = f'<circle cx="{n(c)}" cy="{n(c)}" r="{r}" fill="{fill}"' + \
            (f' fill-opacity="{opacity}"' if opacity != 1 else '') + f' {edge}/>'
        if darken:
            s += f'<circle cx="{n(c)}" cy="{n(c)}" r="{r}" fill-opacity="{darken}"/>'
        return s + (glyph(kind, c) if g else '')

    # The user wants the lights grey all the time (active, inactive, under the mouse); the
    # active window still shows the glyphs on hover so the three can be told apart.
    grey = '#fff'
    states = {
        'active': light(grey, INACTIVE_LIGHT),
        'hover': light(grey, INACTIVE_LIGHT, g=True),
        'pressed': light(grey, INACTIVE_LIGHT + .1, g=True),
        'deactivated': light('#fff', .18),
        'inactive': light('#fff', INACTIVE_LIGHT),
        'hover-inactive': light('#fff', INACTIVE_LIGHT),
        'pressed-inactive': light('#fff', INACTIVE_LIGHT),
        'deactivated-inactive': light('#fff', .12),
    }
    body = ''.join(f'<g id="{k}-center">{box}{v}</g>' for k, v in states.items())
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{BUTTON}" height="{BUTTON}" '
            f'viewBox="0 0 {BUTTON} {BUTTON}">{body}</svg>\n')


TITLE_EDGE_TOP = (TITLE - BUTTON) // 2
TITLE_EDGE_LEFT = LIGHT_X - BUTTON // 2   # measured: Aurorae v2 counts TitleEdgeLeft from the frame's outer edge, borders not added
RC = f"""[General]
ActiveTextColor=235,235,240
InactiveTextColor=140,142,150
TitleAlignment=Center
TitleVerticalAlignment=Center
ButtonGroupHover=true

[Layout]
BorderTop=0
BorderLeft={BORDER}
BorderRight={BORDER}
BorderBottom={BORDER}
TitleEdgeTop={TITLE_EDGE_TOP}
TitleEdgeBottom={TITLE - BUTTON - TITLE_EDGE_TOP}
TitleEdgeLeft={TITLE_EDGE_LEFT}
TitleEdgeRight={TITLE_EDGE_LEFT}
TitleEdgeTopMaximized={TITLE_EDGE_TOP}
TitleEdgeBottomMaximized={TITLE - BUTTON - TITLE_EDGE_TOP}
TitleEdgeLeftMaximized={TITLE_EDGE_LEFT}
TitleEdgeRightMaximized={TITLE_EDGE_LEFT}
TitleBorderLeft=50
TitleBorderRight=50
TitleHeight={BUTTON}
ButtonWidth={BUTTON}
ButtonHeight={BUTTON}
ButtonSpacing={26 - BUTTON}
ButtonMarginTop=0
PaddingTop={PAD_T}
PaddingBottom={PAD_B}
PaddingLeft={PAD_L}
PaddingRight={PAD_R}
"""

METADATA = """{
    "KPackageStructure": "aurorae",
    "KPlugin": {
        "Authors": [{"Name": "macos-desktop-setup"}],
        "Category": "Plasma 6 Window Decorations",
        "ServiceTypes": ["aurorae"],
        "EnabledByDefault": true,
        "Name": "IrixGlass",
        "Description": "Window decoration with the glass look of fm (IRIX file manager, macOS Tahoe skin)",
        "Id": "IrixGlass",
        "License": "GPL-3.0-or-later",
        "Version": "1.1",
        "X-KDE-PluginInfo-blur": true
    }
}
"""


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'IrixGlass'
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'decoration.svg'), 'w') as f:
        f.write(decoration_svg())
    for name, color, kind in LIGHTS:
        with open(os.path.join(out, f'{name}.svg'), 'w') as f:
            f.write(button_svg(color, kind))
    with open(os.path.join(out, 'IrixGlassrc'), 'w') as f:
        f.write(RC)
    with open(os.path.join(out, 'metadata.json'), 'w') as f:
        f.write(METADATA)


if __name__ == '__main__':
    main()
