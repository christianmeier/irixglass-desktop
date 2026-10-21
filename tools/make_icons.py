#!/usr/bin/env python3
"""Generate fm's IRIX "Indigo Magic"-style icon layers into ../icons.

Every SVG uses viewBox -10 -10 120 120: a 100x100 icon box plus room for the
selection glow. Carpets (the coloured bases) and glyphs are separate files,
so selecting an item only swaps the carpet.
"""
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "icons"

DEFS = """<defs>
  <filter id="glow" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="4"/></filter>
  <linearGradient id="carpet" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#8a6cff"/><stop offset="1" stop-color="#3fa0ff"/></linearGradient>
  <linearGradient id="carpetSel" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#5ef0ff"/><stop offset="1" stop-color="#2a8cff"/></linearGradient>
  <linearGradient id="folderFront" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#ffe18f"/><stop offset="1" stop-color="#e7b54c"/></linearGradient>
  <linearGradient id="folderBack" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#d9a347"/><stop offset="1" stop-color="#b17a22"/></linearGradient>
  <linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#2d6bd8"/><stop offset="1" stop-color="#9fd0ff"/></linearGradient>
</defs>"""

CARPET = "6,84 58,97 98,80 46,68"


def carpet(sel=False, strong=False):
    fill = "carpetSel" if sel else "carpet"
    op = .95 if sel else (.75 if strong else .30)
    s = f'<polygon points="{CARPET}" fill="url(#carpetSel)" opacity=".8" filter="url(#glow)"/>' if sel else ""
    return s + f'<polygon points="{CARPET}" fill="url(#{fill})" opacity="{op}" stroke="#1a1040" stroke-opacity=".5"/>'


FOLDER = (
    '<path d="M20,26 L34,18 L50,22 L54,28 L84,34 L84,70 L20,60 Z" fill="url(#folderBack)" stroke="#2a1b05" stroke-width="1.6" stroke-linejoin="round"/>'
    '<polygon points="27,31 78,41 78,63 27,55" fill="#fbf7ea" stroke="#2a1b05"/>'
    '<polygon points="14,42 80,53 88,82 22,70" fill="url(#folderFront)" stroke="#2a1b05" stroke-width="1.6" stroke-linejoin="round"/>')

PAGE = ('<path d="M28,12 L64,18 L76,30 L72,82 L24,75 Z" fill="#fdfdfd" stroke="#1d1d1d" stroke-width="1.5" stroke-linejoin="round"/>'
        '<polygon points="64,18 62,32 76,30" fill="#cfcfd4" stroke="#1d1d1d" stroke-linejoin="round"/>')

DOC = PAGE + "".join(
    f'<line x1="31" y1="{38+i*7}" x2="{68 - (i == 4)*14}" y2="{44.5+i*7}" stroke="#7a7f8c" stroke-width="2"/>' for i in range(5))

IMAGE = PAGE + ('<polygon points="31,34 70,41 68,70 29,64" fill="url(#sky)"/>'
                '<path d="M29,64 L40,52 L50,60 L58,50 L68,62 L68,70 Z" fill="#2f9e55"/>'
                '<circle cx="60" cy="46" r="4" fill="#ffe27a"/>')

EXE = ('<polygon points="50,24 74,36 50,48 26,36" fill="#c9b6ff" stroke="#1a1040" stroke-width="1.4"/>'
       '<polygon points="26,36 50,48 50,74 26,62" fill="#6b4fd8" stroke="#1a1040" stroke-width="1.4"/>'
       '<polygon points="50,48 74,36 74,62 50,74" fill="#3f2aa8" stroke="#1a1040" stroke-width="1.4"/>')


def write(name, body):
    OUT.mkdir(exist_ok=True)
    (OUT / f"{name}.svg").write_text(
        f'<svg xmlns="http://www.w3.org/2000/svg" width="240" height="240" viewBox="-10 -10 120 120">{DEFS}{body}</svg>\n')


# Nord colours for tags (docs/TAGS.md). Menus get a dot per colour: icons/tag-<hex>.svg
NORD_TAGS = ["BF616A", "D08770", "EBCB8B", "A3BE8C", "8FBCBB", "88C0D0", "81A1C1", "5E81AC", "B48EAD", "4C566A"]


def write_tag_dots():
    for hexc in NORD_TAGS:
        (OUT / f"tag-{hexc.lower()}.svg").write_text(
            '<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">'
            f'<circle cx="16" cy="16" r="11" fill="#{hexc}" stroke="#ECEFF4" stroke-opacity=".55" stroke-width="1.5"/></svg>\n')


if __name__ == "__main__":
    write_tag_dots()
    write("carpet", carpet())
    write("carpet-strong", carpet(strong=True))
    write("carpet-sel", carpet(sel=True))
    write("folder", FOLDER)
    write("doc", DOC)
    write("image", IMAGE)
    write("exe", EXE)
    # IRIX-style app icon (a folder on a lit carpet). Not installed: the menu entry and dock use the
    # icon theme's system-file-manager, which the user preferred.
    write("irixfm", carpet(strong=True) + FOLDER)
