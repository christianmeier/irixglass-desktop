#!/usr/bin/env python3
"""Remove the grey rounded squares behind dock (task manager) icons.

Reads WhiteSur's original tasks.svg and makes the background shapes of every
state (normal, focus, hover, minimized, progress, attention) fully transparent.
The small running-app dots are kept.

Usage: patch-tasks.py <tasks.svg in> <tasks.svg out>
"""
import re
import sys

# Background shape ids in WhiteSur(-Liquid) tasks.svgz; the dots are rect1153-3*, rect5, rect12.
BACKGROUNDS = r'(?:rect1123-5[-0-9]*|rect1133-2[-0-9]*|rect1151-9[-0-9]*|rect2|path3|path4|rect9|path10|path11)'


def hide(match):
    tag = match.group(0)
    if 'style="' in tag:
        def restyle(m):
            rest = re.sub(r'opacity:[0-9.]+;?', '', m.group(1)).strip(';')
            return 'style="' + (rest + ';' if rest else '') + 'opacity:0"'
        return re.sub(r'style="([^"]*)"', restyle, tag, count=1)
    closing = '/>' if tag.endswith('/>') else '>'
    return tag[:-len(closing)].rstrip() + ' style="opacity:0"' + closing


def main():
    src, dst = sys.argv[1], sys.argv[2]
    svg = open(src).read()
    svg, count = re.subn(r'<(?:rect|path)\b[^>]*\bid="' + BACKGROUNDS + r'"[^>]*>', hide, svg)
    open(dst, 'w').write(svg)
    print(f'hidden backgrounds: {count}')


if __name__ == '__main__':
    main()
