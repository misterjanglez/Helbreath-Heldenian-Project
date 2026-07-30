#!/usr/bin/env python3
"""
gen_title_layout.py - emit sheet-9's 22 title strips into the layout.

Each strip is the blank plate 3-sliced to the frame's exact rect with the title
set in brass on top. Titles are transcribed from the original art so the wording
is unchanged; only the typeface and plate are new.

Frame 0 is the Character Info panel and is left alone.
"""

import json
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMP = os.path.join(ROOT, 'Sprite Work', 'components')
LAYOUT = os.path.join(COMP, 'layout_game_dialogs.json')
PAK = os.path.join(ROOT, 'Binaries', 'Game', 'sprites', 'interface', 'game_dialogs.pak')

FILE_HEADER, SPRITE_HEADER, RECT = 20, 100, 12

TITLES = {
    1: 'Skills',            2: 'Level up Setting',  3: 'Party Menu',
    4: 'Quest',             5: 'Item Upgrade',      6: 'System Menu',
    7: 'Magic List',        8: 'Tinkering',         9: 'Exchange Item',
    10: 'Repair Item',      11: 'Item for Sale',    12: 'User Agreement',
    13: 'Change Password',  14: 'Spells',           15: 'Commander Menu',
    16: 'Constructor Menu', 17: 'Soldier Menu',     18: 'Cityhall Menu',
    19: 'Guild Menu',       20: 'Notice',           21: 'Items In Storage',
    22: 'History',
}

# the plate's ornate ends eat this much of the width at each side
END_CAP = 46
CAP_HEIGHT = 15


def sheet_rects(path, index):
    data = open(path, 'rb').read()
    off, _ = struct.unpack_from('<II', data, FILE_HEADER + 4 + index * 8)
    p = off + SPRITE_HEADER
    n, = struct.unpack_from('<I', data, p)
    p += 4
    return [struct.unpack_from('<HHHHhh', data, p + i * RECT) for i in range(n)]


def main():
    rects = sheet_rects(PAK, 9)
    layout = json.load(open(LAYOUT))
    frames = layout['sheets'].get('9', {})

    for i, title in sorted(TITLES.items()):
        x, y, w, h = rects[i][:4]
        frames[str(i)] = {'_title': title, 'ops': [
            {'op': 'stretch', 'component': 'title_plate_blank@sheet',
             'at': [0, 0], 'size': [w, h], 'inset': [END_CAP, 0]},
            {'op': 'text', 'font': 'brass', 'string': title,
             'cap': CAP_HEIGHT, 'fit_width': w - 2 * END_CAP - 10,
             'at': ['center', 'center'], 'dy': -1},
        ]}
        print(f'  frame {i:2d}  {w:3d}x{h:2d}  "{title}"')

    layout['sheets']['9'] = frames
    json.dump(layout, open(LAYOUT, 'w'), indent=2)
    print(f'\n{len(TITLES)} title strips written; frame 0 untouched')


if __name__ == '__main__':
    main()
