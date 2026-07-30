#!/usr/bin/env python3
"""Issue #102 — port the items defined in the original roster that we never carried.

A full diff of `repos/HB382_CENTUU/Files/Item{,2,3}.cfg` (568 unique ids) against
our `items` table found 15 ids the original defines and we do not. #91 already
closed the Bag of Gold trio and the five Balls; this ports nine more and records
why the remaining six are declined.

WHAT LANDS

  743 / 744  Gold Pocket 100k and 1M — the last two rungs of the denomination
             ladder #91 started. Their art is sheet 5 idx 121/122 of
             `item-pack.pak`, the two further bag sprites sitting beside the
             118/119/120 that #91 shipped and the owner confirmed in-game, so
             this is the one group whose sprite mapping is empirically verified.

  700-705    The SangAh set, and 736 SangAhGiantSword. Every piece shares its
  736        cfg sprite pair with an item we already ship, so all seven reuse an
             existing display_id and the atlas does not change for them.

WHAT DOES NOT, AND WHY

  709  DarkKnightFlameberge — REDUNDANT, not missing. The original split weapon
       art by gender (709 M, 727 W). Our port carries only 727, as
       `Dark Knight Flameberge` at gender_requirement 0, and display_id 66's
       atlas entry holds both male and female frames — so one neutral item
       already covers both. The same collapse gave us single entries for Dark
       Knight Rapier, Great Sword and Giant Sword. Adding 709 would duplicate.

  623  GM-Shield — declined for now, owner call. Reuses display_id 39.

  1081-1084  The Magin gems — declined as a data fill because they are a
       feature, not a row. `ItemManager.cpp` carries four effect branches
       commented by Magin name (types 13/14/15/30, reading the crafted
       `special_effect_value2` "purity") and the client has a "Crafting Magins
       completion fix", but NO `builditem_configs` row references any of the
       four, so nothing could mint them. Their art is also not locatable: the
       cfg sprite pair is group 22 idx 6/7/8/9 and we ship group 22 at 0-5 and
       10-13, but the `item-pack.pak` on hand is a DIFFERENT REVISION from
       whatever our atlas was built from — only Gold and Gold Bar match
       pixel-exactly — and its sheet 21 is demonstrably not group 22.

TWO THINGS ON THE RECORD ABOUT THE SangAh ARMOUR

  1. It is male-only in the original. There are no W variants; ids 720-723 are
     food (Songpyon, Ginseng, BeefRibSet, Wine). A faithful port therefore sets
     gender_requirement 1, so female characters cannot equip 700/701/702/704.
     Inventing W ids was rejected as a deviation.
  2. The four armour pieces are effectively pure renames. 700/701/702 are
     BYTE-IDENTICAL in cfg to 706/707/708, and 704 differs from 710 only in
     price — which our `items` stores as 0 for this whole family anyway. They
     will look and behave exactly like gear already in the game. The only
     mechanical difference anywhere in the set is durability 50000 on the two
     weapons, against DarkKnight's 30000.

Every new row is built by copying its in-DB sibling and overriding only the
fields the original's cfg actually differs on, so anything the schema gains
later is inherited rather than defaulted.

Usage:
    python Scripts/migrate_roster_items.py --dry-run   # preview + report
    python Scripts/migrate_roster_items.py --verify    # invariant checks only
    python Scripts/migrate_roster_items.py            # apply

Applying touches, each backed up as `.pre102`:
    Binaries/Server/gamedata.db                     (items only; no drop rows)
    Binaries/Game/sprites/items/item_atlas.pak      (2 frames, ground + pack)
    Binaries/Game/contents/ItemSpriteMetadata.json  (2 entries)
"""

import argparse
import json
import shutil
import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DB = ROOT / "Binaries" / "Server" / "gamedata.db"
OUT = ROOT / "Scripts" / "output"

ATLAS = ROOT / "Binaries" / "Game" / "sprites" / "items" / "item_atlas.pak"
META = ROOT / "Binaries" / "Game" / "contents" / "ItemSpriteMetadata.json"
ART = ROOT / "Sprite Work" / "interface_hb382"
ART_SHEET = 5                       # group 6 -> sheet 5; the one verified mapping
SHEET_GROUND, SHEET_PACK = 1, 2     # per item_atlas::ground / ::pack

NAME_LIMIT = 42                     # hb::shared::limits::ItemNameLen

# Items that reuse an existing display_id: no atlas work at all.
#   id -> (name, model_item_id, {column: override})
# `model_item_id` is the in-DB sibling whose row shape and art these inherit.
REUSE_ART = {
	700: ("Sang Ah Hauberk (M)",    706, {}),
	701: ("Sang Ah Full-Helm (M)",  707, {}),
	702: ("Sang Ah Leggings (M)",   708, {}),
	703: ("Sang Ah Flameberge",     727, {"durability": 50000}),
	704: ("Sang Ah Plate Mail (M)", 710, {}),
	705: ("Sang Ah Jewel",          350, {}),
	736: ("Sang Ah Giant Sword",    737, {"durability": 50000}),
}

# Items needing a new atlas frame.
#   id -> (name, model_item_id, source sprite index in sheet 5, {overrides})
# Weight is the cfg value x10, the scaling `items` already uses.
NEW_ART = {
	743: ("Bag of Gold (100000)",  740, 121, {"weight": 30000}),
	744: ("Bag of Gold (1000000)", 741, 122, {"weight": 40000}),
}

# Recorded so the audit result lives with the code, not only in the ticket.
DECLINED = {
	709:  "redundant - 727 'Dark Knight Flameberge' is already gender-neutral",
	623:  "declined for now (owner call); would reuse display_id 39",
	1081: "needs art + a builditem recipe; see module docstring",
	1082: "needs art + a builditem recipe",
	1083: "needs art + a builditem recipe",
	1084: "needs art + a builditem recipe",
}


class Report:
	def __init__(self):
		self.lines = []

	def __call__(self, text=""):
		self.lines.append(text)
		print(text)

	def write(self, path):
		OUT.mkdir(parents=True, exist_ok=True)
		path.write_text("\n".join(self.lines) + "\n", encoding="utf-8")


def load_paklib():
	"""paklib lives with the sprite tooling, not on the default path."""
	sys.path.insert(0, str(ROOT / "Tools" / "DisplayIDManager"))
	import paklib
	return paklib


def plan(db, r):
	db.row_factory = sqlite3.Row
	present = {row["item_id"]: row["name"] for row in db.execute("select item_id, name from items")}
	next_display = db.execute("select max(display_id) from items").fetchone()[0] + 1

	r("=" * 78)
	r("#102 ROSTER ITEMS — PLAN")
	r(f"generated {datetime.now(timezone.utc).isoformat(timespec='seconds')}")
	r("=" * 78)

	reuse, fresh = [], []

	r("")
	r("A. REUSE AN EXISTING display_id (no atlas change)")
	r(f"   {'id':>5}  {'name':24} {'model':>6}  {'display':>7}  overrides")
	for iid, (name, model, over) in sorted(REUSE_ART.items()):
		if iid in present:
			r(f"   {iid:>5}  ALREADY EXISTS as {present[iid]!r} — skipped")
			continue
		row = db.execute("select * from items where item_id = ?", (model,)).fetchone()
		if row is None:
			r(f"   {iid:>5}  MODEL {model} NOT IN items — cannot port")
			continue
		reuse.append((iid, name, model, over, row["display_id"]))
		r(f"   {iid:>5}  {name:24} {model:>6}  {row['display_id']:>7}  {over or '-'}")

	r("")
	r("B. NEW ATLAS FRAME (group 6 / sheet 5 — the verified mapping)")
	r(f"   {'id':>5}  {'name':24} {'model':>6}  {'sprite':>6}  {'display':>7}  overrides")
	for iid, (name, model, sprite, over) in sorted(NEW_ART.items()):
		if iid in present:
			r(f"   {iid:>5}  ALREADY EXISTS as {present[iid]!r} — skipped")
			continue
		row = db.execute("select * from items where item_id = ?", (model,)).fetchone()
		if row is None:
			r(f"   {iid:>5}  MODEL {model} NOT IN items — cannot port")
			continue
		fresh.append((iid, name, model, sprite, over, next_display))
		r(f"   {iid:>5}  {name:24} {model:>6}  {sprite:>6}  {next_display:>7}  {over or '-'}")
		next_display += 1

	r("")
	r("C. DECLINED (recorded, not ported)")
	for iid, why in sorted(DECLINED.items()):
		r(f"   {iid:>5}  {why}")

	r("")
	r(f"   => {len(reuse)} reusing art, {len(fresh)} needing a frame, {len(DECLINED)} declined")
	return reuse, fresh


def verify(db, changes, r):
	reuse, fresh = changes
	ok = True
	r("")
	r("VERIFY")

	used_display = {d for d, in db.execute("select display_id from items")}
	all_new = [(i, n) for i, n, *_ in reuse] + [(i, n) for i, n, *_ in fresh]
	for iid, name in all_new:
		if db.execute("select 1 from items where item_id = ?", (iid,)).fetchone():
			r(f"   FAIL item_id {iid} already exists"); ok = False
		if len(name) > NAME_LIMIT:
			r(f"   FAIL name {name!r} exceeds ItemNameLen {NAME_LIMIT}"); ok = False
	if len({i for i, _ in all_new}) != len(all_new):
		r("   FAIL duplicate item_id in the plan"); ok = False
	r(f"   OK  {len(all_new)} ids free, names within {NAME_LIMIT}, no duplicates")

	# a reused display_id must already exist; a fresh one must not
	for iid, name, model, over, disp in reuse:
		if disp not in used_display:
			r(f"   FAIL item {iid} reuses display_id {disp}, which no item has"); ok = False
	for iid, name, model, sprite, over, disp in fresh:
		if disp in used_display:
			r(f"   FAIL item {iid} claims display_id {disp}, already taken"); ok = False
		used_display.add(disp)
	r(f"   OK  {len(reuse)} reused display_ids exist, {len(fresh)} fresh ones are free")

	# overrides must name real columns
	cols = {c[1] for c in db.execute("pragma table_info(items)")}
	for iid, _, _, over, *_ in reuse:
		for k in over:
			if k not in cols:
				r(f"   FAIL item {iid} overrides unknown column {k!r}"); ok = False
	for iid, _, _, _, over, _ in fresh:
		for k in over:
			if k not in cols:
				r(f"   FAIL item {iid} overrides unknown column {k!r}"); ok = False
	r("   OK  every override names a real items column")

	# the source art must carry the sprite indexes we claim, in both paks
	if fresh:
		try:
			paklib = load_paklib()
			for label, pak in (("pack", "item-pack.pak"), ("ground", "item-ground.pak")):
				sheet = paklib.PAKFile.read(ART / pak).sprites[ART_SHEET]
				for iid, _, _, sprite, _, _ in fresh:
					if sprite >= len(sheet.rectangles):
						r(f"   FAIL {label} sheet {ART_SHEET} has no idx {sprite} (item {iid})")
						ok = False
			r(f"   OK  source art carries all {len(fresh)} sprite indexes in both paks")
		except Exception as exc:
			r(f"   FAIL could not read source art: {exc}"); ok = False

	r(f"   => {'ALL CHECKS PASSED' if ok else 'CHECKS FAILED'}")
	return ok


def apply_db(db, changes, r):
	reuse, fresh = changes
	cur = db.cursor()
	made = 0
	for iid, name, model, over, disp in reuse:
		row = dict(db.execute("select * from items where item_id = ?", (model,)).fetchone())
		row.update(item_id=iid, name=name, **over)
		cur.execute(f"insert into items ({', '.join(row)}) "
		            f"values ({', '.join('?' * len(row))})", tuple(row.values()))
		made += 1
	for iid, name, model, sprite, over, disp in fresh:
		row = dict(db.execute("select * from items where item_id = ?", (model,)).fetchone())
		row.update(item_id=iid, name=name, display_id=disp, **over)
		cur.execute(f"insert into items ({', '.join(row)}) "
		            f"values ({', '.join('?' * len(row))})", tuple(row.values()))
		made += 1
	db.commit()
	r(f"   applied: {made} items ({len(reuse)} reusing art, {len(fresh)} with a new frame)")


def apply_art(fresh, r):
	"""Append the new icons to the atlas ground+pack sheets and register them."""
	paklib = load_paklib()
	from PIL import Image

	atlas = paklib.PAKFile.read(ATLAS)
	# One metadata frame index addresses BOTH sheets, so they must stay equal length.
	counts = {i: len(atlas.sprites[i].rectangles) for i in (SHEET_GROUND, SHEET_PACK)}
	if len(set(counts.values())) != 1:
		raise SystemExit(f"atlas ground/pack sheets differ in length: {counts} — "
		                 "one shared frame index can no longer address both")
	base_frame = next(iter(counts.values()))

	src = {SHEET_GROUND: paklib.PAKFile.read(ART / "item-ground.pak").sprites[ART_SHEET],
	       SHEET_PACK: paklib.PAKFile.read(ART / "item-pack.pak").sprites[ART_SHEET]}

	frame_of = {}
	for sheet_idx, src_sheet in src.items():
		dst = atlas.sprites[sheet_idx]
		img = dst.get_image().convert("RGBA")
		src_img = src_sheet.get_image().convert("RGBA")
		crops = []
		for iid, _, _, sprite, _, _ in fresh:
			sr = src_sheet.rectangles[sprite]
			crops.append((iid, sr,
			              src_img.crop((sr.x, sr.y, sr.x + sr.width, sr.y + sr.height))))
		strip_h = max(c.height for *_, c in crops)
		strip_w = sum(c.width for *_, c in crops)
		grown = Image.new("RGBA", (max(img.width, strip_w), img.height + strip_h), (0, 0, 0, 0))
		grown.paste(img, (0, 0))
		x = 0
		for offset, (iid, sr, crop) in enumerate(crops):
			grown.paste(crop, (x, img.height), crop)
			# source pivots are already in the atlas convention
			dst.rectangles.append(paklib.SpriteRectangle(
				x=x, y=img.height, width=crop.width, height=crop.height,
				pivot_x=sr.pivot_x, pivot_y=sr.pivot_y))
			frame_of[iid] = base_frame + offset
			x += crop.width
		atlas.sprites[sheet_idx] = paklib.Sprite.from_image(grown, dst.rectangles)
		r(f"   sheet {sheet_idx}: +{len(crops)} frames at {base_frame}"
		  f"..{base_frame + len(crops) - 1}, image {img.size} -> {grown.size}")

	atlas.write(ATLAS)

	meta = json.loads(META.read_text(encoding="utf-8"))
	for iid, _, _, _, _, disp in fresh:
		meta.append({"pak_file": None, "id": disp,
		             "inventory_frame_index": frame_of[iid],
		             "ground_frame_index": frame_of[iid]})
	META.write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
	r(f"   metadata: +{len(fresh)} entries, {len(meta)} total")


def check_atlas_additive(backup, r):
	"""Every frame that existed before must survive rect- and pixel-identical."""
	paklib = load_paklib()
	old = paklib.PAKFile.read(backup)
	new = paklib.PAKFile.read(ATLAS)
	bad = checked = 0
	for si in range(len(old.sprites)):
		o, n = old.sprites[si], new.sprites[si]
		oi, ni = o.get_image().convert("RGBA"), n.get_image().convert("RGBA")
		for fi, orect in enumerate(o.rectangles):
			nrect = n.rectangles[fi]
			checked += 1
			same_rect = (orect.x, orect.y, orect.width, orect.height,
			             orect.pivot_x, orect.pivot_y) == \
			            (nrect.x, nrect.y, nrect.width, nrect.height,
			             nrect.pivot_x, nrect.pivot_y)
			oc = oi.crop((orect.x, orect.y, orect.x + orect.width, orect.y + orect.height))
			nc = ni.crop((nrect.x, nrect.y, nrect.x + nrect.width, nrect.y + nrect.height))
			if not same_rect or oc.tobytes() != nc.tobytes():
				r(f"   CHANGED sheet {si} frame {fi}"); bad += 1
	r(f"   atlas additivity: {checked} pre-existing frames checked, {bad} changed"
	  f" ({'OK' if bad == 0 else 'FAIL'})")
	return bad == 0


def main():
	ap = argparse.ArgumentParser(description=__doc__,
	                            formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--db", type=Path, default=DEFAULT_DB)
	ap.add_argument("--dry-run", action="store_true", help="preview only; touch nothing")
	ap.add_argument("--verify", action="store_true", help="run the invariant checks only")
	args = ap.parse_args()

	if not args.db.exists():
		print(f"no such database: {args.db}", file=sys.stderr)
		return 2

	r = Report()
	db = sqlite3.connect(args.db)
	changes = plan(db, r)
	ok = verify(db, changes, r)

	if args.verify:
		r.write(OUT / "roster_items.log")
		return 0 if ok else 1
	if not ok:
		r("")
		r("REFUSING TO APPLY — verification failed")
		r.write(OUT / "roster_items.log")
		return 1
	if args.dry_run:
		r("")
		r("DRY RUN — nothing written")
		r.write(OUT / "roster_items.log")
		return 0

	r("")
	r("APPLY")
	shutil.copy2(args.db, args.db.with_suffix(args.db.suffix + ".pre102"))
	r(f"   db backup: {args.db.name}.pre102")
	apply_db(db, changes, r)
	db.close()

	_, fresh = changes
	if fresh:
		atlas_backup = ATLAS.with_suffix(ATLAS.suffix + ".pre102")
		shutil.copy2(ATLAS, atlas_backup)
		shutil.copy2(META, META.with_suffix(META.suffix + ".pre102"))
		apply_art(fresh, r)
		if not check_atlas_additive(atlas_backup, r):
			r("   WARNING: the atlas is NOT purely additive — restore from .pre102")
			r.write(OUT / "roster_items.log")
			return 1
	else:
		r("   art: nothing to append")

	r.write(OUT / "roster_items.log")
	return 0


if __name__ == "__main__":
	sys.exit(main())
