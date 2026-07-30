#!/usr/bin/env python3
"""
extract_button_labels.py - lift the lettering off the original button frames.

The labels were always art, never code, so restyling the buttons would lose 32
strings unless the glyphs come with. They key cleanly: the text is the brightest
thing inside the button's inner field in both states (white on the idle face,
yellow on the hover face), while the face itself is dark and mottled.

Writes one RGBA mask per frame to Sprite Work/components/labels/.
"""

import io
import os
import struct
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_PAK = os.path.join(ROOT, 'Binaries', 'Game', 'sprites', 'interface',
                       'game_dialogs-original.pak')
OUT = os.path.join(ROOT, 'Sprite Work', 'components', 'labels')

FILE_HEADER, SPRITE_HEADER, RECT = 20, 100, 12
SHEET = 10

# inner field as a fraction of the frame. Wide enough that long labels like
# "Level Set." are not clipped; the rivets sit well below the text threshold so
# they do not get picked up.
FIELD = (0.055, 0.08, 0.945, 0.92)


def load_sheet(path, index):
    data = open(path, 'rb').read()
    count, = struct.unpack_from('<I', data, FILE_HEADER)
    off, size = struct.unpack_from('<II', data, FILE_HEADER + 4 + index * 8)
    p = off + SPRITE_HEADER
    rect_count, = struct.unpack_from('<I', data, p)
    p += 4
    rects = [struct.unpack_from('<HHHHhh', data, p + i * RECT) for i in range(rect_count)]
    p += rect_count * RECT + 4
    img_len = size - (SPRITE_HEADER + 4 + rect_count * RECT + 4)
    return rects, Image.open(io.BytesIO(data[p:p + img_len])).convert('RGB')


def classify(face):
    """idle faces are desaturated brown-grey; hover faces are strongly orange."""
    r, g, b = face
    return 'hover' if (r - b) > 42 and r > 110 else 'idle'


def extract(frame):
    """
    Return an alpha mask of the lettering, or None if the frame has no text.

    Most buttons are bright text on a dark face, but the two orange plates
    ("Character List", "Create New Character") invert that - dark serif on a lit
    ground. Both directions are measured and whichever separates further wins,
    so the plates come out as clean as the buttons.
    """
    w, h = frame.size
    x0, y0 = int(w * FIELD[0]), int(h * FIELD[1])
    x1, y1 = int(w * FIELD[2]), int(h * FIELD[3])
    px = frame.load()

    field = [px[x, y] for y in range(y0, y1) for x in range(x0, x1)]
    if not field:
        return None, None
    lum = sorted(0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2] for c in field)
    base = lum[len(lum) // 2]
    peak = lum[int(len(lum) * 0.985)]
    trough = lum[int(len(lum) * 0.015)]

    n = len(field)
    face = tuple(sum(c[i] for c in field) // n for i in range(3))
    kind = classify(face)

    bright_sep, dark_sep = peak - base, base - trough
    if max(bright_sep, dark_sep) < 34:
        return None, kind
    up = bright_sep >= dark_sep
    edge = peak if up else trough
    cut = base + (edge - base) * 0.45

    mask = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    mp = mask.load()
    hits = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = px[x, y]
            v = 0.299 * r + 0.587 * g + 0.114 * b
            d = (v - cut) if up else (cut - v)
            if d <= 0:
                continue
            a = int(min(255, 255 * d / max(abs(edge - cut), 1)))
            if a > 24:
                mp[x, y] = (255, 255, 255, a)
                hits += 1
    return (mask, (kind, 'bright' if up else 'dark')) if hits >= 8 else (None, (kind, None))


def main():
    os.makedirs(OUT, exist_ok=True)
    rects, atlas = load_sheet(SRC_PAK, SHEET)
    print(f'sheet {SHEET}: {len(rects)} frames, atlas {atlas.size}\n')

    found = skipped = 0
    kinds = {}
    for i, (x, y, w, h, _, _) in enumerate(rects):
        frame = atlas.crop((x, y, x + w, y + h))
        mask, info = extract(frame)
        if mask is None:
            print(f'  frame {i:2d}  {w:3d}x{h:2d}  -- no lettering')
            skipped += 1
            continue
        kind, polarity = info
        mask.save(os.path.join(OUT, f'label_{i:02d}.png'))
        kinds[i] = (kind, polarity)
        found += 1
        print(f'  frame {i:2d}  {w:3d}x{h:2d}  {kind:5s} {polarity:6s}  label extracted')

    with open(os.path.join(OUT, 'kinds.txt'), 'w') as f:
        for i, (kind, polarity) in sorted(kinds.items()):
            f.write(f'{i} {kind} {polarity}\n')
    print(f'\n{found} labels extracted, {skipped} frames without lettering -> {OUT}')


if __name__ == '__main__':
    main()
