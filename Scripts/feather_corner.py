#!/usr/bin/env python3
"""
feather_corner.py - soften a harvested corner tile's inner edges.

A corner cropped off a finished panel brings a square of that panel's parchment
with it. Colour-matching does not hide it (the fields are within 8 units) and
colour-keying cannot cut it out (parchment texture overlaps the brass in hue) -
the giveaway is the texture seam where the tile's grain stops dead.

Feathering the two inner edges lets the panel's own parchment take over
gradually, so there is no line to see. The L-shaped metal band stays fully
opaque; only the interior quadrant fades.
"""

import os

from PIL import Image, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
C = os.path.join(ROOT, 'Sprite Work', 'components')

BAND = 58        # metal band along the two outer edges - always opaque
KEEP = 126       # radius from the corner origin that still holds ornament
FADE_TO = 168    # fully transparent past here


def feather(src, dst):
    """
    Fade only the parchment the tile brought with it.

    Measuring the fade from the band's inner edge looked right and was wrong:
    the corner boss sits about 22px past the band, so it landed mid-fade and
    came out two-thirds transparent - the corners went visibly weak. The
    ornament radiates from the corner origin, so the fade has to as well, and
    it must not start until past the ornament's reach.
    """
    im = Image.open(src).convert('RGBA')
    W, H = im.size
    mask = Image.new('L', (W, H), 255)
    mp = mask.load()
    for y in range(H):
        for x in range(W):
            if x < BAND or y < BAND:
                continue                       # outer band, always solid
            r = (x * x + y * y) ** 0.5
            if r <= KEEP:
                continue                       # ornament, always solid
            if r >= FADE_TO:
                mp[x, y] = 0
            else:
                mp[x, y] = int(255 * (1.0 - (r - KEEP) / (FADE_TO - KEEP)))
    out = im.copy()
    out.putalpha(ImageChops.multiply(im.getchannel('A'), mask))
    out.save(dst)
    px = out.load()
    kept = sum(1 for y in range(H) for x in range(W) if px[x, y][3] > 200)
    print(f'{os.path.basename(dst)}: {100*kept/(W*H):.0f}% fully opaque, ornament kept to r={KEEP}, fades out by r={FADE_TO}')
    return out


out = feather(os.path.join(C, 'frame_corner_tl@hi.png'),
              os.path.join(C, 'frame_corner_feathered@hi.png'))

# proof: the tile over two different grounds - a seam would show on both
for tag, col in (('match', (170, 128, 69)), ('contrast', (110, 150, 90))):
    bg = Image.new('RGB', out.size, col)
    bg.paste(out, (0, 0), out)
    bg.resize((out.width * 2, out.height * 2), Image.NEAREST).save(
        os.path.join(ROOT, 'Scripts', 'output', 'dialog_preview', f'corner_feather_{tag}.png'))
print('previews written to Scripts/output/dialog_preview/')
