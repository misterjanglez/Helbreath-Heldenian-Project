"""Extract sprite sheets out of a .pak or .opk, including foreign-variant paks.

Container layouts known in the wild. The .pak ones share the same sprite block
(20-byte magic, 80 reserved, u32 rect count, 12-byte rects, 4 pad, image blob)
and differ only in the entry table and the image codec:

  ours / Helbreath War   u32 (offset, size) pairs        PNG   RGBA
  Helbreath Korea        u64 offset-only, size implied   BMP   24bpp, magenta key

Some Korean repacks blank both text magics with zeros to keep editors out —
the structure underneath is unchanged, so an all-zero magic is accepted
wherever a text magic is expected (with the entry table doing the vetting).

Helbreath Olympia's .opk is the same idea rebuilt without the text magics:

  u32 first_image_offset, u32 always-1, u32 group_count, then per group
  { u8 0, u32 offset, u32 size, u32 frame_count, frame_count 12-byte rects },
  then the BMP blobs (8/24bpp, green chroma key) back to back. A size==0
  group is an empty placeholder whose offset field holds uninitialized
  garbage; bytes past the last blob are dead data from stale repacks.

A further wrinkle: Helbreath War's interface.pak has a *whole second pak*
appended, and the outer table's last entry points at it. Entries whose payload
starts with the pak magic are recursed into.

Usage:
    python Scripts/pak_extract.py <pak|opk> [<pak|opk> ...] -o <outdir>
    python Scripts/pak_extract.py <pak> --info          # describe, extract nothing
    python Scripts/pak_extract.py <pak> --frame 4:4     # one frame, cropped, as PNG
"""
import argparse
import io
import struct
import sys
from pathlib import Path

try:
	from PIL import Image, ImageChops
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
# Helbreath Olympia's .opk BMPs key on pure green instead.
CHROMA_KEY_OPK = (0, 255, 0)

OPK_HEADER = 12         # u32 first_image_offset, u32 always-1, u32 group_count
OPK_GROUP = 13          # u8 0, u32 offset, u32 size, u32 frame_count

MAX_SHEETS = 1500       # plausibility cap on any layout's sheet/group count


class sheet:
	def __init__(self, index, rects, image, codec, path):
		self.index = index
		self.rects = rects
		self.image = image
		self.codec = codec
		self.path = path          # e.g. "2" or "2.0" for a nested pak


def _magic_ok(buf, magic):
	"""The expected text magic, or the all-zero blank some Korean repacks use."""
	got = buf[:len(magic)]
	return got == magic or got == bytes(len(magic))


def _read_entries(data):
	"""Return (entries, layout_name). Entries are (offset, size) either way.

	A zeroed trailing entry is a real thing — Helbreath War's nested pak declares
	3 entries and fills 2 — so zero entries are kept as (0, 0) and skipped by the
	caller rather than failing the whole table.
	"""
	count, = struct.unpack_from('<I', data, FILE_HEADER)
	if count == 0 or count > MAX_SHEETS:
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


def _read_rects(buf, offset, count):
	"""Decode `count` consecutive 12-byte frame rects."""
	return [struct.unpack_from('<HHHHhh', buf, offset + j * RECT) for j in range(count)]


def _decode(blob, chroma):
	"""Return (PIL image RGBA, codec name). BMPs get their color key punched out.

	8bpp BMPs are keyed on palette *index* 0 whatever its color — the original
	engine's DirectDraw convention (dwColorSpaceLowValue = 0) — so a sheet with
	a white index 0 still keys correctly. Truecolor BMPs key on `chroma`;
	keyed pixels become fully-transparent black.
	"""
	if blob[:8] == b'\x89PNG\r\n\x1a\n':
		return Image.open(io.BytesIO(blob)).convert('RGBA'), 'PNG'
	if blob[:2] == b'BM':
		img = Image.open(io.BytesIO(blob))
		if img.mode == 'P':
			alpha = img.point(lambda i: 255 if i else 0, 'L')
			rgba = img.convert('RGBA')
			rgba.putalpha(alpha)
			return rgba, 'BMP+index0'
		diff = ImageChops.difference(img.convert('RGB'), Image.new('RGB', img.size, chroma))
		r, g, b = diff.split()
		keep = ImageChops.add(ImageChops.add(r, g), b).point(lambda v: 255 if v else 0)
		rgba = Image.composite(img.convert('RGBA'),
		                       Image.new('RGBA', img.size, (0, 0, 0, 0)), keep)
		return rgba, 'BMP+chroma'
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

		if not _magic_ok(blob, MAGIC_SPRITE):
			print(f'    [{tag}] skipped: no sprite magic at {offset:#x}')
			continue

		rect_count, = struct.unpack_from('<I', blob, SPRITE_HEADER)
		rects = _read_rects(blob, SPRITE_HEADER + 4, rect_count)
		image_at = SPRITE_HEADER + 4 + rect_count * RECT + PAD
		try:
			image, codec = _decode(blob[image_at:], CHROMA_KEY)
		except ValueError as e:
			print(f'    [{tag}] skipped: {e}')
			continue
		yield sheet(i, rects, image, codec, tag)


def _parse_opk(data):
	"""Walk the .opk group table; return [(offset, size, rects)] per group, or
	None when the walk doesn't land exactly on first_image_offset — which
	doubles as the structural sniff that tells .opk from arbitrary data."""
	if len(data) < OPK_HEADER + OPK_GROUP:
		return None
	first_off, always_one, group_count = struct.unpack_from('<III', data, 0)
	if (always_one != 1 or not 0 < group_count <= MAX_SHEETS
			or not OPK_HEADER < first_off <= len(data)):
		return None
	groups = []
	pos = OPK_HEADER
	for _ in range(group_count):
		if pos + OPK_GROUP > first_off:
			return None
		offset, size, frames = struct.unpack_from('<III', data, pos + 1)
		end = pos + OPK_GROUP + frames * RECT
		if end > first_off:
			return None
		groups.append((offset, size, _read_rects(data, pos + OPK_GROUP, frames)))
		pos = end
	return groups if pos == first_off else None


def read_opk(data, groups):
	"""Yield sheet objects from a _parse_opk'd Helbreath Olympia .opk."""
	print(f'    layout: opk, {len(groups)} groups')
	for i, (offset, size, rects) in enumerate(groups):
		if size == 0:
			print(f'    [{i}] empty entry')
			continue
		try:
			image, codec = _decode(data[offset:offset + size], CHROMA_KEY_OPK)
		except ValueError as e:
			print(f'    [{i}] skipped: {e}')
			continue
		yield sheet(i, rects, image, codec, str(i))


def read_container(data):
	"""Return the right reader's sheet generator, or raise ValueError.

	Detection happens here, eagerly, so an unsupported file fails at the call
	rather than on first iteration.
	"""
	if data[:len(MAGIC_PAK)] == MAGIC_PAK:
		return read_pak(data)
	if data[:len(MAGIC_PAK)] == bytes(len(MAGIC_PAK)):
		try:
			_read_entries(data)
			return read_pak(data)
		except ValueError:
			pass          # zeroed leading bytes but no pak table - fall through
	groups = _parse_opk(data)
	if groups is not None:
		return read_opk(data, groups)
	raise ValueError('not a pak or opk')


def _bleed(image):
	"""Fill RGB under fully-transparent pixels from the nearest opaque colors.

	The pak sheets keep white RGB under alpha 0; viewers that scale with linear
	filtering (Blender reference images, most editors) blend that white into the
	silhouette as a halo. Alpha is untouched, so composited output is identical.
	"""
	px = image.load()
	w, h = image.size
	filled = [[px[x, y][3] > 0 for x in range(w)] for y in range(h)]
	frontier = [(x, y) for y in range(h) for x in range(w) if filled[y][x]]
	while frontier:
		ring = {}
		for fx, fy in frontier:
			for x, y in ((fx - 1, fy), (fx + 1, fy), (fx, fy - 1), (fx, fy + 1)):
				if not (0 <= x < w and 0 <= y < h) or filled[y][x] or (x, y) in ring:
					continue
				neigh = [px[nx, ny][:3] for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1))
				         if 0 <= nx < w and 0 <= ny < h and filled[ny][nx]]
				ring[(x, y)] = tuple(sum(c) // len(neigh) for c in zip(*neigh))
		for (x, y), rgb in ring.items():
			px[x, y] = (*rgb, 0)
			filled[y][x] = True
		frontier = list(ring)
	return image


def extract_frame(sheets, sheet_tag, frame_idx):
	"""Return (cropped RGBA image, rect) for one frame of one sheet."""
	for s in sheets:
		if s.path != sheet_tag:
			continue
		if not 0 <= frame_idx < len(s.rects):
			raise SystemExit(f'sheet {sheet_tag} has frames 0-{len(s.rects) - 1}, not {frame_idx}')
		x, y, w, h, _, _ = s.rects[frame_idx]
		return s.image.crop((x, y, x + w, y + h)), s.rects[frame_idx]
	raise SystemExit(f'no sheet {sheet_tag!r} in this pak')


def main():
	ap = argparse.ArgumentParser(description=__doc__,
	                             formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument('paks', nargs='+', type=Path)
	ap.add_argument('-o', '--outdir', type=Path, help='write <stem>/sheet_<n>.png here')
	ap.add_argument('--info', action='store_true', help='describe only, write nothing')
	ap.add_argument('--frame', metavar='SHEET:FRAME',
	                help='extract one frame as a cropped PNG (0-based indices as printed by --info)')
	ap.add_argument('--out', type=Path,
	                help='output file for --frame (default Scripts/output/<stem>_s<sheet>_f<frame>.png)')
	args = ap.parse_args()

	if args.frame:
		if len(args.paks) > 1:
			ap.error('--frame works on a single pak')
		sheet_tag, sep, frame_str = args.frame.partition(':')
		if not sep or not frame_str.isdigit():
			ap.error('--frame wants SHEET:FRAME, e.g. 4:4')
		frame_idx = int(frame_str)
	elif not args.info and not args.outdir:
		ap.error('need -o/--outdir unless --info or --frame')

	for pak_path in args.paks:
		print(f'\n=== {pak_path.name}  ({pak_path.stat().st_size} bytes)')
		data = pak_path.read_bytes()
		try:
			sheets = read_container(data)
		except ValueError as e:
			print(f'    {e}')
			continue

		if args.frame:
			image, rect = extract_frame(sheets, sheet_tag, frame_idx)
			image = _bleed(image)
			out = args.out or (Path(__file__).resolve().parent / 'output'
			                   / f'{pak_path.stem}_s{sheet_tag}_f{frame_idx}.png')
			out.parent.mkdir(parents=True, exist_ok=True)
			image.save(out)
			x, y, w, h, pivot_x, pivot_y = rect
			print(f'    [{sheet_tag}:{frame_idx}] {w}x{h} at ({x},{y}), pivot ({pivot_x},{pivot_y})')
			print(f'    -> {out}')
			continue

		dest = None
		if args.outdir:
			dest = args.outdir / pak_path.stem
			dest.mkdir(parents=True, exist_ok=True)

		for s in sheets:
			w, h = s.image.size
			print(f'    [{s.path}] {w}x{h} {s.codec}, {len(s.rects)} frames')
			if dest:
				s.image.save(dest / f'sheet_{s.path}.png')

		if dest:
			print(f'    -> {dest}')


if __name__ == '__main__':
	main()
