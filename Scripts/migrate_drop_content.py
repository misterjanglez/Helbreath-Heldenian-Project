#!/usr/bin/env python3
"""Item Tiers #91 — drop CONTENT curation, on top of #73's absolute-ppb encoding.

#73 changed how rarity is *encoded* and its gate was that `dropodds` reported
identical rates before and after. This script is the opposite: every change here
is a deliberate rate change, quoted as a before/after "1 in N".

Design contract: docs/adr/0005-absolute-drop-chances.md ("Content curation is not
part of this change"), GitHub issue #91.

WHAT THE AUDIT FOUND, AND WHAT THIS SCRIPT DOES

Verified against the original in `repos/HB382_CENTUU/HGServer/Game.cpp`:
  * `NpcDeadItemGenerator` @47298 — the first drop  = our stage 1
  * `DeleteNpc`            @26399 — the second drop = our stage 2 (on corpse decay)
  * `bGetItemNameWhenDeleteNpc` @53889 — the unique-roster arm of stage 2
  * `Files/Item3.cfg:110-112`    — the three GoldPocket definitions
  * `Files/Item.cfg:32-36`       — the five Ball definitions

Two of #91's premises were wrong and are corrected rather than acted on:
  1. Body parts do NOT come from a separate butchering mechanic. `DeleteNpc` IS
     the second drop; per NPC type it reads
        switch (iDice(1,K)) { case 1..K-1: <body part>; case K: <unique roster> }
     so body parts and uniques are two arms of one uniform draw. Body parts are
     faithful stage-2 content and STAY. The 216x Medusa gap #73 cited was an
     artifact of bundling two independent probability spaces into one weighted
     table, and ppb already cured it.
  2. Magic manuals are NOT boss-only. All six sit in ordinary rosters (380 Liche
     + Beholder; 381/382 Demon + Gagoyle + Barlog + Ettin; 852/853 WereWolf /
     Stalker + Liche + Ettin; 857 WereWolf / Stalker). Protection necklaces
     likewise. Both STAY. Nothing to do; recorded for the ticket.

Confirmed faithful, no change:
  * Stones 650/656/657 and Ancient Pieces 868-871 in stage 1 — all seven are
    `case 8` of the first drop's consumable branch.
  * Gold 90 as an ordinary stage-1 row, and stage-1 deduplication (98 -> 23
    tables) — both already landed with #73.

The four changes this script applies:

  A. NEW ITEMS (8). Bag of Gold 740/741/742 and Balls 651-655 were never ported.
     `Item.cfg` field 14 is a sprite index into the very sheet we still ship as
     source art, which is what makes the art mapping authoritative rather than a
     guess: 740/741/742 -> 118/119/120 and 651-655 -> 123/124/125/126/127 of
     sheet 5 in item-pack.pak / item-ground.pak.
     Both families keep the original's `item_type` 0 / sub_type 0, matching their
     own siblings 650/656/657 in our `items` today. `item_sub_type::currency`
     ("Gold, gold sacks") and `component` ("ores, gems, stones") both exist and
     would be the modern classification, but nothing in the server reads
     item_sub_type at all, while `item_type::material` DOES carry a crafting
     deplete-risk (CraftingManager.cpp:402). Reclassifying is a whole-table pass,
     not 8 rows, so these land beside their siblings and the pass stays a
     separate concern.
     Bag of Gold gets NO drop rows here — #89 owns the boss scatter that
     consumes them. This script only unblocks it.

  B. SUPER GREEN POTION 391. Absent from every drop table. It is `case 8`
     sub-case 1, a sibling of the three stones at equal weight, so it lands at
     exactly a stone's ppb in each table that carries the stones.

  C. BALLS 651-655 into stage 1. `case 8` sub-case 10 is `switch (iDice(1,5))`
     over the five balls, so each ball is 1/5 of a stone: ppb = stone_ppb // 5.

  D. BODY-PART RATES aligned to the original exactly: 1/(K x N) per kill, from
     ORIGINAL_BODY_PARTS below. 46 rows re-rated, 7 rows added (Hellbound
     199-204 and Unicorn Antenna 545 — all seven already exist in `items` with
     valid display_ids; they were simply never given drop rows, and s2_Hellbound
     carries the original's roster arm with none of its six body-part arms).
     This moves rates BOTH ways. Demon and Unicorn parts are 3-73x too generous
     today (Unicorn Heart 1 in 204 against the original's 1 in 15,000) and get
     TIGHTENED; the low-tier parts are ~1.5x too stingy and get loosened. The
     ratchet law makes tightening a pre-launch-only move and the world still
     wipes, so this is the window.

B and C target the 22 real monster tables 30001-30022 and deliberately NOT the
boilerplate 30023, which serves 49 catapults, guard towers, mana collectors and
crusade summons rather than monsters. They are not in the original's first-drop
path at all; the stones on them are already a port artifact and this script does
not compound it.

Usage:
    python Scripts/migrate_drop_content.py --dry-run   # preview + full report
    python Scripts/migrate_drop_content.py --verify    # invariant checks only
    python Scripts/migrate_drop_content.py             # apply

Applying touches three artifacts, all reversible from the report:
    Binaries/Server/gamedata.db                  (items + drop_entries)
    Binaries/Game/sprites/items/item_atlas.pak   (8 frames appended, 2 sheets)
    Binaries/Game/contents/ItemSpriteMetadata.json (8 entries appended)
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
ART_PACK = ART / "item-pack.pak"
ART_GROUND = ART / "item-ground.pak"
ART_SHEET = 5                       # sheet 5 of both paks carries the item icons

PPB = 1_000_000_000
STONE_IDS = (650, 656, 657)         # the three case-8 stones; 391 matches their ppb
BALL_IDS = (651, 652, 653, 654, 655)
BAG_IDS = (740, 741, 742)
SUPER_GREEN_POTION = 391
BALL_DIVISOR = 5                    # case 8 sub-case 10 is iDice(1,5) over the balls
MONSTER_S1_LO, MONSTER_S1_HI = 30001, 30022   # 30023 is structure boilerplate

# atlas sheet indexes, per item_atlas::equip/ground/pack in ItemSpriteMetadata.h
SHEET_GROUND, SHEET_PACK = 1, 2

# item_id -> (source sprite index in sheet 5, name, durability, weight)
# Sprite indexes and durability/weight are the original's own, from Item.cfg /
# Item3.cfg. Weight is the cfg value x10, the scaling our `items` already uses
# (Stone of Xelima cfg 4000 -> 40000, Super Green Potion cfg 30 -> 300).
NEW_ITEMS = {
	651: (123, "Green Ball",           3, 40_000),
	652: (124, "Red Ball",             3, 40_000),
	653: (125, "Yellow Ball",          3, 40_000),
	654: (126, "Blue Ball",            3, 40_000),
	655: (127, "Pearl Ball",           3, 40_000),
	740: (118, "Bag of Gold (5000)",   1,  5_000),
	741: (119, "Bag of Gold (10000)",  1, 10_000),
	742: (120, "Bag of Gold (50000)",  1, 20_000),
}

# item_id -> (npc_type, K, N); per-kill rate is 1/(K x N).
# K is the arm count of `switch (iDice(1,K))` in DeleteNpc for that npc type
# (K=1 where there is no enclosing switch), N the inner `iDice(1,N) == x` gate.
# Transcribed from repos/HB382_CENTUU/HGServer/Game.cpp:26459-26630.
ORIGINAL_BODY_PARTS = {
	188: (22, 5, 15),      # Snake Meat            1 in 75
	189: (22, 5, 16),      # Snake Skin            1 in 80
	190: (22, 5, 16),      # Snake Teeth           1 in 80
	191: (22, 5, 17),      # Snake Tongue          1 in 85
	192: (16, 3, 9),       # Ant Leg               1 in 27
	193: (16, 3, 10),      # Ant Antenna           1 in 30
	194: (13, 6, 36),      # Cyclops Eye           1 in 216
	195: (13, 6, 40),      # Cyclops Hand Edge     1 in 240
	196: (13, 6, 30),      # Cyclops Heart         1 in 180
	197: (13, 6, 22),      # Cyclops Meat          1 in 132
	198: (13, 6, 40),      # Cyclops Leather       1 in 240
	199: (27, 7, 40),      # Helhound Heart        1 in 280   (absent today)
	200: (27, 7, 38),      # Helhound Leather      1 in 266   (absent today)
	201: (27, 7, 38),      # Helhound Tail         1 in 266   (absent today)
	202: (27, 7, 36),      # Helhound Teeth        1 in 252   (absent today)
	203: (27, 7, 36),      # Helhound Claw         1 in 252   (absent today)
	204: (27, 7, 50),      # Helhound Tongue       1 in 350   (absent today)
	205: (23, 2, 30),      # Lump of Clay          1 in 60
	206: (14, 4, 11),      # Orc Meat              1 in 44
	207: (14, 4, 20),      # Orc Leather           1 in 80
	208: (14, 4, 21),      # Orc Teeth             1 in 84
	209: (29, 7, 20),      # Ogre Hair             1 in 140
	210: (29, 7, 22),      # Ogre Heart            1 in 154
	211: (29, 7, 25),      # Ogre Meat             1 in 175
	212: (29, 7, 25),      # Ogre Leather          1 in 175
	213: (29, 7, 28),      # Ogre Teeth            1 in 196
	214: (29, 7, 28),      # Ogre Claw             1 in 196
	215: (17, 5, 50),      # Scorpion Pincers      1 in 250
	216: (17, 5, 20),      # Scorpion Meat         1 in 100
	217: (17, 5, 50),      # Scorpion Sting        1 in 250
	218: (17, 5, 40),      # Scorpion Skin         1 in 200
	219: (11, 2, 20),      # Skeleton Bones        1 in 40
	220: (10, 1, 25),      # Slime Jelly           1 in 25
	221: (12, 2, 30),      # Stone Golem Piece     1 in 60
	222: (28, 5, 35),      # Troll Heart           1 in 175
	223: (28, 5, 23),      # Troll Meat            1 in 115
	224: (28, 5, 25),      # Troll Leather         1 in 125
	225: (28, 5, 27),      # Troll Claw            1 in 135
	540: (31, 5, 300),     # Demon Eye             1 in 1,500
	541: (31, 5, 400),     # Demon Heart           1 in 2,000
	542: (31, 5, 1000),    # Demon Meat            1 in 5,000
	543: (31, 5, 200),     # Demon Leather         1 in 1,000
	544: (32, 5, 3000),    # Unicorn Heart         1 in 15,000
	545: (32, 5, 500),     # Unicorn Antenna       1 in 2,500  (absent today)
	546: (32, 5, 100),     # Unicorn Meat          1 in 500
	547: (32, 5, 200),     # Unicorn Leather       1 in 1,000
	548: (33, 8, 28),      # Werewolf Heart        1 in 224
	549: (33, 8, 38),      # Werewolf Nail         1 in 304
	550: (33, 8, 25),      # Werewolf Meat         1 in 200
	551: (33, 8, 30),      # Werewolf Tail         1 in 240
	552: (33, 8, 28),      # Werewolf Teeth        1 in 224
	553: (33, 8, 35),      # Werewolf Leather      1 in 280
	554: (33, 8, 28),      # Werewolf Claw         1 in 224
}


def one_in(ppb):
	return PPB / ppb if ppb else float("inf")


def rarity_span(rows):
	"""'N rows, ppb lo..hi (1 in hi..lo)' for a set of (table, item, ppb) tuples."""
	if not rows:
		return "nothing to do"
	lo = min(ppb for *_, ppb in rows)
	hi = max(ppb for *_, ppb in rows)
	return (f"{len(rows)} rows, ppb {lo:,}..{hi:,} "
	        f"(1 in {one_in(hi):,.0f}..{one_in(lo):,.0f})")


def load_paklib():
	"""paklib lives with the sprite tooling, not on the default path."""
	sys.path.insert(0, str(ROOT / "Tools" / "DisplayIDManager"))
	import paklib
	return paklib


class Report:
	def __init__(self):
		self.lines = []

	def __call__(self, text=""):
		self.lines.append(text)
		print(text)

	def write(self, path):
		OUT.mkdir(parents=True, exist_ok=True)
		path.write_text("\n".join(self.lines) + "\n", encoding="utf-8")


def load_state(db):
	db.row_factory = sqlite3.Row
	items = {r["item_id"]: r["name"] for r in db.execute("select item_id, name from items")}
	# stage-1 monster tables and the per-table stone ppb that 391 and the balls key off
	stone_ppb = {}
	for tid, in db.execute(
		"select drop_table_id from drop_tables where stage = 1 "
		"and drop_table_id between ? and ? order by drop_table_id",
		(MONSTER_S1_LO, MONSTER_S1_HI)):
		row = db.execute(
			"select drop_chance_ppb from drop_entries where drop_table_id = ? and item_id = ?",
			(tid, STONE_IDS[0])).fetchone()
		if row:
			stone_ppb[tid] = row["drop_chance_ppb"]
	# npc_type -> stage-2 table, for placing the absent body parts
	s2_for_type = {}
	for r in db.execute(
		"select npc_type, stage2_table_id from npc_configs where stage2_table_id != 0"):
		s2_for_type.setdefault(r["npc_type"], r["stage2_table_id"])
	# where each body part currently lives
	present = {}
	for r in db.execute(
		"select e.drop_table_id, e.item_id, e.drop_chance_ppb, t.name "
		"from drop_entries e join drop_tables t using (drop_table_id) where t.stage = 2"):
		present.setdefault(r["item_id"], []).append(
			(r["drop_table_id"], r["drop_chance_ppb"], r["name"]))
	return items, stone_ppb, s2_for_type, present


def plan(db, r):
	"""Compute every change without touching anything. Returns the change sets."""
	items, stone_ppb, s2_for_type, present = load_state(db)

	r("=" * 78)
	r("#91 DROP CONTENT CURATION — PLAN")
	r(f"generated {datetime.now(timezone.utc).isoformat(timespec='seconds')}")
	r("=" * 78)

	# ---- A. new items -------------------------------------------------------
	next_display = db.execute("select max(display_id) from items").fetchone()[0] + 1
	new_items = []
	r("")
	r("A. NEW ITEMS (items table + atlas frames + sprite metadata)")
	r(f"   {'id':>5}  {'name':22} {'sprite':>6} {'display_id':>10} {'durab':>5} {'weight':>7}")
	for iid in sorted(NEW_ITEMS):
		sprite, name, durab, weight = NEW_ITEMS[iid]
		if iid in items:
			r(f"   {iid:>5}  ALREADY EXISTS as {items[iid]!r} — skipped")
			continue
		new_items.append((iid, sprite, name, durab, weight, next_display))
		r(f"   {iid:>5}  {name:22} {sprite:>6} {next_display:>10} {durab:>5} {weight:>7,}")
		next_display += 1

	# ---- B. Super Green Potion ---------------------------------------------
	pot_rows = []
	r("")
	r(f"B. SUPER GREEN POTION {SUPER_GREEN_POTION} -> stage-1 monster tables at a stone's ppb")
	existing = {t for t, in db.execute(
		"select drop_table_id from drop_entries where item_id = ?", (SUPER_GREEN_POTION,))}
	for tid, ppb in sorted(stone_ppb.items()):
		if tid in existing:
			r(f"   table {tid}: already present — skipped")
			continue
		pot_rows.append((tid, SUPER_GREEN_POTION, ppb))
	r(f"   {rarity_span(pot_rows)}")

	# ---- C. balls -----------------------------------------------------------
	ball_rows = []
	r("")
	r(f"C. BALLS {BALL_IDS[0]}-{BALL_IDS[-1]} -> stage-1 monster tables at stone_ppb // {BALL_DIVISOR}")
	for tid, ppb in sorted(stone_ppb.items()):
		bp = ppb // BALL_DIVISOR
		for iid in BALL_IDS:
			if db.execute("select 1 from drop_entries where drop_table_id = ? and item_id = ?",
			              (tid, iid)).fetchone():
				continue
			ball_rows.append((tid, iid, bp))
	across = f" across {len(stone_ppb)} tables" if ball_rows else ""
	r(f"   {rarity_span(ball_rows)}{across}")

	# ---- D. body parts ------------------------------------------------------
	bp_updates, bp_inserts, bp_skipped = [], [], []
	r("")
	r("D. BODY PARTS aligned to the original's 1/(K x N)")
	r("   'orig' and 'now' are both 1-in-N kills. 'change' is the generosity factor")
	r("   applied to the row: >1 = looser (more likely), <1 = TIGHTER (rarer).")
	r(f"   {'id':>5} {'name':24} {'orig':>10} {'now':>10} {'change':>9}  table")
	for iid in sorted(ORIGINAL_BODY_PARTS):
		npc_type, K, N = ORIGINAL_BODY_PARTS[iid]
		target = round(PPB / (K * N))
		name = items.get(iid, "?? NOT IN items")
		rows = present.get(iid, [])
		if not rows:
			tid = s2_for_type.get(npc_type)
			if tid is None:
				bp_skipped.append((iid, name, f"no stage-2 table for npc_type {npc_type}"))
				r(f"   {iid:>5} {name[:24]:24} {K*N:>10,} {'ABSENT':>10} {'SKIP':>9}  "
				  f"no s2 table for type {npc_type}")
				continue
			bp_inserts.append((tid, iid, target))
			r(f"   {iid:>5} {name[:24]:24} {K*N:>10,} {'ABSENT':>10} {'ADD':>9}  {tid}")
			continue
		# the canonical row is the one on this npc type's own table
		own = s2_for_type.get(npc_type)
		for tid, cur, tname in rows:
			if tid != own:
				bp_skipped.append((iid, name, f"extra row on {tname} ({tid}) left untouched"))
				r(f"   {iid:>5} {name[:24]:24} {'':>10} {one_in(cur):>10,.0f} {'LEAVE':>9}  "
				  f"{tname} — not this part's monster")
				continue
			if cur == target:
				continue
			bp_updates.append((tid, iid, target, cur))
			r(f"   {iid:>5} {name[:24]:24} {K*N:>10,} {one_in(cur):>10,.0f} "
			  f"{target/cur:>8.2f}x  {tname}")
	r(f"   => {len(bp_updates)} re-rated, {len(bp_inserts)} added, {len(bp_skipped)} left alone")

	return new_items, pot_rows, ball_rows, bp_updates, bp_inserts, bp_skipped


def verify(db, changes, r):
	"""Invariants that must hold after the plan is applied."""
	new_items, pot_rows, ball_rows, bp_updates, bp_inserts, _ = changes
	ok = True
	r("")
	r("VERIFY")

	# every new item id must be free, and every display_id unique
	used_display = {d for d, in db.execute("select display_id from items")}
	for iid, _, name, _, _, disp in new_items:
		if db.execute("select 1 from items where item_id = ?", (iid,)).fetchone():
			r(f"   FAIL item_id {iid} already exists"); ok = False
		if disp in used_display:
			r(f"   FAIL display_id {disp} already used"); ok = False
		used_display.add(disp)
		if len(name) > 42:
			r(f"   FAIL name {name!r} exceeds ItemNameLen 42"); ok = False
	r(f"   OK  {len(new_items)} new item ids free, display_ids unique, names within 42")

	# the source art must actually carry every sprite index we claim
	try:
		paklib = load_paklib()
		for label, path in (("pack", ART_PACK), ("ground", ART_GROUND)):
			sheet = paklib.PAKFile.read(path).sprites[ART_SHEET]
			for iid, sprite, *_ in new_items:
				if sprite >= len(sheet.rectangles):
					r(f"   FAIL {label} sheet {ART_SHEET} has no index {sprite} (item {iid})")
					ok = False
		r(f"   OK  source art carries all {len(new_items)} sprite indexes in both paks")
	except Exception as exc:
		r(f"   FAIL could not read source art: {exc}"); ok = False

	# no drop row may be inserted twice, and every ppb must be >= 1
	seen = set()
	for tid, iid, ppb in pot_rows + ball_rows + bp_inserts:
		if (tid, iid) in seen:
			r(f"   FAIL duplicate insert ({tid}, {iid})"); ok = False
		seen.add((tid, iid))
		if ppb < 1:
			r(f"   FAIL ppb {ppb} < 1 for ({tid}, {iid})"); ok = False
	r(f"   OK  {len(seen)} inserts unique, all ppb >= 1")

	# no table may exceed 1.0 total probability after the change
	delta = {}
	for tid, _, ppb in pot_rows + ball_rows + bp_inserts:
		delta[tid] = delta.get(tid, 0) + ppb
	for tid, _, target, cur in bp_updates:
		delta[tid] = delta.get(tid, 0) + (target - cur)
	worst = None
	for tid, d in sorted(delta.items()):
		total = db.execute(
			"select coalesce(sum(drop_chance_ppb), 0) from drop_entries where drop_table_id = ?",
			(tid,)).fetchone()[0] + d
		if total > PPB:
			r(f"   FAIL table {tid} would sum to {total/1e7:.3f}% > 100%"); ok = False
		if worst is None or total > worst[1]:
			worst = (tid, total)
	if worst:
		r(f"   OK  {len(delta)} tables stay under 100%; worst is {worst[0]} "
		  f"at {worst[1]/1e7:.3f}%")

	# every body part we re-rate or add must exist in `items`
	for tid, iid, ppb in bp_inserts:
		if not db.execute("select 1 from items where item_id = ?", (iid,)).fetchone():
			r(f"   FAIL body part {iid} not in items"); ok = False
	r("   OK  every body part referenced exists in items")

	r(f"   => {'ALL CHECKS PASSED' if ok else 'CHECKS FAILED'}")
	return ok


def apply_db(db, changes, r):
	new_items, pot_rows, ball_rows, bp_updates, bp_inserts, _ = changes
	cur = db.cursor()

	# New items are modelled on sibling 656 (Stone of Xelima) so that anything the
	# schema gains later is inherited rather than defaulted: copy its row, then
	# override the fields the original's cfg actually specifies.
	proto = dict(db.execute("select * from items where item_id = 656").fetchone())
	for iid, sprite, name, durab, weight, disp in new_items:
		row = dict(proto)
		row.update(item_id=iid, name=name, durability=durab, weight=weight, display_id=disp)
		cols = ", ".join(row)
		cur.execute(f"insert into items ({cols}) values ({', '.join('?' * len(row))})",
		            tuple(row.values()))

	for tid, iid, ppb in pot_rows + ball_rows + bp_inserts:
		cur.execute("insert into drop_entries "
		            "(drop_table_id, item_id, drop_chance_ppb, min_count, max_count) "
		            "values (?, ?, ?, 1, 1)", (tid, iid, ppb))
	for tid, iid, target, _ in bp_updates:
		cur.execute("update drop_entries set drop_chance_ppb = ? "
		            "where drop_table_id = ? and item_id = ?", (target, tid, iid))
	db.commit()
	r(f"   applied: {len(new_items)} items, "
	  f"{len(pot_rows) + len(ball_rows) + len(bp_inserts)} drop rows inserted, "
	  f"{len(bp_updates)} re-rated")


def apply_art(new_items, r):
	"""Append the 8 icons to the atlas ground+pack sheets and register them."""
	paklib = load_paklib()
	from PIL import Image

	atlas = paklib.PAKFile.read(ATLAS)
	src = {SHEET_GROUND: paklib.PAKFile.read(ART_GROUND).sprites[ART_SHEET],
	       SHEET_PACK: paklib.PAKFile.read(ART_PACK).sprites[ART_SHEET]}

	# A metadata entry carries ONE frame index that addresses both the ground and
	# the pack sheet, so the two must stay the same length or the indexes diverge.
	counts = {i: len(atlas.sprites[i].rectangles) for i in (SHEET_GROUND, SHEET_PACK)}
	if len(set(counts.values())) != 1:
		raise SystemExit(f"atlas ground/pack sheets differ in length: {counts} — "
		                 "one shared frame index can no longer address both")
	base_frame = next(iter(counts.values()))

	frame_of = {}
	for sheet_idx, src_sheet in src.items():
		dst = atlas.sprites[sheet_idx]
		img = dst.get_image().convert("RGBA")
		src_img = src_sheet.get_image().convert("RGBA")
		crops = []
		for iid, sprite, *_ in new_items:
			sr = src_sheet.rectangles[sprite]
			crops.append((iid, sr,
			              src_img.crop((sr.x, sr.y, sr.x + sr.width, sr.y + sr.height))))
		# one new strip along the bottom, tall enough for the tallest icon
		strip_h = max(c.height for *_, c in crops)
		strip_w = sum(c.width for *_, c in crops)
		grown = Image.new("RGBA", (max(img.width, strip_w), img.height + strip_h), (0, 0, 0, 0))
		grown.paste(img, (0, 0))
		x = 0
		for offset, (iid, sr, crop) in enumerate(crops):
			grown.paste(crop, (x, img.height), crop)
			# the source pivot is already in the atlas convention (negative, about
			# half the extent), so it carries over unchanged
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
	for iid, sprite, name, _, _, disp in new_items:
		meta.append({"pak_file": None, "id": disp,
		             "inventory_frame_index": frame_of[iid],
		             "ground_frame_index": frame_of[iid]})
	META.write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
	r(f"   metadata: +{len(new_items)} entries, {len(meta)} total")
	return frame_of


def main():
	ap = argparse.ArgumentParser(description=__doc__,
	                            formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--db", type=Path, default=DEFAULT_DB)
	ap.add_argument("--dry-run", action="store_true",
	                help="preview every change and write the full report; touch nothing")
	ap.add_argument("--verify", action="store_true",
	                help="run the invariant checks only")
	ap.add_argument("--skip-art", action="store_true",
	                help="apply data only; leave the atlas and sprite metadata alone")
	args = ap.parse_args()

	if not args.db.exists():
		print(f"no such database: {args.db}", file=sys.stderr)
		return 2

	r = Report()
	db = sqlite3.connect(args.db)
	changes = plan(db, r)
	ok = verify(db, changes, r)

	if args.verify:
		r.write(OUT / "drop_content.log")
		return 0 if ok else 1
	if not ok:
		r("")
		r("REFUSING TO APPLY — verification failed")
		r.write(OUT / "drop_content.log")
		return 1
	if args.dry_run:
		r("")
		r("DRY RUN — nothing written")
		r.write(OUT / "drop_content.log")
		return 0

	r("")
	r("APPLY")
	backup = args.db.with_suffix(args.db.suffix + ".pre91")
	shutil.copy2(args.db, backup)
	r(f"   db backup: {backup.name}")
	apply_db(db, changes, r)
	db.close()
	if args.skip_art:
		r("   art: skipped (--skip-art)")
	else:
		shutil.copy2(ATLAS, ATLAS.with_suffix(ATLAS.suffix + ".pre91"))
		shutil.copy2(META, META.with_suffix(META.suffix + ".pre91"))
		apply_art(changes[0], r)
	r.write(OUT / "drop_content.log")
	return 0


if __name__ == "__main__":
	sys.exit(main())
