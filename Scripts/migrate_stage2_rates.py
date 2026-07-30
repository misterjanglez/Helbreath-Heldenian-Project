#!/usr/bin/env python3
"""Item Tiers #90 — ordinary stage-2 rates reconciled against the original.

#91 aligned the body-part arm of the second drop to the original exactly. This
script does the same for the OTHER arm: the unique roster. Every ordinary
(non-boss) stage-2 row is restated as the absolute "1 in N" the original's four
nested gates actually produce, with the arithmetic recorded per row.

SOURCE OF TRUTH

  repos/HB382_CENTUU/HGServer/Game.cpp
    DeleteNpc                  @26399  per-type dispatch, outer iDice(1,K)
    bGetItemNameWhenDeleteNpc  @53889  1/45 global, 1/G per monster, 1/M arm, 1/D inner
  repos/HB382_CENTUU/Files/Item{,2,3}.cfg   the id -> name authority

`bGetItemNameWhenDeleteNpc` is byte-identical in repos/HelbreathServer, so there
is one authoritative original.

THE FOURTH GATE #90 MISSED

#90's table quotes three gates. There are four: `DeleteNpc` dispatches per type
through an outer `iDice(1,K)` whose LAST arm calls the unique roster; the
body-part arms are its siblings. So the true per-(item, monster) chance is

    1/K  x  1/45  x  1/G  x  1/M  x  1/D

and any monster with body parts is K times rarer than #90 claimed. The zombie
has no body parts (K=1) so its figures were right; the snake has K=5, so
Sword of Medusa from a snake is 1 in 29,835,000, not 1 in 5,967,000.

#91's body-part rates are `1/(K x D)` under this same model and all 53 of them
match to the byte, which is what confirms the model rather than the transcription.

WHAT WAS MEASURED (219 ordinary stage-2 rows, before this script)

    53 body-part rows      exact, from #91 — untouched here, asserted in verify
    94 unique rows         map to an original arm, EVERY ONE too common
                           (worst: Gargoyle's Ring of Demon power 1 in 400
                            vs 1 in 1,893,375 — 4,733x)
    72 unique rows         (item, monster) pairs the original never had
    24 rows absent         real original arms we never carried

OWNER DECISIONS (2026-07-29)

  1. The six tables whose monster had no second drop in the original
     (Mountain-Giant t58, Ice-Golem t65, Cannibal-Plant t60, Centaurus t71 +
     Griffin t92, Giant-Lizard t75, MasterMage-Orc t77) KEEP their rows,
     re-rated into the original's band rather than deleted.
  2. The 72 (item, monster) pairs the original never had are KEPT, re-rated to
     that monster's rarest original arm. No content leaves the game.
  3. Beholder carries the INTENT of its arm, not the bug. The original reads
     `if (iDice(1,10) == 11)` — unreachable, so the original's Beholder dropped
     nothing despite carrying a 1/425 gate. Every sibling arm compares against
     13 or 3 with a larger die, so == 11 against iDice(1,10) is a typo; the arm
     is modelled at its evident 1/10.

THE TWO RE-RATING RULES

  Rule A — a row that IS an original arm gets that arm's exact rate.
  Rule B — a row the original never had gets the RAREST arm the original
           authored for that monster. Where the monster has no authored arms at
           all, the fallback is the rarest arm of the Nizie template
           (1/45 x 1/170 x 1/3 x 1/30 = 1 in 688,500) — Nizie is the only type
           in the original's 1/170 gate block that was given an arm set, so it
           is the original's own shape for a late-added monster, not an invented
           number.

FOUR STRUCTURAL CHANGES THIS SCRIPT MAKES

  A. `s2_Frost_x2` is SPLIT. Frost (t63) and Nizie (t79) share one table but the
     original gates them differently — 1/300 vs 1/170 — and one row cannot carry
     two rates. 40026 becomes `s2_Frost` (t63) and new 40037 `s2_Nizie` (t79)
     carries the same roster 1.76x more generously, as authored.

  B. Centaurus (t71) is treated as DISPATCHED. The original authored it a 1/170
     gate AND an arm set (735, 732) but `DeleteNpc` never routes type 71, so the
     whole branch is dead code there. Owner decision 1 keeps the table, so the
     authored arms become real rows and the extras take Rule B. Griffin (t92)
     shares the table and has no original counterpart at all; it inherits.

  C. Items 732 / 738 join `is_special_item` (ItemEnums.h, mirrored into
     Scripts/seed_item_tiers.py). The original's Stone-Golem, Clay-Golem, Frost
     and Barlog rosters drop them, but the §8 validator forbids tier-eligible
     gear in a stage-2 table, and neither id was in the named-unique roster.
     They are structurally identical to the Berserk wands 861/862 — same
     item_type 2 / sub_type 3 / equip_pos 8 / pool 3 / weapon_class 7 — which
     ARE specials and DO sit in stage-2 tables. This repeats #49's ruling that
     added 290/292/762 for the same reason. It is purely additive: 732 and 738
     appear in NO drop table today, either stage, so nothing stops tier-rolling
     that ever rolled.

  D. Six arms are declined as PHANTOM, not missing. Ids 942 IceHammer, 943
     IceAxe (Frost/Nizie), 956/957/958 Drow armour (Dark-Elf) and 947
     DragonWand (Tigerworm) are absent from the ORIGINAL's own Item.cfg, so
     `_bInitItemAttr` fails there too and those arms drop nothing in the
     original either. Not content we failed to port.

Boss tables (loot_grade 5: Hellclaw, Tigerworm, Wyvern, Fire-Wyvern, Abaddon)
are out of #90's scope and are not touched.
"""
import argparse
import shutil
import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DB = ROOT / "Binaries" / "Server" / "gamedata.db"
OUT = ROOT / "Scripts" / "output"
PPB = 1_000_000_000
GLOBAL_GATE = 45          # iDice(1,45) == 13
BOSS_GRADE = 5

# ---------------------------------------------------------------------------
# The original model. Game.cpp:26399 DeleteNpc — outer draw size K per type.
# K = 1 means the type calls the unique roster directly. body{id: D} is that
# type's sibling arms, each `if (iDice(1,D) == x)`.
# ---------------------------------------------------------------------------
DELETE_NPC = {
	10: dict(K=1, unique=False, body={220: 25}),                                      # Slime
	11: dict(K=2, unique=True,  body={219: 20}),                                      # Skeleton
	12: dict(K=2, unique=True,  body={221: 30}),                                      # Stone-Golem
	13: dict(K=6, unique=True,  body={194: 36, 195: 40, 196: 30, 197: 22, 198: 40}),  # Cyclops
	14: dict(K=4, unique=True,  body={206: 11, 207: 20, 208: 21}),                    # Orc
	16: dict(K=3, unique=True,  body={192: 9, 193: 10}),                              # Giant-Ant
	17: dict(K=5, unique=True,  body={215: 50, 216: 20, 217: 50, 218: 40}),           # Scorpion
	18: dict(K=1, unique=True,  body={}),                                             # Zombie
	22: dict(K=5, unique=True,  body={188: 15, 189: 16, 190: 16, 191: 17}),           # Amphis
	23: dict(K=2, unique=True,  body={205: 30}),                                      # Clay-Golem
	27: dict(K=7, unique=True,  body={199: 40, 200: 38, 201: 38, 202: 36,
	                                  203: 36, 204: 50}),                             # Hellbound
	28: dict(K=5, unique=True,  body={222: 35, 223: 23, 224: 25, 225: 27}),           # Troll
	29: dict(K=7, unique=True,  body={209: 20, 210: 22, 211: 25, 212: 25,
	                                  213: 28, 214: 28}),                             # Ogre
	30: dict(K=1, unique=True,  body={}),                                             # Liche
	31: dict(K=5, unique=True,  body={541: 400, 542: 1000, 543: 200, 540: 300}),      # Demon
	32: dict(K=5, unique=True,  body={544: 3000, 545: 500, 546: 100, 547: 200}),      # Unicorn
	33: dict(K=8, unique=True,  body={551: 30, 548: 28, 550: 25, 553: 35,
	                                  552: 28, 554: 28, 549: 38}),                    # WereWolf
}
# Game.cpp:26627-26645 — one group, direct call, no body-part arms.
for _t in (48, 49, 50, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 65, 70):
	DELETE_NPC[_t] = dict(K=1, unique=True, body={})

# Change B. `DeleteNpc` never routes types 71 (Centaurus) or 79 (Nizie), so the
# gate AND arm set the original authored for them is dead code there. Owner
# decision 1 keeps both tables, so they are revived as the direct-call types
# they would have been, and their own authored arithmetic governs them — which
# is the whole reason the Frost/Nizie table has to split.
REVIVED_TYPES = (71, 79)
for _t in REVIVED_TYPES:
	DELETE_NPC[_t] = dict(K=1, unique=True, body={})

# Game.cpp:53939-53972 — per-monster gate `iDice(1,G) != 11 -> return FALSE`.
# Types 71/72/74/75/77/78/79 carry a gate but DeleteNpc never routes them, so
# the whole branch is dead code in the original. Owner decision 1 revives 71.
GATE = {
	11: 465, 12: 340, 13: 85, 14: 595, 17: 510, 18: 720, 22: 510, 23: 340,
	27: 85, 28: 85, 29: 125, 30: 100, 31: 120, 32: 170, 33: 255, 48: 80,
	52: 255, 53: 425, 54: 200, 57: 400, 63: 300, 79: 170, 70: 170, 71: 170,
	74: 170, 72: 170, 75: 170, 77: 170, 78: 170, 59: 120,
}

# Game.cpp:53977-54147 — the item switch. One (item_id, D) per `case` of
# iDice(1,M); D is the arm's inner denominator, 1 when the arm is ungated.
ARMS = {
	(11, 17, 14, 28, 57): [(334, 1), (336, 1), (335, 15), (337, 1), (333, 1),
	                       (634, 15), (635, 25)],
	(13, 27, 29): [(311, 1), (308, 15), (305, 5), (300, 1), (632, 25),
	               (637, 25), (638, 25)],
	(18, 22): [(613, 65), (639, 15), (641, 30), (640, 25)],
	(12,): [(738, 20), (621, 30), (622, 30), (644, 15), (647, 15)],
	(23,): [(738, 20), (621, 30), (622, 30), (647, 15)],
	(32,): [(620, 30), (621, 30), (622, 30), (644, 15)],
	(33, 48): [(852, 20), (857, 20), (853, 20), (620, 1)],
	(30,): [(852, 15), (380, 1), (853, 15), (643, 30), (648, 15), (734, 20)],
	(31,): [(382, 5), (491, 1), (490, 5), (492, 1), (381, 5), (633, 15),
	        (645, 10), (616, 15)],
	(52,): [(382, 5), (610, 15), (611, 15), (612, 15), (381, 5), (633, 15),
	        (645, 10), (630, 30), (631, 40), (735, 20), (20, 30)],
	# Beholder: `if (iDice(1,10) == 11) iItemID = 380;` — no switch, so M = 1.
	# Unreachable as written; owner decision 3 carries the evident 1/10 intent.
	(53,): [(380, 10)],
	(54,): [(618, 20), (958, 15), (956, 15), (957, 15)],
	(63,): [(943, 20), (942, 20), (732, 30)],
	(79,): [(943, 20), (942, 20), (732, 30)],
	(70,): [(382, 5), (381, 5), (732, 30)],
	(71,): [(735, 20), (732, 30)],
	(59,): [(735, 20), (853, 10), (382, 7)],
}
ARM_BY_TYPE = {t: arms for types, arms in ARMS.items() for t in types}

# Rule B fallback: the rarest arm of the Nizie template, the only arm set the
# original authored anywhere in its 1/170 gate block.
NIZIE_FALLBACK = GLOBAL_GATE * 170 * 3 * 30            # 688,500

# Absent from the ORIGINAL's Item.cfg as well as ours — see change D.
PHANTOM_IDS = {942, 943, 947, 956, 957, 958}

# Change C. Kept here so the report names them; the header edit is by hand.
NEW_SPECIALS = {732: "DarkMageMagicStaffW", 738: "DarkMageMagicWand"}

# Change A. The split.
FROST_TABLE = 40026
NIZIE_TABLE = 40037
NIZIE_NPC_TYPE = 79


def one_in(ppb):
	return PPB / ppb if ppb else float("inf")


def ppb_for(n):
	"""Absolute rarity 1-in-N as parts per billion, floored at 1."""
	return max(1, round(PPB / n))


def arithmetic(K, G, M, D):
	parts = ([f"1/{K}"] if K != 1 else []) + [f"1/{GLOBAL_GATE}", f"1/{G}", f"1/{M}"]
	if D != 1:
		parts.append(f"1/{D}")
	return " x ".join(parts)


def orig_unique(npc_type):
	"""{item_id: (one_in_N, arithmetic)} for one type's unique roster."""
	spec = DELETE_NPC.get(npc_type)
	if spec is None or not spec["unique"]:
		return {}
	if npc_type not in GATE or npc_type not in ARM_BY_TYPE:
		return {}
	K, G = spec["K"], GATE[npc_type]
	arms = ARM_BY_TYPE[npc_type]
	M = len(arms)
	return {iid: (K * GLOBAL_GATE * G * M * D, arithmetic(K, G, M, D))
	        for iid, D in arms}


def orig_body(npc_type):
	"""{item_id: (one_in_N, arithmetic)} for one type's body-part arms."""
	spec = DELETE_NPC.get(npc_type)
	if spec is None:
		return {}
	K = spec["K"]
	return {iid: (K * D, " x ".join(([f"1/{K}"] if K != 1 else []) + [f"1/{D}"]))
	        for iid, D in spec["body"].items()}


def rarest_arm(npc_type):
	"""Rule B: the rarest rate the original authored for this monster."""
	rates = orig_unique(npc_type)
	if not rates:
		return NIZIE_FALLBACK
	return max(n for n, _ in rates.values())


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
	tables = {r["drop_table_id"]: dict(r)
	          for r in db.execute("select * from drop_tables where stage = 2")}
	owners = {}
	for r in db.execute("select npc_id, name, npc_type, loot_grade, stage2_table_id "
	                    "from npc_configs where stage2_table_id != 0"):
		owners.setdefault(r["stage2_table_id"], []).append(dict(r))
	rows = {}
	for r in db.execute("select e.drop_table_id, e.item_id, e.drop_chance_ppb "
	                    "from drop_entries e join drop_tables t using (drop_table_id) "
	                    "where t.stage = 2"):
		rows.setdefault(r["drop_table_id"], {})[r["item_id"]] = r["drop_chance_ppb"]
	return items, tables, owners, rows


def rate_type_for(tid, own, r=None):
	"""Which npc_type's arithmetic governs this table after the split."""
	if tid == NIZIE_TABLE:
		return NIZIE_NPC_TYPE
	types = sorted({o["npc_type"] for o in own})
	if tid == FROST_TABLE:
		return 63                                   # Nizie moves out in change A
	gated = [t for t in types if t in GATE]
	if len(gated) > 1 and r is not None:
		r(f"   !! table {tid} is shared by {gated} which the original gates "
		  f"differently — rates cannot both be expressed")
	return gated[0] if gated else types[0]


def plan(db, r):
	items, tables, owners, rows = load_state(db)

	r("=" * 78)
	r("#90 ORDINARY STAGE-2 RATES — PLAN")
	r(f"generated {datetime.now(timezone.utc).isoformat(timespec='seconds')}")
	r("=" * 78)
	r("Every '1 in N' below is the ACHIEVED value after rounding to integer ppb,")
	r("not the exact quotient — ADR 0005 stores parts per billion, so a target of")
	r("1 in 292,950 lands at 1 in 292,912. The arithmetic in brackets is exact.")

	# --- change A: split Frost / Nizie -------------------------------------
	frost = tables[FROST_TABLE]
	new_tables = [dict(drop_table_id=NIZIE_TABLE, name="s2_Nizie",
	                   description="Nizie (npc_type 79); same roster as Frost at the "
	                               "original's 1/170 gate rather than Frost's 1/300",
	                   stage=2, roll_count_min=frost["roll_count_min"],
	                   roll_count_max=frost["roll_count_max"],
	                   placement=frost["placement"], delay=frost["delay"])]
	nizie_npcs = [o["npc_id"] for o in owners[FROST_TABLE]
	              if o["npc_type"] == NIZIE_NPC_TYPE]
	table_renames = [(FROST_TABLE, "s2_Frost", "Frost (npc_type 63)")]
	for tid, name, desc in table_renames:
		tables[tid]["name"] = name              # report the post-rename name
	r("")
	r("A. TABLE SPLIT")
	r(f"   {FROST_TABLE} 's2_Frost_x2' -> 's2_Frost' (t63, gate 1/300)")
	r(f"   {NIZIE_TABLE} 's2_Nizie' NEW  (t79, gate 1/170) for npc_id {nizie_npcs}")

	# Nizie's new table starts as a copy of Frost's row set; both are then rated
	# by their own type below.
	rows[NIZIE_TABLE] = dict(rows[FROST_TABLE])
	tables[NIZIE_TABLE] = new_tables[0]
	owners[NIZIE_TABLE] = [o for o in owners[FROST_TABLE]
	                       if o["npc_type"] == NIZIE_NPC_TYPE]
	owners[FROST_TABLE] = [o for o in owners[FROST_TABLE]
	                       if o["npc_type"] != NIZIE_NPC_TYPE]

	updates, inserts, declined, body_ok, body_bad = [], [], [], [], []

	r("")
	r("B. PER-TABLE RECONCILIATION")
	for tid in sorted(tables):
		own = owners.get(tid, [])
		if not own:
			continue
		if any(o["loot_grade"] == BOSS_GRADE for o in own):
			continue                                # boss: out of scope
		ntype = rate_type_for(tid, own, r)
		u, b = orig_unique(ntype), orig_body(ntype)
		extra_rate = rarest_arm(ntype)
		present = rows.get(tid, {})
		who = ", ".join("{}(t{})".format(o["name"], o["npc_type"]) for o in own)

		r("")
		r(f"   --- {tid} {tables[tid]['name']}  [{who}]")

		# Body parts are #91's and stay put; they are only asserted here.
		for iid in sorted(set(present) & set(b)):
			n, arith = b[iid]
			(body_ok if ppb_for(n) == present[iid] else body_bad).append(
				(tid, iid, present[iid], ppb_for(n), arith))

		if set(present) <= set(b) and not (set(u) - set(present)):
			r("       body parts only, all exact — nothing to do")
			continue
		if not u:
			r(f"       original: no unique second drop at all; table kept per owner "
			  f"decision 1, Rule B rate 1 in {extra_rate:,} (Nizie template)")
		elif ntype in REVIVED_TYPES:
			r(f"       original: authored a 1/{GATE[ntype]} gate and an arm set but "
			  f"DeleteNpc never routed t{ntype}; revived per owner decision 1")

		for iid in sorted(present, key=lambda k: -present[k]):
			cur = present[iid]
			if iid in b:
				continue
			if iid in u:
				n, arith, rule = *u[iid], "A"
			else:
				n, arith, rule = extra_rate, f"Rule B (rarest arm of t{ntype})", "B"
			target = ppb_for(n)
			if target == cur:
				continue
			factor = one_in(target) / one_in(cur)
			updates.append((tid, iid, cur, target, rule, arith))
			r(f"       {rule} {iid:4d} {items.get(iid, '?')[:26]:26s} "
			  f"1 in {one_in(cur):>11,.0f} -> 1 in {one_in(target):>13,.0f}  "
			  f"{factor:>9,.1f}x {'rarer' if factor > 1 else 'commoner'}   [{arith}]")

		for iid in sorted(set(u) - set(present)):
			n, arith = u[iid]
			if iid in PHANTOM_IDS:
				declined.append((tid, iid, n, "absent from the original's own Item.cfg"))
				continue
			if iid not in items:
				declined.append((tid, iid, n, "not in our items table"))
				continue
			target = ppb_for(n)
			inserts.append((tid, iid, target, arith))
			r(f"       + {iid:4d} {items.get(iid, '?')[:26]:26s} "
			  f"{'absent':>11s} -> 1 in {one_in(target):>13,.0f}  ADDED            [{arith}]")

	r("")
	r("C. NAMED-UNIQUE ROSTER (hand edit, not applied by this script)")
	for iid, sym in NEW_SPECIALS.items():
		r(f"   ItemEnums.h is_special_item += ItemId::{sym} ({iid} {items.get(iid,'?')})")
	r("   mirror into Scripts/seed_item_tiers.py SPECIAL_ITEMS "
	  "(verify_special_roster_sync fails loud otherwise)")

	r("")
	r("D. DECLINED ARMS")
	for tid, iid, n, why in declined:
		r(f"   {tid} item {iid}: would be 1 in {n:,} — {why}")

	r("")
	r("E. BODY PARTS (#91's work, asserted not re-rated)")
	r(f"   {len(body_ok)} rows still exactly match the original")
	for tid, iid, cur, want, arith in body_bad:
		r(f"   !! {tid} item {iid}: ours 1 in {one_in(cur):,.0f} but original is "
		  f"1 in {one_in(want):,.0f} [{arith}]")

	# What the change leaves behind, so the loosest rows are visible rather than
	# buried: an ADDED row is a loosening even when every re-rate is a tightening.
	final = {}
	for tid, present in rows.items():
		own = owners.get(tid, [])
		if not own or any(o["loot_grade"] == BOSS_GRADE for o in own):
			continue
		final[tid] = dict(present)
	for tid, iid, _, target, _, _ in updates:
		final.setdefault(tid, {})[iid] = target
	for tid, iid, target, _ in inserts:
		final.setdefault(tid, {})[iid] = target

	r("")
	r("F. THE LOOSEST ORDINARY STAGE-2 ROWS AFTER THE CHANGE (uniques only)")
	body_ids = set()
	for tid in final:
		own = owners.get(tid, [])
		if own:
			body_ids |= set(orig_body(rate_type_for(tid, own)))
	loosest = sorted(((ppb, tid, iid) for tid, rs in final.items()
	                  for iid, ppb in rs.items() if iid not in body_ids),
	                 reverse=True)[:10]
	for ppb, tid, iid in loosest:
		r(f"   1 in {one_in(ppb):>10,.0f}  {tables[tid]['name']:<20} "
		  f"{iid:4d} {items.get(iid, '?')}")

	r("")
	r("G. PER-TABLE 'ANY STAGE-2 ITEM' TOTAL, BEFORE -> AFTER")
	for tid in sorted(final):
		before = sum(rows.get(tid, {}).values())
		after = sum(final[tid].values())
		if before == after:
			continue
		r(f"   {tables[tid]['name']:<20} {before / 1e7:>8.4f}% -> {after / 1e7:>8.4f}%")

	r("")
	r("SUMMARY")
	r(f"   {len(updates)} rows re-rated  "
	  f"({sum(1 for u in updates if u[4] == 'A')} Rule A, "
	  f"{sum(1 for u in updates if u[4] == 'B')} Rule B)")
	r(f"   {len(inserts)} rows added")
	r(f"   {len(declined)} arms declined")
	r(f"   1 table split, 1 table renamed, {len(nizie_npcs)} npc repointed")
	rarer = sum(1 for _, _, cur, tgt, *_ in updates if tgt < cur)
	r(f"   direction: {rarer} rarer, {len(updates) - rarer} commoner, "
	  f"{len(inserts)} newly obtainable")

	return dict(new_tables=new_tables, table_renames=table_renames,
	            nizie_npcs=nizie_npcs, updates=updates, inserts=inserts,
	            declined=declined, body_bad=body_bad)


def verify(db, ch, r):
	ok = True
	r("")
	r("VERIFY")

	# #91's body-part rates must be untouched and still exact.
	if ch["body_bad"]:
		r(f"   FAIL {len(ch['body_bad'])} body-part rows no longer match the original")
		ok = False
	else:
		r("   OK  every body-part row still matches the original exactly")

	# Already applied? Say so plainly instead of reporting it as a defect: the
	# new table exists, nothing is left to change, and re-applying would be the
	# actual error.
	applied = db.execute("select 1 from drop_tables where drop_table_id = ?",
	                     (NIZIE_TABLE,)).fetchone() is not None
	if applied and not ch["updates"] and not ch["inserts"]:
		r(f"   ALREADY APPLIED: table {NIZIE_TABLE} exists and no row differs "
		  f"from the model — nothing to do")
		return False

	# the new table id must be free, and its npc must exist
	if applied:
		r(f"   FAIL drop_table_id {NIZIE_TABLE} already exists"); ok = False
	elif not ch["nizie_npcs"]:
		r("   FAIL no npc found to repoint at the new table"); ok = False
	else:
		r(f"   OK  table id {NIZIE_TABLE} free, "
		  f"{len(ch['nizie_npcs'])} npc to repoint")

	# no insert may collide with an existing row or duplicate another insert
	seen = set()
	for tid, iid, ppb, _ in ch["inserts"]:
		if (tid, iid) in seen:
			r(f"   FAIL duplicate insert ({tid}, {iid})"); ok = False
		seen.add((tid, iid))
		if ppb < 1:
			r(f"   FAIL ppb {ppb} < 1 for ({tid}, {iid})"); ok = False
		if tid != NIZIE_TABLE and db.execute(
				"select 1 from drop_entries where drop_table_id = ? and item_id = ?",
				(tid, iid)).fetchone():
			r(f"   FAIL insert ({tid}, {iid}) already present"); ok = False
		if not db.execute("select 1 from items where item_id = ?", (iid,)).fetchone():
			r(f"   FAIL insert item {iid} not in items"); ok = False
	r(f"   OK  {len(seen)} inserts unique, present in items, ppb >= 1")

	# every update must address a row that exists
	for tid, iid, cur, _, _, _ in ch["updates"]:
		if tid == NIZIE_TABLE:
			continue
		row = db.execute("select drop_chance_ppb from drop_entries "
		                 "where drop_table_id = ? and item_id = ?", (tid, iid)).fetchone()
		if row is None:
			r(f"   FAIL update target ({tid}, {iid}) does not exist"); ok = False
		elif row[0] != cur:
			r(f"   FAIL ({tid}, {iid}) is {row[0]} not the planned-from {cur}"); ok = False
	r(f"   OK  all {len(ch['updates'])} update targets exist at the expected ppb")

	# no table may pass 100%, and the whole point is that stage 2 gets rarer
	delta = {}
	for tid, _, ppb, _ in ch["inserts"]:
		delta[tid] = delta.get(tid, 0) + ppb
	for tid, _, cur, tgt, _, _ in ch["updates"]:
		delta[tid] = delta.get(tid, 0) + (tgt - cur)
	worst = None
	for tid, d in sorted(delta.items()):
		# The new table does not exist yet; it starts life as a copy of Frost's
		# rows, so Frost's current sum is its baseline.
		source = FROST_TABLE if tid == NIZIE_TABLE else tid
		base = db.execute(
			"select coalesce(sum(drop_chance_ppb), 0) from drop_entries "
			"where drop_table_id = ?", (source,)).fetchone()[0]
		total = base + d
		if total > PPB:
			r(f"   FAIL table {tid} would sum to {total / 1e7:.3f}% > 100%"); ok = False
		if worst is None or total > worst[1]:
			worst = (tid, total)
	if worst:
		r(f"   OK  {len(delta)} tables stay under 100%; worst is {worst[0]} "
		  f"at {worst[1] / 1e7:.4f}%")

	# a boss table must not appear anywhere in the plan
	boss = {r0[0] for r0 in db.execute(
		"select distinct stage2_table_id from npc_configs "
		"where loot_grade = ? and stage2_table_id != 0", (BOSS_GRADE,))}
	touched = {tid for tid, *_ in ch["updates"]} | {tid for tid, *_ in ch["inserts"]}
	if boss & touched:
		r(f"   FAIL boss tables touched: {sorted(boss & touched)}"); ok = False
	r(f"   OK  none of the {len(boss)} boss stage-2 tables is touched")

	# the §8 rule: nothing tier-eligible may enter a stage-2 table. Checked here
	# as "every inserted id is already a stage-2 citizen or a declared special".
	incoming = {iid for _, iid, _, _ in ch["inserts"]}
	existing_s2 = {r0[0] for r0 in db.execute(
		"select distinct e.item_id from drop_entries e "
		"join drop_tables t using (drop_table_id) where t.stage = 2")}
	unvetted = incoming - existing_s2 - set(NEW_SPECIALS)
	if unvetted:
		r(f"   FAIL new-to-stage-2 ids not covered by the roster edit: {sorted(unvetted)}")
		ok = False
	r(f"   OK  new-to-stage-2 ids are exactly {sorted(incoming - existing_s2)} "
	  f"(covered by the is_special_item edit)")

	r(f"   => {'ALL CHECKS PASSED' if ok else 'CHECKS FAILED'}")
	return ok


def apply_db(db, ch, r):
	cur = db.cursor()
	for t in ch["new_tables"]:
		cols = ", ".join(t)
		cur.execute(f"insert into drop_tables ({cols}) "
		            f"values ({', '.join('?' * len(t))})", tuple(t.values()))
	for tid, name, desc in ch["table_renames"]:
		cur.execute("update drop_tables set name = ?, description = ? "
		            "where drop_table_id = ?", (name, desc, tid))
	for npc_id in ch["nizie_npcs"]:
		cur.execute("update npc_configs set stage2_table_id = ? where npc_id = ?",
		            (NIZIE_TABLE, npc_id))
	# The new table inherits Frost's rows, then the plan's own updates re-rate
	# them at Nizie's gate.
	cur.execute("insert into drop_entries "
	            "(drop_table_id, item_id, drop_chance_ppb, min_count, max_count) "
	            "select ?, item_id, drop_chance_ppb, min_count, max_count "
	            "from drop_entries where drop_table_id = ?", (NIZIE_TABLE, FROST_TABLE))
	for tid, iid, ppb, _ in ch["inserts"]:
		cur.execute("insert into drop_entries "
		            "(drop_table_id, item_id, drop_chance_ppb, min_count, max_count) "
		            "values (?, ?, ?, 1, 1)", (tid, iid, ppb))
	for tid, iid, _, target, _, _ in ch["updates"]:
		cur.execute("update drop_entries set drop_chance_ppb = ? "
		            "where drop_table_id = ? and item_id = ?", (target, tid, iid))
	db.commit()
	r(f"   applied: 1 table added, {len(ch['table_renames'])} renamed, "
	  f"{len(ch['nizie_npcs'])} npc repointed, {len(ch['inserts'])} rows inserted, "
	  f"{len(ch['updates'])} re-rated")


def main():
	ap = argparse.ArgumentParser(description=__doc__,
	                            formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--db", type=Path, default=DEFAULT_DB)
	ap.add_argument("--dry-run", action="store_true",
	                help="plan only; touch nothing")
	ap.add_argument("--verify", action="store_true",
	                help="plan and run the invariant checks; touch nothing")
	args = ap.parse_args()

	if not args.db.exists():
		sys.exit(f"no such db: {args.db}")

	r = Report()
	db = sqlite3.connect(args.db)
	changes = plan(db, r)

	ok = True
	if args.verify or not args.dry_run:
		ok = verify(db, changes, r)

	if args.dry_run or args.verify:
		r("")
		r("   (no changes written)")
	elif not ok:
		r("")
		r("   REFUSED: verify failed, nothing written")
	else:
		backup = args.db.with_suffix(args.db.suffix + ".pre90")
		shutil.copy2(args.db, backup)
		r("")
		r(f"   db backup: {backup.name}")
		apply_db(db, changes, r)

	db.close()
	r.write(OUT / "stage2_rates.log")
	print(f"\nreport: {OUT / 'stage2_rates.log'}")
	sys.exit(0 if ok else 1)


if __name__ == "__main__":
	main()
