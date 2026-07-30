#!/usr/bin/env python3
"""
gen_button_layout.py - emit the sheet-10 button entries for layout_game_dialogs.json.

76 of sheet 10's 83 frames are the same two buttons at two sizes, so the layout
is generated rather than hand-written. Each frame becomes: the new button art
9-sliced to the frame's exact rect, then the lettering lifted off the original
pak stamped on top in the state's colour.

Frames without extractable lettering, and the five filler textures, are left
alone - the assembler copies them through untouched.
"""

import json
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMP = os.path.join(ROOT, 'Sprite Work', 'components')
LAYOUT = os.path.join(COMP, 'layout_game_dialogs.json')
LABELS = 'Sprite Work/components/labels'
PAK = os.path.join(ROOT, 'Binaries', 'Game', 'sprites', 'interface', 'game_dialogs.pak')

FILE_HEADER, SPRITE_HEADER, RECT = 20, 100, 12

# The original used white on its idle face and yellow on its hover face, which
# worked because both of its faces were dark. The new hover button is much
# lighter (luminance 151 vs the original's), so yellow-on-gold measures 2.16:1 -
# less than half the 4.5:1 small-text minimum. Dark ink on the lit face restores
# the separation at 5.71:1. The convention inverts because the art did.
INK = {'idle': [255, 255, 255], 'hover': [28, 18, 8]}
# Hover lettering is embossed rather than flat: the same mask is stamped twice,
# a warm brass rim offset one pixel down-right and the dark core on top. The
# core carries the contrast (6.31:1); the rim only supplies the engraved look.
EMBOSS_RIM = [214, 170, 92]
# inverted plates carry dark serif lettering, not bright
INK_DARK = [46, 26, 8]

# frames that are textures or portraits, not buttons - never touch these
EXCLUDE = {61, 62, 78, 79, 80, 81, 82}

# frame size -> (component stem, 3-slice inset)
BY_SIZE = {
    (74, 20):  ('btn_std',  [30, 0]),
    (176, 24): ('btn_wide', [36, 0]),
    (171, 30): ('btn_wide', [36, 0]),
    (255, 31): ('btn_wide', [36, 0]),
}


def sheet_rects(path, index):
    data = open(path, 'rb').read()
    off, size = struct.unpack_from('<II', data, FILE_HEADER + 4 + index * 8)
    p = off + SPRITE_HEADER
    n, = struct.unpack_from('<I', data, p)
    p += 4
    return [struct.unpack_from('<HHHHhh', data, p + i * RECT) for i in range(n)]


def main():
    kinds = {}
    kpath = os.path.join(COMP, 'labels', 'kinds.txt')
    for line in open(kpath):
        parts = line.split()
        kinds[int(parts[0])] = (parts[1], parts[2] if len(parts) > 2 else 'bright')

    rects = sheet_rects(PAK, 10)
    layout = json.load(open(LAYOUT))
    frames = {}
    skipped = []

    for i, (x, y, w, h, _, _) in enumerate(rects):
        if i in EXCLUDE:
            skipped.append((i, 'texture/portrait'))
            continue
        if (w, h) not in BY_SIZE:
            skipped.append((i, f'{w}x{h} not a button size'))
            continue
        label = os.path.join(COMP, 'labels', f'label_{i:02d}.png')
        if not os.path.exists(label):
            skipped.append((i, 'no lettering extracted'))
            continue
        stem, inset = BY_SIZE[(w, h)]
        kind, polarity = kinds.get(i, ('idle', 'bright'))
        ink = INK_DARK if polarity == 'dark' else INK[kind]
        ops = [{'op': 'stretch', 'component': f'{stem}_{kind}@sheet',
                'at': [0, 0], 'size': [w, h], 'inset': inset}]
        if kind == 'hover' and polarity != 'dark':
            ops.append({'op': 'mask', 'path': f'{LABELS}/label_{i:02d}.png',
                        'color': EMBOSS_RIM, 'at': [1, 1]})
        ops.append({'op': 'mask', 'path': f'{LABELS}/label_{i:02d}.png',
                    'color': ink, 'at': [0, 0]})
        frames[str(i)] = {'ops': ops}

    layout['sheets']['10'] = frames
    json.dump(layout, open(LAYOUT, 'w'), indent=2)

    print(f'{len(frames)} button frames written to layout_game_dialogs.json')
    n_idle = sum(1 for i in frames if kinds.get(int(i), ('idle',))[0] == 'idle')
    print(f'   {n_idle} idle, {len(frames)-n_idle} hover')
    print(f'\n{len(skipped)} frames left untouched (assembler copies them through):')
    for i, why in skipped:
        print(f'   frame {i:2d}  {why}')


if __name__ == '__main__':
    main()
