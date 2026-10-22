#!/usr/bin/env python3
"""Mockups: IRIX fm (Indigo Magic file manager) themed like macOS Tahoe.

Writes irix-fm-tahoe-a.svg / -b.svg next to this file; render with
rsvg-convert. bg.jpg is the Golden Gate Dark wallpaper scaled to 1600x1000.
"""
import math
from pathlib import Path

HERE = Path(__file__).parent
W, H = 1600, 1000
FONT = "SF Pro Text, SF Pro, sans-serif"

DEFS = """
<defs>
  <filter id="blur" x="-5%" y="-5%" width="110%" height="110%"><feGaussianBlur stdDeviation="22"/></filter>
  <filter id="shadow" x="-20%" y="-20%" width="140%" height="140%"><feGaussianBlur stdDeviation="22"/></filter>
  <filter id="glow" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="4"/></filter>
  <linearGradient id="edge" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#fff" stop-opacity=".65"/>
    <stop offset=".25" stop-color="#fff" stop-opacity=".10"/>
    <stop offset="1" stop-color="#fff" stop-opacity=".16"/>
  </linearGradient>
  <linearGradient id="carpet" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#8a6cff"/><stop offset="1" stop-color="#3fa0ff"/>
  </linearGradient>
  <linearGradient id="carpetSel" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#5ef0ff"/><stop offset="1" stop-color="#2a8cff"/>
  </linearGradient>
  <linearGradient id="folderFront" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#ffe18f"/><stop offset="1" stop-color="#e7b54c"/>
  </linearGradient>
  <linearGradient id="folderBack" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#d9a347"/><stop offset="1" stop-color="#b17a22"/>
  </linearGradient>
  <linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#2d6bd8"/><stop offset="1" stop-color="#9fd0ff"/>
  </linearGradient>
  <linearGradient id="wheel" x1="0" y1="0" x2="1" y2="0">
    <stop offset="0" stop-color="#20222a"/><stop offset=".5" stop-color="#8a8f9c"/><stop offset="1" stop-color="#20222a"/>
  </linearGradient>
  <linearGradient id="wheelV" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#20222a"/><stop offset=".5" stop-color="#8a8f9c"/><stop offset="1" stop-color="#20222a"/>
  </linearGradient>
  <linearGradient id="appP" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#41b6ff"/><stop offset="1" stop-color="#1d4fd8"/></linearGradient>
  <linearGradient id="appW" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#6aa8ff"/><stop offset="1" stop-color="#2b3fbf"/></linearGradient>
  <linearGradient id="appG" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#4fe08a"/><stop offset="1" stop-color="#11894a"/></linearGradient>
  <linearGradient id="appV" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#ffb648"/><stop offset="1" stop-color="#e2531c"/></linearGradient>
</defs>
"""

_clip = 0


def glass(x, y, w, h, r, tint="#16181f", alpha=.45, edge=True):
    """Frosted panel: blurred wallpaper clipped to a rounded rect, a tint, and a top-lit edge."""
    global _clip
    _clip += 1
    cid = f"c{_clip}"
    s = f'<clipPath id="{cid}"><rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}"/></clipPath>'
    s += f'<g clip-path="url(#{cid})"><image href="bg.jpg" width="{W}" height="{H}" filter="url(#blur)"/></g>'
    s += f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{tint}" fill-opacity="{alpha}"/>'
    if edge:
        s += f'<rect x="{x+.5}" y="{y+.5}" width="{w-1}" height="{h-1}" rx="{r}" fill="none" stroke="url(#edge)" stroke-width="1.2"/>'
    return s


def shadow(x, y, w, h, r):
    return f'<rect x="{x}" y="{y+18}" width="{w}" height="{h}" rx="{r}" fill="#000" opacity=".55" filter="url(#shadow)"/>'


def text(x, y, s, size=13, fill="#fff", weight=400, anchor="start", op=1):
    return (f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" font-weight="{weight}" '
            f'fill="{fill}" fill-opacity="{op}" text-anchor="{anchor}">{s}</text>')


def lights(x, y):
    return "".join(f'<circle cx="{x+i*20}" cy="{y}" r="6.5" fill="{c}"/>'
                   for i, c in enumerate(["#ff5f57", "#febc2e", "#28c840"]))


def thumbwheel(x, y, w, h, vertical=False):
    """SGI-style thumbwheel: a ridged cylinder seen edge-on."""
    g = "wheelV" if vertical else "wheel"
    s = f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{min(w, h)/2}" fill="#0b0c10" fill-opacity=".6"/>'
    s += f'<rect x="{x+3}" y="{y+3}" width="{w-6}" height="{h-6}" rx="{min(w, h)/2-3}" fill="url(#{g})"/>'
    length = (h if vertical else w) - 10
    for k in range(-21, 22):
        th = k * math.pi / 44
        p = length / 2 * math.sin(th)
        op = .15 + .55 * math.cos(th)
        if vertical:
            yy = y + h / 2 + p
            s += f'<line x1="{x+6}" y1="{yy:.1f}" x2="{x+w-6}" y2="{yy:.1f}" stroke="#0d0e12" stroke-opacity="{op:.2f}" stroke-width="1.4"/>'
        else:
            xx = x + w / 2 + p
            s += f'<line x1="{xx:.1f}" y1="{y+6}" x2="{xx:.1f}" y2="{y+h-6}" stroke="#0d0e12" stroke-opacity="{op:.2f}" stroke-width="1.4"/>'
    return s


# --- IRIX-style icons, drawn in a 100x100 box, tilted like Indigo Magic icons ---

def carpet(sel, strong=False):
    fill = "carpetSel" if sel else "carpet"
    op = .95 if sel else (.75 if strong else .30)
    s = ""
    if sel:
        s += f'<polygon points="6,84 58,97 98,80 46,68" fill="url(#carpetSel)" opacity=".8" filter="url(#glow)"/>'
    s += f'<polygon points="6,84 58,97 98,80 46,68" fill="url(#{fill})" opacity="{op}" stroke="#1a1040" stroke-opacity=".5"/>'
    return s


def folder(sel=False):
    return carpet(sel) + (
        '<path d="M20,26 L34,18 L50,22 L54,28 L84,34 L84,70 L20,60 Z" fill="url(#folderBack)" stroke="#2a1b05" stroke-width="1.6" stroke-linejoin="round"/>'
        '<polygon points="27,31 78,41 78,63 27,55" fill="#fbf7ea" stroke="#2a1b05"/>'
        '<polygon points="14,42 80,53 88,82 22,70" fill="url(#folderFront)" stroke="#2a1b05" stroke-width="1.6" stroke-linejoin="round"/>')


def page(inner=""):
    s = ('<path d="M28,12 L64,18 L76,30 L72,82 L24,75 Z" fill="#fdfdfd" stroke="#1d1d1d" stroke-width="1.5" stroke-linejoin="round"/>'
         '<polygon points="64,18 62,32 76,30" fill="#cfcfd4" stroke="#1d1d1d" stroke-linejoin="round"/>')
    return s + inner


def doc(sel=False):
    lines = "".join(f'<line x1="31" y1="{38+i*7}" x2="{68 - (i == 4)*14}" y2="{44.5+i*7}" stroke="#7a7f8c" stroke-width="2"/>' for i in range(5))
    return carpet(sel) + page(lines)


def image(sel=False):
    pic = ('<polygon points="31,34 70,41 68,70 29,64" fill="url(#sky)"/>'
           '<path d="M29,64 L40,52 L50,60 L58,50 L68,62 L68,70 Z" fill="#2f9e55"/>'
           '<circle cx="60" cy="46" r="4" fill="#ffe27a"/>')
    return carpet(sel) + page(pic)


def exe(sel=False):
    return carpet(sel, strong=True) + (
        '<polygon points="50,24 74,36 50,48 26,36" fill="#c9b6ff" stroke="#1a1040" stroke-width="1.4"/>'
        '<polygon points="26,36 50,48 50,74 26,62" fill="#6b4fd8" stroke="#1a1040" stroke-width="1.4"/>'
        '<polygon points="50,48 74,36 74,62 50,74" fill="#3f2aa8" stroke="#1a1040" stroke-width="1.4"/>')


def app(grad, letter, sel=False):
    return carpet(sel) + (
        f'<rect x="22" y="12" width="58" height="58" rx="15" fill="url(#{grad})" stroke="#000" stroke-opacity=".35"/>'
        f'<rect x="23" y="13" width="56" height="56" rx="14" fill="none" stroke="#fff" stroke-opacity=".45"/>'
        f'<text x="51" y="52" font-family="{FONT}" font-size="30" font-weight="700" fill="#fff" text-anchor="middle">{letter}</text>')


ITEMS = [
    ("Applications", folder), ("Documents", folder), ("Pictures", folder),
    ("Music", folder), ("Projects", folder), ("Downloads", folder),
    ("PhotoCraft", lambda sel=False: app("appP", "P", sel)),
    ("WordCraft", lambda sel=False: app("appW", "W", sel)),
    ("GridCraft", lambda sel=False: app("appG", "G", sel)),
    ("VectorCraft", lambda sel=False: app("appV", "V", sel)),
    ("install.sh", exe), ("notes.txt", doc),
    ("golden-gate.png", image), ("SESSION-NOTES.md", doc),
]


def icon_grid(x0, y0, cw, ch, cols, scale, selected="Projects", magnify=None):
    s = ""
    for i, (name, fn) in enumerate(ITEMS):
        cx = x0 + (i % cols) * cw + cw / 2
        top = y0 + (i // cols) * ch
        sc = scale * (1.28 if name == magnify else 1)
        sel = name == selected
        gx, gy = cx - 50 * sc, top + (100 * scale - 100 * sc)
        s += f'<g transform="translate({gx:.1f},{gy:.1f}) scale({sc})">{fn(sel)}</g>'
        ly = top + 100 * scale + 20
        if sel:
            tw = len(name) * 7.4 + 14
            s += f'<rect x="{cx - tw/2:.1f}" y="{ly-15}" width="{tw:.1f}" height="21" rx="6" fill="#0a84ff"/>'
        s += text(cx, ly, name, 13, anchor="middle", weight=500 if sel else 400)
    return s


def topbar(menus):
    s = glass(0, 0, W, 30, 0, "#000", .18, edge=False)
    s += '<circle cx="22" cy="15" r="6" fill="none" stroke="#fff" stroke-width="1.6"/>'
    x = 44
    for i, m in enumerate(menus):
        s += text(x, 20, m, 13.5, weight=700 if i == 0 else 400)
        x += len(m) * 8 + 22
    s += text(W - 18, 20, "Tue 20 Oct  21:42", 13.5, anchor="end")
    return s


def dock():
    w, h = 470, 64
    x, y = (W - w) / 2, H - h - 8
    s = shadow(x, y, w, h, 20).replace('opacity=".55"', 'opacity=".35"')
    s += glass(x, y, w, h, 20, "#1b1d25", .30)
    cols = [("#9aa0aa", "#5d636e"), ("#6fd0ff", "#1c6fe0"), ("#ffb35c", "#e0452c"), ("#2a2d36", "#14161c"),
            ("#7c86ff", "#4b3fd0"), ("#41b6ff", "#1d4fd8"), ("#c9ccd4", "#8d929c")]
    for i, (a, b) in enumerate(cols):
        ix = x + 18 + i * 64
        s += (f'<linearGradient id="d{i}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{a}"/><stop offset="1" stop-color="{b}"/></linearGradient>'
              f'<rect x="{ix}" y="{y+9}" width="46" height="46" rx="12" fill="url(#d{i})"/>')
    return s


def sidebar_item(x, y, label, glyph="#0a84ff", sel=False):
    s = ""
    if sel:
        s += f'<rect x="{x-8}" y="{y-17}" width="196" height="26" rx="8" fill="#fff" fill-opacity=".16"/>'
    s += f'<rect x="{x}" y="{y-12}" width="15" height="13" rx="3" fill="none" stroke="{glyph}" stroke-width="1.6"/>'
    return s + text(x + 26, y, label, 13.5)


def svg(body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
            f'width="{W}" height="{H}" viewBox="0 0 {W} {H}">{DEFS}'
            f'<image href="bg.jpg" width="{W}" height="{H}"/>{body}</svg>')


def mockup_a():
    """Finder structure (sidebar, glass toolbar) with IRIX icons, path rail and thumbwheel."""
    x, y, w, h, r = 150, 70, 1300, 830, 26
    s = topbar(["fm", "File", "Edit", "View", "Go", "Window", "Help"])
    s += shadow(x, y, w, h, r) + glass(x, y, w, h, r, "#14161d", .62)
    # floating sidebar
    s += glass(x + 10, y + 10, 220, h - 20, 18, "#2a2d38", .35)
    s += lights(x + 34, y + 36)
    sx = x + 32
    s += text(sx - 6, y + 82, "Favorites", 11, "#fff", 600, op=.45)
    for i, (lab, c) in enumerate([("user", "#0a84ff"), ("Desktop", "#0a84ff"), ("Documents", "#0a84ff"),
                                  ("Downloads", "#0a84ff"), ("Applications", "#0a84ff")]):
        s += sidebar_item(sx, y + 110 + i * 30, lab, c, sel=(i == 0))
    s += text(sx - 6, y + 290, "Indigo Magic", 11, "#fff", 600, op=.45)
    for i, lab in enumerate(["Icon Catalog", "Desks Overview", "File QuickFind"]):
        s += sidebar_item(sx, y + 318 + i * 30, lab, "#bf5af2")
    s += text(sx - 6, y + 438, "Locations", 11, "#fff", 600, op=.45)
    for i, lab in enumerate(["cachyOS", "Network"]):
        s += sidebar_item(sx, y + 466 + i * 30, lab, "#98989d")
    # toolbar: back/forward, IRIX path rail, view switch, search
    tx, ty = x + 250, y + 18
    s += glass(tx, ty, 86, 36, 18, "#3a3d48", .40)
    s += text(tx + 24, ty + 24, "‹", 22, anchor="middle") + text(tx + 62, ty + 24, "›", 22, anchor="middle", op=.4)
    px = tx + 100
    s += glass(px, ty, 600, 36, 18, "#3a3d48", .40)
    # drop pocket: sunken well holding the current directory's icon (drag it out = drag the dir)
    s += f'<rect x="{px+6}" y="{ty+5}" width="34" height="26" rx="9" fill="#000" fill-opacity=".35"/>'
    s += f'<g transform="translate({px+9},{ty+4}) scale(.28)">{folder()}</g>'
    sep = '<tspan fill-opacity=".4">  ›  </tspan>'
    path = sep.join(f'<tspan fill-opacity=".7">{seg}</tspan>' for seg in ["/", "home", "user"])
    s += (f'<text xml:space="preserve" x="{px + 54}" y="{ty + 23}" font-family="{FONT}" font-size="13.5" fill="#fff">'
          f'{path}{sep}<tspan font-weight="600">Projects</tspan></text>')
    s += text(px + 584, ty + 23, "⌄", 14, anchor="end", op=.6)
    vx = px + 615
    s += glass(vx, ty, 128, 36, 18, "#3a3d48", .40)
    s += f'<rect x="{vx+4}" y="{ty+4}" width="40" height="28" rx="14" fill="#fff" fill-opacity=".18"/>'
    for i in range(3):
        gx = vx + 16 + i * 41
        s += f'<rect x="{gx}" y="{ty+11}" width="15" height="14" rx="3" fill="none" stroke="#fff" stroke-opacity=".85" stroke-width="1.5"/>'
    s += glass(vx + 142, ty, 176, 36, 18, "#3a3d48", .40)
    s += f'<circle cx="{vx+162}" cy="{ty+17}" r="6" fill="none" stroke="#fff" stroke-opacity=".6" stroke-width="1.6"/>'
    s += text(vx + 178, ty + 23, "Search", 13.5, op=.5)
    # icon field
    s += icon_grid(x + 262, y + 92, 168, 168, 6, .92)
    # status bar with the thumbwheel that scales the icons
    by = y + h - 40
    s += f'<line x1="{x+240}" y1="{by-12}" x2="{x+w-10}" y2="{by-12}" stroke="#fff" stroke-opacity=".08"/>'
    s += text(x + 262, by + 12, "14 items · 335 GB available", 12, op=.55)
    s += text(x + w - 230, by + 12, "Icon size", 12, anchor="end", op=.55)
    s += thumbwheel(x + w - 216, by - 2, 190, 22)
    s += dock()
    return svg(s)


def mockup_b():
    """Classic IRIX fm layout (drop pocket + path bar, side thumbwheel, big icon field) in Tahoe Clear glass."""
    x, y, w, h, r = 190, 64, 1220, 840, 28
    s = topbar(["fm", "Dir", "View", "Selected", "Help"])
    s += shadow(x, y, w, h, r) + glass(x, y, w, h, r, "#10131a", .28)
    s += lights(x + 26, y + 26)
    s += (f'<text xml:space="preserve" x="{x + w / 2}" y="{y + 31}" font-family="{FONT}" font-size="14" fill="#fff" text-anchor="middle">'
          '<tspan font-weight="600">user</tspan><tspan fill-opacity=".5">  — fm</tspan></text>')
    # directory bar: drop pocket, clickable path, history
    by = y + 56
    s += glass(x + 20, by, 54, 54, 16, "#000", .25)
    s += f'<rect x="{x+26}" y="{by+6}" width="42" height="42" rx="11" fill="#000" fill-opacity=".35"/>'
    s += f'<g transform="translate({x+25},{by+4}) scale(.44)">{folder()}</g>'
    s += glass(x + 86, by, w - 210, 54, 27, "#ffffff", .08)
    cx = x + 112
    for i, seg in enumerate(["/", "home", "user"]):
        last = i == 2
        tw = len(seg) * 9.2 + 26
        s += f'<rect x="{cx}" y="{by+11}" width="{tw}" height="32" rx="16" fill="#fff" fill-opacity="{.22 if last else .08}"/>'
        s += text(cx + tw / 2, by + 33, seg, 16, weight=600 if last else 400, anchor="middle")
        cx += tw + 8
        if not last:
            s += text(cx + 2, by + 33, "/", 16, op=.35)
            cx += 18
    s += glass(x + w - 112, by, 92, 54, 27, "#ffffff", .08)
    s += text(x + w - 66, by + 34, "◷ ⌄", 16, anchor="middle", op=.8)
    # vertical thumbwheel (icon size), left edge as in fm
    fy = by + 74
    fh = h - (fy - y) - 20
    s += text(x + 47, fy + 18, "Size", 11, anchor="middle", op=.55)
    s += thumbwheel(x + 32, fy + 30, 30, fh - 60, vertical=True)
    s += text(x + 47, fy + fh - 8, "＋", 13, anchor="middle", op=.55)
    # icon field: inset glass well
    s += glass(x + 86, fy, w - 106, fh, 20, "#000", .18)
    s += icon_grid(x + 100, fy + 18, 182, 196, 6, 1.0, magnify="Projects")
    s += text(x + w - 36, fy + fh - 16, "14 items", 12, anchor="end", op=.5)
    s += dock()
    return svg(s)


if __name__ == "__main__":
    (HERE / "irix-fm-tahoe-a.svg").write_text(mockup_a())
    (HERE / "irix-fm-tahoe-b.svg").write_text(mockup_b())
