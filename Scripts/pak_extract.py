"""Extract sprite sheets out of a .pak, including foreign-variant paks.

Three container layouts are known in the wild. All three share the same sprite
block (20-byte magic, 80 reserved, u32 rect count, 12-byte rects, 4 pad, image
blob) and differ only in the entry table and the image codec:

  ours / Helbreath War   u32 (offset, size) pairs        PNG   RGBA
  Helbreath Korea        u64 offset-only, size implied   BMP   24bpp, chroma key

A fourth wrinkle: Helbreath War's interface.pak has a *whole second pak*
appended, and the outer table's last entry points at it. Entries whose payload
starts with the pak magic are recursed into.

Usage:
    python Scripts/pak_extract.py <pak> [<pak> ...] -o <outdir>
    python Scripts/pak_extract.py <pak> --info          # describe, extract nothing
"""
import argparse
import io
import struct
import sys
from pathlib import Path

try:
	from PIL import Image
except ImportError:
	sys.exit('Pillow required: python -m pip install Pillow')

FILE_HEADER = 20        # "<Pak file header>" + 3 pad
SPRITE_HEADER = 100     # "<Sprite File Header>" + 80 reserved
RECT = 12               # u16 x, y, w, h; i16 pivot_x, pivot_y
PAD = 4

MAGIC_PAK = b'<Pak file header>'
MAGIC_SPRITE = b'<Sprite File Header>'

# Helbreath Korea's 24bpp BMPs use the classic magenta chroma key.
CHROMA_KEY = (255, 0, 255)


class sheet:
	def __init__(self, index, rects, image, codec, path):
		self.index = index
		self.rects = rects
		self.image = image
		self.codec = codec
		self.path = path          # e.g. "2" or "2.0" for a nested pak


def _read_entries(data):
	"""Return (entries, layout_name). Entries are (offset, size) either way.

	A zeroed trailing entry is a real thing — Helbreath War's nested pak declares
	3 entries and fills 2 — so zero entries are kept as (0, 0) and skipped by the
	caller rather than failing the whole table.
	"""
	count, = struct.unpack_from('<I', data, FILE_HEADER)
	if count == 0 or count > 1500:
		raise ValueError(f'implausible sprite count {count}')
	table = FILE_HEADER + 4
	first = table + count * 8

	pairs = [struct.unpack_from('<II', data, table + i * 8) for i in range(count)]
	live = [p for p in pairs if p != (0, 0)]

	# The two layouts are told apart by the high dword of each entry: in the u64
	# offset-only layout it is always zero, so no entry can carry a size.
	if live and any(size for _, size in live):
		if pairs[0][0] == first and all(o + s <= len(data) for o, s in live):
			return pairs, 'u32 (offset, size)'
		raise ValueError('u32 pair table is out of bounds')

	offsets = [struct.unpack_from('<Q', data, table + i * 8)[0] for i in range(count)]
	if (offsets[0] == first and all(offsets[i] < offsets[i + 1] for i in range(count - 1))
			and offsets[-1] < len(data)):
		bounds = offsets + [len(data)]
		return [(offsets[i], bounds[i + 1] - offsets[i]) for i in range(count)], 'u64 offset-only'

	raise ValueError('entry table matches no known layout')


def _decode(blob):
	"""Return (PIL image RGBA, codec name). BMPs get the chroma key punched out."""
	if blob[:8] == b'\x89PNG\r\n\x1a\n':
		return Image.open(io.BytesIO(blob)).convert('RGBA'), 'PNG'
	if blob[:2] == b'BM':
		img = Image.open(io.BytesIO(blob)).convert('RGBA')
		px = img.load()
		w, h = img.size
		for y in range(h):
			for x in range(w):
				if px[x, y][:3] == CHROMA_KEY:
					px[x, y] = (0, 0, 0, 0)
		return img, 'BMP+chroma'
	raise ValueError(f'unrecognised image blob {blob[:8].hex()}')


def read_pak(data, prefix=''):
	"""Yield sheet objects. Recurses into appended paks."""
	entries, layout = _read_entries(data)
	if not prefix:
		print(f'    layout: {layout}, {len(entries)} entries')

	for i, (offset, size) in enumerate(entries):
		tag = f'{prefix}{i}'
		if (offset, size) == (0, 0):
			print(f'    [{tag}] empty entry')
			continue
		blob = data[offset:offset + size]

		if blob[:len(MAGIC_PAK)] == MAGIC_PAK:
			print(f'    [{tag}] nested pak ({size} bytes) - recursing')
			yield from read_pak(blob, prefix=f'{tag}.')
			continue

		if blob[:len(MAGIC_SPRITE)] != MAGIC_SPRITE:
			print(f'    [{tag}] skipped: no sprite magic at {offset:#x}')
			continue

		rect_count, = struct.unpack_from('<I', blob, SPRITE_HEADER)
		rects = [struct.unpack_from('<HHHHhh', blob, SPRITE_HEADER + 4 + j * RECT)
		         for j in range(rect_count)]
		image_at = SPRITE_HEADER + 4 + rect_count * RECT + PAD
		try:
			image, codec = _decode(blob[image_at:])
		except ValueError as e:
			print(f'    [{tag}] skipped: {e}')
			continue
		yield sheet(i, rects, image, codec, tag)


def main():
	ap = argparse.ArgumentParser(description=__doc__,
	                             formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument('paks', nargs='+', type=Path)
	ap.add_argument('-o', '--outdir', type=Path, help='write <stem>/sheet_<n>.png here')
	ap.add_argument('--info', action='store_true', help='describe only, write nothing')
	args = ap.parse_args()

	if not args.info and not args.outdir:
		ap.error('need -o/--outdir unless --info')

	for pak_path in args.paks:
		print(f'\n=== {pak_path.name}  ({pak_path.stat().st_size} bytes)')
		data = pak_path.read_bytes()
		if data[:len(MAGIC_PAK)] != MAGIC_PAK:
			print('    not a pak')
			continue

		dest = None
		if args.outdir:
			dest = args.outdir / pak_path.stem
			dest.mkdir(parents=True, exist_ok=True)

		for s in read_pak(data):
			w, h = s.image.size
			print(f'    [{s.path}] {w}x{h} {s.codec}, {len(s.rects)} frames')
			if dest:
				s.image.save(dest / f'sheet_{s.path}.png')

		if dest:
			print(f'    -> {dest}')


if __name__ == '__main__':
	main()
