"""Derive the boss scatter rosters and their absolute per-roll chances (#89).

Two independent inputs, unioned:

1. THE ORIGINAL'S MECHANISM AND RATES.
   `bGetMultipleItemNamesWhenDeleteNpc`, repos/HB382_CENTUU/HGServer/Game.cpp:47119
   (byte-identical to repos/HelbreathServer/HGServer/Game.cpp:48051). Called from
   CGame::DeleteNpc, so this is the corpse-decay drop, spread over the fixed 5x5
   spiral. Parameters: Wyvern(66) and Fire-Wyvern(73) get (probability 50, min 5,
   max 15); Abaddon(81) gets (50, 12, 20).

   The loop runs `iMax` times. `if (i > iMin) iProb = iProbability` splits it into
   a HEAD of `iMin+1` rolls at fProb 1.0 and a TAIL of the rest at fProb 5.0, and
   every rarity bound is multiplied by fProb -- so the tail is exactly 5x rarer in
   all three blocks. On a head roll only, `if (iItemID == 0 && iProb == 100)
   iItemID = 90` hands out Gold; THAT fallback is what makes the head guaranteed
   and what puts the floor under the yield.

   Within one roll the blocks cascade: the per-boss jackpot block, then the
   per-boss rare block only if the jackpot produced nothing, then the shared
   common block only if both produced nothing. `iDice(a, b)` is the sum of `a`
   rolls of 1..b (Game.cpp:16519), so `iDice(1, n) == k` is exactly 1/n.

2. THE OLYMPIA ROSTER (owner decision, 2026-07-30).
   https://www.helbreath.net/wiki/Monsters_and_drops -- Ice Wyvern / Fire Wyvern /
   The Abaddon, its "Uncommom Drops" and "Rare Drops" columns, mapped onto our
   item ids and filtered to what we actually carry. The owner's call is that this
   roster ships rather than the original's narrower block contents. It also
   independently corroborates two things: Olympia routes the original's
   mislabelled `case 69` ice block to the Ice Wyvern (all seven live items), and
   Fire-Wyvern's 9-case rare block is real (the older sim script omitted it).

RATE RULE: `ppb = max(original rate for that boss, the item's Olympia tier
anchor)`. Taking the maximum means adopting Olympia's wider roster can never
make an item rarer than the original had it -- every move is a loosening.

Gold is NOT a row. It is the head roll's remainder, exactly as in the original,
so widening the roster necessarily spends gold's share. The report below prints
what that costs.

Run:  python Scripts/derive_scatter_rosters.py [--simulate]
"""
import argparse
import collections
import random
import sqlite3
import sys
from fractions import Fraction
from pathlib import Path

DB = Path(__file__).resolve().parent.parent / "Binaries" / "Server" / "gamedata.db"

PPB = 1_000_000_000

# Gold is the head fallback, never a row.
GOLD = 90

# fProb multipliers the original applies per block (Game.cpp:47137-47139).
FACTOR_JACKPOT = 8
FACTOR_RARE = 4
FACTOR_COMMON = 1

# The five ids the original's blocks reference but its own Item.cfg never defined
# (its roster stops at 890), so _bInitItemAttr failed and those slots dropped
# nothing. Owner decision 2026-07-30: they get no rows.
PHANTOM = {936, 937, 942, 943, 947}

# ---------------------------------------------------------------------------
# 1. The original's cascade, transcribed case by case.
#    Each block: (selector, factor, {case: (item_id, bound, hit)}).
#    A case's chance is 1/selector * 1/(bound * factor * fProb).
# ---------------------------------------------------------------------------

# The shared common block, Game.cpp:47235-47260. All cases share bound 2, hit 2,
# so the block yields something on exactly half of all head rolls.
COMMON_CASES = {
    1: 740, 2: 741, 3: 742,                 # Bag of Gold medium / large / largest
    4: 650, 5: 650, 6: 650, 7: 650,         # Zemstone of Sacrifice
    8: 656, 9: 656,                         # Stone of Xelima
    10: 657, 11: 657, 12: 657,              # Stone of Merien
    13: 335, 14: 335, 15: 335,              # Emerald Ring
    16: 290, 17: 290, 18: 290,              # Flameberge +3 (LLF)
    19: 259, 20: 259,                       # Magic Wand (M.Shield)
    21: 300, 22: 311, 23: 305, 24: 308,     # the four Magic Necklaces
}
COMMON_BLOCK = (24, FACTOR_COMMON,
                {c: (i, 2, 2) for c, i in COMMON_CASES.items()})

# Wyvern. The archive labels this `case 69`, but 69 is Devlin and Wyvern is 66,
# so it is unreachable dead code in both archives. Owner decision 2026-07-30:
# repair it -- route type 66 here. Olympia lists exactly these seven live items
# for its Ice Wyvern, which is independent confirmation of the intent.
WYVERN_JACKPOT = (4, FACTOR_JACKPOT, {
    1: (634, 6000, 3),      # Ring of Wizard
    2: (636, 5000, 3),      # Ring of Grand Mage
    3: (614, 3000, 2),      # Sword of Ice Elemental
    4: (380, 4500, 3),      # Ice Storm Manual
})
WYVERN_RARE = (6, FACTOR_RARE, {
    1: (642, 500, 2),       # Necklace of Ice Protection
    2: (643, 2000, 2),      # Necklace of Ice Elemental
    3: (943, 1000, 3),      # Ice Axe        -- phantom, never defined
    4: (734, 1500, 3),      # Ring of Arch Mage
    5: (942, 500, 3),       # Ice Hammer     -- phantom, never defined
    6: (738, 500, 2),       # Dark Mage Magic Wand (the comment says Berserk
})                          #   Wand MS.20, but 738 is DarkMageMagicWand in the
                            #   original's own cfg and 861 is the Berserk wand.

FIREWYVERN_JACKPOT = (7, FACTOR_JACKPOT, {
    1: (860, 5000, 3),      # Necklace of Xelima
    2: (630, 3000, 2),      # Ring of the Xelima
    3: (738, 3000, 2),      # Dark Mage Magic Wand (same comment error)
    4: (735, 3000, 2),      # Ring of Dragon Power
    5: (20, 3000, 2),       # Excalibur
    6: (382, 3000, 3),      # Bloody Shock Wave Manual
    7: (381, 3000, 3),      # Mass Fire Strike Manual
})
FIREWYVERN_RARE = (9, FACTOR_RARE, {
    1: (645, 1000, 2),      # Necklace of Efreet
    2: (638, 500, 2),       # Necklace of Fire Protection
    3: (636, 1000, 3),      # Ring of Grand Mage
    4: (734, 800, 3),       # Ring of Arch Mage
    5: (634, 500, 3),       # Ring of Wizard
    6: (290, 500, 2),       # Flameberge +3 (LLF)
    7: (490, 500, 3),       # Blood Sword
    8: (491, 500, 3),       # Blood Axe
    9: (492, 500, 3),       # Blood Rapier
})

ABADDON_JACKPOT = (6, FACTOR_JACKPOT, {
    1: (20, 100, 3),        # Excalibur
    2: (647, 100, 3),       # Necklace of Stone Golem
    3: (860, 100, 3),       # Necklace of Xelima
    4: (936, 100, 3),       # Merien Hat     -- phantom, never defined
    5: (631, 100, 2),       # Ring of the Abaddon
    6: (937, 100, 2),       # Merien Helm    -- phantom, never defined
})
ABADDON_RARE = (15, FACTOR_RARE, {          # cases 8 and 9 are gaps
    1: (650, 4, 3), 2: (490, 4, 3), 3: (491, 4, 3), 4: (492, 4, 3),
    5: (611, 4, 3), 6: (610, 4, 3), 7: (612, 4, 3),
    10: (645, 4, 3), 11: (638, 4, 3), 12: (382, 4, 3), 13: (381, 4, 3),
    14: (259, 4, 3), 15: (947, 4, 3),
})

# ---------------------------------------------------------------------------
# 2. The Olympia roster. Names resolved against our items table on 2026-07-30;
#    ids we do not carry are dropped and listed in OLYMPIA_ABSENT for the record.
# ---------------------------------------------------------------------------
OLYMPIA = {
    66: {  # Ice Wyvern
        "uncommon": [657, 656, 259, 337, 308, 305, 311, 300, 334, 634],
        "rare": [335, 636, 642, 848, 734, 380, 614, 643],
    },
    73: {  # Fire Wyvern
        "uncommon": [657, 656, 259, 337, 308, 305, 311, 300, 334, 634, 735],
        "rare": [335, 636, 638, 491, 492, 490, 645, 847, 630, 20, 382,
                 612, 611, 610, 734],
    },
    81: {  # The Abaddon
        "uncommon": [656, 657, 634, 735],
        "rare": [636, 335, 638, 645, 642, 644, 637, 620, 621, 622, 491, 492,
                 490, 382, 630, 850, 849, 851, 20, 857, 847, 734, 859, 641,
                 646, 611, 610, 612, 762, 643, 846, 631],
    },
}

# Olympia entries with no counterpart in our items table. Its "Superior X" /
# "Exceptional X" names are its tier prefixes, which our tier system generates,
# so they are correctly absent rather than missing.
OLYMPIA_ABSENT = [
    "Magic Necklace(HR+60)", "Charge Wand (Ice-Storm)",
    "Charge Wand (Inhibition-Casting)", "Charge Wand (Cancellation)",
    "Haste Manual", "Hat of Divinity(M)", "Hat of Divinity(W)",
    "Sword of Heavens", "Sword of Thirst", "Ring of Thirst",
    "Necklace of Thirst", "Windslasher", "Bane", "Vortex Gem",
    "Scroll of Renown", "Unlearn Talent Ticket", "Necklace of Earth Protection",
    "Necklace of Earth Elemental", "Rejuvenation Gem (8)",
    "Rejuvenation Gem (14)", "Blood Gem (8)", "Blood Gem (14)", "Mind Gem (8)",
    "Mind Gem (14)", "Armor Gem (8)", "Armor Gem (14)", "Enchanted Gem (2)",
    "Enchanted Gem (4)", "Tactical Gem (3)", "Tactical Gem (5)",
]

# Tier anchors, as "1 in N per head roll", taken from the original's own
# structure so the two inputs are on one scale: a single common-block case is
# 1 in 48, and Abaddon's rare block is 1 in 240 per case.
ANCHOR = {"uncommon": Fraction(1, 48), "rare": Fraction(1, 240)}

# Floor for anything in a roster at all. The original's per-boss jackpot bounds
# reach 1 in 280,000 PER ROLL -- 1 in 36,000 kills -- which is a row that never
# fires. Anything worth listing should be reachable, so the rare anchor is the
# floor. Without it, items Olympia does not list (Necklace of Xelima and Mass
# Fire Strike Manual on Fire-Wyvern, Necklace of Stone Golem on Abaddon) would
# tighten by three orders of magnitude against the shipped data as a side effect
# of the roster swap rather than because anyone chose it.
FLOOR = ANCHOR["rare"]

BOSSES = [
    # npc_id, npc_type, name, table_id, i_min, i_max, jackpot, rare
    (46, 66, "Wyvern",      40034, 5,  15, WYVERN_JACKPOT,     WYVERN_RARE),
    (47, 73, "Fire Wyvern", 40035, 5,  15, FIREWYVERN_JACKPOT, FIREWYVERN_RARE),
    (48, 81, "Abaddon",     40036, 12, 20, ABADDON_JACKPOT,    ABADDON_RARE),
]

# The original's scatter gold amount is iDice(10, 15000) -- ten rolls of 1..15000,
# so ~75,005 a pile. NOT the monster's gold dice, which stage 1 uses.
GOLD_THROWS, GOLD_MIN, GOLD_MAX = 10, 1, 15000


def block_chances(block, f_prob):
    """Per-roll chance of each item id from one block, and the chance the block
    produced nothing. Phantom ids keep their probability -- they block the rest
    of the cascade in the original -- but yield no item."""
    selector, factor, cases = block
    out = collections.defaultdict(Fraction)
    produced = Fraction(0)
    for item_id, bound, _hit in cases.values():
        bound_eff = int(bound * factor * f_prob)
        p = Fraction(1, selector) * Fraction(1, bound_eff)
        produced += p
        if item_id not in PHANTOM:
            out[item_id] += p
    return out, Fraction(1) - produced


def original_chances(jackpot, rare, f_prob):
    """One roll of the full cascade: jackpot, then rare, then common."""
    chances = collections.defaultdict(Fraction)
    j, j_none = block_chances(jackpot, f_prob)
    for k, v in j.items():
        chances[k] += v
    r, r_none = block_chances(rare, f_prob)
    for k, v in r.items():
        chances[k] += j_none * v
    c, c_none = block_chances(COMMON_BLOCK, f_prob)
    for k, v in c.items():
        chances[k] += j_none * r_none * v
    nothing = j_none * r_none * c_none
    return chances, nothing


def build_roster(npc_type, jackpot, rare):
    """ppb per item = max(the original's head-roll rate, the Olympia anchor)."""
    orig, _ = original_chances(jackpot, rare, f_prob=1)
    olympia = OLYMPIA[npc_type]
    tier_of = {}
    for tier in ("uncommon", "rare"):
        for item_id in olympia[tier]:
            tier_of[item_id] = tier

    roster = {}
    for item_id in set(orig) | set(tier_of):
        if item_id in PHANTOM:
            continue
        roster[item_id] = max(orig.get(item_id, Fraction(0)),
                              ANCHOR.get(tier_of.get(item_id), Fraction(0)),
                              FLOOR)
    return roster, orig, tier_of


def ppb_of(chance):
    return int(chance * PPB + Fraction(1, 2))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--simulate", action="store_true",
                    help="cross-check the analytic figures against a run of the "
                         "implementation (flat table, guaranteed head, 5x tail)")
    ap.add_argument("--trials", type=int, default=200_000)
    ap.add_argument("--emit", action="store_true",
                    help="print the roster as a Python literal for the migration")
    ap.add_argument("--compare", action="store_true",
                    help="print every row's movement against the shipped tables")
    args = ap.parse_args()

    names, con = {}, None
    if DB.exists():
        con = sqlite3.connect(DB)
        names = {r[0]: r[1] for r in con.execute("select item_id, name from items")}
    else:
        print(f"note: {DB} not found; ids will print without names\n")

    emit = {}
    for npc_id, npc_type, label, table_id, i_min, i_max, jackpot, rare in BOSSES:
        head = i_min + 1
        tail = i_max - head
        roster, orig, tier_of = build_roster(npc_type, jackpot, rare)

        total = sum(ppb_of(c) for c in roster.values())
        if total > PPB:
            sys.exit(f"{label}: authored rows sum to {total} ppb, over the "
                     f"denominator -- the anchors are too generous")

        # A head roll always yields: the remainder becomes Gold. A tail roll runs
        # at 1/5 the rarity and can still come up empty.
        p_head_item = Fraction(total, PPB)
        p_head_gold = 1 - p_head_item
        p_tail_item = Fraction(total, PPB * 5)

        expected = head + tail * p_tail_item
        gold_per_kill = head * p_head_gold
        orig_total = sum(orig.values())
        orig_expected = head + tail * orig_total / 5
        orig_gold = head * (1 - orig_total)

        print(f"=== {label}  (npc {npc_id}, type {npc_type}, table {table_id}) ===")
        print(f"  {i_max} rolls: {head} guaranteed head + {tail} tail at 5x rarity")
        print(f"  rows {len(roster)}   authored total {total:,} ppb "
              f"({float(p_head_item):.4%} of a head roll)")
        print(f"  items per kill   {float(expected):6.2f}   "
              f"(range {head}-{i_max}; the original's roster gave "
              f"{float(orig_expected):.2f})")
        print(f"  Gold per kill    {float(gold_per_kill):6.2f}   "
              f"({float(gold_per_kill / expected):.1%} of drops; the original's "
              f"roster gave {float(orig_gold):.2f} = "
              f"{float(orig_gold / orig_expected):.1%})")
        print(f"  Gold amount      {GOLD_THROWS} x 1..{GOLD_MAX:,} = "
              f"~{GOLD_THROWS * (GOLD_MAX + 1) // 2:,} a pile, "
              f"~{float(gold_per_kill) * GOLD_THROWS * (GOLD_MAX + 1) / 2:,.0f} a kill")
        print(f"  {'id':>4} {'item':<32} {'1 in (roll)':>12} {'per kill':>9}  source")
        for item_id in sorted(roster, key=lambda i: -roster[i]):
            chance = roster[item_id]
            per_kill = chance * (head + Fraction(tail, 5))
            o = orig.get(item_id, Fraction(0))
            tier = tier_of.get(item_id)
            if o and tier and ANCHOR[tier] > o:
                source = f"olympia {tier} (original was 1 in {float(1/o):,.0f})"
            elif o:
                source = "original" + (f" == olympia {tier}" if tier else "")
            else:
                source = f"olympia {tier}"
            print(f"  {item_id:>4} {names.get(item_id, '?')[:32]:<32} "
                  f"{float(1/chance):>12,.0f} {float(per_kill):>9.4f}  {source}")
        print(f"  {GOLD:>4} {'Gold (head fallback, not a row)':<32} "
              f"{float(1/p_head_gold):>12,.1f} {float(gold_per_kill):>9.4f}  "
              f"original mechanism")
        print()

        emit[table_id] = {
            "npc_id": npc_id, "label": label,
            "roll_count": i_max, "guaranteed_rolls": head,
            "guarantee_item_id": GOLD, "tail_rarity_divisor": 5,
            "rows": {i: ppb_of(c) for i, c in sorted(roster.items())},
        }

        if args.simulate:
            simulate(label, total, head, i_max, args.trials, expected,
                     gold_per_kill)

    print("=== Olympia names with no counterpart in our items table ===")
    for n in OLYMPIA_ABSENT:
        print(f"  {n}")

    if args.compare and DB.exists():
        compare_to_shipped(con, emit, names)

    if args.emit:
        print("\n=== SCATTER = \\")
        print(repr(emit))


def compare_to_shipped(con, emit, names):
    """Every row's movement against what gamedata.db ships today. This is the
    review surface: the roster override loosens some rows by three orders of
    magnitude and tightens others by four, and neither is visible from the
    per-boss tables above."""
    print("\n=== movement against the shipped tables ===")
    for table_id, spec in emit.items():
        head = spec["guaranteed_rolls"]
        kill_rolls = Fraction(head) + Fraction(spec["roll_count"] - head, 5)
        today = {r[0]: r[1] for r in con.execute(
            "select item_id, drop_chance_ppb from drop_entries"
            " where drop_table_id = ?", (table_id,))}
        old_rolls = spec["roll_count"]     # today every roll is a full-rate roll

        moves = []
        for item_id in sorted(set(today) | set(spec["rows"])):
            was = Fraction(today.get(item_id, 0), PPB) * old_rolls
            now = Fraction(spec["rows"].get(item_id, 0), PPB) * kill_rolls
            if was == 0:
                factor, tag = None, "NEW"
            elif now == 0:
                factor, tag = None, "REMOVED"
            else:
                factor = float(now / was)
                tag = f"x{factor:,.1f}" if factor >= 1 else f"/{1 / factor:,.1f}"
            moves.append((float(was), float(now), tag, item_id,
                          abs(float(now / was)) if was and now else 1e9))
        moves.sort(key=lambda m: -m[4])

        print(f"\n  --- {spec['label']} (table {table_id}) --- per kill")
        print(f"  {'id':>4} {'item':<30} {'today':>9} {'#89':>9}  move")
        for was, now, tag, item_id, _ in moves:
            print(f"  {item_id:>4} {names.get(item_id, '?')[:30]:<30} "
                  f"{was:>9.4f} {now:>9.4f}  {tag}")


def simulate(label, total, head, rolls, trials, expected, gold_expected):
    """Run the implementation's own model: one flat table, a guaranteed head whose
    remainder is Gold, and a tail at 1/5 rarity. Confirms the analytic figures."""
    rng = random.Random(20260730)
    counts, gold = [], 0
    for _ in range(trials):
        n = 0
        for k in range(rolls):
            threshold = total if k < head else total // 5
            if rng.randrange(PPB) < threshold:
                n += 1
            elif k < head:
                n += 1            # the Gold fallback
                gold += 1
        counts.append(n)
    mean = sum(counts) / trials
    print(f"  simulated {trials:,} kills: min {min(counts)} max {max(counts)} "
          f"mean {mean:.3f} (analytic {float(expected):.3f}), "
          f"gold {gold / trials:.3f} (analytic {float(gold_expected):.3f})")
    if abs(mean - float(expected)) > 0.05:
        sys.exit(f"{label}: simulation disagrees with the analytic mean")
    print()


if __name__ == "__main__":
    main()
