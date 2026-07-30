#!/usr/bin/env python3
"""
build_dialog_sheet.py - composite dialog art from components and repack a .pak.

The frame rects inside a .pak are the contract the C++ draws against, so this
tool never repacks the atlas. It renders each frame at its existing rect size
and pastes it back at its existing position; frames with no layout are copied
through untouched.

    python Scripts/build_dialog_sheet.py --dry-run          # write preview PNGs only
    python Scripts/build_dialog_sheet.py --diff             # + compare against a reference
    python Scripts/build_dialog_sheet.py --apply            # rewrite the .pak (backs up first)

Layout files live beside the components and declare, per frame index, a list of
draw ops. See layout_game_dialogs.json.
"""

import argparse
import json
import io
import os
import shutil
import struct
import sys

try:
    from PIL import Image, ImageChops
except ImportError:
    sys.exit("Pillow required:  pip install Pillow")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMPONENTS = os.path.join(ROOT, 'Sprite Work', 'components')
PAK = os.path.join(ROOT, 'Binaries', 'Game', 'sprites', 'interface', 'game_dialogs.pak')
LAYOUT = os.path.join(COMPONENTS, 'layout_game_dialogs.json')
PREVIEW = os.path.join(ROOT, 'Scripts', 'output', 'dialog_preview')

FILE_HEADER = 20        # 17-byte signature + 3 pad
SPRITE_HEADER = 100     # 20-byte signature + 80 pad
RECT = 12               # x,y,w,h (u16) + pivotX,pivotY (i16)


# --------------------------------------------------------------------------
# pak read / write
# --------------------------------------------------------------------------

def load_pak(path):
    data = open(path, 'rb').read()
    if data[:17] != b'<Pak file header>':
        raise ValueError(f'not a pak: {path}')
    count, = struct.unpack_from('<I', data, FILE_HEADER)
    entries = [struct.unpack_from('<II', data, FILE_HEADER + 4 + i * 8) for i in range(count)]
    sheets = []
    for offset, size in entries:
        header = data[offset:offset + SPRITE_HEADER]
        p = offset + SPRITE_HEADER
        rect_count, = struct.unpack_from('<I', data, p)
        p += 4
        rects = [struct.unpack_from('<HHHHhh', data, p + i * RECT) for i in range(rect_count)]
        p += rect_count * RECT
        pad = data[p:p + 4]
        p += 4
        img_len = size - (SPRITE_HEADER + 4 + rect_count * RECT + 4)
        sheets.append({'header': header, 'rects': rects, 'pad': pad,
                       'image': data[p:p + img_len]})
    return sheets


def save_pak(path, sheets):
    """Rewrite the pak, recomputing offsets. Rect tables are passed through."""
    blobs = []
    for s in sheets:
        body = (s['header']
                + struct.pack('<I', len(s['rects']))
                + b''.join(struct.pack('<HHHHhh', *r) for r in s['rects'])
                + s['pad']
                + s['image'])
        blobs.append(body)

    out = bytearray()
    out += b'<Pak file header>' + b'\x00' * 3
    out += struct.pack('<I', len(blobs))
    table_at = len(out)
    out += b'\x00' * (len(blobs) * 8)
    entries = []
    for b in blobs:
        entries.append((len(out), len(b)))
        out += b
    for i, (off, size) in enumerate(entries):
        struct.pack_into('<II', out, table_at + i * 8, off, size)
    open(path, 'wb').write(bytes(out))
    return len(out)


# --------------------------------------------------------------------------
# components
# --------------------------------------------------------------------------

class Library:
    def __init__(self, directory):
        self.dir = directory
        self.cache = {}

    def get(self, name):
        if name not in self.cache:
            path = os.path.join(self.dir, f'{name}.png')
            if not os.path.exists(path):
                raise FileNotFoundError(f'component not found: {path}')
            self.cache[name] = Image.open(path).convert('RGBA')
        return self.cache[name]


def slice_resize(img, w, h, inset, interior=None):
    """
    9-slice (or 3-slice when an inset axis is 0) to exactly w x h.

    Stretching a 6px-tall centre band to 200px smears it into vertical streaks,
    which is what a big recessed panel needs most and tolerates least. When
    `interior` is supplied it is tiled into the centre instead, so the grain
    stays at its native scale no matter how far the border is pulled apart.
    """
    iw, ih = img.size
    cw, ch = inset
    cw = min(cw, iw // 2, max(w // 2, 1))
    ch = min(ch, ih // 2, max(h // 2, 1))
    if cw <= 0 and ch <= 0:
        return img.resize((w, h), Image.LANCZOS)

    out = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    mw, mh = max(w - 2 * cw, 1), max(h - 2 * ch, 1)

    def part(box, size):
        return img.crop(box).resize(size, Image.LANCZOS)

    # centre
    if interior is not None:
        for ty in range(ch, ch + mh, interior.height):
            for tx in range(cw, cw + mw, interior.width):
                out.paste(interior.crop((0, 0,
                                         min(interior.width, cw + mw - tx),
                                         min(interior.height, ch + mh - ty))), (tx, ty))
    else:
        out.paste(part((cw, ch, iw - cw, ih - ch), (mw, mh)), (cw, ch))
    if ch > 0:
        out.paste(part((cw, 0, iw - cw, ch), (mw, ch)), (cw, 0))
        out.paste(part((cw, ih - ch, iw - cw, ih), (mw, ch)), (cw, h - ch))
    if cw > 0:
        out.paste(part((0, ch, cw, ih - ch), (cw, mh)), (0, ch))
        out.paste(part((iw - cw, ch, iw, ih - ch), (cw, mh)), (w - cw, ch))
    if cw > 0 and ch > 0:
        out.paste(img.crop((0, 0, cw, ch)), (0, 0))
        out.paste(img.crop((iw - cw, 0, iw, ch)), (w - cw, 0))
        out.paste(img.crop((0, ih - ch, cw, ih)), (0, h - ch))
        out.paste(img.crop((iw - cw, ih - ch, iw, ih)), (w - cw, h - ch))
    return out


_source_cache = {}


def recolor(img, target, contrast=0.82):
    """
    Move an image's field colour onto `target` without amplifying what sits on it.

    A per-channel gain looks like the obvious way to do this, but gain scales
    deviations as well as the field: shifting a panel from luminance 99 to 129
    multiplies every value by 1.30, and the faint emblem ghosted into the
    parchment gets 30% louder with it. Measured on the rebuilt bodies that took
    the watermark spread from 34 to 44 and read as garish.

    An affine remap moves the field and controls the deviation separately:

        out = target + (in - field) * contrast

    contrast slightly under 1 also compensates for the field being brighter
    than it was - the same absolute deviation is more visible on a light ground.
    """
    small = img.resize((min(img.width, 96), min(img.height, 96)), Image.BOX)
    px = small.load()
    pix = [px[x, y] for y in range(small.height) for x in range(small.width)]
    pix.sort(key=lambda c: 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2])
    band = pix[int(len(pix) * 0.55):int(len(pix) * 0.85)] or pix
    n = len(band)
    field = [sum(c[i] for c in band) / n for i in range(3)]
    lut = []
    for c in range(3):
        lut += [max(0, min(255, int(round(target[c] + (v - field[c]) * contrast))))
                for v in range(256)]
    return img.point(lut)


# --------------------------------------------------------------------------
# text
# --------------------------------------------------------------------------

FONT_ROOT = os.path.join(ROOT, 'Sprite Work', 'components', 'font')
PUNCT_NAME = {'.': 'period', ',': 'comma', ':': 'colon', '/': 'slash',
              '(': 'lparen', ')': 'rparen', '+': 'plus', '-': 'minus',
              "'": 'apos', '!': 'excl', '?': 'quest', '%': 'percent',
              '"': 'dquote', '#': 'hash', '$': 'dollar', '&': 'amp',
              '*': 'star', ';': 'semi', '<': 'lt', '=': 'eq', '>': 'gt',
              '@': 'at', '[': 'lbrack', '\\': 'backslash', ']': 'rbrack',
              '^': 'caret', '_': 'underscore', '`': 'backtick',
              '{': 'lbrace', '|': 'pipe', '}': 'rbrace', '~': 'tilde'}
_font_cache = {}


def _load_font(variant):
    if variant not in _font_cache:
        d = os.path.join(FONT_ROOT, variant)
        metrics = json.load(open(os.path.join(d, 'metrics.json')))
        _font_cache[variant] = (d, metrics)
    return _font_cache[variant]


def _glyph_file(variant, ch):
    d, _ = _load_font(variant)
    name = PUNCT_NAME.get(ch) or (ch if (ch.isupper() or ch.isdigit()) else ch + '_lc')
    return os.path.join(d, name + '.png')


def render_text(variant, string, cap, fit_width=None, tracking=0.10):
    """
    Lay a string out on a shared baseline.

    Metrics are stored as fractions of cap height, so a glyph's vertical offset
    is descent * cap - which is what stops descenders and floating marks from
    each being bottom-aligned to their own box.
    """
    _, metrics = _load_font(variant)
    usable = [c for c in string if c == ' ' or c in metrics]
    if not usable:
        return None

    while True:
        gap = max(1, round(cap * tracking))
        space = max(2, round(cap * 0.38))
        laid, width = [], 0
        for ch in usable:
            if ch == ' ':
                laid.append(None); width += space; continue
            m = metrics[ch]
            gw = max(1, round(m['w'] * cap))
            gh = max(1, round(m['h'] * cap))
            laid.append((ch, gw, gh, m['descent'] * cap))
            width += gw + gap
        if fit_width is None or width <= fit_width or cap <= 6:
            break
        cap -= 1

    above = max((g[2] - g[3] for g in laid if g), default=cap)
    below = max((g[3] for g in laid if g), default=0)
    out = Image.new('RGBA', (max(width, 1), max(1, round(above + below))), (0, 0, 0, 0))
    x = 0
    for g in laid:
        if g is None:
            x += space; continue
        ch, gw, gh, desc = g
        img = Image.open(_glyph_file(variant, ch)).convert('RGBA').resize((gw, gh), Image.LANCZOS)
        out.alpha_composite(img, (x, int(round(above - (gh - desc)))))
        x += gw + gap
    return out


# --------------------------------------------------------------------------
# draw ops
# --------------------------------------------------------------------------

def render_frame(lib, spec, size):
    """Render one frame from its op list."""
    w, h = size
    canvas = Image.new('RGBA', (w, h), (0, 0, 0, 0))

    for op in spec.get('ops', []):
        kind = op['op']

        if kind == 'fill':
            tile = lib.get(op['component'])
            x0, y0, x1, y1 = op.get('rect', [0, 0, w, h])
            for ty in range(y0, y1, tile.height):
                for tx in range(x0, x1, tile.width):
                    canvas.alpha_composite(tile, (tx, ty))

        elif kind == 'frame':
            corner = lib.get(op['corner'])
            c = op.get('corner_size', corner.width)
            c = min(c, w // 2, h // 2)
            cs = corner.resize((c, c), Image.LANCZOS)
            th = op.get('thickness', 14)
            et, eb = lib.get(op['edge_top']), lib.get(op['edge_bottom'])
            el, er = lib.get(op['edge_left']), lib.get(op['edge_right'])
            mw, mh = max(w - 2 * c, 1), max(h - 2 * c, 1)
            canvas.alpha_composite(et.resize((mw, th), Image.LANCZOS), (c, 0))
            canvas.alpha_composite(eb.resize((mw, th), Image.LANCZOS), (c, h - th))
            canvas.alpha_composite(el.resize((th, mh), Image.LANCZOS), (0, c))
            canvas.alpha_composite(er.resize((th, mh), Image.LANCZOS), (w - th, c))
            canvas.alpha_composite(cs, (0, 0))
            canvas.alpha_composite(cs.transpose(Image.FLIP_LEFT_RIGHT), (w - c, 0))
            canvas.alpha_composite(cs.transpose(Image.FLIP_TOP_BOTTOM), (0, h - c))
            canvas.alpha_composite(cs.transpose(Image.ROTATE_180), (w - c, h - c))

        elif kind == 'alpha_from':
            # Adopt the original frame's silhouette. Panels have rounded or
            # cut-away corners that are transparent in the source; without this
            # the rebuilt frame would square them off.
            src_pak = op.get('pak', 'game_dialogs-original')
            path = os.path.join(os.path.dirname(PAK), src_pak + '.pak')
            key = (path, op['sheet'])
            if key not in _source_cache:
                sheets = load_pak(path)
                _source_cache[key] = (sheets[op['sheet']]['rects'],
                                      Image.open(io.BytesIO(sheets[op['sheet']]['image'])).convert('RGBA'))
            rects, atlas = _source_cache[key]
            sx, sy, sw, sh = rects[op['frame']][:4]
            a = atlas.crop((sx, sy, sx + sw, sy + sh)).getchannel('A')
            if a.size != (w, h):
                a = a.resize((w, h), Image.LANCZOS)
            cur = canvas.getchannel('A')
            canvas.putalpha(Image.eval(Image.merge('L', [cur]), lambda v: v) if False
                            else ImageChops.multiply(cur, a))

        elif kind == 'source':
            # Carry a frame's interior over from another pak, recoloured to the
            # new palette. Several bodies look like they hold a decorative
            # watermark but actually hold working layout - the Change Password
            # form, the level-up +/- rows, the exchange grid. Regenerating those
            # from a frame plus an emblem would quietly delete the interface.
            # Keeping the pixels and restyling only the border loses nothing.
            src_pak = op.get('pak', 'game_dialogs-original')
            path = os.path.join(os.path.dirname(PAK), src_pak + '.pak')
            key = (path, op['sheet'])
            if key not in _source_cache:
                sheets = load_pak(path)
                _source_cache[key] = (sheets[op['sheet']]['rects'],
                                      Image.open(io.BytesIO(sheets[op['sheet']]['image'])).convert('RGBA'))
            rects, atlas = _source_cache[key]
            sx, sy, sw, sh = rects[op['frame']][:4]
            src = atlas.crop((sx, sy, sx + sw, sy + sh))   # RGBA
            inset = op.get('inset', 0)
            if inset:
                src = src.crop((inset, inset, sw - inset, sh - inset))
            if op.get('recolor_to'):
                alpha = src.getchannel('A')
                src = recolor(src.convert('RGB'), op['recolor_to']).convert('RGBA')
                src.putalpha(alpha)
            if src.size != (w - 2 * inset, h - 2 * inset):
                src = src.resize((max(1, w - 2 * inset), max(1, h - 2 * inset)), Image.LANCZOS)
            canvas.alpha_composite(src, (inset, inset))

        elif kind == 'text':
            piece = render_text(op['font'], op['string'], op.get('cap', 14),
                                op.get('fit_width'), op.get('tracking', 0.10))
            if piece is not None and op.get('color'):
                tint = Image.new('RGBA', piece.size, tuple(op['color']) + (255,))
                tint.putalpha(piece.getchannel('A'))
                piece = tint
            if piece is not None:
                ax = op.get('at', ['center', 'center'])
                x = (w - piece.width) // 2 if ax[0] == 'center' else int(ax[0])
                y = (h - piece.height) // 2 if ax[1] == 'center' else int(ax[1])
                canvas.alpha_composite(piece, (x + op.get('dx', 0), y + op.get('dy', 0)))

        elif kind == 'mask':
            # Stamp an extracted alpha mask in a flat colour. Used for button
            # lettering lifted off the original art, so the shipped typeface
            # survives a restyle of the button underneath it.
            path = op['path']
            if not os.path.isabs(path):
                path = os.path.join(ROOT, path)
            m = Image.open(path).convert('RGBA')
            col = tuple(op.get('color', [255, 255, 255]))
            tint = Image.new('RGBA', m.size, col + (255,))
            tint.putalpha(m.getchannel('A'))
            if m.size != (w, h):
                tint = tint.resize((w, h), Image.LANCZOS)
            canvas.alpha_composite(tint, tuple(op.get('at', [0, 0])))

        elif kind == 'image':
            # Drop a finished image straight into the frame. Used where reference
            # art already exists at full quality and reassembling it from its own
            # parts would only lose detail.
            path = op['path']
            if not os.path.isabs(path):
                path = os.path.join(ROOT, path)
            src = Image.open(path).convert('RGBA')
            box = op.get('rect', [0, 0, w, h])
            canvas.alpha_composite(
                src.resize((box[2] - box[0], box[3] - box[1]), Image.LANCZOS),
                (box[0], box[1]))

        elif kind in ('stamp', 'stretch', 'repeat'):
            img = lib.get(op['component'])
            inset = op.get('inset', [0, 0])
            count = op.get('count', 1)
            step = op.get('step', [0, 0])
            ax, ay = op['at']
            sz = op.get('size')
            interior = lib.get(op['interior']) if op.get('interior') else None
            for i in range(count):
                x = int(round(ax + step[0] * i))
                y = int(round(ay + step[1] * i))
                piece = img if sz is None else slice_resize(img, sz[0], sz[1], inset, interior)
                if op.get('flip_h'):
                    piece = piece.transpose(Image.FLIP_LEFT_RIGHT)
                canvas.alpha_composite(piece, (x, y))

        else:
            raise ValueError(f'unknown op: {kind}')

    return canvas


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--pak', default=PAK)
    ap.add_argument('--layout', default=LAYOUT)
    ap.add_argument('--components', default=COMPONENTS)
    ap.add_argument('--dry-run', action='store_true', help='write preview PNGs, touch nothing')
    ap.add_argument('--apply', action='store_true', help='rewrite the pak (backs up first)')
    ap.add_argument('--diff', metavar='PNG', help='compare frame 0 against a reference image')
    args = ap.parse_args()

    if not (args.dry_run or args.apply):
        args.dry_run = True

    layout = json.load(open(args.layout))
    lib = Library(args.components)
    sheets = load_pak(args.pak)
    os.makedirs(PREVIEW, exist_ok=True)

    total = 0
    for sheet_key, frames in layout['sheets'].items():
        sheet_idx = int(sheet_key)
        sheet = sheets[sheet_idx]
        atlas_w = max(r[0] + r[2] for r in sheet['rects'])
        atlas_h = max(r[1] + r[3] for r in sheet['rects'])
        atlas = Image.open(io.BytesIO(sheet['image'])).convert('RGBA')
        if atlas.size != (atlas_w, atlas_h):
            print(f'  note: atlas {atlas.size} vs rect extent {(atlas_w, atlas_h)}')

        for frame_key, spec in frames.items():
            fi = int(frame_key)
            x, y, fw, fh, _, _ = sheet['rects'][fi]
            img = render_frame(lib, spec, (fw, fh))
            # replace the rect outright - pasting would let the old frame show
            # through wherever the new one is transparent
            atlas.paste(img, (x, y))
            img.save(os.path.join(PREVIEW, f'sheet{sheet_idx}_frame{fi}.png'))
            print(f'  sheet {sheet_idx} frame {fi:2d}  {fw}x{fh} at ({x},{y})  '
                  f'{len(spec.get("ops", []))} ops')
            total += 1

            if args.diff and fi == 0:
                ref = Image.open(args.diff).convert('RGB').resize((fw, fh), Image.LANCZOS)
                sbs = Image.new('RGB', (fw * 2 + 8, fh), (255, 0, 255))
                sbs.paste(ref, (0, 0)); sbs.paste(img, (fw + 8, 0))
                sbs.resize(((fw * 2 + 8) * 2, fh * 2), Image.NEAREST).save(
                    os.path.join(PREVIEW, f'diff_sheet{sheet_idx}_frame{fi}.png'))
                px_r, px_i = ref.load(), img.load()
                err = sum(abs(px_r[i, j][k] - px_i[i, j][k])
                          for j in range(0, fh, 3) for i in range(0, fw, 3) for k in range(3))
                n = len(range(0, fh, 3)) * len(range(0, fw, 3)) * 3
                print(f'      mean abs diff vs reference: {err/n:.1f}/255')

        buf = io.BytesIO()
        atlas.save(buf, 'PNG', optimize=True)
        sheet['image'] = buf.getvalue()
        atlas.save(os.path.join(PREVIEW, f'atlas_sheet{sheet_idx}.png'))

    print(f'\n{total} frame(s) rendered -> {PREVIEW}')

    if args.apply:
        backup = args.pak + '.bak_preassembler'
        if not os.path.exists(backup):
            shutil.copy2(args.pak, backup)
            print(f'backed up -> {os.path.basename(backup)}')
        size = save_pak(args.pak, sheets)
        print(f'wrote {args.pak}  ({size:,} bytes)')
        check = load_pak(args.pak)
        for i, (a, b) in enumerate(zip(sheets, check)):
            if a['rects'] != b['rects']:
                sys.exit(f'FAIL: rect table changed for sheet {i}')
        print(f'verified: all {len(check)} rect tables unchanged')
    else:
        print('dry run - pak untouched. Re-run with --apply to write.')


if __name__ == '__main__':
    main()
