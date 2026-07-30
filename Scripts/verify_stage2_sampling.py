#!/usr/bin/env python3
"""Item Tiers #90 — two independent samplers must agree on the second drop.

#90 asks for the result to be "verified with a sampling harness, not by eye".
Eyeballing a table of 1-in-millions figures proves nothing, but neither does
naive sampling: no feasible trial count resolves 1 in 29,411,765. So this runs
two tiers and is explicit about which rows each one actually covers.

  TIER 1 — EXACT.  Every ordinary stage-2 row in the shipped gamedata.db is
    checked against the original's arithmetic in integer/rational terms. No
    sampling error, and it covers all rows including the rarest. This is the
    proof; tier 2 is the independence check.

  TIER 2 — SAMPLING.  Two samplers are run over the same trial count and their
    empirical frequencies compared with a two-proportion z-test at 4 sigma
    (the convention #50/#51 established for this codebase):

      sampler A  simulates the ORIGINAL's nested dice directly — the outer
                 iDice(1,K) of DeleteNpc, the 1/45 global gate, the 1/G
                 per-monster gate, the 1/M arm pick and the arm's inner 1/D.
                 It never reads our database.
      sampler B  simulates OUR engine over the shipped rows — resolve_drop_chances
                 is the identity while every multiplier is 1.0 and no ordinary
                 table saturates, so the row is picked by walking the cumulative
                 ppb against a uniform draw in [0, 1e9), exactly as
                 roll_drop_row does.

    A row whose expected hit count is under MIN_EXPECTED is reported as beyond
    sampling power rather than counted as a pass.

Sampler A is a transcription of the C control flow and sampler B of the C++
roll, so agreement is evidence the migration says what the original does, not
merely that the migration matches the script that wrote it. Rows the original
never had (Rule B in migrate_stage2_rates.py) have nothing to compare against
and are reported separately.

Usage:
    python Scripts/verify_stage2_sampling.py                 # 200M trials/monster
    python Scripts/verify_stage2_sampling.py --trials 20000000
"""
import argparse
import sqlite3
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from migrate_stage2_rates import (          # the model, single-sourced
	ARM_BY_TYPE, BOSS_GRADE, DELETE_NPC, GATE, GLOBAL_GATE, PPB,
	Report, one_in, orig_body, orig_unique, ppb_for, rarest_arm,
)

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DB = ROOT / "Binaries" / "Server" / "gamedata.db"
OUT = ROOT / "Scripts" / "output"
CHUNK = 10_000_000          # draws per vectorised batch
MIN_EXPECTED = 30.0         # below this a z-test on counts is meaningless
SIGMA = 4.0


def sample_original(npc_type, trials, rng):
	"""Sampler A — the original's nested dice, vectorised. -> {item_id: hits}."""
	spec = DELETE_NPC[npc_type]
	K = spec["K"]
	# A type reaches the unique roster only if DeleteNpc routes it AND
	# bGetItemNameWhenDeleteNpc has both a gate case and an item switch for it.
	# Giant-Ant (t16) is routed but falls through `default: return FALSE`;
	# Slime (t10) is never routed and its body check is direct, not a switch arm.
	has_roster = spec["unique"] and npc_type in GATE and npc_type in ARM_BY_TYPE
	# Source order, not id order: body parts occupy cases 1..K-1 exactly as
	# written and the unique roster is case K. Permuting them changes no
	# marginal, but the shape is worth asserting.
	body = list(spec["body"].items())
	assert len(body) == (K - 1 if spec["unique"] else K), \
		f"t{npc_type}: {len(body)} body arms but outer draw is 1..{K}"
	counts = {}

	done = 0
	while done < trials:
		n = min(CHUNK, trials - done)
		done += n
		# DeleteNpc: switch (iDice(1,K)). Arm K is the unique roster; arms
		# 1..K-1 are the body parts, in source order.
		outer = rng.integers(1, K + 1, size=n) if K > 1 else np.ones(n, dtype=np.int64)

		for slot, (item_id, D) in enumerate(body, start=1):
			hit = (outer == slot) & (rng.integers(1, D + 1, size=n) == 1)
			counts[item_id] = counts.get(item_id, 0) + int(np.count_nonzero(hit))

		if not has_roster:
			continue

		G = GATE[npc_type]
		arms = ARM_BY_TYPE[npc_type]
		M = len(arms)
		# bGetItemNameWhenDeleteNpc: iDice(1,45) == 13, then iDice(1,G) == 11.
		reached = (outer == K)
		reached &= rng.integers(1, GLOBAL_GATE + 1, size=n) == 13
		reached &= rng.integers(1, G + 1, size=n) == 11
		# the item switch: iDice(1,M), then the arm's own iDice(1,D)
		arm = rng.integers(1, M + 1, size=n)
		for slot, (item_id, D) in enumerate(arms, start=1):
			pick = reached & (arm == slot)
			if D != 1:
				# The original compares against a fixed constant (13, 3 or 11);
				# any single face of a D-sided die is 1/D, so face 1 stands in.
				pick = pick & (rng.integers(1, D + 1, size=n) == 1)
			counts[item_id] = counts.get(item_id, 0) + int(np.count_nonzero(pick))
	return counts


def sample_ours(chances, trials, rng):
	"""Sampler B — roll_drop_row over the shipped ppb column. -> hits per row."""
	cumulative = np.cumsum(np.asarray(chances, dtype=np.int64))
	counts = np.zeros(len(chances), dtype=np.int64)
	done = 0
	while done < trials:
		n = min(CHUNK, trials - done)
		done += n
		draw = rng.integers(0, PPB, size=n)
		# searchsorted with 'right' reproduces `if (draw < cumulative) return i`
		idx = np.searchsorted(cumulative, draw, side="right")
		hit = idx < len(chances)
		counts += np.bincount(idx[hit], minlength=len(chances))
	return counts


def z_two_proportion(h1, h2, n):
	"""Two-sided z for two binomial proportions over the same trial count."""
	p1, p2 = h1 / n, h2 / n
	pool = (h1 + h2) / (2 * n)
	if pool <= 0.0 or pool >= 1.0:
		return 0.0
	se = (2.0 * pool * (1.0 - pool) / n) ** 0.5
	return 0.0 if se == 0.0 else abs(p1 - p2) / se


def main():
	ap = argparse.ArgumentParser(description=__doc__,
	                             formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--db", type=Path, default=DEFAULT_DB)
	ap.add_argument("--trials", type=int, default=200_000_000)
	ap.add_argument("--seed", type=int, default=20260730)
	args = ap.parse_args()

	db = sqlite3.connect(args.db)
	db.row_factory = sqlite3.Row
	items = {r["item_id"]: r["name"] for r in db.execute("select item_id, name from items")}

	# stage-2 table -> the npc_type whose arithmetic governs it. The Frost/Nizie
	# split means npc_configs now answers this on its own; where a table is still
	# shared (Centaur + Griffin) the gated type wins, since the other has no
	# original counterpart at all.
	types_for = {}
	grades_for = {}
	for row in db.execute("select npc_type, loot_grade, stage2_table_id "
	                      "from npc_configs where stage2_table_id != 0"):
		types_for.setdefault(row["stage2_table_id"], []).append(row["npc_type"])
		grades_for.setdefault(row["stage2_table_id"], set()).add(row["loot_grade"])
	governing = {}
	for tid, types in types_for.items():
		if BOSS_GRADE in grades_for[tid]:
			continue                                    # boss: out of #90's scope
		gated = [t for t in types if t in GATE]
		governing[tid] = gated[0] if gated else types[0]

	r = Report()
	r("=" * 78)
	r("#90 STAGE-2 VERIFICATION — exact check plus two independent samplers")
	r(f"trials per monster: {args.trials:,}   sigma gate: {SIGMA}   "
	  f"min expected hits: {MIN_EXPECTED:g}")
	r("=" * 78)

	rng = np.random.default_rng(args.seed)
	exact_ok = exact_bad = 0
	sampled = powerless = sample_bad = 0
	rule_b_rows = 0
	failures = []

	for tid in sorted(governing):
		ntype = governing[tid]
		if ntype is None:
			continue
		found = db.execute("select name from drop_tables where drop_table_id = ?",
		                   (tid,)).fetchone()
		if found is None:
			sys.exit(f"npc_configs references drop_table {tid} but drop_tables has "
			         f"no such row — is this database mid-migration?")
		name = found["name"]
		rows = [(x["item_id"], x["drop_chance_ppb"]) for x in db.execute(
			"select item_id, drop_chance_ppb from drop_entries "
			"where drop_table_id = ? order by rowid", (tid,))]
		if not rows:
			continue
		u, b = orig_unique(ntype), orig_body(ntype)
		extra = rarest_arm(ntype)

		# ---- tier 1: exact ------------------------------------------------
		local_bad = []
		for item_id, ppb in rows:
			if item_id in b:
				want, why = ppb_for(b[item_id][0]), "body part"
			elif item_id in u:
				want, why = ppb_for(u[item_id][0]), "original arm"
			else:
				want, why = ppb_for(extra), "Rule B extra"
				rule_b_rows += 1
			if ppb == want:
				exact_ok += 1
			else:
				exact_bad += 1
				local_bad.append((item_id, ppb, want, why))

		# ---- tier 2: sampling --------------------------------------------
		# Only rows with an original counterpart can be compared, and only those
		# with enough expected hits carry any power. A table with none of either
		# would burn the whole trial budget on a result nobody reads.
		comparable = [(i, p) for i, p in rows if i in u or i in b]
		testable = [(i, p) for i, p in comparable
		            if args.trials * p / PPB >= MIN_EXPECTED]
		powerless += len(comparable) - len(testable)
		if not testable:
			if local_bad:
				r("")
				r(f"   --- {tid} {name} (t{ntype})")
				for item_id, ppb, want, why in local_bad:
					r(f"       EXACT FAIL {item_id} {items.get(item_id, '?')}: "
					  f"stored 1 in {one_in(ppb):,.0f}, model says "
					  f"1 in {one_in(want):,.0f} ({why})")
			continue

		hits_ours = sample_ours([p for _, p in rows], args.trials, rng)
		by_item_ours = {i: int(hits_ours[k]) for k, (i, _) in enumerate(rows)}
		hits_orig = sample_original(ntype, args.trials, rng)

		lines = []
		for item_id, ppb in testable:
			expected = args.trials * ppb / PPB
			ho, hb = by_item_ours.get(item_id, 0), hits_orig.get(item_id, 0)
			z = z_two_proportion(ho, hb, args.trials)
			sampled += 1
			flag = ""
			if z > SIGMA:
				sample_bad += 1
				flag = f"   <-- {z:.2f} sigma"
				failures.append((name, item_id, ho, hb, z))
			lines.append(f"       {item_id:4d} {items.get(item_id, '?')[:26]:26s} "
			             f"ours {ho:>8,}  orig {hb:>8,}  exp {expected:>10,.0f}  "
			             f"z={z:>5.2f}{flag}")

		if lines or local_bad:
			r("")
			r(f"   --- {tid} {name} (t{ntype})")
			for item_id, ppb, want, why in local_bad:
				r(f"       EXACT FAIL {item_id} {items.get(item_id, '?')}: "
				  f"stored 1 in {one_in(ppb):,.0f}, model says 1 in {one_in(want):,.0f} ({why})")
			for line in lines:
				r(line)

	r("")
	r("TIER 1 — EXACT")
	r(f"   {exact_ok} rows match the model exactly, {exact_bad} do not")
	r("")
	r("TIER 2 — SAMPLING")
	r(f"   {sampled} rows had enough expected hits to test; "
	  f"{sampled - sample_bad} agreed within {SIGMA:g} sigma, {sample_bad} did not")
	r(f"   {powerless} rows are rarer than {args.trials:,} trials can resolve "
	  f"(expected hits < {MIN_EXPECTED:g}) — covered by tier 1 only")
	r(f"   {rule_b_rows} Rule B rows have no original counterpart to compare against")
	for name, item_id, ho, hb, z in failures:
		r(f"   FAIL {name} item {item_id}: ours {ho:,} vs original {hb:,} ({z:.2f} sigma)")

	ok = exact_bad == 0 and sample_bad == 0
	r("")
	r(f"   => {'PASS' if ok else 'FAIL'}")
	r.write(OUT / "stage2_sampling.log")
	print(f"\nreport: {OUT / 'stage2_sampling.log'}")
	sys.exit(0 if ok else 1)


if __name__ == "__main__":
	main()
