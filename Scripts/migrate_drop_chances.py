#!/usr/bin/env python3
"""Item Tiers #73 — convert drop tables to absolute per-(item, monster) rarity.

Every `drop_entries` row stops being a weight relative to whatever else is in
its table and becomes `drop_chance_ppb` — an absolute per-kill chance in parts
per billion for that item from that monster. Stage 1 and stage 2 split into
separate tables driven by one identical engine, and `npc_configs` references
one of each.

Design contract: docs/adr/0005-absolute-drop-chances.md, PLANS/ItemTiers_Plan.md
§8 Amendment A1.

**This step changes no rates.** Its whole value is that it changed nothing: the
conversion reproduces every current per-kill probability, and the report below
proves it row by row. Boss de-saturation (#87) is a separate, labelled step —
pass --desaturate-bosses for that, and only after this one has been verified.

Usage:
    python Scripts/migrate_drop_chances.py --dry-run     # preview + full report
    python Scripts/migrate_drop_chances.py --verify      # conversion checks only
    python Scripts/migrate_drop_chances.py               # apply
    python Scripts/migrate_drop_chances.py --desaturate-bosses --dry-run

The conversion, per table (the gate is today's gating, which #72 established is
per monster — a monster with no gold dice spends its gold roll on nothing and
reaches the item roll at full rate):

    gate  = reaches_item_roll x first_drop_chance/10000
    stage 1              ppb = round(1e9 x gate x w/W)
      ... plus an item-90 row at exactly 300000000 for gold-carrying monsters,
          because the gold preempt retires and gold competes as an ordinary row
    stage 2 ordinary     ppb = round(1e9 x 0.05 x w/W)      (W includes item_id=0)
    stage 2 guaranteed   ppb = round(1e9 x w/W_real)        (sums to exactly 1e9)
    stage 2 scatter      ppb = round(1e9 x expected_i / roll_count) where
                         expected_i = w_i/W_real + (scatter-1) x w_i/W

The scatter case is exact in EXPECTATION per row rather than exact in
distribution: today's first scatter roll excludes the "nothing" slot and the
rest include it, which one uniform row-set cannot express. Restoring the
original's guaranteed 5-15 / 12-20 items is a deliberate data edit for content
curation, not this migration (which would change rates).
"""

import argparse
import sqlite3
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DB = ROOT / "Binaries" / "Server" / "gamedata.db"
OUT = ROOT / "Scripts" / "output"

PPB = 1_000_000_000
GOLD_ITEM_ID = 90
GOLD_PPB = 300_000_000              # today's flat 30% gold chance, exactly
SECONDARY_PPB_GATE = 0.05           # today's base_secondary_drop_chance, 500/10000
GOLD_PREEMPT = 0.30                 # today's base_gold_drop_chance, 3000/10000
NEVER_DROP_NPC_TYPES = {21, 34, 64}  # guards, dummies, crops - the pipeline's own gate

STAGE1_ID_BASE = 30000
STAGE2_ID_BASE = 40000

# Boss de-saturation (#87, deferred into #73): scale the five boss tables'
# NON-GOLD stage-1 rows so stage 1 stops being a certainty. 0.2 is #87's
# recorded recommendation - twice the non-boss rate, ~2.3x headroom restored.
# The gold row is left at 300000000.
BOSS_STAGE1_SCALE = 0.2

# Legacy fidelity (#73): the original's own arithmetic, from the comments in
# NpcDeadItemGenerator -- repos/HelbreathServer/HGServer/Game.cpp:48254 and
# repos/HB382_CENTUU/HGServer/Game.cpp:47322, which agree.
#
#   m_iPrimaryDropRate 6500  ->  35% of kills drop something
#     of that, iDice(1,10000) <= 6000  ->  60% is gold   = 35 x 60      = 21.0%
#     else m_iSecondaryDropRate 9000   ->  90% consumable = 35 x 40 x 90 = 12.6%
#                                          10% equipment  = 35 x 40 x 10 =  1.4%
#   nothing                                                              = 65.0%
#
# ADR 0005's baseline table measures this against "ours, grades 1-4", so that is
# the scope: bosses keep spec §8's deliberate divergence, which exists because
# stage 1 is now the only venue that tier-rolls.
LEGACY_GOLD_PPB = 210_000_000
LEGACY_CONSUMABLE_PPB = 126_000_000
LEGACY_GEAR_PPB = 14_000_000
LEGACY_FIDELITY_GRADES = {1, 2, 3, 4}

ITEM_ENUMS = ROOT / "Sources" / "Dependencies" / "Shared" / "Item" / "ItemEnums.h"
GEAR_SUB_TYPES = {3, 4, 5}          # weapon, armor, accessory (ItemEnums.h)

# The one group whose members make the mechanical name actively misleading:
# 51 tables of 7 all-consumable rows shared by guard towers, crusade
# structures, dummies and summons. "s1_Dummy_x51" would name 2 of 51.
GROUP_NAME_OVERRIDES = {"Dummy": "structure"}


def special_item_ids():
    """The is_special_item roster, parsed out of ItemEnums.h rather than
    retyped, so this cannot drift from the roller's own list."""
    import re
    text = ITEM_ENUMS.read_text(encoding="utf-8", errors="replace")
    ids = {name: int(value) for name, value in
           re.findall(r"constexpr\s+short\s+(\w+)\s*=\s*(-?\d+)\s*;", text)}
    body = text[text.index("inline bool is_special_item"):]
    body = body[:body.index("\n}")]
    roster = {ids[n] for n in re.findall(r"case\s+ItemId::(\w+)\s*:", body) if n in ids}
    for low, high in re.findall(
            r"i_dnum\s*>=\s*ItemId::(\w+)\s*&&\s*i_dnum\s*<=\s*ItemId::(\w+)", body):
        roster.update(range(ids[low], ids[high] + 1))
    return roster


def drop_category_of(world, item_id):
    """Mirror of hb::server::drop_category_of (DropModel.cpp)."""
    if item_id == GOLD_ITEM_ID:
        return "gold"
    if item_id in world["special_items"]:
        return "unique"
    return "gear" if world["sub_types"].get(item_id) in GEAR_SUB_TYPES else "consumable"


# --------------------------------------------------------------------------
# read
# --------------------------------------------------------------------------

def read_world(db):
    """Everything the conversion needs, straight off the pre-migration schema."""
    w = {}
    w["fdc"] = {g: f for g, f in db.execute(
        "SELECT grade, first_drop_chance FROM loot_grades")}
    w["tables"] = {t: dict(id=t, name=n, description=d,
                           guaranteed=bool(g), scatter=s)
                   for t, n, d, g, s in db.execute(
        "SELECT drop_table_id, name, description, guaranteed_secondary,"
        " scatter_count FROM drop_tables ORDER BY drop_table_id")}

    rows = defaultdict(list)
    for tid, stage, item, weight, mn, mx in db.execute(
            "SELECT drop_table_id, tier, item_id, weight, min_count, max_count"
            " FROM drop_entries ORDER BY drop_table_id, tier, item_id"):
        if stage not in (1, 2) or weight <= 0:
            continue                       # the loader skips these too
        rows[(tid, stage)].append(dict(item=item, weight=weight, mn=mn, mx=mx))
    w["rows"] = rows

    npcs = []
    for nid, name, ntype, gmin, gmax, tid, grade in db.execute(
            "SELECT npc_id, name, npc_type, gold_min, gold_max, drop_table_id,"
            " loot_grade FROM npc_configs ORDER BY npc_id"):
        places_gold = gmin > 0 or gmax > 0
        reaches = (1.0 - GOLD_PREEMPT) if places_gold else 1.0
        npcs.append(dict(
            id=nid, name=name, type=ntype, table=tid, grade=grade,
            places_gold=places_gold,
            never_drops=ntype in NEVER_DROP_NPC_TYPES,
            gate1=reaches * w["fdc"].get(grade, 0) / 10000.0))
    w["npcs"] = npcs
    w["special_items"] = special_item_ids()
    w["sub_types"] = {i: t for i, t in db.execute(
        "SELECT item_id, item_sub_type FROM items")}
    return w


# --------------------------------------------------------------------------
# convert
# --------------------------------------------------------------------------

def distribute(shares, target_ppb):
    """Split `target_ppb` across `shares` (any positive scale) as integers that
    sum to EXACTLY target_ppb, by largest remainder.

    Rounding each row on its own leaves a table a few ppb off its intended sum,
    and both rules that matter are exact integer comparisons - a guaranteed
    table sums to exactly 1_000_000_000, and saturation is a > against it. A
    float tolerance there is precisely what the ppb encoding exists to avoid.
    """
    total = sum(shares)
    scaled = [s / total * target_ppb for s in shares]
    floors = [int(x) for x in scaled]
    order = sorted(range(len(scaled)), key=lambda i: scaled[i] - floors[i],
                   reverse=True)
    for i in order[:target_ppb - sum(floors)]:
        floors[i] += 1
    return floors


def convert_stage1(world, table_id, gate, places_gold):
    """One stage-1 table's rows as (item, ppb, mn, mx), gold row included."""
    src = [r for r in world["rows"].get((table_id, 1), []) if r["item"] != 0]
    # The item rows sum to exactly the gate; "nothing" is whatever is left over.
    ppbs = distribute([r["weight"] for r in src], round(PPB * gate))
    out = [(r["item"], p, r["mn"], r["mx"]) for r, p in zip(src, ppbs)]
    if places_gold:
        # Count comes from the monster's own gold dice at spawn time, exactly as
        # it does today, so min/max on the row are unused. A boss therefore lands
        # on exactly 700000000 + 300000000 = 1e9 - saturated, which is the
        # defect #87 records and step 2 corrects.
        out.append((GOLD_ITEM_ID, GOLD_PPB, 0, 0))
    return sorted(out)


def convert_stage2(world, table_id):
    """One stage-2 table as (rows, roll_count, placement, delay)."""
    t = world["tables"][table_id]
    src = world["rows"].get((table_id, 2), [])
    total = sum(r["weight"] for r in src)
    total_real = sum(r["weight"] for r in src if r["item"] != 0)
    real = [r for r in src if r["item"] != 0]
    scatter = t["scatter"]
    roll_count = scatter if scatter > 0 else 1

    if scatter > 0:
        # Today: roll 0 excludes "nothing" when the table is guaranteed, rolls
        # 1..n-1 include it. Encode each row's EXPECTED COUNT divided by the
        # roll count, which reproduces every per-row expectation exactly and
        # keeps every ratio inside the table.
        shares = [((r["weight"] / total_real) if t["guaranteed"]
                   else (r["weight"] / total))
                  + (scatter - 1) * r["weight"] / total for r in real]
        target = round(PPB * sum(shares) / roll_count)
    elif t["guaranteed"]:
        # A second item on every kill: the rows must sum to EXACTLY 1e9.
        shares = [r["weight"] for r in real]
        target = PPB
    else:
        shares = [r["weight"] for r in real]
        target = round(PPB * SECONDARY_PPB_GATE * total_real / total)

    ppbs = distribute(shares, target)
    rows = [(r["item"], p, r["mn"], r["mx"]) for r, p in zip(real, ppbs)]
    return (sorted(rows), roll_count,
            "spiral" if scatter > 0 else "single", "decay")


def apply_legacy_fidelity(world, rows):
    """Re-price one stage-1 table at the original's outcome rates.

    The original decided the OUTCOME first and only then which item: 21% gold,
    12.6% consumable, 1.4% equipment, 65% nothing. So each bucket gets its
    share and the rows inside it keep their relative rarity to each other -
    the ratios the migration preserved are untouched, only the three bucket
    totals move.

    Uniques ride the consumable bucket because that is where the original put
    them: the stones and Ancient Pieces are cases 8 and 9 of the dwValue table
    INSIDE the consumable branch, not the equipment branch.

    A bucket with no rows is skipped and its share falls through to "nothing" -
    a monster with no equipment in its table drops no equipment, which is what
    the original does too.
    """
    buckets = {"gold": LEGACY_GOLD_PPB,
               "consumable": LEGACY_CONSUMABLE_PPB,
               "gear": LEGACY_GEAR_PPB}
    grouped = defaultdict(list)
    for row in rows:
        category = drop_category_of(world, row[0])
        grouped["consumable" if category == "unique" else category].append(row)

    out = []
    for bucket, target in buckets.items():
        members = grouped.get(bucket)
        if not members:
            continue
        ppbs = distribute([r[1] for r in members], target)
        out += [(r[0], p, r[2], r[3]) for r, p in zip(members, ppbs)]
    return sorted(out)


def group_name(prefix, members, table_names, count):
    """A name that says what editing this definition will affect."""
    first = table_names[members[0]].replace("drops_", "")
    first = GROUP_NAME_OVERRIDES.get(first, first)
    return f"{prefix}_{first}" if count == 1 else f"{prefix}_{first}_x{count}"


def build(world, desaturate_bosses, legacy_fidelity):
    """The whole new dataset, plus the bookkeeping the report and verify need."""
    npc_by_table = defaultdict(list)
    for n in world["npcs"]:
        if not n["never_drops"] and n["table"] != 0:
            npc_by_table[n["table"]].append(n)

    boss_tables = {n["table"] for n in world["npcs"] if n["grade"] == 5}

    # ---- stage 1: convert, then collapse identical definitions -------------
    stage1_of_table = {}          # old table id -> signature
    unreferenced = []
    for tid in sorted(t for (t, s) in world["rows"] if s == 1):
        users = npc_by_table.get(tid)
        if not users:
            unreferenced.append(tid)
            continue
        gates = {(u["gate1"], u["places_gold"]) for u in users}
        if len(gates) != 1:
            raise SystemExit(
                f"table {tid} is referenced with {len(gates)} different gates; "
                "the conversion assumes one gate per table (see #73 analysis)")
        gate, places_gold = gates.pop()
        rows = convert_stage1(world, tid, gate, places_gold)
        grade = users[0]["grade"]
        if desaturate_bosses and tid in boss_tables:
            # Scale the NON-GOLD rows only, through distribute() so the table
            # lands on exactly its intended sum rather than a few ppb off it.
            items = [r for r in rows if r[0] != GOLD_ITEM_ID]
            gold = [r for r in rows if r[0] == GOLD_ITEM_ID]
            target = round(sum(r[1] for r in items) * BOSS_STAGE1_SCALE)
            ppbs = distribute([r[1] for r in items], target)
            rows = sorted([(r[0], p, r[2], r[3])
                           for r, p in zip(items, ppbs)] + gold)
        if legacy_fidelity and grade in LEGACY_FIDELITY_GRADES:
            rows = apply_legacy_fidelity(world, rows)
        stage1_of_table[tid] = tuple(rows)

    s1_members = defaultdict(list)
    for tid, sig in stage1_of_table.items():
        s1_members[sig].append(tid)

    table_names = {t: v["name"] for t, v in world["tables"].items()}
    stage1_defs = {}              # signature -> new table row
    for sig in sorted(s1_members, key=lambda s: min(s1_members[s])):
        members = sorted(s1_members[sig])
        new_id = STAGE1_ID_BASE + len(stage1_defs) + 1
        stage1_defs[sig] = dict(
            id=new_id, rows=sig, stage=1, roll_count=1,
            placement="single", delay="death",
            name=group_name("s1", members, table_names, len(members)),
            description=("shared by %d monsters: %s" % (
                len(members), ", ".join(
                    table_names[m].replace("drops_", "") for m in members))
                if len(members) > 1 else
                world["tables"][members[0]]["description"]),
            members=members)

    # ---- stage 2: same treatment, keyed on the whole definition ------------
    stage2_of_table, s2_members = {}, defaultdict(list)
    for tid in sorted(t for (t, s) in world["rows"] if s == 2):
        if tid not in npc_by_table:
            if tid not in unreferenced:
                unreferenced.append(tid)
            continue
        rows, roll_count, placement, delay = convert_stage2(world, tid)
        sig = (tuple(rows), roll_count, placement, delay)
        stage2_of_table[tid] = sig
        s2_members[sig].append(tid)

    stage2_defs = {}
    for sig in sorted(s2_members, key=lambda s: min(s2_members[s])):
        members = sorted(s2_members[sig])
        rows, roll_count, placement, delay = sig
        new_id = STAGE2_ID_BASE + len(stage2_defs) + 1
        stage2_defs[sig] = dict(
            id=new_id, rows=rows, stage=2, roll_count=roll_count,
            placement=placement, delay=delay,
            name=group_name("s2", members, table_names, len(members)),
            description=("shared by %d monsters: %s" % (
                len(members), ", ".join(
                    table_names[m].replace("drops_", "") for m in members))
                if len(members) > 1 else
                world["tables"][members[0]]["description"]),
            members=members)

    # ---- npc_configs references -------------------------------------------
    npc_refs = {}
    for n in world["npcs"]:
        s1 = stage1_defs[stage1_of_table[n["table"]]]["id"] \
            if n["table"] in stage1_of_table else 0
        s2 = stage2_defs[stage2_of_table[n["table"]]]["id"] \
            if n["table"] in stage2_of_table else 0
        npc_refs[n["id"]] = (s1, s2)

    return dict(stage1_defs=stage1_defs, stage2_defs=stage2_defs,
                stage1_of_table=stage1_of_table, stage2_of_table=stage2_of_table,
                npc_refs=npc_refs, unreferenced=sorted(set(unreferenced)),
                npc_by_table=npc_by_table, boss_tables=boss_tables)


# --------------------------------------------------------------------------
# verify — the migration's pass/fail gate
# --------------------------------------------------------------------------

def old_probabilities(world, npc):
    """Today's per-kill probability (stage 2 scatter: expected count) per item."""
    out = {}
    tid = npc["table"]
    src = world["rows"].get((tid, 1), [])
    if src:
        total = sum(r["weight"] for r in src)
        for r in src:
            if r["item"] != 0:
                out[(1, r["item"])] = npc["gate1"] * r["weight"] / total
    if npc["places_gold"]:
        out[(1, GOLD_ITEM_ID)] = out.get((1, GOLD_ITEM_ID), 0.0) + GOLD_PREEMPT

    src = world["rows"].get((tid, 2), [])
    if src:
        t = world["tables"][tid]
        total = sum(r["weight"] for r in src)
        total_real = sum(r["weight"] for r in src if r["item"] != 0)
        for r in src:
            if r["item"] == 0:
                continue
            if t["scatter"] > 0:
                first = (r["weight"] / total_real) if t["guaranteed"] \
                    else (r["weight"] / total)
                p = first + (t["scatter"] - 1) * r["weight"] / total
            elif t["guaranteed"]:
                p = r["weight"] / total_real
            else:
                p = SECONDARY_PPB_GATE * r["weight"] / total
            out[(2, r["item"])] = p
    return out


def new_probabilities(built, npc):
    out = {}
    s1_id, s2_id = built["npc_refs"][npc["id"]]
    for defs, stage, want in ((built["stage1_defs"], 1, s1_id),
                              (built["stage2_defs"], 2, s2_id)):
        if want == 0:
            continue
        d = next(v for v in defs.values() if v["id"] == want)
        for item, ppb, _, _ in d["rows"]:
            out[(stage, item)] = ppb / PPB * d["roll_count"]
    return out


def verify(world, built, desaturate_bosses, legacy_fidelity):
    """Per-row old-vs-new. Returns (findings, worst) with worst in relative terms."""
    findings, worst = [], []
    for npc in world["npcs"]:
        if npc["never_drops"] or npc["table"] == 0:
            continue
        old = old_probabilities(world, npc)
        new = new_probabilities(built, npc)
        for key in sorted(set(old) | set(new)):
            o, n = old.get(key, 0.0), new.get(key, 0.0)
            if o == 0.0 and n == 0.0:
                continue
            if o == 0.0 or n == 0.0:
                findings.append(f"{npc['name']} stage {key[0]} item {key[1]}: "
                                f"{o:.10f} -> {n:.10f} (row appeared/vanished)")
                continue
            rel = abs(n - o) / o
            # Rows a labelled step moved ON PURPOSE are separated from the
            # migration's own residual, so one can never hide inside the other.
            deliberate = (
                (desaturate_bosses and npc["grade"] == 5
                 and key[0] == 1 and key[1] != GOLD_ITEM_ID)
                or (legacy_fidelity and npc["grade"] in LEGACY_FIDELITY_GRADES
                    and key[0] == 1))
            worst.append((rel, npc["name"], key, o, n, deliberate))
    worst.sort(reverse=True)
    return findings, worst


# --------------------------------------------------------------------------
# report
# --------------------------------------------------------------------------

def write_report(world, built, worst, findings, desaturate_bosses,
                 legacy_fidelity, path):
    L = []
    add = L.append
    add("Item Tiers #73 - drop-chance migration")
    add(f"generated {datetime.now(timezone.utc):%Y-%m-%d %H:%M:%SZ}")
    steps = []
    if desaturate_bosses: steps.append("boss de-saturation (#87)")
    if legacy_fidelity:   steps.append("legacy fidelity 21/12.6/1.4/65")
    add("mode: " + (" + ".join(steps) if steps
                    else "mechanical conversion - no rate changes"))
    add("")

    old_tables = len(world["tables"])
    old_entries = sum(len(v) for v in world["rows"].values())
    new_tables = len(built["stage1_defs"]) + len(built["stage2_defs"])
    new_entries = (sum(len(d["rows"]) for d in built["stage1_defs"].values())
                   + sum(len(d["rows"]) for d in built["stage2_defs"].values()))
    add("=" * 76)
    add("before / after")
    add("=" * 76)
    add(f"  drop_tables       {old_tables:>6}  ->  {new_tables:>6}"
        f"   ({len(built['stage1_defs'])} stage-1 + {len(built['stage2_defs'])} stage-2)")
    add(f"  drop_entries      {old_entries:>6}  ->  {new_entries:>6}")
    add(f"  stage-1 tables collapsed: "
        f"{len(built['stage1_of_table'])} -> {len(built['stage1_defs'])}")
    add(f"  stage-2 tables collapsed: "
        f"{len(built['stage2_of_table'])} -> {len(built['stage2_defs'])}")
    if built["unreferenced"]:
        add(f"  dropped {len(built['unreferenced'])} table(s) no npc_config references: "
            + ", ".join(f"{t} '{world['tables'][t]['name']}'"
                        for t in built["unreferenced"]))
    add("")

    add("=" * 76)
    add("per-row verification - old vs new per-kill probability")
    add("=" * 76)
    real = [w for w in worst if not w[5]]
    scaled = [w for w in worst if w[5]]
    add(f"  rows compared: {len(worst)}")
    if scaled:
        add(f"  rows a labelled step moved deliberately: {len(scaled)} (excluded below)")
    for thr, lbl in ((1e-9, "1 ppb"), (1e-6, "1 ppm"), (1e-4, "0.01%"),
                     (1e-3, "0.1%"), (1e-2, "1%")):
        add(f"    off by more than {lbl:>6}: {sum(1 for w in real if w[0] > thr)}")
    add("")
    add("  worst 15 (quantisation of 2004-era weights into ppb):")
    for rel, name, key, o, n, _ in real[:15]:
        add(f"    {rel*100:9.6f}%  {name:<22} stage {key[0]} item {key[1]:>4}  "
            f"{o:.10f} -> {n:.10f}")
    if scaled:
        add("")
        add("  rows moved deliberately by a labelled step (expected, not a regression):")
        for rel, name, key, o, n, _ in scaled[:10]:
            add(f"    {rel*100:9.4f}%  {name:<22} stage {key[0]} item {key[1]:>4}  "
                f"{o:.10f} -> {n:.10f}")
    if findings:
        add("")
        add("  ROWS THAT APPEARED OR VANISHED:")
        for f in findings:
            add(f"    {f}")
    add("")

    add("=" * 76)
    add("saturation - summed probability per table (a multiplier cannot move 1.0)")
    add("=" * 76)
    for label, defs in (("stage 1", built["stage1_defs"]),
                        ("stage 2", built["stage2_defs"])):
        rows = sorted(defs.values(), key=lambda d: -sum(r[1] for r in d["rows"]))
        add(f"  {label}:")
        for d in rows:
            s = sum(r[1] for r in d["rows"])
            flag = ""
            if s == PPB:
                flag = "  <- sums to exactly 1.0 (guaranteed; multiplier is a no-op)"
            elif s > PPB:
                flag = "  <- OVER 1.0, would clamp"
            if s >= PPB * 0.9 or flag:
                add(f"    {d['id']} {d['name']:<28} sum={s/PPB:>8.6f}"
                    f" roll_count={d['roll_count']}{flag}")
        add(f"    ... {sum(1 for d in rows if sum(r[1] for r in d['rows']) < PPB*0.9)}"
            f" further table(s) below 0.9")
    add("")

    add("=" * 76)
    add("new drop tables")
    add("=" * 76)
    for label, defs in (("stage 1", built["stage1_defs"]),
                        ("stage 2", built["stage2_defs"])):
        add(f"  {label}:")
        for d in sorted(defs.values(), key=lambda d: d["id"]):
            add(f"    {d['id']}  {d['name']:<30} rows={len(d['rows']):>3} "
                f"roll={d['roll_count']} {d['placement']}/{d['delay']}  "
                f"[{len(d['members'])} old table(s)]")
    add("")

    add("=" * 76)
    add("every row, as stored and as read")
    add("=" * 76)
    for label, defs in (("stage 1", built["stage1_defs"]),
                        ("stage 2", built["stage2_defs"])):
        for d in sorted(defs.values(), key=lambda d: d["id"]):
            add(f"  {d['id']} {d['name']} ({label}, roll_count={d['roll_count']})")
            for item, ppb, mn, mx in d["rows"]:
                add(f"      item {item:>4}  ppb {ppb:>12,}  = 1 in "
                    f"{PPB/ppb:>14,.2f}  count {mn}-{mx}")
    path.write_text("\n".join(L) + "\n", encoding="utf-8")
    return L


# --------------------------------------------------------------------------
# apply
# --------------------------------------------------------------------------

NEW_SCHEMA = """
DROP VIEW IF EXISTS drop_entries_readable;
DROP TABLE IF EXISTS drop_entries;
DROP TABLE IF EXISTS drop_tables;
CREATE TABLE drop_tables (
  drop_table_id  INTEGER PRIMARY KEY,
  name           TEXT NOT NULL,
  description    TEXT NOT NULL,
  stage          INTEGER NOT NULL DEFAULT 1,
  roll_count_min INTEGER NOT NULL DEFAULT 1,
  roll_count_max INTEGER NOT NULL DEFAULT 1,
  placement      TEXT NOT NULL DEFAULT 'single',
  delay          TEXT NOT NULL DEFAULT 'death'
);
CREATE TABLE drop_entries (
  drop_table_id   INTEGER NOT NULL,
  item_id         INTEGER NOT NULL,
  drop_chance_ppb INTEGER NOT NULL,
  min_count       INTEGER NOT NULL,
  max_count       INTEGER NOT NULL,
  PRIMARY KEY (drop_table_id, item_id)
);
CREATE TABLE IF NOT EXISTS drop_multipliers (
  scope      TEXT NOT NULL,
  key        TEXT NOT NULL,
  multiplier REAL NOT NULL DEFAULT 1.0,
  PRIMARY KEY (scope, key)
);
-- Rarity is authored and reasoned about as "1 in N"; the column is ppb so the
-- guaranteed-table rule can be an exact integer equality. This view is the
-- readable half, for hand-editing gamedata.db.
CREATE VIEW drop_entries_readable AS
  SELECT e.drop_table_id, t.name AS table_name, t.stage, e.item_id,
         e.drop_chance_ppb, 1000000000.0 / e.drop_chance_ppb AS one_in,
         e.min_count, e.max_count
  FROM drop_entries e JOIN drop_tables t USING (drop_table_id)
  ORDER BY e.drop_table_id, e.drop_chance_ppb DESC;
"""

MULTIPLIER_SEED = ([("global", "")]
                   + [("stage", s) for s in ("1", "2")]
                   + [("category", c) for c in ("gear", "consumable", "gold", "unique")]
                   + [("grade", str(g)) for g in range(1, 6)])


def apply(db, built):
    db.executescript(NEW_SCHEMA)
    for scope, key in MULTIPLIER_SEED:
        db.execute("INSERT OR IGNORE INTO drop_multipliers(scope, key, multiplier)"
                   " VALUES(?,?,1.0)", (scope, key))

    for defs in (built["stage1_defs"], built["stage2_defs"]):
        for d in sorted(defs.values(), key=lambda d: d["id"]):
            db.execute(
                "INSERT INTO drop_tables(drop_table_id, name, description, stage,"
                " roll_count_min, roll_count_max, placement, delay)"
                " VALUES(?,?,?,?,?,?,?,?)",
                (d["id"], d["name"], d["description"], d["stage"],
                 d["roll_count"], d["roll_count"], d["placement"], d["delay"]))
            for item, ppb, mn, mx in d["rows"]:
                db.execute(
                    "INSERT INTO drop_entries(drop_table_id, item_id,"
                    " drop_chance_ppb, min_count, max_count) VALUES(?,?,?,?,?)",
                    (d["id"], item, ppb, mn, mx))

    cols = [r[1] for r in db.execute("PRAGMA table_info(npc_configs)")]
    if "stage1_table_id" not in cols:
        db.execute("ALTER TABLE npc_configs ADD COLUMN stage1_table_id"
                   " INTEGER NOT NULL DEFAULT 0")
    if "stage2_table_id" not in cols:
        db.execute("ALTER TABLE npc_configs ADD COLUMN stage2_table_id"
                   " INTEGER NOT NULL DEFAULT 0")
    for npc_id, (s1, s2) in built["npc_refs"].items():
        db.execute("UPDATE npc_configs SET stage1_table_id=?, stage2_table_id=?"
                   " WHERE npc_id=?", (s1, s2, npc_id))

    # first_drop_chance is subsumed by the per-row chances and retires. SQLite
    # can drop a column since 3.35; rebuild the table if this one cannot.
    if "first_drop_chance" in [r[1] for r in db.execute("PRAGMA table_info(loot_grades)")]:
        try:
            db.execute("ALTER TABLE loot_grades DROP COLUMN first_drop_chance")
        except sqlite3.OperationalError:
            db.executescript(
                "CREATE TABLE loot_grades_new (grade INTEGER PRIMARY KEY,"
                " name TEXT NOT NULL, weight_common INTEGER NOT NULL,"
                " weight_rare INTEGER NOT NULL, weight_epic INTEGER NOT NULL,"
                " weight_legendary INTEGER NOT NULL);"
                "INSERT INTO loot_grades_new SELECT grade, name, weight_common,"
                " weight_rare, weight_epic, weight_legendary FROM loot_grades;"
                "DROP TABLE loot_grades;"
                "ALTER TABLE loot_grades_new RENAME TO loot_grades;")

    db.execute("INSERT OR REPLACE INTO meta(key, value)"
               " VALUES('schema_version','10')")


# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--db", type=Path, default=DEFAULT_DB)
    ap.add_argument("--dry-run", action="store_true",
                    help="preview and write the full report; change nothing")
    ap.add_argument("--verify", action="store_true",
                    help="run the conversion checks only, no report, no write")
    ap.add_argument("--desaturate-bosses", action="store_true",
                    help="step 2 (#87): scale the five boss tables' non-gold "
                         "stage-1 rows by %.2f. A deliberate rate change - run "
                         "it only after step 1 has been verified." % BOSS_STAGE1_SCALE)
    ap.add_argument("--legacy-fidelity", action="store_true",
                    help="re-price grade 1-4 stage-1 tables at the original's "
                         "21%% gold / 12.6%% consumable / 1.4%% equipment / 65%% "
                         "nothing (NpcDeadItemGenerator). A deliberate rate "
                         "change - a fidelity fix, not a rebalance.")
    args = ap.parse_args()

    if not args.db.exists():
        sys.exit(f"no such database: {args.db}")

    db = sqlite3.connect(args.db)
    cols = [r[1] for r in db.execute("PRAGMA table_info(drop_entries)")]
    if "drop_chance_ppb" in cols:
        sys.exit("drop_entries already carries drop_chance_ppb - this database "
                 "has been migrated. Restore a pre-migration copy to re-run.")

    world = read_world(db)
    built = build(world, args.desaturate_bosses, args.legacy_fidelity)
    findings, worst = verify(world, built, args.desaturate_bosses,
                             args.legacy_fidelity)

    real = [w for w in worst if not w[5]]
    over_1pct = [w for w in real if w[0] > 1e-2]
    ok = not findings and not over_1pct

    OUT.mkdir(parents=True, exist_ok=True)
    suffix = ("_desaturate" if args.desaturate_bosses else "") +              ("_legacy" if args.legacy_fidelity else "")
    report = OUT / f"drop_migration{suffix}.log"
    if not args.verify:
        write_report(world, built, worst, findings, args.desaturate_bosses,
                     args.legacy_fidelity, report)

    print(f"stage-1 tables {len(built['stage1_of_table'])} -> "
          f"{len(built['stage1_defs'])} definitions")
    print(f"stage-2 tables {len(built['stage2_of_table'])} -> "
          f"{len(built['stage2_defs'])} definitions")
    print(f"rows compared: {len(worst)}   worst non-deliberate drift: "
          f"{real[0][0]*100:.6f}%" if real else "no rows compared")
    print(f"rows over 1%: {len(over_1pct)}   appeared/vanished: {len(findings)}")
    for f in findings[:10]:
        print("  !", f)
    if not args.verify:
        print(f"report: {report}")

    if args.verify:
        print("VERIFY:", "PASS" if ok else "FAIL")
        return 0 if ok else 1
    if args.dry_run:
        print("dry run - nothing written")
        return 0
    if not ok:
        sys.exit("refusing to apply: verification failed (see above)")

    apply(db, built)
    db.commit()
    print(f"applied to {args.db}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
