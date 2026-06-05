#!/usr/bin/env python3
"""Embed the built PiLab Panel web app into a C source file.

Usage:
  python tools/embed_web_assets.py panel_web/dist/index.html main/web/panel_web_assets.c

The generated file is intentionally marked as generated. Edit files under
panel_web/src instead, then rebuild the web app and regenerate this C file.
"""
from __future__ import annotations
import sys
from pathlib import Path


def c_string_literal(data: str) -> str:
    parts = []
    for line in data.splitlines(True):
        escaped = line.replace('\\', '\\\\').replace('"', '\\"')
        # Escape question marks so generated C strings cannot accidentally form C trigraphs,
        # e.g. JavaScript nullish coalescing followed by a quote: ??''.
        # In C, \? prevents trigraph recognition and still emits '?'.
        escaped = escaped.replace('?', '\\?')
        escaped = escaped.replace('\n', '\\n')
        parts.append(f'"{escaped}"')
    return '\n'.join(parts)

def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])
    html = src.read_text(encoding='utf-8')
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(
        '// AUTO-GENERATED FILE. DO NOT EDIT BY HAND.\n'
        '// Source: panel_web/dist/index.html or panel_web/dist/panel.html\n'
        '// Edit panel_web/src and regenerate with tools/embed_web_assets.py.\n\n'
        '#include "panel_web_assets.h"\n\n'
        'const char panel_index_html[] =\n'
        + c_string_literal(html) + '\n;\n',
        encoding='utf-8'
    )
    print(f'Embedded {src} -> {dst} ({len(html)} bytes)')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
