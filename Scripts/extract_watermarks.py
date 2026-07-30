#!/usr/bin/env python3
"""
extract_watermarks.py - lift the themed emblem out of each original panel body.

Every dialog body carries a faint emblem ghosted into its parchment - knights,
an arcane circle, a smith's hammer, a siege field. Rebuilding the bodies from
the component kit would drop all of them, which is how the character panel lost
its row labels. They are recoverable: the emblem is simply the field's darker
deviation from clean parchment.

Writes a black alpha mask per frame to Sprite Work/components/watermarks/.
"""

import io
import json
import os
import struct

from PIL import Image, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'Binaries', 'Game', 'sprites', 'interface',
                   'game_dialogs-original.pak')
OUT = os.path.join(ROOT, 'Sprite Work', 'components', 'watermarks')

FILE_HEADER, SPRITE_HEADER, RECT = 20, 100, 12

# (sheet, frame) -> inset from the frame edge that clears the border art
TARGETS = {
    (0, 0): 18, (0, 1): 18, (1, 0): 20, (1, 2): 20, (1, 4): 16,
    (2, 0): 18, (2, 1): 18, (3, 0): 16, (3, 1): 18, (3, 2): 16,
    (3, 3): 16, (3, 4): 14, (4, 0): 18, (4, 21): 18,
    (6, 4): 14, (6, 9): 14, (6, 10): 14, (7, 0): 20,
}


def load_sheet(path, index):
    data = open(path, 'rb').read()
    off, size = struct.unpack_from('<II', data, FILE_HEADER + 4 + index * 8)
    p = off + SPRITE_HEADER
    n, = struct.unpack_from('<I', data, p)
    p += 4
    rects = [struct.unpack_from('<HHHHhh', data, p + i * RECT) for i in range(n)]
    p += n * RECT + 4
    img_len = size - (SPRITE_HEADER + 4 + n * RECT + 4)
    return rects, Image.open(io.BytesIO(data[p:p + img_len])).convert('RGB')


def extract(frame, inset):
    """
    Emblem = how far each pixel falls below the clean parchment level.

    A single global threshold would catch the panel's vignetting as well as the
    art, so the reference level is taken from a heavily blurred copy: that
    tracks the slow shading and leaves only the emblem's own contrast behind.
    """
    w, h = frame.size
    inner = frame.crop((inset, inset, w - inset, h - inset))
    if inner.width < 8 or inner.height < 8:
        return None, 0.0

    grey = inner.convert('L')
    flat = grey.filter(ImageFilter.GaussianBlur(max(inner.width, inner.height) / 7.0))
    gp, fp = grey.load(), flat.load()

    deltas = [fp[x, y] - gp[x, y]
              for y in range(0, inner.height, 2) for x in range(0, inner.width, 2)]
    deltas.sort()
    strong = deltas[int(len(deltas) * 0.995)]
    if strong < 6:
        return None, 0.0

    mask = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    mp = mask.load()
    hits = 0
    for y in range(inner.height):
        for x in range(inner.width):
            d = fp[x, y] - gp[x, y]
            if d <= 1:
                continue
            a = int(min(255, 255 * d / strong))
            if a > 10:
                mp[x + inset, y + inset] = (0, 0, 0, a)
                hits += 1
    return mask, hits / float(inner.width * inner.height)


def main():
    os.makedirs(OUT, exist_ok=True)
    index = {}
    by_sheet = {}
    for (s, f), inset in sorted(TARGETS.items()):
        if s not in by_sheet:
            by_sheet[s] = load_sheet(SRC, s)
        rects, atlas = by_sheet[s]
        x, y, w, h = rects[f][:4]
        frame = atlas.crop((x, y, x + w, y + h))
        mask, cover = extract(frame, inset)
        if mask is None:
            print(f'  sheet {s} frame {f:2d}  {w:3d}x{h:3d}  -- no emblem found')
            continue
        name = f'wm_s{s}_f{f}'
        mask.save(os.path.join(OUT, name + '.png'))
        index[f'{s}.{f}'] = {'component': name, 'size': [w, h], 'coverage': round(cover, 3)}
        print(f'  sheet {s} frame {f:2d}  {w:3d}x{h:3d}  -> {name}   coverage {cover*100:4.1f}%')

    json.dump(index, open(os.path.join(OUT, 'index.json'), 'w'), indent=2)
    print(f'\n{len(index)} watermarks -> {OUT}')


if __name__ == '__main__':
    main()
