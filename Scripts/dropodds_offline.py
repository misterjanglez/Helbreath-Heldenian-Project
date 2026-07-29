#!/usr/bin/env python3
"""Offline replica of the `dropodds` server report (Item Tiers #66), for the
#68 drop-structure evaluation.

The live command needs a running server; this reproduces its arithmetic from
the shipped gamedata.db so options can be costed against real numbers on a
workstation. It deliberately mirrors, term for term:

  Server/EntityManager.h        base_gold_drop_chance, apply_drop_multiplier,
                                npc_type_never_drops
  Server/EntityManager.cpp      npc_dead_item_generator (gold preempt ordering)
  Shared/Item/ModifierIds.h     derive_tier_item_class
  Shared/Item/ItemEnums.h       is_special_item
  Server/CmdDropOdds.cpp        shares_of, the per-grade rollup, the chain

The two classifier functions are parsed out of the headers rather than
retyped, so the item roster cannot drift from the roller's.

Usage:  python Scripts/dropodds_offline.py [--csv]
"""

import argparse
import csv
import re
import sqlite3
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DB = ROOT / "Binaries" / "Server" / "gamedata.db"
ITEM_ENUMS = ROOT / "Sources" / "Dependencies" / "Shared" / "Item" / "ItemEnums.h"
OUT = ROOT / "Scripts" / "output"

# --- constants mirrored from Server/EntityManager.h ------------------------
BASE_GOLD_DROP_CHANCE = 3000        # 30%, out of 10000
NEVER_DROP_NPC_TYPES = {21, 34, 64}  # guards, dummies, crops

# --- enum values mirrored from Shared/Item/ItemEnums.h ---------------------
SUB_WEAPON, SUB_ARMOR, SUB_ACCESSORY = 3, 4, 5
WEAPON_WAND, WEAPON_BOW = 7, 8
POS_HEAD, POS_BODY, POS_ARMS, POS_LEGGINGS = 1, 2, 3, 4
POS_LEFT_HAND, POS_BACK, POS_FULL_BODY = 7, 12, 13

GOLD_ITEM_ID = 90


def special_item_ids():
	"""The is_special_item roster, parsed out of ItemEnums.h."""
	text = ITEM_ENUMS.read_text(encoding="utf-8", errors="replace")
	ids = {name: int(value) for name, value in
		re.findall(r"constexpr\s+short\s+(\w+)\s*=\s*(-?\d+)\s*;", text)}

	body = text[text.index("inline bool is_special_item"):]
	body = body[:body.index("\n}")]

	roster = {ids[name] for name in re.findall(r"case\s+ItemId::(\w+)\s*:", body)
		if name in ids}
	for low, high in re.findall(
			r"i_dnum\s*>=\s*ItemId::(\w+)\s*&&\s*i_dnum\s*<=\s*ItemId::(\w+)", body):
		roster.update(range(ids[low], ids[high] + 1))
	return roster


def tier_item_class(sub_type, weapon_class, equip_pos):
	"""Port of derive_tier_item_class (Shared/Item/ModifierIds.h)."""
	if sub_type == SUB_ACCESSORY:
		return None
	if weapon_class == WEAPON_BOW:
		return "bow"
	if weapon_class == WEAPON_WAND:
		return "wand"
	if equip_pos == POS_LEFT_HAND:
		return "shield"
	if sub_type == SUB_WEAPON:
		return "melee_weapon"
	if sub_type != SUB_ARMOR:
		return None
	return {POS_HEAD: "helm", POS_BODY: "body_armor", POS_ARMS: "body_armor",
		POS_FULL_BODY: "body_armor", POS_LEGGINGS: "leggings",
		POS_BACK: "cape"}.get(equip_pos)


def load(db_path):
	db = sqlite3.connect(db_path)
	db.row_factory = sqlite3.Row
	specials = special_item_ids()

	items, gear = {}, set()
	for row in db.execute(
			"select item_id, name, item_sub_type, weapon_class, equip_pos from items"):
		items[row["item_id"]] = row["name"]
		if row["item_id"] in (0, GOLD_ITEM_ID) or row["item_id"] in specials:
			continue
		if tier_item_class(row["item_sub_type"], row["weapon_class"], row["equip_pos"]):
			gear.add(row["item_id"])

	tables = {row["drop_table_id"]: dict(row) for row in db.execute(
		"select drop_table_id, name, guaranteed_secondary, scatter_count from drop_tables")}

	entries = {}
	for row in db.execute(
			"select drop_table_id, item_id, weight from drop_entries where tier = 1"):
		entries.setdefault(row["drop_table_id"], []).append(
			(row["item_id"], row["weight"]))

	npcs = [dict(row) for row in db.execute(
		"select npc_id, name, npc_type, drop_table_id, loot_grade, gold_min, gold_max "
		"from npc_configs")]
	grades = {row["grade"]: dict(row) for row in db.execute("select * from loot_grades")}
	return items, gear, tables, entries, npcs, grades


def shares_of(rows, gear):
	"""Port of CmdDropOdds.cpp shares_of, plus the gold/consumable split that
	the #68 options need in order to be costed."""
	total = sum(weight for _, weight in rows)
	s = dict(nothing=0.0, gold=0.0, consumable=0.0, gear=0.0,
		entries=len(rows), gear_entries=0, total_weight=total)
	if total <= 0:
		return s
	for item_id, weight in rows:
		share = weight / total
		if item_id == 0:
			s["nothing"] += share
		elif item_id == GOLD_ITEM_ID:
			s["gold"] += share
		elif item_id in gear:
			s["gear"] += share
			s["gear_entries"] += 1
		else:
			s["consumable"] += share
	s["any_item"] = s["gold"] + s["consumable"] + s["gear"]
	return s


def apply_drop_multiplier(base, multiplier):
	return max(0, min(10000, int(base * multiplier)))


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("--csv", action="store_true", help="also write CSVs to Scripts/output")
	parser.add_argument("--primary", type=float, default=1.0, help="drop_rates.primary")
	parser.add_argument("--gold", type=float, default=1.0, help="drop_rates.gold")
	args = parser.parse_args()

	items, gear, tables, entries, npcs, grades = load(DB)

	gold_chance = apply_drop_multiplier(BASE_GOLD_DROP_CHANCE, args.gold) / 10000.0
	no_gold = 1.0 - gold_chance

	shares = {tid: shares_of(rows, gear) for tid, rows in entries.items()}

	rollup = {g: dict(npcs=0, no_table=0, any_sum=0.0, gear_sum=0.0, gold_sum=0.0,
		cons_sum=0.0, tables=set(), gear_shares=[]) for g in grades}
	never, ungraded = 0, []
	table_grades = {}

	for npc in npcs:
		if npc["npc_type"] in NEVER_DROP_NPC_TYPES:
			never += 1
			continue
		g = npc["loot_grade"]
		if g not in grades:
			ungraded.append(npc)
			continue
		r = rollup[g]
		r["npcs"] += 1
		tid = npc["drop_table_id"]
		if tid not in shares:
			r["no_table"] += 1
			continue
		s = shares[tid]
		r["any_sum"] += s["any_item"]
		r["gear_sum"] += s["gear"]
		r["gold_sum"] += s["gold"]
		r["cons_sum"] += s["consumable"]
		r["tables"].add(tid)
		r["gear_shares"].append(s["gear"])
		table_grades.setdefault(tid, set()).add(g)

	def pct(p):
		return "-" if p <= 0 else f"{p * 100:.4f}%"

	def one_in(p):
		return "-" if p <= 0 else f"{1 / p:,.0f}"

	print(f"dropodds (offline replica) - db {DB.relative_to(ROOT)}")
	print(f"  drop_rates: primary x{args.primary:.2f}, gold x{args.gold:.2f}")
	print(f"  gold preempt {pct(gold_chance)} - a gold drop skips the stage-1 item roll")
	print(f"  {never} guard/dummy/crop configs never drop and are excluded")
	for npc in ungraded:
		print(f"  NOT PRICED - npc {npc['npc_id']} '{npc['name']}' grade {npc['loot_grade']}")
	print()

	head = ("grade", "npcs", "first", "any item", "gold row", "consum.", "gear",
		"Common", "Rare", "Epic", "Legendary")
	print("Per-kill odds by loot grade:")
	print("  " + f"{head[0]:<13}" + "".join(f"{h:>11}" for h in head[1:]))

	rows_out = []
	for g in sorted(grades):
		r, cfg = rollup[g], grades[g]
		first = apply_drop_multiplier(cfg["first_drop_chance"], args.primary) / 10000.0
		roll = no_gold * first
		n = max(r["npcs"], 1)
		any_item = roll * r["any_sum"] / n
		gear_odds = roll * r["gear_sum"] / n
		gold_row = roll * r["gold_sum"] / n
		cons = roll * r["cons_sum"] / n
		weights = [cfg["weight_common"], cfg["weight_rare"], cfg["weight_epic"],
			cfg["weight_legendary"]]
		total_w = sum(weights)
		tiers = [gear_odds * w / total_w if total_w else 0.0 for w in weights]
		cells = [str(r["npcs"]), f"{first * 100:.2f}%", pct(any_item), pct(gold_row),
			pct(cons), pct(gear_odds)] + [pct(t) for t in tiers]
		print("  " + f"{g} {cfg['name']:<11}"[:13].ljust(13) + "".join(f"{c:>11}" for c in cells))
		rows_out.append(dict(grade=g, name=cfg["name"], npcs=r["npcs"],
			first_drop_chance=cfg["first_drop_chance"], any_item=any_item,
			gold_row=gold_row, consumable=cons, gear=gear_odds,
			common=tiers[0], rare=tiers[1], epic=tiers[2], legendary=tiers[3],
			tables=len(r["tables"])))

	print()
	print("Same odds as kills per outcome (1 in N):")
	print("  " + f"{'grade':<13}" + "".join(f"{h:>11}" for h in
		("gear", "Common", "Rare", "Epic", "Legendary")))
	for row in rows_out:
		w = grades[row["grade"]]
		cells = [one_in(row["gear"])] + [one_in(row[k]) for k in
			("common", "rare", "epic", "legendary")]
		print("  " + f"{row['grade']} {row['name']:<11}"[:13].ljust(13) +
			"".join(f"{c:>11}" for c in cells))

	print()
	print("Gear share spread WITHIN each grade (the term a per-grade knob would collapse):")
	print("  " + f"{'grade':<13}" + "".join(f"{h:>11}" for h in
		("tables", "min", "median", "mean", "max", "zero-gear")))
	for g in sorted(grades):
		vals = rollup[g]["gear_shares"]
		if not vals:
			continue
		zero = sum(1 for v in vals if v <= 0)
		cells = [str(len(rollup[g]["tables"])), pct(min(vals)),
			pct(statistics.median(vals)), pct(statistics.mean(vals)),
			pct(max(vals)), f"{zero}/{len(vals)}"]
		print("  " + f"{g} {grades[g]['name']:<11}"[:13].ljust(13) +
			"".join(f"{c:>11}" for c in cells))

	print()
	print("Stage-1 drop tables in use:")
	print(f"  {'id':>5}  {'name':<28}{'rows':>6}{'gear':>6}{'nothing':>10}"
		f"{'gold':>10}{'consum.':>10}{'gear sh.':>10}  grades")
	table_rows = []
	for tid in sorted(table_grades):
		s, t = shares[tid], tables[tid]
		gl = ",".join(str(x) for x in sorted(table_grades[tid]))
		print(f"  {tid:>5}  {t['name'][:28]:<28}{s['entries']:>6}{s['gear_entries']:>6}"
			f"{pct(s['nothing']):>10}{pct(s['gold']):>10}{pct(s['consumable']):>10}"
			f"{pct(s['gear']):>10}  {gl}")
		table_rows.append(dict(drop_table_id=tid, name=t["name"], rows=s["entries"],
			gear_rows=s["gear_entries"], nothing=s["nothing"], gold=s["gold"],
			consumable=s["consumable"], gear=s["gear"], grades=gl,
			guaranteed_secondary=t["guaranteed_secondary"], scatter=t["scatter_count"]))
	unused = len(tables) - len(table_grades)
	print(f"  {len(table_grades)} table(s) in use; {unused} of {len(tables)} referenced by no npc_config")

	if args.csv:
		OUT.mkdir(parents=True, exist_ok=True)
		for name, data in (("dropodds_grades.csv", rows_out),
				("dropodds_tables.csv", table_rows)):
			with (OUT / name).open("w", newline="", encoding="utf-8") as handle:
				writer = csv.DictWriter(handle, fieldnames=list(data[0]))
				writer.writeheader()
				writer.writerows(data)
			print(f"  wrote {(OUT / name).relative_to(ROOT)}")
	return 0


if __name__ == "__main__":
	sys.exit(main())
