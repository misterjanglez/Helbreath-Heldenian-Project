#!/usr/bin/env python3
"""
build_font_atlas.py - turn a chroma-keyed alphabet sheet into a usable glyph set.

Cuts each glyph out, re-attaches the dots that come off 'i' and 'j' as their own
connected components, sorts into reading order and maps to characters.

    python Scripts/build_font_atlas.py <sheet.png> [--render "sample text"]
"""

import argparse
import json
import os
import sys
from collections import deque

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_ROOT = os.path.join(ROOT, 'Sprite Work', 'components', 'font')


PUNCT_NAME = {'.': 'period', ',': 'comma', ':': 'colon', '/': 'slash',
              '(': 'lparen', ')': 'rparen', '+': 'plus', '-': 'minus',
              "'": 'apos', '!': 'excl', '?': 'quest', '%': 'percent',
              '"': 'dquote', '#': 'hash', '$': 'dollar', '&': 'amp',
              '*': 'star', ';': 'semi', '<': 'lt', '=': 'eq', '>': 'gt',
              '@': 'at', '[': 'lbrack', '\\': 'backslash', ']': 'rbrack',
              '^': 'caret', '_': 'underscore', '`': 'backtick',
              '{': 'lbrace', '|': 'pipe', '}': 'rbrace', '~': 'tilde'}

LAYOUTS = {
    'full':  ['ABCDEFGHIJKLM', 'NOPQRSTUVWXYZ',
              'abcdefghijklm', 'nopqrstuvwxyz', '0123456789', ".,:/()+-'!?%"],
    'extra': ['"#$&*;<=>@', '[\\]^_`{|}~'],
}
ROWS = LAYOUTS['full']


def components(im, key, tol=54, min_px=120):
    W, H = im.size
    px = im.load()

    def is_key(c):
        return all(abs(c[i] - key[i]) <= tol for i in range(3))

    seen = bytearray(W * H)
    out = []
    for sy in range(H):
        for sx in range(W):
            i = sy * W + sx
            if seen[i]:
                continue
            if is_key(px[sx, sy]):
                seen[i] = 1
                continue
            q = deque([(sx, sy)]); seen[i] = 1
            x0 = x1 = sx; y0 = y1 = sy; n = 0
            while q:
                x, y = q.popleft(); n += 1
                x0 = min(x0, x); x1 = max(x1, x); y0 = min(y0, y); y1 = max(y1, y)
                for dx, dy in ((1,0),(-1,0),(0,1),(0,-1),(1,1),(-1,-1),(1,-1),(-1,1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < W and 0 <= ny < H:
                        j = ny * W + nx
                        if not seen[j]:
                            seen[j] = 1
                            if not is_key(px[nx, ny]):
                                q.append((nx, ny))
            if n >= min_px:
                out.append([x0, y0, x1, y1])
    return out


def group_rows(boxes, expected_rows):
    """
    Split into text rows, given how many rows the sheet is known to have.

    Clustering on glyph centres does not survive a punctuation row: '_' sits on
    the baseline while '^' and '~' ride high, so their centres are further apart
    than two adjacent rows are. Since the row count is known from the layout,
    cut at the largest vertical gaps instead - a gap between rows is always
    wider than any gap inside one.
    """
    boxes = sorted(boxes, key=lambda b: b[1])
    if expected_rows <= 1:
        return [sorted(boxes, key=lambda z: z[0])]
    gaps = []
    for i in range(1, len(boxes)):
        gaps.append((boxes[i][1] - max(b[3] for b in boxes[:i]), i))
    cuts = sorted(i for _, i in sorted(gaps, reverse=True)[:expected_rows - 1])
    rows, start = [], 0
    for c in list(cuts) + [len(boxes)]:
        if boxes[start:c]:
            rows.append(sorted(boxes[start:c], key=lambda z: z[0]))
        start = c
    return rows


def split_row(boxes, expected, ink=None):
    """
    Reconcile a row's connected components to exactly `expected` glyphs.

    Too many: several glyphs arrive in pieces - 'i' and 'j' have loose dots,
    ':' is two dots, '!' and '?' are a mark plus a dot, '%' is three parts.
    Cut at the largest horizontal gaps; everything inside a cut is one glyph.

    Too few: on the hover set the glow halos bridge neighbouring letters into a
    single blob. Split the widest box at the column carrying least ink, which is
    the valley between two letters. `ink` is a per-column count over the row.
    """
    boxes = sorted(boxes, key=lambda b: b[0])

    while len(boxes) > expected:
        gaps = []
        for i in range(1, len(boxes)):
            gaps.append((boxes[i][0] - max(b[2] for b in boxes[:i]), i))
        _, i = min(gaps)                                  # tightest gap -> same glyph
        a, b = boxes[i - 1], boxes[i]
        boxes[i - 1:i + 1] = [[min(a[0], b[0]), min(a[1], b[1]),
                               max(a[2], b[2]), max(a[3], b[3])]]

    while len(boxes) < expected and ink is not None:
        widest = max(range(len(boxes)), key=lambda i: boxes[i][2] - boxes[i][0])
        b = boxes[widest]
        lo, hi = b[0], b[2]
        margin = int((hi - lo) * 0.22)
        window = range(lo + margin, hi - margin)
        if not window:
            break
        cut = min(window, key=lambda x: ink[x])
        boxes[widest:widest + 1] = [[lo, b[1], cut - 1, b[3]], [cut + 1, b[1], hi, b[3]]]
        boxes.sort(key=lambda z: z[0])

    return boxes


def stroke_weight(glyphs):
    """
    Median horizontal ink-run length across a glyph set - i.e. stem thickness.

    Two sheets of the same typeface at different sizes share every proportion
    except scale, so the ratio of their stroke weights is the ratio of their
    sizes. That lets a sheet with no uppercase (the punctuation set) be pinned
    to the alphabet's cap height.
    """
    runs = []
    for ch, g in glyphs.items():
        p = g.load()
        for y in range(0, g.height, max(1, g.height // 24)):
            n = 0
            for x in range(g.width):
                if p[x, y][3] > 140:
                    n += 1
                else:
                    if n >= 2:
                        runs.append(n)
                    n = 0
            if n >= 2:
                runs.append(n)
    if not runs:
        return 1.0
    runs.sort()
    # lower quartile: stems, not the long horizontals of serifs and crossbars
    return runs[len(runs) // 4]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('sheet')
    ap.add_argument('--variant', default='brass', help='subfolder: brass | hover | black')
    ap.add_argument('--layout', default='full', choices=sorted(LAYOUTS), help='sheet layout')
    ap.add_argument('--render', help='sample string to render as a proof')
    ap.add_argument('--height', type=int, default=10, help='cap height for the render')
    args = ap.parse_args()

    global ROWS
    ROWS = LAYOUTS[args.layout]

    im = Image.open(args.sheet).convert('RGB')
    W, H = im.size
    px = im.load()
    key = tuple(sum(c[i] for c in [px[2, 2], px[W-3, 2], px[2, H-3], px[W-3, H-3]]) // 4
                for i in range(3))

    raw = components(im, key, min_px=40)
    rows = group_rows(raw, len(ROWS))
    print(f'sheet {W}x{H}, key #{key[0]:02X}{key[1]:02X}{key[2]:02X}')
    print(f'{len(raw)} components in {len(rows)} rows: {[len(r) for r in rows]}')
    if len(rows) != len(ROWS):
        print(f'  ERROR: expected {len(ROWS)} rows, got {len(rows)}')
        return
    def row_ink(row):
        """per-column non-key pixel count across this row's vertical band"""
        y0, y1 = min(b[1] for b in row), max(b[3] for b in row)
        col = [0] * W
        for x in range(W):
            c = 0
            for y in range(y0, y1 + 1):
                p = px[x, y]
                if not all(abs(p[i] - key[i]) <= 54 for i in range(3)):
                    c += 1
            col[x] = c
        return col

    rows = [split_row(r, len(chars), row_ink(r)) for r, chars in zip(rows, ROWS)]
    print(f'after merging multi-part glyphs:      {[len(r) for r in rows]}')
    if [len(r) for r in rows] != [len(r) for r in ROWS]:
        print(f'  ERROR: expected {[len(r) for r in ROWS]}')
        return

    OUT = os.path.join(OUT_ROOT, args.variant)
    os.makedirs(OUT, exist_ok=True)

    def is_key(c, tol=48):
        return all(abs(c[i] - key[i]) <= tol for i in range(3))

    # A magenta key has R and B high with G low, so `min(R,B) - G` measures how
    # much key is mixed into a pixel. Thresholding that to a hard alpha leaves
    # every anti-aliased edge tinted - on the glowing hover set it left a quarter
    # of some glyphs magenta. Instead treat each edge pixel as a blend of glyph
    # over key, derive alpha from the spill, and un-blend to recover the glyph's
    # true colour.
    key_spill = max(min(key[0], key[2]) - key[1], 1)

    def unkey(px_rgb):
        r, g, b = px_rgb
        spill = min(r, b) - g
        if spill <= 0:
            return (r, g, b, 255)                      # no key mixed in
        a = 1.0 - min(spill / key_spill, 1.0)
        if a <= 0.004:
            return (0, 0, 0, 0)
        out = []
        for i, v in enumerate((r, g, b)):
            fg = (v - (1.0 - a) * key[i]) / a
            out.append(max(0, min(255, int(round(fg)))))
        return (out[0], out[1], out[2], int(round(a * 255)))

    # Per-row baseline. Most glyphs rest on it, so the median bottom edge finds
    # it; descenders (g j p q y , ;) then carry a positive descent and things
    # like '^' and '"' a negative one. Without this every glyph gets bottom-
    # aligned to its own box and the line wobbles.
    metrics = {}
    baselines = []
    for row, chars in zip(rows, ROWS):
        bottoms = sorted(b[3] for b in row)
        baselines.append(bottoms[len(bottoms) // 2])

    glyphs = {}
    for row, chars, baseline in zip(rows, ROWS, baselines):
        for b, ch in zip(row, chars):
            metrics[ch] = {'w': b[2] - b[0] + 1, 'h': b[3] - b[1] + 1,
                           'descent': b[3] - baseline}
            g = im.crop((b[0], b[1], b[2] + 1, b[3] + 1)).convert('RGBA')
            gp = g.load()
            for y in range(g.height):
                for x in range(g.width):
                    r, gg, bb, _ = gp[x, y]
                    gp[x, y] = unkey((r, gg, bb))
            name = PUNCT_NAME.get(ch) or (ch if (ch.isupper() or ch.isdigit()) else f'{ch}_lc')
            g.save(os.path.join(OUT, f'{name}.png'))
            glyphs[ch] = g
    print(f'{len(glyphs)} glyphs -> {OUT}')

    # Metrics accumulate across the sheets that make up one variant, so merge
    # rather than overwrite - the alphabet and the punctuation arrive separately.
    mpath = os.path.join(OUT, 'metrics.json')
    merged = {}
    if os.path.exists(mpath):
        merged = json.load(open(mpath))

    # The punctuation sheets came back drawn larger than the alphabets, so a
    # glyph's raw pixel height means nothing on its own. Stroke weight scales
    # with font size for the same typeface, so it anchors one sheet against
    # another: measure it here, and store every metric as a fraction of this
    # sheet's cap height so the renderer can mix sheets freely.
    stroke = stroke_weight(glyphs)
    if any(c.isupper() for c in metrics):
        cap = max(metrics[c]['h'] for c in metrics if c.isupper())
    else:
        ref_stroke = merged.get('_sheet', {}).get('stroke')
        ref_cap = merged.get('_sheet', {}).get('cap')
        if ref_stroke and ref_cap:
            cap = ref_cap * (stroke / ref_stroke)
            print(f'  no uppercase on this sheet; cap inferred from stroke weight '
                  f'{stroke:.1f} vs {ref_stroke:.1f} -> {cap:.1f}px')
        else:
            cap = max(m['h'] for m in metrics.values())

    for ch, m in metrics.items():
        merged[ch] = {'w': m['w'] / cap, 'h': m['h'] / cap,
                      'descent': m['descent'] / cap}
    if any(c.isupper() for c in metrics):
        merged['_sheet'] = {'stroke': stroke, 'cap': cap}
    json.dump(merged, open(mpath, 'w'), indent=1, sort_keys=True)
    n = len([k for k in merged if k != '_sheet'])
    print(f'metrics for {len(metrics)} glyphs merged -> metrics.json '
          f'({n} total, normalised to cap {cap:.0f}px)')

    # TextLib wants a contiguous ASCII run, so report what a drop-in font is short of
    full = set(chr(c) for c in range(32, 127))
    missing = sorted(full - set(merged) - {' '})
    print(f'short of full ASCII 32-126 by {len(missing)}: {" ".join(missing)}')

    if args.render:
        cap = max(g.height for c, g in glyphs.items() if c.isupper())
        scale = args.height / cap
        pieces, total = [], 0
        for ch in args.render:
            if ch == ' ':
                pieces.append(None); total += int(args.height * 0.35); continue
            if ch not in glyphs:
                continue
            g = glyphs[ch]
            nw, nh = max(1, round(g.width * scale)), max(1, round(g.height * scale))
            gg = g.resize((nw, nh), Image.LANCZOS)
            pieces.append(gg); total += nw + max(1, round(args.height * 0.08))
        base = max((p.height for p in pieces if p), default=args.height)
        out = Image.new('RGBA', (total + 4, base + 4), (0, 0, 0, 0))
        x = 2
        for p in pieces:
            if p is None:
                x += int(args.height * 0.35); continue
            out.paste(p, (x, 2 + base - p.height), p)
            x += p.width + max(1, round(args.height * 0.08))
        out.save(os.path.join(OUT, '_sample.png'))
        print(f'sample "{args.render}" at cap height {args.height}px -> {OUT}\\_sample.png  ({out.size})')


if __name__ == '__main__':
    main()
