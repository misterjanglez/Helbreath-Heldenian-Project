#!/usr/bin/env python3
"""seed_item_tiers.py - Item Tiers Cycle 2-E seeding + data-entry passes.

Fills the tiered tables in Binaries/Server/gamedata.db verbatim from the locked
design contract (PLANS/ItemTiers_Plan.md sections 5/7/8/10/11) and drives the
data-entry reports. Idempotent: every seeded table is DELETE + INSERT inside one
transaction. The script never creates tables - the server owns DDL (Tiers 2-A);
missing tables are a clear error telling you to boot the Debug server once.

Default action (no flags) seeds:
  1. tier_curves + tier_curve_overrides   (spec 7, verbatim)
  2. tier_buckets + modifier_catalog + modifier_eligibility (spec 4/7; legacy
     pool weights mined as bucket weights; bucket_class_rules cleared - v1 OFF)
  3. loot_grades                          (spec 8 launch rates)
  4. enchant_categories + enchant_steps   (spec 10; success % extracted verbatim
     from check_is_item_upgrade_success - Open Q1 default)
  5. tier_presentation                    (spec 11 palette + name template)
  6. tier_settings                        (spec 5 marquee constants + posture)
  plus two behavior-preserving legacy-table parity edits (see LEGACY_PARITY).

Data-entry passes:
  --grade-report           propose loot grades for all NPCs -> CSV (owner
                           hand-adjusts proposed_grade, then --apply-grades)
  --apply-grades CSV       write npc_configs.loot_grade from the reviewed CSV
  --boss-stage1-report     boss first-drop table composition + gear share
  --stage2-scan            tier-eligible gear in stage-2 tables (mirrors the
                           boot validator rule) -> proposal CSV for --apply-drops
  --apply-drops CSV        apply reviewed drop_entries edits (upsert/delete/
                           move_stage1 actions)
  --place-cape             ensure the generic Cape (402) restricted-mob rows
                           exist (recorded pick: the current 11 placements)
  --legacy-pool-audit      droppable tier-scope gear missing a legacy pool

The Binaries/Server/gameconfigs/*.sql snapshots must be re-exported by hand
after seeding (Trading Post Phase-4 finding).

Proposal CSVs land in Scripts/output/ - apply them promptly: grep.py
auto-clears that directory when it grows past 10MB.
"""

import argparse
import csv
import os
import re
import sqlite3
import sys
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DB = os.path.join(ROOT, 'Binaries', 'Server', 'gamedata.db')
OUTPUT_DIR = os.path.join(ROOT, 'Scripts', 'output')
ITEM_ENUMS_H = os.path.join(ROOT, 'Sources', 'Dependencies', 'Shared', 'Item', 'ItemEnums.h')

# ---------------------------------------------------------------------------
# Code mirrors (Sources/Dependencies/Shared/Item/ModifierIds.h + ItemEnums.h).
# The validator classifies gear with these; the reports must agree with it.
# ---------------------------------------------------------------------------

# tier_item_class values
MELEE, BOW, WAND, BODY, HELM, LEGGINGS, SHIELD, CAPE = 1, 2, 3, 4, 5, 6, 7, 8
CLASS_NAMES = {0: 'none', MELEE: 'melee_weapon', BOW: 'bow', WAND: 'wand', BODY: 'body_armor',
               HELM: 'helm', LEGGINGS: 'leggings', SHIELD: 'shield', CAPE: 'cape'}
ARMOR_FAMILY = (BODY, HELM, LEGGINGS, SHIELD, CAPE)
WEAPON_CLASSES = (MELEE, BOW)

# item_sub_type / weapon_class / EquipPos values used by derive_tier_item_class
SUB_WEAPON, SUB_ARMOR, SUB_ACCESSORY = 3, 4, 5
WC_WAND, WC_BOW = 7, 8
EP_HEAD, EP_BODY, EP_ARMS, EP_LEGGINGS, EP_LEFTHAND, EP_BACK, EP_FULLBODY = 1, 2, 3, 4, 7, 12, 13

# ItemEffectType values the legacy roll gate accepts (is_legacy_rollable_effect_type)
LEGACY_ROLLABLE_EFFECT_TYPES = {1, 2, 13}  # Attack / Defense / AttackManaSave

# is_special_item roster (ItemEnums.h), resolved to numeric ids. Includes the
# 2-E fixes: TheDevastator/DarkExecutor/LightingBlade corrected to 846/847/848
# (the old 879/880/881 pointed at summon scrolls + an armor dye), and the
# owner-ruled stage-2 named uniques Flameberge +3 LLF (290), Golden Axe LLF
# (292), Giant Battle Hammer (762). verify_special_roster_sync() re-derives
# this set from the header on every run and fails loud on drift.
SPECIAL_ITEMS = frozenset(
    {20, 259, 290, 291, 292, 300, 305, 308, 311, 335, 336, 337, 400, 401,
     427, 428, 490, 491, 492, 610, 611, 612, 613, 614, 616, 618, 620, 621,
     622, 630, 631, 632, 633, 634, 635, 636, 646, 647, 648, 656, 657, 734,
     735, 753, 762, 845, 846, 847, 848, 849, 850, 851, 858, 859, 860, 861,
     862, 863, 864, 865, 866, 867, 868, 869, 870, 871, 1108, 1109, 1110, 1111}
    | set(range(403, 427))       # Hero items 403-426
    | set(range(1086, 1102)))    # Magic necklaces 1086-1101

NOTHING_ITEM, GOLD_ITEM = 0, 90

TIERED_TABLES = (
    'tier_buckets', 'modifier_catalog', 'modifier_eligibility', 'bucket_class_rules',
    'tier_curves', 'tier_curve_overrides', 'loot_grades',
    'enchant_categories', 'enchant_steps', 'tier_presentation', 'tier_settings')


def derive_tier_item_class(sub_type, weapon, equip_pos):
    """Mirror of hb::shared::item::derive_tier_item_class (ModifierIds.h)."""
    if sub_type == SUB_ACCESSORY:
        return 0
    if weapon == WC_BOW:
        return BOW
    if weapon == WC_WAND:
        return WAND
    if equip_pos == EP_LEFTHAND:
        return SHIELD
    if sub_type == SUB_WEAPON:
        return MELEE
    if sub_type != SUB_ARMOR:
        return 0
    return {EP_HEAD: HELM, EP_BODY: BODY, EP_ARMS: BODY, EP_FULLBODY: BODY,
            EP_LEGGINGS: LEGGINGS, EP_BACK: CAPE}.get(equip_pos, 0)


# ---------------------------------------------------------------------------
# Seed data - spec 7 curves + overrides, verbatim.
# ---------------------------------------------------------------------------

TIER_CURVES = [
    # curve_id, name, p50, p90, p99, cap_chance_den
    (1, 'common',    0.15, 0.40, 0.65, 2_000_000),
    (2, 'rare',      0.25, 0.50, 0.75,   500_000),
    (3, 'epic',      0.35, 0.60, 0.82,   100_000),
    (4, 'legendary', 0.45, 0.70, 0.88,    25_000),
]

# Spec 7 overrides: pair halves Epic 1/640 + Legendary 1/320 (per pair
# modifier), ALL STATS Legendary 1/50,000. Bodies inherit the tier default.
PAIR_MODIFIERS = (30, 31, 32, 33)
TIER_CURVE_OVERRIDES = (
    [(m, 3, 640) for m in PAIR_MODIFIERS]
    + [(m, 4, 320) for m in PAIR_MODIFIERS]
    + [(34, 4, 50_000)])

# ---------------------------------------------------------------------------
# Seed data - spec 4 buckets. Values follow the tier_bucket enum in
# ModifierIds.h (damage=1 .. marquee=9); sort_order is tooltip order.
# ---------------------------------------------------------------------------

TIER_BUCKETS = [
    (1, 'DAMAGE', 1), (2, 'PRECISION', 2), (3, 'HANDLING', 3), (4, 'ECONOMY', 4),
    (5, 'CASTING', 5), (6, 'SET_AXIS', 6), (7, 'COMBAT_UTILITY', 7),
    (8, 'ATTRIBUTES', 8), (9, 'MARQUEE', 9),
]

# ---------------------------------------------------------------------------
# Seed data - modifier catalog (spec 4 membership, spec 7 bands + aggregate
# caps, ModifierIds.h ids/effects). Display fields for ids 1-23 are the live
# 2-B fold values (trailing spaces preserved - they are the shipped labels).
# Bands are display units; stored byte = display / multiplier.
# Value-less rows (agile) keep multiplier 0 with a zeroed band axis.
# aggregate_cap: spec 7 table - 546 structural for DR/MR/PR/HP/SP/MP recovery,
# 80 absorbs, MC 13 / CC 20 / move speed 20, attributes 20; band max elsewhere.
# min_tier: pairs Epic+ (3), ALL STATS + marquee Legendary-only (4), rest 1.
# tier_attribute params: STR 1, DEX 2, INT 3, VIT 4, MAG 5, CHR 6.
# ---------------------------------------------------------------------------

MODIFIER_CATALOG = [
    # id name display_name effect_label effect_format eff p1 p2 bkt mult mt mq bmin bmax cap
    (1, 'sharp', 'Sharp', '', '+{}', 6, 0, 0, 1, 1, 1, 0, 1, 13, 13),
    (2, 'critical', 'Critical', 'Critical Hit Damage', '+{}', 1, 0, 0, 1, 1, 1, 0, 1, 13, 13),
    (3, 'poisoning', 'Poisoning', 'Poison Damage', '+{}', 2, 0, 0, 1, 5, 1, 0, 20, 65, 65),
    (4, 'righteous', 'Righteous', '', '', 3, 0, 0, 1, 1, 1, 0, 3, 10, 10),
    (5, 'ancient', 'Ancient', '', '+{}', 8, 0, 0, 1, 1, 1, 0, 1, 13, 13),
    (6, 'hitting_probability', '', 'Hitting Probability', '+{}', 13, 0, 0, 2, 7, 1, 0, 21, 91, 91),
    (7, 'consecutive_attack', '', 'Consecutive Attack Damage', '+{}', 21, 0, 0, 2, 1, 1, 0, 1, 7, 7),
    (8, 'agile', 'Agile', 'Attack Speed -1', '', 4, 0, 0, 3, 0, 1, 0, 0, 0, 0),
    (9, 'light', 'Light', '', '-{}%', 5, 0, 0, 3, 4, 1, 0, 4, 52, 52),
    (10, 'strong', 'Strong', 'Durability', '+{}%', 7, 0, 0, 3, 7, 1, 0, 14, 91, 91),
    (11, 'experience', '', 'Experience', '+{}%', 22, 0, 0, 4, 5, 1, 0, 5, 20, 20),
    (12, 'gold', '', 'Gold', '+{}%', 23, 0, 0, 4, 10, 1, 0, 10, 50, 50),
    (13, 'spell_success', 'Special', 'Magic Casting Probability ', '+{}%', 9, 0, 0, 5, 3, 1, 0, 3, 39, 39),
    (14, 'defense_ratio', '', 'Defense Ratio', '+{}', 14, 0, 0, 6, 7, 1, 0, 21, 91, 546),
    (15, 'physical_absorb', '', 'Physical Absorption', '+{}%', 19, 0, 0, 6, 3, 1, 0, 9, 39, 80),
    (16, 'magic_resist', '', 'Magic Resistance', '+{}%', 18, 0, 0, 6, 7, 1, 0, 21, 91, 546),
    (17, 'magic_absorb', '', 'Magic Absorption', '+{}%', 20, 0, 0, 6, 3, 1, 0, 9, 39, 80),
    (18, 'hp_recovery', '', 'HP recovery ', '{}%', 15, 0, 0, 6, 7, 1, 0, 7, 91, 546),
    (19, 'sp_recovery', '', 'SP recovery ', '{}%', 16, 0, 0, 6, 7, 1, 0, 7, 91, 546),
    (20, 'mp_recovery', '', 'MP recovery ', '{}%', 17, 0, 0, 6, 7, 1, 0, 7, 91, 546),
    (21, 'poison_resist', '', 'Poison Resistance', '+{}%', 12, 0, 0, 6, 7, 1, 0, 21, 91, 546),
    (22, 'mana_converting', 'Mana Converting', 'Replace {}% damage to mana', '', 10, 0, 0, 7, 1, 1, 0, 1, 7, 13),
    (23, 'crit_chance', 'Critical', 'Crit Increase Chance ', '{}%', 11, 0, 0, 7, 1, 1, 0, 1, 7, 20),
    # ATTRIBUTES ladder - singles (any tier)
    (24, 'attr_str', 'Strength', 'STR', '+{}', 24, 1, 0, 8, 1, 1, 0, 1, 10, 20),
    (25, 'attr_dex', 'Dexterity', 'DEX', '+{}', 24, 2, 0, 8, 1, 1, 0, 1, 10, 20),
    (26, 'attr_mag', 'Magic', 'MAG', '+{}', 24, 5, 0, 8, 1, 1, 0, 1, 10, 20),
    (27, 'attr_int', 'Intelligence', 'INT', '+{}', 24, 3, 0, 8, 1, 1, 0, 1, 10, 20),
    (28, 'attr_vit', 'Vitality', 'VIT', '+{}', 24, 4, 0, 8, 1, 1, 0, 1, 10, 20),
    (29, 'attr_chr', 'Charisma', 'CHR', '+{}', 24, 6, 0, 8, 1, 1, 0, 1, 10, 20),
    # ATTRIBUTES ladder - archetype pairs (Epic+, two independent rolls)
    (30, 'attr_str_dex', 'STR & DEX', 'STR & DEX', '+{} / +{}', 25, 1, 2, 8, 1, 3, 0, 1, 7, 20),
    (31, 'attr_int_mag', 'INT & MAG', 'INT & MAG', '+{} / +{}', 25, 3, 5, 8, 1, 3, 0, 1, 7, 20),
    (32, 'attr_vit_str', 'VIT & STR', 'VIT & STR', '+{} / +{}', 25, 4, 1, 8, 1, 3, 0, 1, 7, 20),
    (33, 'attr_vit_int', 'VIT & INT', 'VIT & INT', '+{} / +{}', 25, 4, 3, 8, 1, 3, 0, 1, 7, 20),
    # ATTRIBUTES ladder - ALL STATS (Legendary-only, one shared roll)
    (34, 'attr_all_stats', 'All Stats', 'All Stats', '+{}', 26, 0, 0, 8, 1, 4, 0, 1, 7, 20),
    # MARQUEE (Legendary-only)
    (35, 'sunder', 'Sunder', 'Sunder Chance', '{}%', 27, 0, 0, 9, 1, 4, 1, 3, 15, 15),
    (36, 'bleed', 'Bleed', 'Bleed Chance', '{}%', 28, 0, 0, 9, 1, 4, 1, 3, 15, 15),
    (37, 'mp_drain', 'MP Drain', 'MP Drain per Hit', '{}', 29, 0, 0, 9, 1, 4, 1, 3, 15, 15),
    (38, 'sp_drain', 'SP Drain', 'SP Drain per Hit', '{}', 30, 0, 0, 9, 1, 4, 1, 3, 15, 15),
    (39, 'cast_time_reduction', 'Cast Time Reduction', 'Cast Time', '-{}%', 31, 0, 0, 9, 1, 4, 1, 5, 20, 20),
    (40, 'move_speed', 'Movement Speed', 'Movement Speed', '+{}%', 32, 0, 0, 9, 1, 4, 1, 3, 10, 20),
]

# ---------------------------------------------------------------------------
# Seed data - per-class eligibility + weights (spec 4). Legacy-line weights are
# the authoring-time (2-E) snapshot of the curated attribute_pool_entries
# (pool 1 weapons, pool 2 armor, pool 3 wands - the spec 2 reference input);
# after seeding, modifier_eligibility is its own dial and legacy pool tuning
# does NOT flow through. New lines get designed weights: attribute singles 100,
# pairs 50, ALL STATS 20, marquee lines 100.
# Physical Absorb gets NO cape row (spec 4 eligibility note).
# ---------------------------------------------------------------------------

ATTRIBUTE_LADDER = (
    [(m, 100) for m in (24, 25, 26, 27, 28, 29)]
    + [(m, 50) for m in PAIR_MODIFIERS]
    + [(34, 20)])

WEAPON_BUCKET_WEIGHTS = (
    # DAMAGE (pool 1 prefix weights)
    [(1, 1600), (2, 1500), (3, 1600), (4, 2000), (5, 301)]
    # PRECISION (pool 1 secondary weights)
    + [(6, 4999), (7, 3500)]
    # HANDLING (pool 1 prefix weights)
    + [(8, 2000), (9, 299), (10, 700)]
    # ECONOMY (pool 1 secondary weights)
    + [(11, 501), (12, 1000)]
    + ATTRIBUTE_LADDER
    # MARQUEE weapon exotics
    + [(35, 100), (36, 100), (37, 100), (38, 100)])

WAND_BUCKET_WEIGHTS = (
    [(13, 10000)]                       # CASTING (pool 3 prefix)
    + [(6, 4999), (7, 3500)]            # PRECISION (pool 3 secondary)
    + [(11, 501), (12, 1000)]           # ECONOMY (pool 3 secondary)
    + ATTRIBUTE_LADDER
    + [(39, 100)])                      # MARQUEE cast-time reduction

ARMOR_BUCKET_WEIGHTS = (
    # SET-AXIS (pool 2 secondary weights)
    [(14, 1000), (15, 400), (16, 1900), (17, 201), (18, 1000), (19, 1500), (20, 1000), (21, 3000)]
    # HANDLING (pool 2 prefix weights)
    + [(9, 3000), (10, 5999)]
    # COMBAT UTILITY (pool 2 prefix weights)
    + [(22, 555), (23, 446)]
    + ATTRIBUTE_LADDER)

def build_eligibility():
    rows = []
    for cls in WEAPON_CLASSES:
        rows += [(m, cls, w) for m, w in WEAPON_BUCKET_WEIGHTS]
    rows += [(m, WAND, w) for m, w in WAND_BUCKET_WEIGHTS]
    for cls in ARMOR_FAMILY:
        for m, w in ARMOR_BUCKET_WEIGHTS:
            if m == 15 and cls == CAPE:
                continue  # Physical Absorb: capes cover no hit location
            rows.append((m, cls, w))
        if cls in (LEGGINGS, CAPE):
            rows.append((40, cls, 100))  # MARQUEE movement speed
    return rows

# ---------------------------------------------------------------------------
# Seed data - spec 8 loot grades (weights out of 10000, staircase zero-weights
# are the hard gate; first_drop_chance replaces BASE_PRIMARY_DROP_CHANCE).
# ---------------------------------------------------------------------------

LOOT_GRADES = [
    # grade, name, common, rare, epic, legendary, first_drop_chance
    (1, 'vermin',   10000,    0,    0,   0,  1000),
    (2, 'standard',  9000, 1000,    0,   0,  1000),
    (3, 'veteran',   7500, 2200,  300,   0,  1000),
    (4, 'elite',     6000, 3200,  750,  50,  1000),
    (5, 'boss',      3500, 4000, 2200, 300, 10000),
]

# ---------------------------------------------------------------------------
# Seed data - spec 10 enchant. Success % extracted verbatim from
# check_is_item_upgrade_success (ItemManager.cpp:5381): base ladder
# 30/25/20/15/10/10/8/8/5/3% by current bonus; the Merien-stone route
# (armor + wands) doubles it. Stored as basis points out of 10000. The crafted
# quality bonus (special_effect_value2) stays code-side. Caps +7 (+10 crafted),
# destroy-on-fail from +4 (safe through +3), endurance growth 15% armor/wand,
# 20% crafted (spec 10 launch values).
# Category ids are the 3-F code enum: 1 weapon, 2 shield, 3 armor, 4 wand,
# 5 crafted_weapon. The last two columns (merien_doubled, safe_through) are
# step-derivation inputs only - the DB table stores the first four.
# ---------------------------------------------------------------------------

ENCHANT_CATEGORIES = [
    # category, name, cap, endurance_growth_pct, merien_doubled, safe_through
    (1, 'weapon', 7, 0, False, 3),
    (2, 'shield', 7, 0, False, 3),
    (3, 'armor', 7, 15, True, 3),
    (4, 'wand', 7, 15, True, 3),
    (5, 'crafted_weapon', 10, 20, False, 3),
]

_XELIMA_LADDER = [3000, 2500, 2000, 1500, 1000, 1000, 800, 800, 500, 300]

def build_enchant_steps():
    rows = []
    for category, _, cap, _, merien_doubled, safe_through in ENCHANT_CATEGORIES:
        for step in range(1, cap + 1):
            pct = _XELIMA_LADDER[step - 1] * (2 if merien_doubled else 1)
            rows.append((category, step, pct, 0 if step <= safe_through else 1))
    return rows

# ---------------------------------------------------------------------------
# Seed data - spec 11 presentation (locked palette, final player-facing names).
# ---------------------------------------------------------------------------

TIER_PRESENTATION = [
    # tier, name, r, g, b, name_template
    (1, 'Common',    255, 255, 255, '{tier} {name}'),
    (2, 'Rare',      128, 192, 128, '{tier} {name}'),
    (3, 'Epic',      150, 160, 225, '{tier} {name}'),
    (4, 'Legendary', 255, 176,  16, '{tier} {name}'),
]

# ---------------------------------------------------------------------------
# Seed data - spec 5 marquee launch constants + anti-cheat posture knobs.
# ---------------------------------------------------------------------------

TIER_SETTINGS = [
    ('sunder_defense_delta', '-50'),
    ('sunder_duration_ms', '5000'),
    ('bleed_tick_damage', '5'),
    ('bleed_tick_interval_ms', '2000'),
    ('bleed_duration_ms', '8000'),
    ('cast_check_floor_ms', '1500'),
    ('move_grace_pct', '85'),
    ('posture_move', 'log'),
    ('posture_cast', 'log'),
]

# ---------------------------------------------------------------------------
# Legacy-table parity edits (recorded 2-E picks). The legacy-mode validator
# requires catalog multiplier == legacy-table multiplier (tooltips must scale
# like rolls), so the two spec-verbatim band rescales need matching legacy
# rows - both edits preserve today's legacy behavior exactly:
#   - Righteous: multiplier 0 -> 1, min/max stay 0 (legacy still rolls value 0;
#     the 3-F drift fix later gives the value a combat hook + a real range).
#   - Experience: multiplier 10 -> 5, min/max 2 -> 4 (fixed roll stays fixed;
#     effect and display stay 4 x 5 = +20%). Fresh-start rule covers any
#     dev-world item that stored the old byte.
# ---------------------------------------------------------------------------

LEGACY_PARITY = [
    ("UPDATE attribute_prefix_types SET multiplier = 1 WHERE prefix_id = 3 AND multiplier <> 1",
     'righteous multiplier 0 -> 1 (min/max stay 0)'),
    ("UPDATE attribute_secondary_types SET multiplier = 5, min_value = 4, max_value = 4 "
     "WHERE secondary_id = 11 AND multiplier <> 5",
     'experience multiplier 10 -> 5, fixed roll 2 -> 4 (effect stays +20%)'),
]

# ---------------------------------------------------------------------------
# Loot-grade proposal (seeding step 7). Mechanical first pass by exp_max, with
# the Apocalypse boss roster explicit; the owner hand-adjust pass follows.
# Recorded thresholds (2-E pick): < 50 vermin, < 2000 standard, < 7000 veteran,
# >= 7000 elite. NPCs/structures with exp 0 land in vermin (harmless - their
# tables hold no ordinary gear).
# ---------------------------------------------------------------------------

BOSS_NPCS = {44: 'Helclaw', 45: 'Tiger Worm', 46: 'Wyvern', 47: 'Fire Wyvern', 48: 'Abaddon'}
GRADE_THRESHOLDS = [(50, 1, 'exp_max < 50'), (2000, 2, 'exp_max < 2000'), (7000, 3, 'exp_max < 7000')]

def propose_grade(npc_id, exp_max):
    if npc_id in BOSS_NPCS:
        return 5, 'Apocalypse boss roster'
    for limit, grade, reason in GRADE_THRESHOLDS:
        if exp_max < limit:
            return grade, reason
    return 4, 'exp_max >= 7000'

# Generic Cape (402) restricted-mob placement (seeding step 10). Recorded pick:
# the placements already present in the live table (Plate Mail pattern).
CAPE_ITEM = 402
CAPE_PLACEMENTS = [
    # (drop_table_id, weight) - all stage 1
    (20031, 100),  # Demon
    (20032, 100),  # Unicorn
    (20049, 100),  # Helclaw
    (20050, 100),  # Tiger Worm
    (20052, 100),  # Gargoyle
    (20058, 100),  # Mountain Giant
    (20085, 50),   # Centaurus / Griffin
    (20089, 100),  # Nizie
    (20090, 100),  # Wyvern
    (20091, 100),  # Fire Wyvern
    (20092, 100),  # Abaddon
]


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def fail(message):
    print(f'ERROR: {message}', file=sys.stderr)
    sys.exit(2)


def verify_special_roster_sync():
    """Fail loud if SPECIAL_ITEMS drifts from the C++ is_special_item roster.

    Re-derives the roster from ItemEnums.h: the explicit `case ItemId::X:`
    labels plus the two range checks in the default branch (Hero items and
    Magic necklaces, bounds read from the named constants).
    """
    try:
        header = open(ITEM_ENUMS_H, encoding='utf-8').read()
    except OSError as error:
        fail(f'cannot read {ITEM_ENUMS_H} to verify SPECIAL_ITEMS sync: {error}')
    constants = {m.group(1): int(m.group(2)) for m in
                 re.finditer(r'constexpr\s+short\s+(\w+)\s*=\s*(\d+)\s*;', header)}
    try:
        body = header.split('inline bool is_special_item', 1)[1].split('default:', 1)[0]
        expected = ({constants[m.group(1)] for m in re.finditer(r'case\s+ItemId::(\w+)\s*:', body)}
                    | set(range(constants['AresdenHeroHelmM'], constants['ElvineHeroLeggingsW'] + 1))
                    | set(range(constants['MagicNecklaceDFp15'], constants['MagicNecklaceRM30'] + 1)))
    except (IndexError, KeyError) as error:
        fail(f'cannot parse is_special_item from {ITEM_ENUMS_H}: {error}')
    if expected != SPECIAL_ITEMS:
        fail('SPECIAL_ITEMS out of sync with ItemEnums.h is_special_item -'
             f' missing here: {sorted(expected - SPECIAL_ITEMS)},'
             f' extra here: {sorted(SPECIAL_ITEMS - expected)}')


def connect(db_path):
    if not os.path.exists(db_path):
        fail(f'database not found: {db_path}')
    db = sqlite3.connect(db_path)
    db.row_factory = sqlite3.Row
    existing = {r[0] for r in db.execute("SELECT name FROM sqlite_master WHERE type='table'")}
    missing = [t for t in TIERED_TABLES if t not in existing]
    if missing:
        fail('tiered tables missing: ' + ', '.join(missing)
             + '\nThe server owns DDL (Tiers 2-A) - boot the Debug server once against this '
               'database to create/migrate the schema, then rerun.')
    return db


def load_items(db):
    return {row['item_id']: row for row in db.execute(
        'SELECT item_id, name, item_sub_type, weapon_class, equip_pos,'
        ' item_effect_type, attribute_pool_id FROM items')}


def item_class(items, item_id):
    row = items.get(item_id)
    if row is None:
        return 0
    return derive_tier_item_class(row['item_sub_type'], row['weapon_class'], row['equip_pos'])


def ordinary_tier_gear(items, item_id):
    """Mirror of the validator's ordinary_tier_gear (TierConfigValidator.cpp)."""
    return (item_id not in (NOTHING_ITEM, GOLD_ITEM)
            and item_class(items, item_id) != 0
            and item_id not in SPECIAL_ITEMS)


def item_name(items, item_id):
    if item_id == NOTHING_ITEM:
        return '(nothing)'
    if item_id == GOLD_ITEM:
        return '(gold)'
    return items[item_id]['name'] if item_id in items else f'(unknown {item_id})'


def output_path(filename):
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    return os.path.join(OUTPUT_DIR, filename)


# ---------------------------------------------------------------------------
# Seeding (default action)
# ---------------------------------------------------------------------------

def seed(db):
    catalog_rows = [row + (None,) * 8 for row in MODIFIER_CATALOG]  # window columns NULL
    eligibility = build_eligibility()
    enchant_steps = build_enchant_steps()

    catalog_columns = ('modifier_id, name, display_name, effect_label, effect_format,'
                       ' effect_id, effect_param1, effect_param2, bucket_id, multiplier,'
                       ' min_tier, marquee, band_min, band_max, aggregate_cap,'
                       ' window_min_t1, window_max_t1, window_min_t2, window_max_t2,'
                       ' window_min_t3, window_max_t3, window_min_t4, window_max_t4')
    plan = [
        ('tier_curves',
         'INSERT INTO tier_curves (curve_id, name, p50, p90, p99, cap_chance_den)'
         ' VALUES (?,?,?,?,?,?)', TIER_CURVES),
        ('tier_curve_overrides',
         'INSERT INTO tier_curve_overrides (modifier_id, tier, cap_chance_den)'
         ' VALUES (?,?,?)', TIER_CURVE_OVERRIDES),
        ('tier_buckets',
         'INSERT INTO tier_buckets (bucket_id, name, sort_order) VALUES (?,?,?)', TIER_BUCKETS),
        ('modifier_catalog',
         f'INSERT INTO modifier_catalog ({catalog_columns}) VALUES ({",".join("?" * 23)})',
         catalog_rows),
        ('modifier_eligibility',
         'INSERT INTO modifier_eligibility (modifier_id, item_class, weight)'
         ' VALUES (?,?,?)', eligibility),
        ('bucket_class_rules', None, []),  # mandatory-bucket flag OFF everywhere in v1
        ('loot_grades',
         'INSERT INTO loot_grades (grade, name, weight_common, weight_rare, weight_epic,'
         ' weight_legendary, first_drop_chance) VALUES (?,?,?,?,?,?,?)', LOOT_GRADES),
        ('enchant_categories',
         'INSERT INTO enchant_categories (category, name, cap, endurance_growth_pct)'
         ' VALUES (?,?,?,?)', [row[:4] for row in ENCHANT_CATEGORIES]),
        ('enchant_steps',
         'INSERT INTO enchant_steps (category, step, success_pct, destroy_on_fail)'
         ' VALUES (?,?,?,?)', enchant_steps),
        ('tier_presentation',
         'INSERT INTO tier_presentation (tier, name, color_r, color_g, color_b, name_template)'
         ' VALUES (?,?,?,?,?,?)', TIER_PRESENTATION),
        ('tier_settings',
         'INSERT INTO tier_settings (key, value) VALUES (?,?)', TIER_SETTINGS),
    ]

    with db:  # one transaction: all-or-nothing
        for table, insert_sql, rows in plan:
            db.execute(f'DELETE FROM {table}')
            if rows:
                db.executemany(insert_sql, rows)
            print(f'  {table}: {len(rows)} rows')
        for sql, description in LEGACY_PARITY:
            changed = db.execute(sql).rowcount
            print(f'  legacy parity: {description} - {"applied" if changed else "already applied"}')
    print('Seed complete (single transaction committed).')
    print('Reminder: re-export the Binaries/Server/gameconfigs/*.sql snapshots by hand.')


# ---------------------------------------------------------------------------
# Data-entry passes
# ---------------------------------------------------------------------------

def grade_report(db):
    path = output_path('loot_grade_proposal.csv')
    rows = db.execute('SELECT npc_id, name, exp_max, hp_max, drop_table_id, loot_grade'
                      ' FROM npc_configs ORDER BY exp_max, npc_id').fetchall()
    with open(path, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(['npc_id', 'name', 'exp_max', 'hp_max', 'drop_table_id',
                         'current_grade', 'proposed_grade', 'reason'])
        counts = Counter()
        for npc_id, name, exp_max, hp_max, table_id, current in rows:
            grade, reason = propose_grade(npc_id, exp_max)
            counts[grade] += 1
            writer.writerow([npc_id, name, exp_max, hp_max, table_id, current, grade, reason])
    print(f'{len(rows)} NPCs -> {path}')
    for grade, grade_name, *_ in LOOT_GRADES:
        print(f'  grade {grade} {grade_name}: {counts[grade]}')
    print('Hand-adjust proposed_grade, then: seed_item_tiers.py --apply-grades ' + path)


def apply_grades(db, csv_path):
    if not os.path.exists(csv_path):
        fail(f'CSV not found: {csv_path}')
    known = {r[0] for r in db.execute('SELECT npc_id FROM npc_configs')}
    updates = []
    with open(csv_path, newline='', encoding='utf-8') as f:
        for line, row in enumerate(csv.DictReader(f), start=2):
            try:
                npc_id, grade = int(row['npc_id']), int(row['proposed_grade'])
            except (KeyError, ValueError):
                fail(f'{csv_path}:{line}: needs integer npc_id and proposed_grade columns')
            if npc_id not in known:
                fail(f'{csv_path}:{line}: unknown npc_id {npc_id}')
            if not 1 <= grade <= 5:
                fail(f'{csv_path}:{line}: grade {grade} outside 1..5')
            updates.append((grade, npc_id))
    with db:
        db.executemany('UPDATE npc_configs SET loot_grade = ? WHERE npc_id = ?', updates)
    missing = known - {npc_id for _, npc_id in updates}
    print(f'{len(updates)} NPC loot grades applied from {csv_path}')
    if missing:
        print(f'  note: {len(missing)} NPCs not in CSV kept their current grade: {sorted(missing)}')


def boss_stage1_report(db):
    items = load_items(db)
    npc_tables = dict(db.execute('SELECT npc_id, drop_table_id FROM npc_configs'))
    for npc_id, boss in BOSS_NPCS.items():
        table_id = npc_tables[npc_id]
        rows = db.execute('SELECT item_id, weight, min_count, max_count FROM drop_entries'
                          ' WHERE drop_table_id = ? AND tier = 1 ORDER BY weight DESC',
                          (table_id,)).fetchall()
        total = sum(w for _, w, _, _ in rows)
        gear = sum(w for i, w, _, _ in rows if ordinary_tier_gear(items, i))
        print(f'\n{boss} (npc {npc_id}, drop table {table_id}) - stage 1, total weight {total},'
              f' tier-gear share {100.0 * gear / total:.1f}%')
        for item_id, weight, min_count, max_count in rows:
            cls = item_class(items, item_id)
            tag = ('SPECIAL' if item_id in SPECIAL_ITEMS
                   else CLASS_NAMES[cls] if cls else '')
            print(f'  {item_id:5d} {item_name(items, item_id):32s} w={weight:5d}'
                  f' ({100.0 * weight / total:5.1f}%) x{min_count}-{max_count} {tag}')
    print('\nCuration is drop_entries data entry: build a CSV for --apply-drops'
          ' (columns: drop_table_id, stage, item_id, weight, min_count, max_count, action).')


def stage2_scan(db):
    items = load_items(db)
    rows = db.execute('SELECT de.drop_table_id, dt.name, de.item_id, de.weight,'
                      ' de.min_count, de.max_count'
                      ' FROM drop_entries de JOIN drop_tables dt USING (drop_table_id)'
                      ' WHERE de.tier = 2 ORDER BY de.drop_table_id, de.item_id').fetchall()
    hits = [r for r in rows if ordinary_tier_gear(items, r[2])]
    if not hits:
        print('stage-2 tables clean: no tier-eligible ordinary gear (validator rule passes).')
        return
    path = output_path('stage2_gear_proposal.csv')
    with open(path, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(['drop_table_id', 'table_name', 'stage', 'item_id', 'item_name',
                         'weight', 'min_count', 'max_count', 'action'])
        for table_id, table_name, item_id, weight, min_count, max_count in hits:
            writer.writerow([table_id, table_name, 2, item_id, item_name(items, item_id),
                             weight, min_count, max_count, 'move_stage1'])
    print(f'{len(hits)} tier-eligible gear rows in stage-2 tables (validator would block'
          f' tiered boot) -> {path}')
    for table_id, table_name, item_id, weight, *_ in hits:
        print(f'  table {table_id} {table_name}: {item_id} {item_name(items, item_id)} w={weight}')
    print("Review the action column (move_stage1 | delete | keep), then --apply-drops " + path)


def apply_drops(db, csv_path):
    if not os.path.exists(csv_path):
        fail(f'CSV not found: {csv_path}')
    actions = []
    with open(csv_path, newline='', encoding='utf-8') as f:
        for line, row in enumerate(csv.DictReader(f), start=2):
            try:
                action = row['action'].strip().lower()
                table_id, stage, item_id = int(row['drop_table_id']), int(row['stage']), int(row['item_id'])
                weight = int(row.get('weight') or 0)
                min_count = int(row.get('min_count') or 1)
                max_count = int(row.get('max_count') or 1)
            except (KeyError, ValueError):
                fail(f'{csv_path}:{line}: bad row (needs drop_table_id, stage, item_id, action)')
            if action not in ('move_stage1', 'delete', 'upsert', 'keep'):
                fail(f'{csv_path}:{line}: unknown action {action!r}')
            actions.append((action, table_id, stage, item_id, weight, min_count, max_count))
    with db:
        for action, table_id, stage, item_id, weight, min_count, max_count in actions:
            if action == 'keep':
                pass
            elif action == 'delete':
                db.execute('DELETE FROM drop_entries WHERE drop_table_id=? AND tier=? AND item_id=?',
                           (table_id, stage, item_id))
            elif action == 'move_stage1':
                db.execute('DELETE FROM drop_entries WHERE drop_table_id=? AND tier=1 AND item_id=?',
                           (table_id, item_id))
                db.execute('UPDATE drop_entries SET tier=1 WHERE drop_table_id=? AND tier=? AND item_id=?',
                           (table_id, stage, item_id))
            elif action == 'upsert':
                db.execute('DELETE FROM drop_entries WHERE drop_table_id=? AND tier=? AND item_id=?',
                           (table_id, stage, item_id))
                db.execute('INSERT INTO drop_entries (drop_table_id, tier, item_id, weight,'
                           ' min_count, max_count) VALUES (?,?,?,?,?,?)',
                           (table_id, stage, item_id, weight, min_count, max_count))
    counts = Counter(action for action, *_ in actions)
    print('drop_entries edits applied: '
          + ', '.join(f'{action} x{count}' for action, count in sorted(counts.items())))


def place_cape(db):
    added = 0
    with db:
        for table_id, weight in CAPE_PLACEMENTS:
            exists = db.execute('SELECT 1 FROM drop_entries WHERE drop_table_id=? AND tier=1'
                                ' AND item_id=?', (table_id, CAPE_ITEM)).fetchone()
            if not exists:
                db.execute('INSERT INTO drop_entries (drop_table_id, tier, item_id, weight,'
                           ' min_count, max_count) VALUES (?,1,?,?,1,1)',
                           (table_id, CAPE_ITEM, weight))
                added += 1
                print(f'  added Cape 402 to table {table_id} (w={weight})')
    print(f'Cape 402 restricted-mob placement: {len(CAPE_PLACEMENTS)} tables,'
          f' {added} added, {len(CAPE_PLACEMENTS) - added} already present.')


def legacy_pool_audit(db):
    items = load_items(db)
    droppable = {r[0] for r in db.execute('SELECT DISTINCT item_id FROM drop_entries')}
    gaps, non_rolling = [], []
    for item_id in sorted(droppable):
        if not ordinary_tier_gear(items, item_id):
            continue
        row = items[item_id]
        name, effect_type, pool = row['name'], row['item_effect_type'], row['attribute_pool_id']
        if effect_type in LEGACY_ROLLABLE_EFFECT_TYPES:
            if not pool or pool <= 0:
                gaps.append((item_id, name, effect_type))
        elif pool and pool > 0:
            non_rolling.append((item_id, name, effect_type, pool))
    if gaps:
        print(f'{len(gaps)} droppable tier-scope items missing a legacy pool'
              ' (legacy validator will block boot):')
        for item_id, name, effect_type in gaps:
            print(f'  {item_id:5d} {name} (effect_type {effect_type})')
    else:
        print('legacy pool audit clean: every droppable rollable tier-scope item has a pool.')
    if non_rolling:
        print(f'info: {len(non_rolling)} droppable items carry a pool but a non-rolling'
              ' effect type (pool is inert - 2-C effect-type gate):')
        for item_id, name, effect_type, pool in non_rolling:
            print(f'  {item_id:5d} {name} (effect_type {effect_type}, pool {pool})')


# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--db', default=DEFAULT_DB, help=f'gamedata.db path (default {DEFAULT_DB})')
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--grade-report', action='store_true')
    action.add_argument('--apply-grades', metavar='CSV')
    action.add_argument('--boss-stage1-report', action='store_true')
    action.add_argument('--stage2-scan', action='store_true')
    action.add_argument('--apply-drops', metavar='CSV')
    action.add_argument('--place-cape', action='store_true')
    action.add_argument('--legacy-pool-audit', action='store_true')
    args = parser.parse_args()

    verify_special_roster_sync()
    db = connect(args.db)
    try:
        if args.grade_report:
            grade_report(db)
        elif args.apply_grades:
            apply_grades(db, args.apply_grades)
        elif args.boss_stage1_report:
            boss_stage1_report(db)
        elif args.stage2_scan:
            stage2_scan(db)
        elif args.apply_drops:
            apply_drops(db, args.apply_drops)
        elif args.place_cape:
            place_cape(db)
        elif args.legacy_pool_audit:
            legacy_pool_audit(db)
        else:
            print(f'Seeding tiered tables in {args.db} ...')
            seed(db)
    finally:
        db.close()


if __name__ == '__main__':
    main()
