# Generates the "IrixGlass" Kvantum widget style, so Qt windows look like fm
# (fm/: qml/Main.qml, Pill.qml, EdgeRing.qml, GlassScrollBar.qml):
#   window/base   fm bodyTint rgba(20, 22, 29, .62) over KWin blur
#   buttons       fm pills: fully rounded, tint rgba(58, 61, 72, .40), edge ring brightest at the top
#   hover/pressed white .08 / .16, toggled white .18 (fm's selected view segment)
#   fields        pills too, 2 px #0a84ff focus ring
#   list rows     #0a84ff, radius 6; menus/tooltips fm's notice pill rgba(28, 30, 38, .82) + white .18 line
#   scroll bars   fm's GlassScrollBar: overlay thumb, no groove or arrows
#   accent        #0a84ff (selection, focus, checks, sliders, progress)
#
# Every SVG element is drawn here (no Inkscape file); only WhiteSur Liquid Dark's kvconfig
# is used as the base for the settings, with the overrides below.
# The window decoration to go with it is theme/gen-decoration.py (Aurorae "IrixGlass").
#
# Usage: gen-kvantum.py <WhiteSur Kvantum dir> <output dir>
import os
import sys

NAME = 'IrixGlass'
SOURCE = 'WhiteSurLiquidDark'

ACCENT = '#0a84ff'
PILL = '#3a3d48'            # fm pillTint rgb(58, 61, 72)
NOTICE = '#1c1e26'          # fm notice pill rgb(28, 30, 38)
GLASS = '#14161d'           # fm bodyTint rgb(20, 22, 29)

COLORS = {
    'window.color': '#14161D9E',            # fm bodyTint (alpha .62)
    'inactive.window.color': '#14161D9E',   # fm doesn't dim inactive windows
    'base.color': '#14161D9E',              # views sit on the same glass, like fm's icon field
    'inactive.base.color': '#14161D9E',
    'alt.base.color': '#1C1E26A6',          # list rows: fm's alternate row (white .035)
    'inactive.alt.base.color': '#1C1E26A6',
    'button.color': '#3A3D48',              # fm pillTint
    'light.color': '#4A4E5C',
    'mid.light.color': '#424654',
    'dark.color': '#121318',
    'mid.color': '#2A2D38',
    'highlight.color': '#0A84FF',
    'inactive.highlight.color': '#0A84FF',
    'text.color': '#FFFFFF',
    'inactive.text.color': '#E6E6EB',
    'window.text.color': '#FFFFFF',
    'inactive.window.text.color': '#E6E6EB',
    'button.text.color': '#FFFFFF',
    'disabled.text.color': '#8C8E96',
    'tooltip.base.color': '#1C1E26',        # fm's notice pill rgba(28, 30, 38, .82)
    'tooltip.text.color': '#FFFFFF',
    'highlight.text.color': '#FFFFFF',
    'inactive.highlight.text.color': '#FFFFFF',
    'link.color': '#0A84FF',
    'link.visited.color': '#BF5AF2',        # fm's Indigo Magic purple
    'progress.indicator.text.color': '#FFFFFF',
}

# Settings on top of WhiteSur's kvconfig: {section: {key: value}}; None removes the key.
PILLS = '32'    # frame.expansion: controls up to 32 px tall are fully rounded, taller ones get radius 16
SETTINGS = {
    '%General': {
        'comment': 'fm (IRIX file manager) glass: pills, edge rings and #0a84ff, drawn by gen-kvantum.py',
        'author': 'IrixGlass (gen-kvantum.py), settings based on WhiteSur by Vince Liuice',
        'scroll_width': '12', 'scroll_min_extent': '36',
        'transient_scrollbar': 'true', 'transient_groove': 'false',   # fm's overlay scroll bars
        'slider_width': '4', 'slider_handle_width': '18', 'slider_handle_length': '18',
        'progressbar_thickness': '6', 'check_size': '16',
        'menu_shadow_depth': '0',   # Kvantum's menu shadows hide the rounded frame on Wayland
        'tooltip_shadow_depth': '0',
        'menu_blur_radius': '12', 'tooltip_blur_radius': '8',
        'layout_spacing': '6', 'layout_margin': '8',
        'toolbar_interior_spacing': '4', 'toolbar_icon_size': '16',
        'reduce_menu_opacity': '0', 'reduce_window_opacity': '0',
        'tree_branch_line': 'false',
        'menubar_mouse_tracking': 'true', 'spread_menuitems': 'false',
        'combo_focus_rect': 'false', 'vertical_spin_indicators': 'true',
    },
    'PanelButtonCommand': {
        'frame.top': '4', 'frame.bottom': '4', 'frame.left': '4', 'frame.right': '4',
        'frame.expansion': PILLS,
        'text.normal.color': '#ffffff', 'text.focus.color': '#ffffff',
        'text.press.color': '#ffffff', 'text.toggle.color': '#ffffff',
        'text.margin.left': '8', 'text.margin.right': '8',
        'min_width': '28', 'min_height': '26',
    },
    'Focus': {'frame.top': '2', 'frame.bottom': '2', 'frame.left': '2', 'frame.right': '2'},
    'ToolbarButton': {
        'frame.top': '4', 'frame.bottom': '4', 'frame.left': '4', 'frame.right': '4',
        'frame.expansion': PILLS,
        'text.normal.color': '#ffffff', 'text.focus.color': '#ffffff',
        'text.normal.inactive.color': '#c8c9ce', 'text.toggle.inactive.color': '#c8c9ce',
    },
    'ToolbarLineEdit': {'frame.expansion': PILLS},
    'Dock': {'interior': 'false', 'frame': 'false', 'text.normal.color': '#ffffff'},
    'DockTitle': {'text.normal.color': '#c8c9ce'},
    'IndicatorSpinBox': {'frame': 'false', 'interior': 'false', 'indicator.size': '10',
                         'text.normal.color': '#ffffff'},
    'RadioButton': {'text.normal.color': '#ffffff'},
    'CheckBox': {'text.normal.color': '#ffffff'},
    'GenericFrame': {'frame.expansion': '0',
                     'frame.top': '8', 'frame.bottom': '8', 'frame.left': '8', 'frame.right': '8'},
    'LineEdit': {'frame.top': '4', 'frame.bottom': '4', 'frame.left': '4', 'frame.right': '4',
                 'frame.expansion': PILLS, 'text.margin.left': '8', 'text.margin.right': '8'},
    'IndicatorArrow': {'indicator.size': '10'},
    'ToolboxTab': {'text.normal.color': '#ffffff'},
    'Tab': {'text.normal.color': '#d8d9de', 'frame.expansion': PILLS, 'text.toggle.inactive.color': '#ffffff',
            'frame.top': '4', 'frame.bottom': '4', 'frame.left': '4', 'frame.right': '4',
            'text.margin.left': '10', 'text.margin.right': '10', 'indicator.size': '14'},
    'TabFrame': {'frame.expansion': '0',
                 'frame.top': '12', 'frame.bottom': '12', 'frame.left': '12', 'frame.right': '12'},
    'TreeExpander': {'indicator.size': '10'},
    'HeaderSection': {'frame.expansion': '0', 'text.normal.color': '#b4b6bd',
                      'frame.top': '0', 'frame.bottom': '1', 'frame.left': '0', 'frame.right': '0',
                      'text.margin.left': '6', 'text.margin.right': '6'},
    'Toolbar': {'interior': 'false', 'frame': 'false', 'text.normal.color': '#ffffff',
                'text.normal.inactive.color': '#c8c9ce'},
    'Slider': {'frame.expansion': PILLS,
               'frame.top': '2', 'frame.bottom': '2', 'frame.left': '2', 'frame.right': '2'},
    'Progressbar': {'frame.expansion': PILLS, 'text.normal.color': '#ffffff',
                    'frame.top': '3', 'frame.bottom': '3', 'frame.left': '3', 'frame.right': '3'},
    'ProgressbarContents': {'frame.expansion': PILLS,
                            'frame.top': '3', 'frame.bottom': '3', 'frame.left': '3', 'frame.right': '3'},
    'ItemView': {'frame.expansion': '0', 'text.normal.color': '#ffffff',
                 'frame.top': '6', 'frame.bottom': '6', 'frame.left': '6', 'frame.right': '6'},
    'Scrollbar': {'indicator.size': '0'},
    'ScrollbarSlider': {'frame.expansion': '0', 'indicator.size': '0',
                        'frame.top': '6', 'frame.bottom': '6', 'frame.left': '6', 'frame.right': '6'},
    'MenuBarItem': {'frame.expansion': '0', 'text.normal.color': '#ffffff',
                    'frame.top': '6', 'frame.bottom': '6', 'frame.left': '6', 'frame.right': '6',
                    'text.margin.top': '0', 'text.margin.bottom': '0'},
    'TitleBar': {'text.normal.color': '#c8c9ce', 'text.focus.color': '#ffffff', 'text.italic': 'false'},
    'Menu': {'frame.expansion': '0', 'text.normal.color': '#ffffff',
             'frame.top': '10', 'frame.bottom': '10', 'frame.left': '10', 'frame.right': '10'},
    'MenuItem': {'frame.expansion': '0', 'text.normal.color': '#ffffff', 'indicator.element': 'arrow',
                 'indicator.size': '10',
                 'frame.top': '6', 'frame.bottom': '6', 'frame.left': '6', 'frame.right': '6',
                 'text.margin.top': '0', 'text.margin.bottom': '0'},
    'ComboBox': {'indicator.size': '10'},
    'GroupBox': {'frame.expansion': '0', 'text.normal.color': '#ffffff', 'interior': 'false',
                 'interior.x.patternsize': None, 'interior.y.patternsize': None,
                 'frame.top': '10', 'frame.bottom': '10', 'frame.left': '10', 'frame.right': '10'},
    'ToolTip': {'frame.expansion': '0',
                'frame.top': '8', 'frame.bottom': '8', 'frame.left': '8', 'frame.right': '8',
                'text.margin.top': '2', 'text.margin.bottom': '2',
                'text.margin.left': '4', 'text.margin.right': '4'},
    'Window': {'frame': 'false'},
}

# ---------------------------------------------------------------- SVG drawing

C = 10          # size of the stretchable centre pieces
EXPAND = 14     # design size of the corners of expanded (fully rounded) frames, half a 28 px pill
STATES = ('normal', 'focused', 'pressed', 'toggled', 'disabled')


def f(v):
    t = ('%.3f' % v).rstrip('0').rstrip('.')
    return t[1:] if t.startswith('0.') else '-' + t[2:] if t.startswith('-0.') else t


class Svg:
    def __init__(self):
        self.items = []
        self.defs = {}
        self.x = 0
        self.y = 0
        self.row = 0

    def grad(self, stops, x2=0, y2=1, radial=False):
        """A gradient in bounding-box units; stops are (offset, colour, alpha)."""
        key = (tuple(stops), x2, y2, radial)
        if key not in self.defs:
            gid = 'g%d' % len(self.defs)
            st = ''.join('<stop offset="%s" stop-color="%s" stop-opacity="%s"/>' % (f(o), c, f(a))
                         for o, c, a in stops)
            if radial:
                cx, cy = x2, y2   # the gradient's centre corner
                self.defs[key] = (gid, '<radialGradient id="%s" cx="%s" cy="%s" r="1">%s</radialGradient>'
                                  % (gid, f(cx), f(cy), st))
            else:
                self.defs[key] = (gid, '<linearGradient id="%s" x1="0" y1="0" x2="%s" y2="%s">%s</linearGradient>'
                                  % (gid, f(x2), f(y2), st))
        return 'url(#%s)' % self.defs[key][0]

    def add(self, eid, w, h, body):
        """Places an element of w x h at its own spot; a clear rect fixes its bounds."""
        if self.x + w > 600:
            self.x = 0
            self.y += self.row + 4
            self.row = 0
        self.items.append('<g id="%s" transform="translate(%s,%s)"><rect width="%s" height="%s" opacity="0"/>%s</g>'
                          % (eid, f(self.x), f(self.y), f(w), f(h), body))
        self.x += w + 4
        self.row = max(self.row, h)

    def text(self):
        defs = ''.join(d for _, d in self.defs.values())
        return ('<svg xmlns="http://www.w3.org/2000/svg" width="600" height="%d">\n<defs>%s</defs>\n%s\n</svg>\n'
                % (self.y + self.row + 4, defs, '\n'.join(self.items)))


def paint(fill, alpha):
    return ' fill="%s"' % fill + ('' if alpha == 1 else ' fill-opacity="%s"' % f(alpha))


def rect(x, y, w, h, fill, alpha=1.0, rx=0):
    pos = ''.join(' %s="%s"' % (k, f(v)) for k, v in (('x', x), ('y', y), ('rx', rx)) if v)
    return '<rect%s width="%s" height="%s"%s/>' % (pos, f(w), f(h), paint(fill, alpha))


def path(d, fill, alpha=1.0):
    return '<path d="%s"%s/>' % (d, paint(fill, alpha))


def fills(shape, layers):
    """shape(fill, alpha) for every (colour, alpha) layer."""
    return ''.join(shape(c, a) for c, a in layers)


class Look:
    """How a frame looks: fill layers, a ring and an outer margin.
    ring: ('edge', top, mid, bottom, width) for fm's EdgeRing (white), or
          ('solid', colour, alpha, width)."""
    def __init__(self, layers=(), ring=None, margin=0):
        self.layers = list(layers)
        self.ring = ring
        self.margin = margin


def ring_paint(svg, look, part):
    """Fill for the ring piece of a part (top / topleft / left ...)."""
    r = look.ring
    if r[0] == 'solid':
        return r[1], r[2]
    _, top, mid, bot, _ = r
    if part == 'top':
        return '#ffffff', top
    if part == 'bottom':
        return '#ffffff', bot
    if part in ('left', 'right'):
        return '#ffffff', mid
    if part.startswith('top'):
        return svg.grad([(0, '#ffffff', top), (.6, '#ffffff', mid * 1.4), (1, '#ffffff', mid)]), 1
    return svg.grad([(0, '#ffffff', mid), (.4, '#ffffff', mid), (1, '#ffffff', bot)]), 1


def frame(svg, name, F, look, interior=True, prefix=''):
    """Kvantum frame pieces name-top ... name-bottomright (F x F corners, radius F - margin)
    and, with interior, the centre element name."""
    m = look.margin
    R = F - m                       # corner radius
    L = look.ring[-1] if look.ring else 0
    corners = {'topleft': (F, F, -1, -1), 'topright': (0, F, 1, -1),
               'bottomleft': (F, 0, -1, 1), 'bottomright': (0, 0, 1, 1)}
    for part, (cx, cy, dx, dy) in corners.items():
        s = 1 if dx * dy > 0 else 0
        hx, vy = cx + dx * R, cy + dy * R
        body = fills(lambda c, a: path('M%s %sL%s %sA%s %s 0 0 %d %s %sZ' % (
            f(cx), f(cy), f(hx), f(cy), f(R), f(R), s, f(cx), f(vy)), c, a), look.layers)
        if L:
            ri = R - L
            d = 'M%s %sA%s %s 0 0 %d %s %sL%s %sA%s %s 0 0 %d %s %sZ' % (
                f(hx), f(cy), f(R), f(R), s, f(cx), f(vy),
                f(cx), f(cy + dy * ri), f(ri), f(ri), 1 - s, f(cx + dx * ri), f(cy))
            c, a = ring_paint(svg, look, part)
            body += path(d, c, a)
        svg.add(prefix + name + '-' + part, F, F, body)
    edges = {'top': (C, F), 'bottom': (C, F), 'left': (F, C), 'right': (F, C)}
    for part, (w, h) in edges.items():
        if part in ('top', 'bottom'):
            y = m if part == 'top' else 0
            body = fills(lambda c, a: rect(0, y, w, F - m, c, a), look.layers)
            if L:
                c, a = ring_paint(svg, look, part)
                body += rect(0, m if part == 'top' else F - m - L, w, L, c, a)
        else:
            x = m if part == 'left' else 0
            body = fills(lambda c, a: rect(x, 0, F - m, h, c, a), look.layers)
            if L:
                c, a = ring_paint(svg, look, part)
                body += rect(m if part == 'left' else F - m - L, 0, L, h, c, a)
        svg.add(prefix + name + '-' + part, w, h, body)
    if interior:
        svg.add(prefix + name, C, C, fills(lambda c, a: rect(0, 0, C, C, c, a), look.layers))


def framed(svg, base, F, looks, interior=True, expand=True):
    """base-<state> frames for every state in looks, plus 'expand-' copies drawn at
    EXPAND size for fully rounded (expanded) frames."""
    for state, look in looks.items():
        name = base + '-' + state if state else base
        frame(svg, name, F, look, interior)
        if expand:
            frame(svg, name, EXPAND, look, interior, prefix='expand-')


def edge(top=.65, mid=.10, bot=.16, width=1.2, k=1.0):
    """fm's EdgeRing (Pill.qml uses bottom .16), alphas scaled by k."""
    return ('edge', top * k, mid * k, bot * k, width)


def pill_looks(base_alpha=.40):
    tint = (PILL, base_alpha)
    return {
        'normal': Look([tint], edge()),
        'focused': Look([tint, ('#ffffff', .08)], edge(k=1.15)),
        'pressed': Look([tint, ('#ffffff', .16)], edge(k=1.15)),
        'toggled': Look([tint, ('#ffffff', .18)], edge(k=1.2)),
        'disabled': Look([(PILL, base_alpha * .6)], edge(k=.45)),
    }


def stroke(d, colour, alpha, width):
    return ('<path d="%s" fill="none" stroke="%s" stroke-opacity="%s" stroke-width="%s" '
            'stroke-linecap="round" stroke-linejoin="round"/>' % (d, colour, f(alpha), f(width)))


INDICATOR = {'normal': .85, 'focused': 1, 'pressed': 1, 'toggled': 1, 'disabled': .35}
CHEVRONS = {   # in a 10 x 10 box, fm's thin chevrons
    'down': 'M2.5 3.8L5 6.3L7.5 3.8', 'up': 'M2.5 6.2L5 3.7L7.5 6.2',
    'left': 'M6.2 2.5L3.7 5L6.2 7.5', 'right': 'M3.8 2.5L6.3 5L3.8 7.5',
    'plus': 'M5 2.2V7.8M2.2 5H7.8', 'minus': 'M2.2 5H7.8',
}


def indicators(svg):
    for state, a in INDICATOR.items():
        for name, d in CHEVRONS.items():
            for family in ('arrow', 'tarrow', 'flat-arrow', 'spin'):
                if family == 'spin' and name in ('left', 'right'):
                    continue
                if family == 'flat-arrow' and name in ('plus', 'minus'):
                    continue
                svg.add('%s-%s-%s' % (family, name, state), 10, 10, stroke(d, '#ffffff', a, 1.3))
        # combo boxes: macOS up/down chevrons
        svg.add('comboarrow-' + state, 10, 10, stroke('M3 4L5 2.2L7 4M3 6L5 7.8L7 6', '#ffffff', a, 1.2))
        svg.add('carrow-' + state, 10, 10, stroke(CHEVRONS['down'], '#ffffff', a, 1.3))
        # tree expanders: Finder's disclosure chevrons
        svg.add('tree-plus-' + state, 10, 10, stroke(CHEVRONS['right'], '#ffffff', a * .8, 1.3))
        svg.add('tree-minus-' + state, 10, 10, stroke(CHEVRONS['down'], '#ffffff', a * .8, 1.3))
        # tab close button: a cross, with a white .16 disc on hover
        disc = '<circle cx="5" cy="5" r="5" fill="#ffffff" fill-opacity="%s"/>' % (
            {'focused': .16, 'pressed': .26}.get(state, 0))
        svg.add('tab-close-' + state, 10, 10, disc + stroke('M3.2 3.2L6.8 6.8M6.8 3.2L3.2 6.8', '#ffffff', a, 1.2))
    for state in ('normal', 'disabled'):
        svg.add('arrow-menu-' + state, 10, 10, stroke(CHEVRONS['right'], '#ffffff', INDICATOR[state], 1.3))
    # MDI title buttons: fm's traffic lights
    for state, a in (('normal', 1), ('disabled', .35)):
        for name, col in (('close', '#ff5f57'), ('minimize', '#febc2e'), ('maximize', '#28c840'),
                          ('restore', '#28c840'), ('shade', '#febc2e')):
            svg.add('mdi-%s-%s' % (name, state), 12, 12,
                    '<circle cx="6" cy="6" r="5.5" fill="%s" fill-opacity="%s"/>' % (col, f(a)))


def checks(svg):
    """Check boxes and radio buttons, macOS dark: glass box with fm's ring, #0a84ff when on."""
    S = 16
    tick = 'M4.4 8.3L7 10.8L11.6 5.4'
    dash = 'M4.8 8H11.2'
    box = lambda c, a: rect(.5, .5, 15, 15, c, a, 4.5)
    disc = lambda c, a: '<circle cx="8" cy="8" r="7.5" fill="%s" fill-opacity="%s"/>' % (c, f(a))
    for state in ('normal', 'focused', 'pressed', 'disabled'):
        k = .4 if state == 'disabled' else 1
        lift = {'focused': .08, 'pressed': .16}.get(state, 0)
        for kind, shape, inner in (('checkbox', box, lambda c, a: rect(1.5, 1.5, 13, 13, c, a, 3.5)),
                                   ('radio', disc, lambda c, a: '<circle cx="8" cy="8" r="6.5" fill="%s" fill-opacity="%s"/>' % (c, f(a)))):
            # off: ring (white gradient) around a pill-tinted face
            off = (shape(svg.grad([(0, '#ffffff', .55 * k), (.5, '#ffffff', .22 * k), (1, '#ffffff', .3 * k)]), 1)
                   + inner('#24262f', 1 if k == 1 else .6) + inner('#ffffff', .06 + lift))
            on_face = shape(ACCENT, k) + inner('#ffffff', lift * .6) + \
                shape(svg.grad([(0, '#ffffff', .35 * k), (.5, '#ffffff', 0), (1, '#ffffff', 0)]), 1)
            if kind == 'checkbox':
                svg.add('checkbox-' + state, S, S, off)
                svg.add('checkbox-checked-' + state, S, S, on_face + stroke(tick, '#ffffff', k, 1.8))
                svg.add('checkbox-tristate-' + state, S, S, on_face + stroke(dash, '#ffffff', k, 1.8))
            else:
                svg.add('radio-' + state, S, S, off)
                svg.add('radio-checked-' + state, S, S, on_face +
                        '<circle cx="8" cy="8" r="3" fill="#ffffff" fill-opacity="%s"/>' % f(k))
        # menus and item views: a bare check mark, like macOS menus
        for pre in ('menu', 'item'):
            svg.add('%s-checkbox-%s' % (pre, state), S, S, '')
            svg.add('%s-checkbox-checked-%s' % (pre, state), S, S, stroke(tick, '#ffffff', k, 1.6))
            svg.add('%s-checkbox-tristate-%s' % (pre, state), S, S, stroke(dash, '#ffffff', k, 1.6))
            svg.add('%s-radio-%s' % (pre, state), S, S, '')
            svg.add('%s-radio-checked-%s' % (pre, state), S, S,
                    '<circle cx="8" cy="8" r="3" fill="#ffffff" fill-opacity="%s"/>' % f(k))


def build():
    svg = Svg()
    pills = pill_looks()

    # push buttons, also combo boxes ([ComboBox] uses 'button'); the default button is macOS blue
    framed(svg, 'button', 4, pills)
    framed(svg, 'button', 4, {'default': Look([(ACCENT, .92)], edge(.55, .12, .18))})

    # tool buttons: bare until hovered, then fm's white .08 / .16, white .18 when on
    framed(svg, 'tbutton', 4, {
        'normal': Look([]),
        'focused': Look([('#ffffff', .08)], edge(k=.6)),
        'pressed': Look([('#ffffff', .16)], edge(k=.7)),
        'toggled': Look([('#ffffff', .18)], edge(k=.8)),
        'disabled': Look([]),
    })

    # text fields: pills; focus = 2 px #0a84ff .85 ring (fm's path field)
    field = {
        'normal': Look([(PILL, .40)], edge()),
        'focused': Look([(PILL, .48)], ('solid', ACCENT, .85, 2)),
        'disabled': Look([(PILL, .24)], edge(k=.45)),
    }
    framed(svg, 'lineedit', 4, field)
    framed(svg, 'tlineedit', 4, field)

    # tabs: a pill segmented control; the current tab is fm's white .18 segment
    framed(svg, 'tab', 4, {
        'normal': Look([(PILL, .40)], edge()),
        'focused': Look([(PILL, .40), ('#ffffff', .08)], edge()),
        'toggled': Look([(PILL, .40), ('#ffffff', .18)], edge(k=1.2)),
        'disabled': Look([(PILL, .24)], edge(k=.45)),
    })
    frame(svg, 'tframe-normal', 12, Look([('#ffffff', .03)], ('solid', '#ffffff', .10, 1)))

    # group boxes and plain frames: faint glass panes like fm's sidebar
    # (Kvantum doesn't draw a group box's interior, so a fill would only tint the frame band)
    frame(svg, 'group-normal', 10, Look([], edge(.30, .09, .12, 1)), interior=False)
    frame(svg, 'common-normal', 8, Look([], ('solid', '#ffffff', .10, 1)))

    # list rows: #0a84ff radius 6 (fm's list view); hover white .07; unfocused selection white .16
    frame(svg, 'itemview-focused', 6, Look([('#ffffff', .07)]))
    frame(svg, 'itemview-pressed', 6, Look([(ACCENT, 1)]))
    frame(svg, 'itemview-toggled', 6, Look([(ACCENT, 1)]))
    frame(svg, 'itemview-toggled-inactive', 6, Look([('#ffffff', .16)]))
    frame(svg, 'itemview-pressed-inactive', 6, Look([('#ffffff', .16)]))

    # column headers: no buttons, a hairline under them, like fm's list columns
    for state, a in (('normal', 0), ('focused', .05), ('pressed', .10), ('toggled', 0)):
        layers = [('#ffffff', a)] if a else []
        name = 'header-' + state
        svg.add(name, C, C, fills(lambda c, al: rect(0, 0, C, C, c, al), layers))
        svg.add(name + '-bottom', C, 1, rect(0, 0, C, 1, '#ffffff', .12))
    svg.add('header-separator', 2, 20, rect(.5, 4, 1, 12, '#ffffff', .14))

    # menus and tooltips: fm's notice pill rgba(28, 30, 38, .82) with a white .18 line
    notice = Look([(NOTICE, .82)], ('solid', '#ffffff', .18, 1))
    frame(svg, 'menu-normal', 10, notice)
    frame(svg, 'tooltip-normal', 8, notice)
    frame(svg, 'menuitem-normal', 6, Look([(ACCENT, 1)]))
    frame(svg, 'menuitem-focused', 6, Look([(ACCENT, 1)]))
    svg.add('menuitem-separator', 20, 3, rect(0, 1, 20, 1, '#ffffff', .14))
    svg.add('menuitem-tearoff-focused', 20, 3, rect(0, 1, 20, 1, '#ffffff', .3))

    # menu bar items: white .16, rounded like fm's sidebar rows
    for state in ('focused', 'pressed', 'toggled'):
        frame(svg, 'menubaritem-' + state, 6, Look([('#ffffff', .16)]))

    # scroll bars: fm's GlassScrollBar thumb (white .32 / .45 / .55), 2 px clear margin
    for state, a in (('normal', .32), ('focused', .45), ('pressed', .55), ('disabled', .15)):
        frame(svg, 'scrollbarslider-' + state, 6, Look([('#ffffff', a)], margin=2), interior=True)

    # sliders: white .16 groove, #0a84ff filled part, white knob
    framed(svg, 'slider', 2, {
        'normal': Look([('#ffffff', .16)]),
        'toggled': Look([(ACCENT, 1)]),
        'disabled': Look([('#ffffff', .08)]),
    })
    framed(svg, 'slider', 2, {'toggled-disabled': Look([(ACCENT, .4)])}, expand=True)
    for state, a in (('normal', 1), ('focused', 1), ('pressed', .92), ('disabled', .45)):
        knob = ('<circle cx="9" cy="9.6" r="8" fill="#000000" fill-opacity="%s"/>' % f(.25 * a)
                + '<circle cx="9" cy="9" r="7.5" fill="#f4f4f6" fill-opacity="%s"/>' % f(a)
                + '<circle cx="9" cy="9" r="7" fill="none" stroke="#000000" stroke-opacity=".08"/>')
        svg.add('slidercursor-' + state, 18, 18, knob)
        svg.add('slidercursor-tickless-' + state, 18, 18, knob)
    svg.add('slider-tick-normal', 2, 6, rect(.5, 0, 1, 6, '#ffffff', .3))

    # progress bars: white .12 groove, #0a84ff bar with a lit top
    framed(svg, 'progress', 3, {'normal': Look([('#ffffff', .12)])})
    framed(svg, 'progress-pattern', 3, {
        'normal': Look([(ACCENT, 1)], edge(.45, .08, .10, 1)),
        'disabled': Look([(ACCENT, .4)]),
    })

    # focus: fm's 2 px #0a84ff .85 ring
    frame(svg, 'focus', 2, Look([], ('solid', ACCENT, .85, 2)), interior=False)
    frame(svg, 'focus', EXPAND, Look([], ('solid', ACCENT, .85, 2)), interior=False, prefix='expand-')

    # separators, grips
    svg.add('tbutton-separator', 8, 20, rect(3.5, 3, 1, 14, '#ffffff', .18))
    svg.add('button-default-indicator', 4, 4, '')
    svg.add('toolbar-handle', 6, 20, ''.join('<circle cx="3" cy="%s" r="1" fill="#ffffff" fill-opacity=".3"/>' % y
                                            for y in (6, 10, 14)))
    svg.add('toolbar-separator', 8, 20, rect(3.5, 3, 1, 14, '#ffffff', .18))
    for state, a in (('normal', .25), ('focused', .5), ('pressed', .6)):
        svg.add('splitter-grip-' + state, 20, 4, ''.join(
            '<circle cx="%s" cy="2" r="1" fill="#ffffff" fill-opacity="%s"/>' % (x, f(a)) for x in (6, 10, 14)))
    for state, a in (('normal', .3), ('focused', .5)):
        svg.add('resize-grip-' + state, 12, 12, stroke('M10.5 4.5L4.5 10.5M10.5 8L8 10.5', '#ffffff', a, 1.2))

    # windows: fm's glass tint (KWin blurs behind it)
    for name in ('window-normal', 'dialog-normal', 'window-normal-inactive', 'dialog-normal-inactive'):
        svg.add(name, C, C, rect(0, 0, C, C, GLASS, .62))
    svg.add('titlebar-normal', C, C, rect(0, 0, C, C, '#ffffff', .04))
    svg.add('titlebar-focused', C, C, rect(0, 0, C, C, '#ffffff', .07))

    # dials: a glass ring with a blue handle
    svg.add('dial', 40, 40, '<circle cx="20" cy="20" r="19" fill="%s" fill-opacity=".40"/>' % PILL
            + '<circle cx="20" cy="20" r="18.4" fill="none" stroke="#ffffff" stroke-opacity=".3" stroke-width="1.2"/>')
    svg.add('dial-handle', 8, 8, '<circle cx="4" cy="4" r="3.5" fill="%s"/>' % ACCENT)
    svg.add('dial-notches', 40, 40, '<circle cx="20" cy="20" r="19.5" fill="none" stroke="#ffffff" '
            'stroke-opacity=".25" stroke-dasharray="1 5"/>')

    indicators(svg)
    checks(svg)
    return svg.text()


def kvconfig(text):
    out = []
    section = None
    pending = {}

    def flush():
        for k, v in pending.items():
            if v is not None:
                out.append(f'{k}={v}')

    for line in text.splitlines():
        s = line.strip()
        if s.startswith('['):
            blank = bool(out) and out[-1] == ''
            if blank:
                out.pop()
            flush()
            if blank:
                out.append('')
            section = s[1:-1]
            pending = dict(SETTINGS.get(section, {}))
            if section == 'GeneralColors':
                pending.update(COLORS)
        elif '=' in s and section is not None:
            key = s.split('=', 1)[0].strip()
            if key in pending:
                v = pending.pop(key)
                if v is None:
                    continue
                line = f'{key}={v}'
            elif section != 'GeneralColors':
                # WhiteSur's text greys -> fm's white
                line = line.replace('#dedede', '#ffffff').replace('#dfdfdf', '#ffffff')
        out.append(line)
    flush()
    for section, keys in SETTINGS.items():   # sections WhiteSur doesn't have
        if f'[{section}]' not in text:
            out += ['', f'[{section}]'] + [f'{k}={v}' for k, v in keys.items() if v is not None]
    return '\n'.join(out).rstrip('\n') + '\n'


def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    with open(os.path.join(src, f'{SOURCE}.kvconfig')) as fh:
        conf = kvconfig(fh.read())
    with open(os.path.join(dst, f'{NAME}.kvconfig'), 'w') as fh:
        fh.write(conf)
    with open(os.path.join(dst, f'{NAME}.svg'), 'w') as fh:
        fh.write(build())


if __name__ == '__main__':
    main()
