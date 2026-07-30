#!/usr/bin/env python3
"""
pack_ui_font.py - pack the serif glyph set into sprite_fonts.pak as a game font.

Three constraints come from SFMLBitmapFont:
  * glyphs are drawn at a fixed y with no per-glyph offset, so every frame must
    be the same height with the glyph positioned inside it against a shared
    baseline;
  * the frame's width IS the advance, so side bearing has to be baked in;
  * sprites must be pure white - the engine multiplies a tint over them, so a
    coloured glyph could never be recoloured. Shape comes from alpha.

Frames run contiguously over ASCII 32..126, space included as a blank.

    python Scripts/pack_ui_font.py --cap 11 --apply
"""

import argparse
import io
import json
import os
import struct

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_SRC = os.path.join(ROOT, 'Sprite Work', 'components', 'font', 'black')
PAK = os.path.join(ROOT, 'Binaries', 'Game', 'sprites', 'interface', 'sprite_fonts.pak')
PREVIEW = os.path.join(ROOT, 'Scripts', 'output', 'dialog_preview')

FILE_HEADER, SPRITE_HEADER, RECT = 20, 100, 12
FIRST, LAST = 32, 126

PUNCT_NAME = {'.': 'period', ',': 'comma', ':': 'colon', '/': 'slash',
              '(': 'lparen', ')': 'rparen', '+': 'plus', '-': 'minus',
              "'": 'apos', '!': 'excl', '?': 'quest', '%': 'percent',
              '"': 'dquote', '#': 'hash', '$': 'dollar', '&': 'amp',
              '*': 'star', ';': 'semi', '<': 'lt', '=': 'eq', '>': 'gt',
              '@': 'at', '[': 'lbrack', '\\': 'backslash', ']': 'rbrack',
              '^': 'caret', '_': 'underscore', '`': 'backtick',
              '{': 'lbrace', '|': 'pipe', '}': 'rbrace', '~': 'tilde'}


def glyph_path(ch):
    n = PUNCT_NAME.get(ch) or (ch if (ch.isupper() or ch.isdigit()) else ch + '_lc')
    return os.path.join(FONT_SRC, n + '.png')


def load_pak(path):
    data = open(path, 'rb').read()
    count, = struct.unpack_from('<I', data, FILE_HEADER)
    entries = [struct.unpack_from('<II', data, FILE_HEADER + 4 + i * 8) for i in range(count)]
    sheets = []
    for off, size in entries:
        header = data[off:off + SPRITE_HEADER]
        p = off + SPRITE_HEADER
        n, = struct.unpack_from('<I', data, p)
        p += 4
        rects = [struct.unpack_from('<HHHHhh', data, p + i * RECT) for i in range(n)]
        p += n * RECT
        pad = data[p:p + 4]
        p += 4
        img_len = size - (SPRITE_HEADER + 4 + n * RECT + 4)
        sheets.append({'header': header, 'rects': rects, 'pad': pad,
                       'image': data[p:p + img_len]})
    return sheets


def save_pak(path, sheets):
    blobs = []
    for s in sheets:
        blobs.append(s['header'] + struct.pack('<I', len(s['rects']))
                     + b''.join(struct.pack('<HHHHhh', *r) for r in s['rects'])
                     + s['pad'] + s['image'])
    out = bytearray(b'<Pak file header>' + b'\x00' * 3)
    out += struct.pack('<I', len(blobs))
    table = len(out)
    out += b'\x00' * (len(blobs) * 8)
    entries = []
    for b in blobs:
        entries.append((len(out), len(b)))
        out += b
    for i, (o, s) in enumerate(entries):
        struct.pack_into('<II', out, table + i * 8, o, s)
    open(path, 'wb').write(bytes(out))
    return len(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cap', type=int, default=11, help='cap height in game pixels')
    ap.add_argument('--bearing', type=int, default=1, help='side bearing baked into the advance')
    ap.add_argument('--apply', action='store_true')
    args = ap.parse_args()

    metrics = json.load(open(os.path.join(FONT_SRC, 'metrics.json')))
    cap = args.cap

    # Cell must clear the tallest ascender and the deepest descender across the
    # whole set, or a 'g' would be clipped and a '^' pushed off the top.
    above = max((m['h'] - m['descent']) for c, m in metrics.items() if c != '_sheet')
    below = max(m['descent'] for c, m in metrics.items() if c != '_sheet')
    cell_h = int(round((above + below) * cap)) + 2
    baseline = int(round(above * cap)) + 1
    print(f'cap {cap}px -> cell height {cell_h}px, baseline at {baseline}px from cell top')

    prepared = []
    for code in range(FIRST, LAST + 1):
        ch = chr(code)
        if ch == ' ' or ch not in metrics:
            prepared.append((ch, None, max(3, int(cap * 0.34))))
            continue
        m = metrics[ch]
        gw = max(1, int(round(m['w'] * cap)))
        gh = max(1, int(round(m['h'] * cap)))
        img = Image.open(glyph_path(ch)).convert('RGBA').resize((gw, gh), Image.LANCZOS)
        # engine multiplies a tint over the sprite, so RGB must be white
        white = Image.new('RGBA', img.size, (255, 255, 255, 0))
        white.putalpha(img.getchannel('A'))
        prepared.append((ch, white, gw + args.bearing * 2))

    per_row = 16
    col_w = max(a for _, _, a in prepared) + 2
    rows = (len(prepared) + per_row - 1) // per_row
    atlas = Image.new('RGBA', (col_w * per_row, cell_h * rows), (0, 0, 0, 0))
    rects = []
    for i, (ch, img, adv) in enumerate(prepared):
        cx, cy = (i % per_row) * col_w, (i // per_row) * cell_h
        if img is not None:
            m = metrics[ch]
            top = baseline - int(round((m['h'] - m['descent']) * cap))
            atlas.alpha_composite(img, (cx + args.bearing, cy + top))
        rects.append((cx, cy, adv, cell_h, 0, 0))

    os.makedirs(PREVIEW, exist_ok=True)
    atlas.save(os.path.join(PREVIEW, 'ui_font_atlas.png'))
    print(f'atlas {atlas.size}, {len(rects)} frames (ASCII {FIRST}..{LAST})')

    sheets = load_pak(PAK)
    buf = io.BytesIO()
    atlas.save(buf, 'PNG', optimize=True)
    new_sheet = {'header': sheets[0]['header'], 'rects': rects,
                 'pad': sheets[0]['pad'], 'image': buf.getvalue()}
    idx = len(sheets)
    print(f'would append as sheet {idx} of sprite_fonts.pak (currently {len(sheets)} sheets)')

    if not args.apply:
        print('dry run - pak untouched. Re-run with --apply.')
        return

    backup = PAK + '.bak_prefont'
    if not os.path.exists(backup):
        import shutil
        shutil.copy2(PAK, backup)
        print(f'backed up -> {os.path.basename(backup)}')
    sheets.append(new_sheet)
    size = save_pak(PAK, sheets)
    check = load_pak(PAK)
    assert len(check) == idx + 1 and check[idx]['rects'] == rects, 'round-trip mismatch'
    for i in range(idx):
        assert check[i]['rects'] == load_pak(backup)[i]['rects'], f'sheet {i} rects changed'
    print(f'wrote {PAK} ({size:,} bytes); sheet {idx} added, sheets 0-{idx-1} unchanged')


if __name__ == '__main__':
    main()
