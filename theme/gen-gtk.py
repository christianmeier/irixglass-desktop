# Generates the "IrixGlass-Dark" GTK 3 / GTK 4 theme: WhiteSur-Dark recoloured
# with fm's palette (fm/, qml/Main.qml), so GTK windows match fm and
# the Qt side (Kvantum IrixGlass, colour scheme IrixGlass):
#   window / header  rgb(30, 32, 41)  fm's glass tint rgba(20,22,29,.62) as it reads
#                    over the blurred wallpaper (GTK can't ask KWin for blur, so opaque)
#   views            rgb(24, 26, 34)  (the colour scheme's View background)
#   sidebar/popover  rgb(36, 39, 49)  fm's sidebar rgba(42,45,56,.35) over the glass
#   buttons          rgb(58, 61, 72)  fm's pill tint, pill-shaped (radius = height / 2)
#   accent           #0a84ff, red #ff453a, orange #ff9f0a, green #30d158, purple #bf5af2
#   text             white; secondary white .55
#   title buttons    iPadOS 26 window controls like every other window (see lights_css())
#
# WhiteSur keeps its CSS compiled into gtk.gresource; this reads it through Gio,
# swaps the colours, appends the fm overrides and writes plain gtk.css files
# (plus the assets the CSS points to) into <output dir>/gtk-3.0 and gtk-4.0.
# Without libadwaita installed, GTK 4 apps read gtk-4.0/gtk(-dark).css of the
# theme named by gsettings org.gnome.desktop.interface gtk-theme (via the portal).
#
# Usage: gen-gtk.py <WhiteSur-Dark dir> <output dir>
#   e.g. gen-gtk.py ~/.themes/WhiteSur-Dark ~/.themes/IrixGlass-Dark
import itertools
import os
import re
import shutil
import subprocess
import sys

import gi

gi.require_version('Gio', '2.0')
from gi.repository import Gio  # noqa: E402

NAME = 'IrixGlass-Dark'

# Neutral greys (WhiteSur level -> fm blue-grey), interpolated in between.
# 0x24 view, 0x33 window, 0x37 header bar (= window: fm has no grey title bar),
# 0x65 button (fm pill). From 0xa0 up greys are text and stay neutral; the
# near-whites (0xd0+) become white like fm's text.
GREY_ANCHORS = [
    (0x00, (0, 0, 0)),
    (0x24, (24, 26, 34)),
    (0x33, (30, 32, 41)),
    (0x37, (30, 32, 41)),
    (0x65, (58, 61, 72)),
    (0xa0, (160, 160, 160)),
    (0xcf, (207, 207, 207)),
    (0xd0, (255, 255, 255)),
    (0xff, (255, 255, 255)),
]
# Exact swaps (lower-case hex), applied before the grey mapping.
HEX = {
    '#0860f2': '#0a84ff',   # WhiteSur accent -> fm accent
    '#2e7cf7': '#2f95ff',   # lighter accent (hover)
    '#4d8ef9': '#4aa3ff',
    '#3484e2': '#0a84ff',   # link colour
    '#2861c6': '#0a6fdc',   # darker accent (pressed)
    '#274578': '#164a85',
    '#ed5f5d': '#ff453a',   # destructive / error
    '#e9524a': '#ff5f57',   # wm close (fm traffic light)
    '#ba423b': '#d9483f',
    '#f77466': '#ff6961',
    '#e9873a': '#ff9f0a',   # warning
    '#79b757': '#30d158',   # success
}
RGBA = {
    (8, 96, 242): (10, 132, 255),
    (237, 95, 93): (255, 69, 58),
    (51, 51, 51, 0.96): (36, 39, 49, 1),   # sidebar / popover / menu: fm's sidebar, lighter than the glass;
                                           # opaque, as GTK gets no blur behind it
    (42, 42, 42, 0.96): (30, 32, 41, 0.96),
}

WINDOW = '#1e2029'
VIEW = '#181a22'
SIDEBAR = '#242731'
PILL = '#3a3d48'
ACCENT = '#0a84ff'

TITLEBUTTONS = ('close', 'minimize', 'maximize', 'restore')   # WhiteSur's title button PNGs
BOX = 16                # WhiteSur's title button box (logical px)


def grey(level):
    for (a, ca), (b, cb) in itertools.pairwise(GREY_ANCHORS):
        if a <= level <= b:
            t = 0 if b == a else (level - a) / (b - a)
            return tuple(round(x + (y - x) * t) for x, y in zip(ca, cb))
    return (level,) * 3


def is_grey(r, g, b):
    return max(r, g, b) - min(r, g, b) <= 3


def map_rgb(r, g, b):
    if is_grey(r, g, b):
        return grey(round((r + g + b) / 3))
    return (r, g, b)


def recolour(css):
    def hexsub(m):
        h = m.group(0).lower()
        if len(h) == 4:
            h = '#' + ''.join(c * 2 for c in h[1:])
        if h in HEX:
            return HEX[h]
        r, g, b = (int(h[i:i + 2], 16) for i in (1, 3, 5))
        return '#%02x%02x%02x' % map_rgb(r, g, b)

    def rgbasub(m):
        parts = [p.strip() for p in m.group(2).split(',')]
        if len(parts) not in (3, 4):
            return m.group(0)
        try:
            r, g, b = (int(p) for p in parts[:3])
        except ValueError:
            return m.group(0)
        a = float(parts[3]) if len(parts) == 4 else None
        key4 = (r, g, b, a)
        if key4 in RGBA:
            nr, ng, nb, na = RGBA[key4]
        elif (r, g, b) in RGBA:
            nr, ng, nb = RGBA[(r, g, b)]
            na = a
        else:
            nr, ng, nb = map_rgb(r, g, b)
            na = a
        if na is None:
            return f'{m.group(1)}({nr}, {ng}, {nb})'
        return f'{m.group(1)}({nr}, {ng}, {nb}, {parts[3] if key4 not in RGBA else na})'

    css = re.sub(r'#[0-9a-fA-F]{6}\b|#[0-9a-fA-F]{3}\b(?![-\w])', hexsub, css)
    css = re.sub(r'\b(rgba?)\(([^()]*)\)', rgbasub, css)
    return css


def window_radius(css):
    """WhiteSur's 12 px window / title bar corners -> fm's 22 px (rules naming
    window, decoration, headerbar or titlebar as the styled element)."""
    target = re.compile(r'^(window|decoration|headerbar)\b|\.titlebar\b')

    def rule(m):
        sel, body = m.group(1), m.group(2)
        last = [re.split(r'[\s>+~]+', part.strip())[-1] for part in sel.split(',')]
        if any(target.search(p) for p in last):
            body = re.sub(r'(radius:)([^;]*)', lambda r: r.group(1) + r.group(2).replace('12px', '22px'), body)
        return sel + '{' + body + '}'
    return re.sub(r'([^{}]*)\{([^{}]*)\}', rule, css)


# fm overrides, appended after WhiteSur's rules (same selectors win by order).
# GTK 3 draws the placeholder through Pango, which ignores alpha: use opaque colours.
SECONDARY = '#9a9ca3'   # fm's secondary text (white .55) over the glass
HAIRLINE = '#2c2e37'    # white .07 over the glass: header bar / sidebar separators

COMMON = f"""
/* ---- IrixGlass (fm) overrides ---- */
@define-color placeholder_text_color {SECONDARY};

/* fm pills: buttons and entries are fully rounded */
button, entry, entry.search, headerbar entry, headerbar entry.search, headerbar button,
combobox > box > button, dropdown > button, menubutton > button, .linked > button:only-child {{
  border-radius: 9999px;
}}
.linked:not(.vertical) > button:first-child:not(:only-child),
.linked:not(.vertical) > entry:first-child:not(:only-child),
.linked:not(.vertical) > menubutton:first-child:not(:only-child) > button {{
  border-radius: 9999px 0 0 9999px;
}}
.linked:not(.vertical) > button:last-child:not(:only-child),
.linked:not(.vertical) > entry:last-child:not(:only-child),
.linked:not(.vertical) > menubutton:last-child:not(:only-child) > button {{
  border-radius: 0 9999px 9999px 0;
}}
spinbutton button, spinbutton > button {{ border-radius: 0; }}
spinbutton:not(.vertical) > button:last-child {{ border-radius: 0 9999px 9999px 0; }}
button {{ color: #ffffff; }}
button:disabled {{ color: rgba(255, 255, 255, 0.35); }}

/* header bar: the same glass as the window body, no grey bar, no dimming when inactive */
headerbar, headerbar:backdrop, .titlebar:not(headerbar), .titlebar:backdrop:not(headerbar) {{
  background-color: {WINDOW};
  background-image: none;
  color: #ffffff;
  border-color: {HAIRLINE};
  box-shadow: none;
}}

/* lists: selected rows in fm's blue (sidebars keep fm's white .16 highlight) */
list:not(.navigation-sidebar) > row:selected,
listview:not(.navigation-sidebar) > row:selected,
columnview > listview > row:selected,
list.boxed-list > row:selected {{
  background-color: {ACCENT};
  color: #ffffff;
}}
placessidebar list:not(.navigation-sidebar) > row:selected, .sidebar list:not(.navigation-sidebar) > row:selected,
list.sidebar:not(.navigation-sidebar) > row:selected, list.navigation-sidebar > row:selected {{
  background-color: rgba(255, 255, 255, 0.16);
  color: #ffffff;
}}
selection, *:selected {{ color: #ffffff; }}
"""

GTK4 = f"""
@define-color headerbar_bg_color {WINDOW};
@define-color headerbar_backdrop_color {WINDOW};
@define-color headerbar_border_color {HAIRLINE};
@define-color headerbar_shade_color rgba(0, 0, 0, 0.25);
@define-color sidebar_bg_color {SIDEBAR};
@define-color sidebar_backdrop_color {SIDEBAR};
@define-color window_fg_color #ffffff;
@define-color view_fg_color #ffffff;
@define-color card_fg_color #ffffff;
@define-color popover_fg_color #ffffff;
@define-color sidebar_fg_color #ffffff;
@define-color dialog_fg_color #ffffff;
@define-color accent_color {ACCENT};
@define-color accent_bg_color {ACCENT};
entry > text > placeholder, text > placeholder {{ color: {SECONDARY}; opacity: 1; }}
/* the header bar is fm's toolbar height; the window controls: lights_css() */
headerbar {{ min-height: 62px; }}
"""

GTK3 = f"""
@define-color theme_fg_color #ffffff;
@define-color fg_color #ffffff;
@define-color theme_unfocused_fg_color #ffffff;
@define-color wm_bg {WINDOW};
@define-color wm_bg_unfocused {WINDOW};
@define-color titlebar_gradient_a {WINDOW};
@define-color titlebar_gradient_b {WINDOW};
@define-color link_color {ACCENT};
headerbar, .titlebar:not(headerbar) {{ min-height: 62px; }}
"""

# iPadOS 26 window controls (WWDC25 session 208, measured from Apple's frames), the same numbers as
# Firefox (configs/firefox/customChrome.css), the IrixGlass title bars (gen-decoration.py) and fm
# (TrafficLights.qml): at rest a grey glass capsule 38.667 x 20.667 px (see-through, 1 device px
# light rim; left edge 16 px from the window, centred in the 62 px header bar) with three muted 6 px
# dots 9.333 px apart; under the pointer it grows to 72.667 x 28.667 px, the dots become the vivid
# 16 px lights 22 px apart, and the glyphs grow out of a point. Only the light under the pointer gets
# the coloured glow (and, where possible, lifts to 1.08). Inactive windows: grey dots.
# GTK lays widgets out on whole pixels, so its numbers are the nearest whole ones (see lights_css).
#   name, resting dot, light, glyph colour
LIGHTS3 = [('close', '#df5356', '#e90018', '#520000'), ('minimize', '#eebb46', '#f5cb00', '#5b2301'),
           ('maximize', '#5ecf4b', '#01e33e', '#0d7100')]
GLYPHS = {
    'close': '<path d="M13.7 13.7L26.3 26.3M26.3 13.7L13.7 26.3" stroke="{c}" stroke-width="4" '
             'stroke-linecap="round" fill="none"/>',
    'minimize': '<path d="M13.8 20H26.2" stroke="{c}" stroke-width="3.3" stroke-linecap="round" fill="none"/>',
    'maximize': '<path d="M12.5 12.5H23L12.5 23ZM27.5 27.5H17L27.5 17Z" fill="{c}" stroke="{c}" '
                'stroke-width="1.6" stroke-linejoin="round"/>',
}
SETTLE, SPRING = 'cubic-bezier(.3, 0, .2, 1)', 'cubic-bezier(.3, 1.45, .6, 1)'
CAPSULE = 'background-color: rgba(255, 255, 255, 0.08); box-shadow: inset 0 0 0 0.667px rgba(255, 255, 255, 0.14);'


def rgba(hex_, a):
    return f'rgba({int(hex_[1:3], 16)}, {int(hex_[3:5], 16)}, {int(hex_[5:7], 16)}, {a})'


def glow_svg(light, w, h):
    """The hovered light's glow as an image layer filling its button (w x h px), fading out inside it."""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">'
            f'<defs><radialGradient id="g" cx="{w / 2}" cy="{h / 2 + 2}" r="{min(w, h) / 2}" '
            f'gradientUnits="userSpaceOnUse"><stop offset=".55" stop-color="{light}" stop-opacity=".5"/>'
            f'<stop offset="1" stop-color="{light}" stop-opacity="0"/></radialGradient></defs>'
            f'<rect width="{w}" height="{h}" fill="url(#g)"/></svg>\n')


def lights_css(gtk4):
    """Both GTK versions: each button is a fixed box and its dot is an SVG disc whose background-size
    grows from 6 to 16 px (GTK animates sizes and cross-fades images); the capsule is the box around
    the buttons, which fills the bar's height, so vertical margins give it its height.
    GTK 4: windowcontrols, whose :hover covers all three; buttons 10 / 22 px wide (pitch), the icon
    shrunk to 1 px (0 makes GTK log criticals), the glow an image layer filling the button, the lift
    a transform. GTK 3: box.left never gets :hover (no event window, and GTK 3 doesn't give
    ancestors the prelight state), so the group can't grow (checked live: only the hovered button
    did, which split the capsule). Like Chromium, its capsule keeps the grown size with the resting
    dots in it, and the button under the pointer becomes the light with glyph and glow (box-shadow;
    no lift). Its buttons can't get smaller than their 16 px icon; pitch 22 = 16 + box spacing 6."""
    box = 'headerbar windowcontrols.start' if gtk4 else 'headerbar > box.left'
    btn = f'{box} button' if gtk4 else f'{box} button.titlebutton'
    grow = f'{box}:hover'
    if gtk4:   # bar content 46 px tall (62 - 2 x 8 padding)
        pad, pad_g, vm, vm_g = 4, 3, 13, 9
    else:      # the 62 px bar; always the grown capsule
        pad, pad_g, vm, vm_g = 6, 6, 17, 17
    css = f"""
/* iPadOS 26 window controls (gen-gtk.py lights_css) */
{box} {{
  margin: {vm}px 0 {vm}px 8px; padding: 0 {pad}px;{' border-spacing: 0;' if gtk4 else ''}
  border-radius: 999px; {CAPSULE}
  transition: all 250ms {SETTLE};
}}
"""
    if gtk4:
        css += f"""{grow} {{ margin: {vm_g}px 0 {vm_g}px 8px; padding: 0 {pad_g}px; background-color: rgba(255, 255, 255, 0.10);
  transition: all 380ms {SPRING}; }}
"""
    if gtk4:
        css += f"""
{btn} {{ min-width: 10px; min-height: 0; padding: 0; margin: 0; border: none; border-radius: 999px;
  box-shadow: none; color: transparent; background-color: transparent;
  background-repeat: no-repeat; background-position: center;
  transition: all 250ms {SETTLE}; }}
{btn} > image {{ -gtk-icon-size: 1px; opacity: 0; min-width: 0; min-height: 0; }}
{grow} button {{ min-width: 22px; transition: all 380ms {SPRING}; }}
{grow} button:hover {{ transform: scale(1.08); }}
{grow} button:hover:active {{ transform: scale(1.02); }}
"""
    else:
        css += f"""
{btn} {{ min-width: 16px; min-height: 16px; padding: 0; margin: 0; border: none;
  box-shadow: none; color: transparent; -gtk-icon-transform: scale(0);
  background-repeat: no-repeat; background-position: center;
  transition: all 250ms {SETTLE}; }}
"""
    for name, _dot, light, _glyph in LIGHTS3:
        sel = f'{btn}.{name}'
        names = [sel] + ([f'{btn}.restore'] if name == 'maximize' and gtk4 else [])
        rest_sel = ', '.join(names + [f'{n}:backdrop' for n in names] + [f'{n}:hover' for n in names])
        # GTK 4: the whole group grows on hover; GTK 3: only the hovered button turns into its light
        grow_sel = (', '.join(f'{grow} {n.split(" ")[-1]}' for n in names) if gtk4
                    else ', '.join(f'{n}:hover' for n in names))
        hover_sel = ', '.join(f'{grow} {n.split(" ")[-1]}:hover' for n in names)

        def url(kind):
            return f'url("windows-assets/{kind}.svg")'
        glyph, dot, lit = url(f'glyph-{name}'), url(f'disc-{name}-dot'), url(f'disc-{name}-light')
        css += f"""{rest_sel} {{ background-image: {glyph}, {dot}; background-size: 0 0, 6px 6px; }}
{', '.join(n + ':backdrop' for n in names)} {{ background-image: {glyph}, {url('disc-grey')}; }}
{grow_sel}{''.join(f', {grow} {n.split(" ")[-1]}:backdrop' for n in names) if gtk4 else ''} {{ background-image: {glyph}, {lit};
  background-size: 16px 16px, 16px 16px; }}
{', '.join(n + ':active' for n in names)} {{ background-image: {glyph}, {url(f'disc-{name}-pressed')}; }}
"""
        if gtk4:   # glow: a third layer filling the button, fading out inside it
            css += f"""{hover_sel} {{ background-image: {glyph}, {lit}, {url(f'glow-{name}')};
  background-size: 16px 16px, 16px 16px, 100% 100%; }}
"""
        else:
            css += f"""{sel}:hover {{ border-radius: 999px; box-shadow: 0 2px 7px {rgba(light, .5)}; }}
"""
    return css

# Chromium's own frame in GTK mode (browser.custom_chrome_frame, extensions.theme.system_theme 1;
# ui/gtk/nav_button_provider_gtk.cc in Chromium 156). Chromium doesn't use a GTK widget: it renders
# each button x state (normal, :hover, :hover:active, :disabled, each also with :backdrop) once into a
# static image, through style contexts with the path
#   window.background.csd[.maximized] headerbar.header-bar.titlebar windowcontrols
#     button.titlebutton.<close|minimize|maximize> image
# (.default-decoration instead of .header-bar for app windows; restore uses .maximize). Only the
# button's background, frame and the window-*-symbolic icon are drawn (GTK 4 also the image's
# background), never the windowcontrols box. All states are drawn at the normal state's size
# (min-width/height + padding + border); the margins place the buttons, 6 px apart (fixed) plus
# margin-right + margin-left. Hover is per button (views::ImageButton cross-fades the normal and
# hovered images, nothing else moves). Normal GTK apps never match these rules: GTK 3 has no
# windowcontrols node, GTK 4's window buttons have no .titlebutton, and only Chromium's window
# node has the class .chromium.
# So Chromium gets the iPadOS controls in their grown geometry, all the time: the 72.667 x 28.667
# capsule split into three touching button images (each paints its slice, like the title bars),
# muted 6 px dots at rest, the vivid light with glyph, glow and lift on the button under the pointer.
CHR_H = 36                              # button height; the capsule is centred in it
CHR_X = 2                               # capsule left edge in the strip
CHR_PITCH, CHR_LIGHT, CHR_DOT = 22, 16, 6
CHR_CAP_W, CHR_CAP_H = 72.667, 28.667
CHR_FIRST = CHR_X + (CHR_CAP_W - 2 * CHR_PITCH) / 2            # centre of the close light
# button boxes in the strip (x, width), split about halfway between the lights; even widths, so at
# scale 1.5 they meet on whole pixels (odd ones leave a 1 px overlap seam)
CHR_BOX = {'close': (0, 28), 'minimize': (28, 22), 'maximize': (50, 26)}
CHR_MARGIN_L = 2                        # first button: capsule left edge 16 px from the window edge
# Chromium scales all buttons down when height + margin-top + margin-bottom is taller than the room
# it gives them in the tab strip row. Equal negative vertical margins keep the 36 px images
# unscaled and still centred: Chromium places them at (room - height) / 2 either way.
CHR_VMARGIN = -(CHR_H // 2 - 4)
CHR_STATES = ('rest', 'hover', 'pressed', 'backdrop')


def chromium_svg(name, state):
    """One button's slice of the strip: the capsule (see-through fill, light rim) and this button's
    dot or light. The glow is a radial gradient that fades out inside the button's box."""
    x0, w = CHR_BOX[name]
    i = [n for n, *_ in LIGHTS3].index(name)
    _n, dot, light, glyph_c = LIGHTS3[i]
    cx, cy = CHR_FIRST + i * CHR_PITCH, CHR_H / 2
    cy0, r = cy - CHR_CAP_H / 2, CHR_CAP_H / 2
    rim = 2 / 3
    cap = f'x="{CHR_X}" y="{cy0:.3f}" width="{CHR_CAP_W}" height="{CHR_CAP_H}" rx="{r:.3f}"'
    capr = (f'x="{CHR_X + rim / 2:.3f}" y="{cy0 + rim / 2:.3f}" width="{CHR_CAP_W - rim:.3f}" '
            f'height="{CHR_CAP_H - rim:.3f}" rx="{r - rim / 2:.3f}"')
    grown = state in ('hover', 'pressed')
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w * 2}" height="{CHR_H * 2}" '
           f'viewBox="{x0} 0 {w} {CHR_H}">'
           f'<rect {cap} fill="#fff" fill-opacity="0.08"/>'   # one fill in every state: the slices must match
           f'<rect {capr} fill="none" stroke="#fff" stroke-opacity="0.14" stroke-width="{rim:.3f}"/>')
    if not grown:
        fill = dot if state == 'rest' else '#fff" fill-opacity="0.3'
        svg += f'<circle cx="{cx:.3f}" cy="{cy}" r="{CHR_DOT / 2}" fill="{fill}"/>'
    else:
        lift = 1.08 if state == 'hover' else 1.02
        rl = CHR_LIGHT / 2 * lift
        rx = min(rl + 6, cx - x0 - .3, x0 + w - cx - .3)
        ry = min(rl + 7, CHR_H - cy - 2.3)
        svg += (f'<defs><radialGradient id="gl" gradientUnits="userSpaceOnUse" cx="{cx:.3f}" cy="{cy + 2}" '
                f'r="{ry:.3f}" gradientTransform="translate({cx:.3f} {cy + 2}) scale({rx / ry:.4f} 1) '
                f'translate({-cx:.3f} {-(cy + 2)})"><stop offset="0" stop-color="{light}" stop-opacity=".5"/>'
                f'<stop offset=".55" stop-color="{light}" stop-opacity=".35"/>'
                f'<stop offset="1" stop-color="{light}" stop-opacity="0"/></radialGradient></defs>'
                f'<ellipse cx="{cx:.3f}" cy="{cy + 2}" rx="{rx:.3f}" ry="{ry:.3f}" fill="url(#gl)"/>'
                f'<circle cx="{cx:.3f}" cy="{cy}" r="{rl:.3f}" fill="{light}"/>')
        if state == 'pressed':
            svg += f'<circle cx="{cx:.3f}" cy="{cy}" r="{rl:.3f}" fill="#000" fill-opacity="0.18"/>'
        svg += (f'<g transform="translate({cx - rl:.3f} {cy - rl:.3f}) scale({2 * rl / 40:.4f})">'
                + GLYPHS[name].replace('{c}', glyph_c) + '</g>')
    return svg + '</svg>\n'


def chromium_buttons(dst):
    """The PNGs chromium_css() points to (2x: Chromium renders at the next integer scale)."""
    for name in CHR_BOX:
        for state in CHR_STATES:
            out = os.path.join(dst, f'chromium-{name}-{state}.png')
            subprocess.run(['rsvg-convert', '-o', out], input=chromium_svg(name, state).encode(),
                           check=True)


def chromium_css():
    # Chromium's window node has the class .chromium, and WhiteSur styles it: GTK 3
    # `window.background.chromium headerbar.titlebar button.titlebutton` forces 16 x 16 and margin 0,
    # GTK 4 `window.background.chromium headerbar.header-bar.titlebar button.titlebutton.minimize`
    # tints the icon. These selectors outrank both (and `.maximized headerbar button.titlebutton.*`).
    btns = [f'window.background.chromium headerbar.{h}.titlebar windowcontrols button.titlebutton'
            for h in ('header-bar', 'default-decoration')]
    btns.append('window.background.csd headerbar.titlebar windowcontrols button.titlebutton')

    def sel_(suffix):
        return ', '.join(b + suffix for b in btns)
    css = f"""
/* Chromium's own frame in GTK mode (gen-gtk.py chromium_css) */
{sel_(' image')} {{ background: none; box-shadow: none; border: none; }}
"""
    for name, (_x, w) in CHR_BOX.items():
        def img(state):
            return f'url("windows-assets/chromium-{name}-{state}.png")'
        ml = CHR_MARGIN_L if name == 'close' else -3
        css += f"""{sel_('.' + name)} {{ min-width: {w}px; min-height: {CHR_H}px; padding: 0;
  margin: {CHR_VMARGIN}px -3px {CHR_VMARGIN}px {ml}px; border: none; border-radius: 0;
  box-shadow: none; outline: none; color: transparent; -gtk-icon-shadow: none; transition: none;
  background-color: transparent; background-repeat: no-repeat; background-position: 0 0;
  background-size: {w}px {CHR_H}px; background-image: {img('rest')}; }}
{sel_(f'.{name}:backdrop')}, {sel_(f'.{name}:disabled')} {{ background-image: {img('backdrop')}; }}
{sel_(f'.{name}:hover')} {{ background-image: {img('hover')}; }}
{sel_(f'.{name}:hover:active')} {{ background-image: {img('pressed')}; }}
"""
    return css


def title_buttons(dst):
    """The glyph SVGs lights_css() points to; WhiteSur's title button PNGs (still named by its
    CSS, overridden by lights_css) become empty placeholders."""
    os.makedirs(dst, exist_ok=True)
    for name, _dot, _light, colour in LIGHTS3:
        with open(os.path.join(dst, f'glyph-{name}.svg'), 'w') as f:
            f.write('<svg xmlns="http://www.w3.org/2000/svg" width="40" height="40" viewBox="0 0 40 40">'
                    + GLYPHS[name].replace('{c}', colour) + '</svg>\n')
    def disc(fill):
        return (f'<svg xmlns="http://www.w3.org/2000/svg" width="40" height="40" viewBox="0 0 40 40">'
                f'<circle cx="20" cy="20" r="20" {fill}/></svg>\n')
    discs = {'disc-grey': disc('fill="#fff" fill-opacity="0.3"')}
    for name, dot, light, _colour in LIGHTS3:
        discs[f'disc-{name}-dot'] = disc(f'fill="{dot}"')
        discs[f'disc-{name}-light'] = disc(f'fill="{light}"')
        discs[f'disc-{name}-pressed'] = disc(f'fill="{light}"/><circle cx="20" cy="20" r="20" fill-opacity="0.18"')
    for name, _dot, light, _colour in LIGHTS3:
        discs[f'glow-{name}'] = glow_svg(light, 22, 28)
    for base, svg in discs.items():
        with open(os.path.join(dst, f'{base}.svg'), 'w') as f:
            f.write(svg)
    empty = f'<svg xmlns="http://www.w3.org/2000/svg" width="{BOX}" height="{BOX}"/>'
    for name in TITLEBUTTONS:
        for state in ('', '-hover', '-active', '-backdrop', '-backdrop-hover'):
            svg = empty
            base = f'titlebutton-{name}{state}'
            for variant in ('', '-dark'):
                for suffix, px in (('', BOX), ('@2', BOX * 2)):
                    out = os.path.join(dst, f'{base}{variant}{suffix}.png')
                    subprocess.run(['rsvg-convert', '-w', str(px), '-h', str(px), '-o', out],
                                   input=svg.encode(), check=True)


def extract(resource, prefix, dst):
    for child in resource.enumerate_children(prefix, 0):
        path = prefix + child
        if child.endswith('/'):
            extract(resource, path, os.path.join(dst, child))
        else:
            os.makedirs(dst, exist_ok=True)
            with open(os.path.join(dst, child), 'wb') as f:
                f.write(resource.lookup_data(path, 0).get_data())


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__ or 'usage: gen-gtk.py <WhiteSur-Dark dir> <output dir>')
    src, out = (os.path.expanduser(a) for a in sys.argv[1:])
    os.makedirs(out, exist_ok=True)
    for version, extra in (('gtk-3.0', GTK3), ('gtk-4.0', GTK4)):
        res = Gio.Resource.load(os.path.join(src, version, 'gtk.gresource'))
        vdir = os.path.join(out, version)
        shutil.rmtree(vdir, ignore_errors=True)
        extract(res, '/org/gnome/theme/', vdir)
        with open(os.path.join(vdir, 'gtk-dark.css'), encoding='utf-8') as f:
            css = f.read()
        css = window_radius(recolour(css)) + COMMON + extra + lights_css(version == 'gtk-4.0') + chromium_css()
        css = '/* Generated by theme/gen-gtk.py from WhiteSur-Dark: do not edit */\n' + css
        for name in ('gtk.css', 'gtk-dark.css'):
            with open(os.path.join(vdir, name), 'w', encoding='utf-8') as f:
                f.write(css)
        title_buttons(os.path.join(vdir, 'windows-assets'))
        chromium_buttons(os.path.join(vdir, 'windows-assets'))
        thumb = os.path.join(src, version, 'thumbnail.png')
        if os.path.exists(thumb):
            shutil.copy(thumb, vdir)
    # GTK 2 and the rest stay WhiteSur's: link them so apps asking for them still work.
    for extra in ('gtk-2.0', 'metacity-1', 'xfwm4'):
        link = os.path.join(out, extra)
        if os.path.lexists(link):
            os.remove(link) if os.path.islink(link) else shutil.rmtree(link)
        if os.path.exists(os.path.join(src, extra)):
            os.symlink(os.path.join(src, extra), link)
    with open(os.path.join(out, 'index.theme'), 'w') as f:
        f.write(f"""[Desktop Entry]
Type=X-GNOME-Metatheme
Name={NAME}
Comment=WhiteSur-Dark recoloured like fm (IRIX file manager, macOS Tahoe glass)
Encoding=UTF-8

[X-GNOME-Metatheme]
GtkTheme={NAME}
MetacityTheme=WhiteSur-Dark
IconTheme=WhiteSur-dark
CursorTheme=WhiteSur-cursors
ButtonLayout=close,minimize,maximize:menu
""")
    print(f'{NAME} -> {out}')


if __name__ == '__main__':
    main()
