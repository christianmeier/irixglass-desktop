#!/usr/bin/env python3
"""Plasma style pieces in fm's look: pop-ups, tooltips, buttons, fields, highlights.

Writes into a Plasma style (the WhiteSurLiquid-dark desktoptheme) everything the
user sees apart from the dock and the menu bar (gen-panel-background.py and
patch-tasks.py own those):

  dialogs/background.svgz         applet pop-ups, system tray, calendar, KRunner,
                                  notifications, volume/brightness OSD
  solid/dialogs/background.svgz   the same, opaque (used without compositing)
  widgets/tooltip.svgz            panel tooltips
  solid/widgets/tooltip.svgz      PlasmaComponents menus and tooltips inside pop-ups
  widgets/button.svgz             push buttons and tool buttons in pop-ups
  widgets/lineedit.svg            text fields (KRunner's search field)
  widgets/viewitem.svg            list highlights (KRunner results, menus)
  widgets/listitem.svg            PlasmaComponents list items
  widgets/tabbar.svgz             tab bars in pop-ups
  widgets/menubaritem.svgz        global menu entries in the top bar
  widgets/plasmoidheading.svgz    pop-up header / footer separator
  widgets/scrollbar.svgz          recoloured: fm's overlay thumb, no track
  colors                          the theme's colours = IrixGlass.colors

Values come from fm (~/CODE/irixfm, qml/Main.qml, EdgeRing.qml, Pill.qml):
  edge ring   white .65 at the top -> .10 -> .55 at the bottom (EdgeRing)
  panels      18 px corners; pills fully rounded, rgb(58,61,72); accent #0a84ff
  selection   white .16, radius 8; hover white .07-.08
  notice pill rgba(28,30,38,.82), border white .18 (fm's in-window pop-up)
Plasma pop-ups get no blur (panel blur is off in the theme's plasmarc), so the
pop-up glass is the notice pill's tint, a bit denser so text stays readable.

Every edge layer is a whole number of device pixels (PX = 1/DEVICE_SCALE) and
every corner radius times DEVICE_SCALE is whole, so edges stay crisp at 150%.

Usage: gen-plasma-dialogs.py <theme dir> [IrixGlass.colors]
Originals are kept once as <file>.orig next to each file it replaces.
"""
import gzip
import os
import shutil
import sys

DEVICE_SCALE = 1.5          # display scale (150%); update if the scale changes
PX = 1 / DEVICE_SCALE       # one device pixel in logical px
S = 4                       # size of the stretchable middle pieces
ACCENT = '#0a84ff'

# Pop-ups (dialogs/background): fm's 18 px panel corners, notice-pill glass.
DIALOG_R = 18
DIALOG_TINT = ('#1c1e26', 0.90)          # rgb(28,30,38): fm's notice pill, denser (no blur)
DIALOG_MARGIN = 8                        # content margin (WhiteSur's value)
# Edge ring alphas: top edge, end of top corner, side middle, start of bottom
# corner, bottom edge. fm's ring fades over the top/bottom quarter of the
# height; here the side pieces are stretched, so the side gradient does that.
DIALOG_RING = (0.65, 0.50, 0.10, 0.40, 0.55)
RING_LAYERS = [(1, 1.0), (1, 0.45)]      # (device px, factor): crisp line + soft inner half
SHADOW = 20                              # shadow reach outside the pop-up (px)
SHADOW_ALPHA = 0.38                      # shadow strength at the edge
# falloff (fraction of reach, alpha factor): roughly Gaussian
SHADOW_STOPS = [(0, 1.0), (0.12, 0.72), (0.3, 0.38), (0.55, 0.14), (0.8, 0.04), (1, 0)]

# Tooltips: smaller corners, same glass.
TIP_R = 10
TIP_TINT = ('#1c1e26', 0.92)
TIP_MARGIN = 6
TIP_RING = (0.45, 0.30, 0.16, 0.24, 0.32)
TIP_SHADOW = 12

# Buttons: fm's pills. Corner = half the usual button height (28 px), so
# buttons read as fully rounded; Plasma shrinks corners on smaller buttons.
PILL_R = 14
PILL = ('#3a3d48', 0.80)                 # rgb(58,61,72), denser than in fm: no wallpaper below
PILL_HOVER = ('#474b58', 0.90)
PILL_PRESSED = ('#5a5f70', 0.95)
PILL_RING = (0.40, 0.14, 0.10, 0.10, 0.16)   # EdgeRing on a pill: bottomAlpha .16
BUTTON_MARGIN = 6
TOOL_MARGIN = 4
FOCUS_RING = (ACCENT, 0.85)              # fm's focus ring: #0a84ff .85, 2 px

# Highlights: fm's selection white .16 radius 8, hover white .07.
ITEM_R = 8
SEL = ('#ffffff', 0.16)
SEL_HOVER = ('#ffffff', 0.20)
HOVER = ('#ffffff', 0.08)

# Scroll bars: fm's GlassScrollBar thumb alphas (white), no track.
SCROLL_THUMB = 0.32
SCROLL_HOVER = 0.45

# Text field: dark well with a soft ring, fm's path pill in edit mode.
FIELD_R = 10
FIELD = ('#000000', 0.25)
FIELD_RING = (0.10, 0.10, 0.10, 0.12, 0.18)


def f4(v):
    return f'{v:.4f}'.rstrip('0').rstrip('.')


class Sheet:
    """One SVG file: elements laid out in rows, gradients in <defs>."""

    def __init__(self):
        self.defs, self.out, self.y, self.gid = [], [], 0.0, 0

    def grad(self, color, stops):
        """Vertical gradient (objectBoundingBox) with (offset, alpha) stops."""
        self.gid += 1
        gid = f'g{self.gid}'
        s = ''.join(f'<stop offset="{f4(o)}" stop-color="{color}" stop-opacity="{f4(a)}"/>'
                    for o, a in stops)
        self.defs.append(f'<linearGradient id="{gid}" x1="0" y1="0" x2="0" y2="1">{s}</linearGradient>')
        return f'url(#{gid})'

    def radial(self, cx, cy, r, stops):
        self.gid += 1
        gid = f'g{self.gid}'
        s = ''.join(f'<stop offset="{f4(o)}" stop-color="#000" stop-opacity="{f4(a)}"/>'
                    for o, a in stops)
        self.defs.append(f'<radialGradient id="{gid}" gradientUnits="userSpaceOnUse" '
                         f'cx="{f4(cx)}" cy="{f4(cy)}" r="{f4(r)}">{s}</radialGradient>')
        return f'url(#{gid})'

    def hint(self, eid, w, h):
        w, h = max(w, 0.2), max(h, 0.2)   # a 0-size hint is ignored; WhiteSur uses 0.2
        self.out.append(f'<rect id="{eid}" fill="#f0f" x="-{f4(w + 10)}" y="{f4(self.y)}" '
                        f'width="{f4(w)}" height="{f4(h)}"/>')
        self.y += h + 2

    def svg(self):
        h = self.y + 4
        return ('<?xml version="1.0" encoding="UTF-8"?>\n'
                f'<svg xmlns="http://www.w3.org/2000/svg" width="300" height="{f4(h)}" '
                f'viewBox="-100 0 400 {f4(h)}">\n<defs>' + ''.join(self.defs) + '</defs>\n'
                + '\n'.join(self.out) + '\n</svg>\n')


def fill_attr(fill):
    color, alpha = fill
    return f'fill="{color}" fill-opacity="{f4(alpha)}"'


def band(R, r1, r2):
    """Quarter ring between radii r1 > r2 around (R,R), top-left corner."""
    d = f'M{f4(R - r1)},{f4(R)}A{f4(r1)},{f4(r1)} 0 0 1 {f4(R)},{f4(R - r1)}'
    if r2 > 1e-4:
        d += f'L{f4(R)},{f4(R - r2)}A{f4(r2)},{f4(r2)} 0 0 0 {f4(R - r2)},{f4(R)}Z'
    else:
        d += f'L{f4(R)},{f4(R)}Z'
    return d


def frame(sh, prefix, R, fill, ring=None, ring_color='#ffffff', layers=RING_LAYERS,
          margin=None, mask=False):
    """Nine-piece frame with circular corners (radius R), a body fill and an
    optional top-lit edge ring. ring = (top, top corner end, side middle,
    bottom corner start, bottom) alphas. Writes '<prefix>topleft' etc."""
    p = prefix
    x0, y0 = 0.0, sh.y
    widths = [n * PX for n, _ in layers] if ring else []
    total = sum(widths)

    def corner(eid, tx, ty, sx, sy, grads):
        g = f'<g id="{eid}" transform="translate({f4(tx)},{f4(ty)}) scale({sx},{sy})">'
        r = R
        for w, gfill in zip(widths, grads):
            g += f'<path fill="{gfill}" d="{band(R, r, r - w)}"/>'
            r -= w
        if fill:
            g += f'<path {fill_attr(fill)} d="{band(R, r, 0)}"/>'
        else:
            g += f'<path fill="#000" fill-opacity="0" d="{band(R, r, 0)}"/>'
        sh.out.append(g + '</g>')

    def edge(eid, x, y, horizontal, outer_first, fills):
        g = f'<g id="{eid}">'
        off = 0.0
        bands = []
        for w, f in zip(widths, fills):
            bands.append((off, w, f'fill="{f}"'))
            off += w
        bands.append((off, R - off, fill_attr(fill) if fill else 'fill="#000" fill-opacity="0"'))
        for o, w, attr in bands:
            o = o if outer_first else R - o - w
            if horizontal:
                g += f'<rect {attr} x="{f4(x)}" y="{f4(y + o)}" width="{S}" height="{f4(w)}"/>'
            else:
                g += f'<rect {attr} x="{f4(x + o)}" y="{f4(y)}" width="{f4(w)}" height="{S}"/>'
        sh.out.append(g + '</g>')

    if ring:
        top, top_c, mid, bot_c, bottom = ring
        tl = [sh.grad(ring_color, [(0, top * k), (1, top_c * k)]) for _, k in layers]
        bl = [sh.grad(ring_color, [(0, bottom * k), (1, bot_c * k)]) for _, k in layers]
        side = [sh.grad(ring_color, [(0, top_c * k), (0.22, mid * k), (0.78, mid * k), (1, bot_c * k)])
                for _, k in layers]
        topf = [sh.grad(ring_color, [(0, top * k), (1, top * k)]) for _, k in layers]
        botf = [sh.grad(ring_color, [(0, bottom * k), (1, bottom * k)]) for _, k in layers]
    else:
        tl = bl = side = topf = botf = []

    W = 2 * R + S
    corner(p + 'topleft', x0, y0, 1, 1, tl)
    edge(p + 'top', x0 + R, y0, True, True, topf)
    corner(p + 'topright', x0 + W, y0, -1, 1, tl)
    edge(p + 'left', x0, y0 + R, False, True, side)
    c = fill_attr(fill) if fill else 'fill="#000" fill-opacity="0"'
    sh.out.append(f'<rect id="{p}center" {c} x="{f4(x0 + R)}" y="{f4(y0 + R)}" width="{S}" height="{S}"/>')
    edge(p + 'right', x0 + R + S, y0 + R, False, False, side)
    corner(p + 'bottomleft', x0, y0 + W, 1, -1, bl)
    edge(p + 'bottom', x0 + R, y0 + R + S, True, False, botf)
    corner(p + 'bottomright', x0 + W, y0 + W, -1, -1, bl)
    sh.y += W + 4

    if mask:
        frame(sh, 'mask-' + p, R, ('#000000', 1))
    if margin is not None:
        for side_ in ('top', 'bottom', 'left', 'right'):
            sh.hint(f'{p}hint-{side_}-margin', margin, margin)


def shadow(sh, R, reach, alpha):
    """KWin shadow tiles around a frame with corner radius R. Corner tiles reach
    R into the window; the rounded window shape is cut out, so the glass never
    shows shadow through it."""
    y0 = sh.y
    T = reach + R
    stops_lin = [(1 - o, alpha * a) for o, a in SHADOW_STOPS][::-1]   # top (far) -> bottom (edge)
    rad = [((R + o * reach) / T, alpha * a) for o, a in SHADOW_STOPS]
    rad = [(0, 0), (R / T - 1e-4, 0)] + rad
    # top-left corner in local coords: tile (0,0)-(T,T), circle centre (T,T)
    g = sh.radial(T, T, T, rad)
    hole = (f'M{f4(reach)},{f4(T)}A{f4(R)},{f4(R)} 0 0 1 {f4(T)},{f4(reach)}L{f4(T)},{f4(T)}Z')
    d = f'M0,0H{f4(T)}V{f4(T)}H0Z' + hole
    for eid, tx, ty, sx, sy in (('topleft', 0, y0, 1, 1), ('topright', 2 * T + S, y0, -1, 1),
                                ('bottomleft', 0, y0 + 2 * T + S, 1, -1),
                                ('bottomright', 2 * T + S, y0 + 2 * T + S, -1, -1)):
        sh.out.append(f'<g id="shadow-{eid}" transform="translate({f4(tx)},{f4(ty)}) scale({sx},{sy})">'
                      f'<path fill="{g}" fill-rule="evenodd" d="{d}"/></g>')
    gv = sh.grad('#000', stops_lin)          # vertical: 0 = outside, 1 = window edge
    sh.gid += 1
    hid = f'g{sh.gid}'
    s = ''.join(f'<stop offset="{f4(o)}" stop-color="#000" stop-opacity="{f4(a)}"/>' for o, a in stops_lin)
    sh.defs.append(f'<linearGradient id="{hid}" x1="0" y1="0" x2="1" y2="0">{s}</linearGradient>')
    # top: outside at y=0; bottom: flipped; left: outside at x=0; right: flipped
    sh.out.append(f'<rect id="shadow-top" fill="{gv}" x="{f4(T)}" y="{f4(y0 + T - reach)}" width="{S}" height="{f4(reach)}"/>')
    sh.out.append(f'<g id="shadow-bottom" transform="translate({f4(T)},{f4(y0 + T + S + reach)}) scale(1,-1)">'
                  f'<rect fill="{gv}" width="{S}" height="{f4(reach)}"/></g>')
    sh.out.append(f'<rect id="shadow-left" fill="url(#{hid})" x="{f4(T - reach)}" y="{f4(y0 + T)}" width="{f4(reach)}" height="{S}"/>')
    sh.out.append(f'<g id="shadow-right" transform="translate({f4(T + S + reach)},{f4(y0 + T)}) scale(-1,1)">'
                  f'<rect fill="url(#{hid})" width="{f4(reach)}" height="{S}"/></g>')
    sh.out.append(f'<rect id="shadow-center" fill="none" x="{f4(T)}" y="{f4(y0 + T)}" width="{S}" height="{S}"/>')
    sh.y += 2 * T + S + 4
    for side_ in ('top', 'bottom', 'left', 'right'):
        sh.hint(f'shadow-hint-{side_}-margin', reach, reach)


def popup(R, tint, ring, margin, reach, solid=False):
    sh = Sheet()
    fill = (tint[0], 0.98) if solid else tint
    frame(sh, '', R, fill, ring, margin=margin, mask=True)
    sh.hint('hint-stretch-borders', 4, 4)
    shadow(sh, R, reach, SHADOW_ALPHA)
    return sh.svg()


def button():
    sh = Sheet()
    frame(sh, 'normal-', PILL_R, PILL, PILL_RING, margin=BUTTON_MARGIN)
    frame(sh, 'mask-normal-', PILL_R, ('#000000', 1))
    frame(sh, 'hover-', PILL_R, PILL_HOVER, PILL_RING, margin=BUTTON_MARGIN)
    frame(sh, 'pressed-', PILL_R, PILL_PRESSED, PILL_RING, margin=BUTTON_MARGIN)
    focus = (FOCUS_RING[1],) * 5
    two = [(1, 1.0), (1, 1.0)]
    frame(sh, 'focus-', PILL_R, None, focus, ring_color=FOCUS_RING[0], layers=two, margin=0)
    frame(sh, 'toolbutton-hover-', PILL_R, HOVER, margin=TOOL_MARGIN)
    frame(sh, 'toolbutton-pressed-', PILL_R, ('#ffffff', 0.18), margin=TOOL_MARGIN)
    frame(sh, 'toolbutton-focus-', PILL_R, None, focus, ring_color=FOCUS_RING[0], layers=two, margin=0)
    sh.hint('hint-stretch-borders', 4, 4)
    return sh.svg()


def lineedit():
    sh = Sheet()
    frame(sh, 'base-', FIELD_R, FIELD, FIELD_RING, margin=6)
    frame(sh, 'hover-', FIELD_R, None, (0.22,) * 5, layers=[(1, 1.0)], margin=0)
    focus = (FOCUS_RING[1],) * 5
    frame(sh, 'focus-', FIELD_R, None, focus, ring_color=FOCUS_RING[0],
          layers=[(1, 1.0), (1, 1.0), (1, 0.35)], margin=0)
    sh.hint('hint-focus-over-base', 4, 4)
    sh.hint('hint-stretch-borders', 4, 4)
    return sh.svg()


def viewitem():
    sh = Sheet()
    frame(sh, 'normal-', ITEM_R, None)
    frame(sh, 'hover-', ITEM_R, HOVER)
    frame(sh, 'selected-', ITEM_R, SEL)
    frame(sh, 'selected+hover-', ITEM_R, SEL_HOVER)
    sh.hint('hint-stretch-borders', 4, 4)
    return sh.svg()


def listitem():
    sh = Sheet()
    frame(sh, 'normal-', ITEM_R, None, margin=4)
    frame(sh, 'hover-', ITEM_R, HOVER)
    frame(sh, 'pressed-', ITEM_R, SEL, margin=4)
    frame(sh, 'section-', ITEM_R, ('#ffffff', 0.05), margin=4)
    sh.out.append(f'<rect id="separator" fill="#fff" fill-opacity=".10" x="0" y="{f4(sh.y)}" width="20" height="{f4(PX)}"/>')
    sh.y += 2
    sh.hint('hint-stretch-borders', 4, 4)
    return sh.svg()


def tabbar():
    """Active tab = fm's segmented-control thumb: white .18, rounded."""
    sh = Sheet()
    for d in ('north', 'south', 'east', 'west'):
        frame(sh, f'{d}-active-tab-', ITEM_R, ('#ffffff', 0.16), (0.30, 0.12, 0.08, 0.08, 0.10),
              layers=[(1, 1.0)])
        for side_ in ('top', 'bottom', 'left', 'right'):
            sh.hint(f'{d}-focus-hint-{side_}-margin', 6, 6)
    sh.hint('hint-stretch-borders', 4, 4)
    return sh.svg()


def menubaritem():
    """Global menu entries in the top bar: macOS menu-title highlight."""
    sh = Sheet()
    frame(sh, 'normal-', 6, None, margin=6)
    frame(sh, 'hover-', 6, ('#ffffff', 0.14), margin=6)
    frame(sh, 'pressed-', 6, ('#ffffff', 0.20), margin=6)
    sh.hint('hint-stretch-borders', 4, 4)
    return sh.svg()


def plasmoidheading():
    """Header/footer of pop-ups: no fill, a 1-device-px line like fm's dividers."""
    sh = Sheet()
    line = ('#ffffff', 0.10)
    for kind, line_at_bottom in (('header', True), ('footer', False)):
        y0 = sh.y
        for i, n in enumerate(('topleft', 'top', 'topright', 'left', 'center', 'right',
                               'bottomleft', 'bottom', 'bottomright')):
            x, yy = (i % 3) * 6, y0 + (i // 3) * 6
            g = f'<g id="{kind}-{n}"><rect fill="#000" fill-opacity="0" x="{x}" y="{yy}" width="4" height="4"/>'
            if (line_at_bottom and n.startswith('bottom')) or (not line_at_bottom and n.startswith('top')):
                ly = yy + 4 - PX if line_at_bottom else yy
                g += f'<rect {fill_attr(line)} x="{x}" y="{f4(ly)}" width="4" height="{f4(PX)}"/>'
            sh.out.append(g + '</g>')
        sh.y = y0 + 20
    for side_ in ('top', 'bottom', 'left', 'right'):
        sh.hint(f'hint-{side_}-margin', 6, 6)
    sh.hint('hint-stretch-borders', 4, 4)
    return sh.svg()


def scrollbar(orig):
    """fm's GlassScrollBar: no track, a white thumb (.32, .45 on hover).
    Recolours WhiteSur's slim scroll bar (keeps its sizes and insets)."""
    import xml.etree.ElementTree as ET
    alphas = (('mouseover-slider-', SCROLL_HOVER), ('slider-', SCROLL_THUMB), ('background-', 0.0))
    root = ET.fromstring(orig)

    def walk(el, alpha):
        eid = el.get('id', '')
        for pre, a in alphas:
            if eid.startswith(pre) and 'hint' not in eid:
                alpha = a
                break
        st = el.get('style', '')
        if alpha is not None and 'currentColor' in st:
            st = ';'.join(p for p in st.split(';') if p and not p.startswith('opacity'))
            el.set('style', f'{st};opacity:{f4(alpha)}')
        for c in el:
            walk(c, alpha)

    walk(root, None)
    ET.register_namespace('', 'http://www.w3.org/2000/svg')
    return ET.tostring(root, encoding='unicode')


def read_orig(theme, rel):
    path = os.path.join(theme, rel)
    src = path + '.orig' if os.path.exists(path + '.orig') else path
    with open(src, 'rb') as f:
        data = f.read()
    return (gzip.decompress(data) if data[:2] == b'\x1f\x8b' else data).decode()


def write(theme, rel, svg):
    path = os.path.join(theme, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.exists(path) and not os.path.exists(path + '.orig'):
        shutil.copy2(path, path + '.orig')
    data = svg.encode()
    if rel.endswith('.svgz'):
        with open(path, 'wb') as f:
            with gzip.GzipFile(fileobj=f, mode='wb', compresslevel=9, mtime=0, filename='') as z:
                z.write(data)
    else:
        with open(path, 'wb') as f:
            f.write(data)
    print(f'{rel}: {len(data)} bytes')


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    theme = sys.argv[1]
    write(theme, 'dialogs/background.svgz', popup(DIALOG_R, DIALOG_TINT, DIALOG_RING, DIALOG_MARGIN, SHADOW))
    write(theme, 'solid/dialogs/background.svgz',
          popup(DIALOG_R, DIALOG_TINT, DIALOG_RING, DIALOG_MARGIN, SHADOW, solid=True))
    write(theme, 'widgets/tooltip.svgz', popup(TIP_R, TIP_TINT, TIP_RING, TIP_MARGIN, TIP_SHADOW))
    write(theme, 'solid/widgets/tooltip.svgz', popup(TIP_R, TIP_TINT, TIP_RING, TIP_MARGIN, TIP_SHADOW, solid=True))
    write(theme, 'widgets/button.svgz', button())
    write(theme, 'widgets/lineedit.svg', lineedit())
    write(theme, 'widgets/viewitem.svg', viewitem())
    write(theme, 'widgets/listitem.svg', listitem())
    write(theme, 'widgets/tabbar.svgz', tabbar())
    write(theme, 'widgets/menubaritem.svgz', menubaritem())
    write(theme, 'widgets/plasmoidheading.svgz', plasmoidheading())
    write(theme, 'widgets/scrollbar.svgz', scrollbar(read_orig(theme, 'widgets/scrollbar.svgz')))
    if len(sys.argv) > 2:
        colors = os.path.join(theme, 'colors')
        if os.path.exists(colors) and not os.path.exists(colors + '.orig'):
            shutil.copy2(colors, colors + '.orig')
        shutil.copyfile(sys.argv[2], colors)
        print('colors: copied from', sys.argv[2])


if __name__ == '__main__':
    main()
