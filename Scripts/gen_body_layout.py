#!/usr/bin/env python3
"""
gen_body_layout.py - restyle the dialog bodies across the panel sheets.

Each body keeps its own interior, recoloured onto the new parchment palette,
and gets the new frame kit around the edge. That restyles every dialog without
touching what is inside them - which matters because several of these panels
hold working layout (the Change Password form, the level-up +/- rows, the
exchange grid) rather than decoration.

Sheet 6 frame 0 is the inventory chest, bespoke art with no border to replace,
so it is left alone.
"""

import json
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMP = os.path.join(ROOT, 'Sprite Work', 'components')
LAYOUT = os.path.join(COMP, 'layout_game_dialogs.json')
PAK = os.path.join(ROOT, 'Binaries', 'Game', 'sprites', 'interface', 'game_dialogs.pak')

FILE_HEADER, SPRITE_HEADER, RECT = 20, 100, 12

PARCHMENT = [175, 133, 77]          # measured field colour of the new art
CORNER, BAND = 41, 14

# (sheet, frame): how far the original's own border art reaches in
BODIES = {
    (0, 0): 16, (0, 1): 16, (1, 0): 18, (1, 2): 18, (1, 4): 15,
    (2, 0): 16, (2, 1): 16, (3, 0): 15, (3, 1): 16, (3, 2): 15,
    (3, 3): 15, (3, 4): 13, (4, 0): 16, (4, 21): 16,
    (6, 4): 13, (6, 9): 13, (6, 10): 13, (7, 0): 18,
}


def sheet_rects(path, index):
    data = open(path, 'rb').read()
    off, _ = struct.unpack_from('<II', data, FILE_HEADER + 4 + index * 8)
    p = off + SPRITE_HEADER
    n, = struct.unpack_from('<I', data, p)
    p += 4
    return [struct.unpack_from('<HHHHhh', data, p + i * RECT) for i in range(n)]


def main():
    layout = json.load(open(LAYOUT))
    rects_cache, count = {}, 0

    for (s, f), inset in sorted(BODIES.items()):
        if s not in rects_cache:
            rects_cache[s] = sheet_rects(PAK, s)
        w, h = rects_cache[s][f][2:4]
        # a corner cannot exceed half the panel, or opposite corners overlap
        corner = min(CORNER, w // 2, h // 2)

        frames = layout['sheets'].setdefault(str(s), {})
        frames[str(f)] = {'_body': f'sheet {s} frame {f}', 'ops': [
            {'op': 'fill', 'component': 'parchment_tile_256'},
            {'op': 'source', 'pak': 'game_dialogs-original', 'sheet': s, 'frame': f,
             'inset': inset, 'recolor_to': PARCHMENT},
            {'op': 'frame',
             'corner': 'frame_corner_tl@sheet', 'corner_size': corner, 'thickness': BAND,
             'edge_top': 'frame_edge_h@sheet', 'edge_bottom': 'frame_edge_h@sheet',
             'edge_left': 'frame_edge_v@sheet', 'edge_right': 'frame_edge_v@sheet'},
        ]}
        print(f'  sheet {s} frame {f:2d}  {w:3d}x{h:3d}  interior inset {inset}px, corner {corner}px')
        count += 1

    json.dump(layout, open(LAYOUT, 'w'), indent=2)
    print(f'\n{count} bodies written to the layout')


if __name__ == '__main__':
    main()
