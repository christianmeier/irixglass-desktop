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
#   title buttons    fm's traffic lights (13.3 px, #ff5f57 #febc2e #28c840, glyphs on hover)
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

LIGHTS = {'close': ('#ff5f57', 'x'), 'minimize': ('#febc2e', '-'),
          'maximize': ('#28c840', '+'), 'restore': ('#28c840', '+')}
DIAMETER = 40 / 3       # fm TrafficLights.diameter
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
/* Traffic lights where fm has them: first centre 30 px from the window's left edge and 31 px from
   its top, 26 px apart; the header bar is fm's toolbar height. Offsets measured on screen. */
headerbar {{ min-height: 62px; }}
headerbar windowcontrols {{ border-spacing: 10px; padding: 0 0 0 14px; }}
headerbar windowcontrols button.close, headerbar windowcontrols button.minimize,
headerbar windowcontrols button.maximize {{ min-width: 16px; min-height: 16px; padding: 0; margin: 0; }}
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
/* Traffic lights where fm has them (see GTK4). GTK 3 spaces them with margins. */
headerbar, .titlebar:not(headerbar) {{ min-height: 62px; }}
headerbar > box.left {{ padding-left: 12px; }}
headerbar button.titlebutton.close, headerbar button.titlebutton.minimize,
headerbar button.titlebutton.maximize {{ margin: 0 2px; }}
"""


def glyph(kind, c, scale):
    s = DIAMETER * .22
    st = (f'stroke="#000000" stroke-opacity="0.6" stroke-width="{1.3}" '
          'stroke-linecap="round" fill="none"')
    if kind == 'x':
        d = f'M{c - s},{c - s} L{c + s},{c + s} M{c + s},{c - s} L{c - s},{c + s}'
    elif kind == '-':
        d = f'M{c - s * 1.2},{c} L{c + s * 1.2},{c}'
    else:
        d = f'M{c - s * 1.2},{c} L{c + s * 1.2},{c} M{c},{c - s * 1.2} L{c},{c + s * 1.2}'
    return f'<path d="{d}" {st}/>'


def light_svg(color, kind, opacity=1, g=False, darken=0):
    c, r = BOX / 2, DIAMETER / 2
    s = (f'<circle cx="{c}" cy="{c}" r="{r}" fill="{color}" fill-opacity="{opacity}" '
         f'stroke="#000000" stroke-opacity="0.25" stroke-width="0.5"/>')
    if darken:
        s += f'<circle cx="{c}" cy="{c}" r="{r}" fill="#000000" fill-opacity="{darken}"/>'
    if g:
        s += glyph(kind, c, 1)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{BOX}" height="{BOX}" '
            f'viewBox="0 0 {BOX} {BOX}">{s}</svg>\n')


def title_buttons(dst):
    """fm's traffic lights in place of WhiteSur's title button PNGs (1x and @2)."""
    os.makedirs(dst, exist_ok=True)
    for name, (_color, kind) in LIGHTS.items():
        states = {
            # grey all the time (the user's choice); glyphs on hover in the active window
            '': light_svg('#ffffff', kind, opacity=.22),
            '-hover': light_svg('#ffffff', kind, opacity=.22, g=True),
            '-active': light_svg('#ffffff', kind, opacity=.32, g=True),
            '-backdrop': light_svg('#ffffff', kind, opacity=.22),   # macOS grey, same as the decoration
            '-backdrop-hover': light_svg('#ffffff', kind, opacity=.22),   # stays grey under the mouse
        }
        for state, svg in states.items():
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
        css = window_radius(recolour(css)) + COMMON + extra
        css = '/* Generated by theme/gen-gtk.py from WhiteSur-Dark: do not edit */\n' + css
        for name in ('gtk.css', 'gtk-dark.css'):
            with open(os.path.join(vdir, name), 'w', encoding='utf-8') as f:
                f.write(css)
        title_buttons(os.path.join(vdir, 'windows-assets'))
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
