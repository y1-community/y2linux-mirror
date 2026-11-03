#!/usr/bin/env python3
"""Regenerate font-ter16x32.h from the locked Linux source (printable ASCII only)."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    source = ROOT / '.cache/sources/linux-6.18/lib/fonts/font_ter16x32.c'
    text = source.read_text()
    marker = '{ 0, 0, FONTDATAMAX, 0 }, {'
    body = text[text.index(marker) + len(marker):]
    body = re.sub(r'/\*.*?\*/', '', body[:body.index('}')], flags=re.S)
    values = [int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]{2}', body)]
    if len(values) != 256 * 64:
        sys.exit('unexpected font size')
    rows = []
    for glyph in range(95):
        chunk = values[(32 + glyph) * 64:(33 + glyph) * 64]
        rows.append('    /* %r */ ' % chr(32 + glyph) + ', '.join('0x%02x' % v for v in chunk) + ',')
    (ROOT / 'tools/update/font-ter16x32.h').write_text(
        '/* SPDX-License-Identifier: GPL-2.0 */\n'
        '/* Terminus 16x32, printable ASCII (32..126), extracted unchanged from Linux\n'
        ' * 6.18 lib/fonts/font_ter16x32.c (GPL-2.0) by tools/update/make-font.py.\n'
        ' * Two bytes per row, 32 rows, most significant bit leftmost. */\n'
        '#ifndef Y2_RECOVERY_FONT_H\n#define Y2_RECOVERY_FONT_H\n'
        '#define FONT_W 16\n#define FONT_H 32\n#define FONT_FIRST 32\n#define FONT_COUNT 95\n'
        'static const unsigned char font_ter16x32[FONT_COUNT * 64] = {\n'
        + '\n'.join(rows) + '\n};\n#endif\n')


if __name__ == '__main__':
    main()
