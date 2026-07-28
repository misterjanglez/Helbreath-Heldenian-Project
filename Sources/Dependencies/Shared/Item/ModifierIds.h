// ModifierIds.h: Unified modifier ID space for the Item Tiers system
//
// The flat namespace every rolled item modifier occupies in storage, on the
// wire, and in the DB — successor to the retired positional
// AttributePrefixType / SecondaryEffectType pair (Tiers 1-B).
// Both Roll strategies (legacy and tiered) emit unified IDs: strategies
// differ in what they roll, never in what they store.
//
// Design contract: PLANS/ItemTiers_Plan.md §4 (catalog), §12 (encoding).
// The ID assignment below is the recorded implementation-time pick
// (PLANS/ItemTiers_Implementation_Plan.md, "Implementation-time picks").
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>

#include "ItemEnums.h"

namespace hb::shared::item {

//------------------------------------------------------------------------
// Unified modifier IDs — 0 = empty slot, 1-255 usable, renumbered from
// scratch. IDs are data (rows in the replicated modifier catalog, keyed by
// modifier_catalog.modifier_id); this enum is the authoritative assignment
// table, in spec §4 catalog order — bucket blocks:
//
//   1-5 DAMAGE · 6-7 PRECISION · 8-10 HANDLING · 11-12 ECONOMY ·
//   13 CASTING · 14-21 SET-AXIS · 22-23 COMBAT UTILITY ·
//   24-34 ATTRIBUTES · 35-40 MARQUEE
//
// Light and Strong roll on both weapons and armor as the same modifier —
// one ID each; per-class eligibility lives in modifier_eligibility data.
//------------------------------------------------------------------------
namespace modifier_id {
enum modifier_id : uint8_t
{
	empty               = 0,

	// DAMAGE (weapons)
	sharp               = 1,
	critical            = 2,
	poisoning           = 3,
	righteous           = 4,
	ancient             = 5,   // keeps its durability rider (derived at equip)

	// PRECISION
	hitting_probability = 6,
	consecutive_attack  = 7,

	// HANDLING
	agile               = 8,
	light               = 9,
	strong              = 10,

	// ECONOMY
	experience          = 11,
	gold                = 12,

	// CASTING (wands)
	spell_success       = 13,  // legacy "Special"

	// SET-AXIS (armor family)
	defense_ratio       = 14,
	physical_absorb     = 15,
	magic_resist        = 16,
	magic_absorb        = 17,
	hp_recovery         = 18,
	sp_recovery         = 19,
	mp_recovery         = 20,
	poison_resist       = 21,

	// COMBAT UTILITY (armor family)
	mana_converting     = 22,
	crit_chance         = 23,

	// ATTRIBUTES — singles (any tier)
	attr_str            = 24,
	attr_dex            = 25,
	attr_mag            = 26,
	attr_int            = 27,
	attr_vit            = 28,
	attr_chr            = 29,

	// ATTRIBUTES — archetype pairs (two independent rolls)
	attr_str_dex        = 30,
	attr_int_mag        = 31,
	attr_vit_str        = 32,
	attr_vit_int        = 33,

	// ATTRIBUTES — all stats (one shared roll)
	attr_all_stats      = 34,

	// MARQUEE
	sunder              = 35,  // weapons
	bleed               = 36,  // weapons
	mp_drain            = 37,  // weapons
	sp_drain            = 38,  // weapons
	cast_time_reduction = 39,  // wands
	move_speed          = 40   // capes + leggings
};
}

//------------------------------------------------------------------------
// Code-side effect IDs — the behavior key each modifier_catalog row
// carries (catalog column effect_id). Code switches on these — never on
// modifier IDs — so new catalog rows can reuse existing behaviors (e.g.
// every attribute single is add_attribute with effect_param1 naming the
// attribute).
//
// Values are arbitrary stable keys, grouped legacy-then-new; they imply NO
// numeric correspondence with the legacy enums. The authoritative
// legacy -> unified translation table is below.
//------------------------------------------------------------------------
namespace effect_id {
enum effect_id : uint8_t
{
	none                = 0,

	// legacy prefix behaviors
	critical            = 1,
	poisoning           = 2,
	righteous           = 3,
	agile               = 4,
	light               = 5,   // effective-weight reduction (derived at equip)
	sharp               = 6,
	strong              = 7,   // endurance bonus (derived at equip)
	ancient             = 8,   // extra damage + endurance rider
	spell_success       = 9,
	mana_converting     = 10,
	crit_chance         = 11,

	// legacy secondary behaviors
	poison_resist       = 12,
	hitting_probability = 13,
	defense_ratio       = 14,
	hp_recovery         = 15,
	sp_recovery         = 16,
	mp_recovery         = 17,
	magic_resist        = 18,
	physical_absorb     = 19,
	magic_absorb        = 20,
	consecutive_attack  = 21,
	experience_bonus    = 22,
	gold_bonus          = 23,

	// attribute ladder (params name the attribute(s))
	add_attribute       = 24,  // effect_param1 = tier_attribute; rolls value
	add_attribute_pair  = 25,  // effect_param1/2 = tier_attribute; rolls value + value2
	add_all_attributes  = 26,  // one shared roll applied to every attribute

	// marquee mechanics
	sunder              = 27,
	bleed               = 28,
	mp_drain            = 29,
	sp_drain            = 30,
	cast_time_reduction = 31,
	move_speed          = 32
};
}

//------------------------------------------------------------------------
// Legacy -> unified translation — THE one table (Tiers 1-B).
//
// The retired positional enums survive only as raw numbers at three
// boundaries: crafted-item recipe nibbles (CBuildItem::m_attribute), the
// GM tester-menu attribute bitmask (both 4-bit, so they must stay in the
// legacy 1-12 spaces on the wire), and the legacy-keyed
// attribute_prefix_types / attribute_secondary_types config rows (replaced
// by the modifier catalog in Tiers 1-D). Translate to unified IDs at those
// boundaries; everything past them speaks modifier_id only.
//
// The third legacy behavior enum, AddEffectType (ItemEnums.h), needs no
// row here: accessory add-effects are base-item config read from
// m_item_effect_value1/2, not rolled instance modifiers — accessories are
// outside tier scope (spec §1) and never enter the unified ID space.
//
// Legacy prefix 4 was a reserved hole; it maps to empty.
//------------------------------------------------------------------------
constexpr uint8_t legacy_prefix_to_modifier_id(uint8_t legacy)
{
	constexpr uint8_t table[13] = {
		modifier_id::empty,               //  0 None
		modifier_id::critical,            //  1 Critical
		modifier_id::poisoning,           //  2 Poisoning
		modifier_id::righteous,           //  3 Righteous
		modifier_id::empty,               //  4 Reserved (unused)
		modifier_id::agile,               //  5 Agile
		modifier_id::light,               //  6 Light
		modifier_id::sharp,               //  7 Sharp
		modifier_id::strong,              //  8 Strong
		modifier_id::ancient,             //  9 Ancient
		modifier_id::spell_success,       // 10 Special
		modifier_id::mana_converting,     // 11 ManaConverting
		modifier_id::crit_chance          // 12 CritChance
	};
	return legacy < 13 ? table[legacy] : modifier_id::empty;
}

constexpr uint8_t legacy_secondary_to_modifier_id(uint8_t legacy)
{
	constexpr uint8_t table[13] = {
		modifier_id::empty,               //  0 None
		modifier_id::poison_resist,       //  1 PoisonResistance
		modifier_id::hitting_probability, //  2 HittingProb
		modifier_id::defense_ratio,       //  3 DefenseRatio
		modifier_id::hp_recovery,         //  4 HPRecovery
		modifier_id::sp_recovery,         //  5 SPRecovery
		modifier_id::mp_recovery,         //  6 MPRecovery
		modifier_id::magic_resist,        //  7 MagicResistance
		modifier_id::physical_absorb,     //  8 PhysicalAbsorb
		modifier_id::magic_absorb,        //  9 MagicAbsorb
		modifier_id::consecutive_attack,  // 10 ConsecutiveAttack
		modifier_id::experience,          // 11 ExperienceBonus
		modifier_id::gold                 // 12 GoldBonus
	};
	return legacy < 13 ? table[legacy] : modifier_id::empty;
}

//------------------------------------------------------------------------
// Which roll strategy the world runs (meta.item_system from Phase 2 on;
// hardcoded legacy until then). Replicated to clients in the modifier
// catalog header so rendering can be mode-aware (tier names/colors vs
// legacy prefix tints).
//------------------------------------------------------------------------
namespace item_system_mode {
enum item_system_mode : uint8_t
{
	legacy = 0,
	tiered = 1
};
}

// Number of tiers (Common / Rare / Epic / Legendary) — sizes the wire
// presentation table and every client/server tier array.
inline constexpr uint8_t tier_count = 4;

//------------------------------------------------------------------------
// Buckets — the thematic groups of the modifier catalog (spec §4). An item
// carries at most one modifier per bucket; the wire catalog's bucket_id
// gives tooltips a stable ordering key. Values follow §4 catalog order
// (the modifier_id block comment above).
//------------------------------------------------------------------------
namespace tier_bucket {
enum tier_bucket : uint8_t
{
	none           = 0,
	damage         = 1,
	precision      = 2,
	handling       = 3,
	economy        = 4,
	casting        = 5,
	set_axis       = 6,
	combat_utility = 7,
	attributes     = 8,
	marquee        = 9
};
}

//------------------------------------------------------------------------
// Attribute keys for the ATTRIBUTES-ladder effect params
// (modifier_catalog.effect_param1/effect_param2). Values match the
// hb::shared::net::StatId wire ids (NetMessages.h) so the two attribute
// keyspaces never diverge.
//------------------------------------------------------------------------
namespace tier_attribute {
enum tier_attribute : uint8_t
{
	none         = 0,
	strength     = 1,
	dexterity    = 2,
	intelligence = 3,
	vitality     = 4,
	magic        = 5,
	charisma     = 6
};
}

//------------------------------------------------------------------------
// Tier item classes — the eight gear classes the tier system rolls
// (spec §1 scope). Keys modifier_eligibility.item_class.
//------------------------------------------------------------------------
namespace tier_item_class {
enum tier_item_class : uint8_t
{
	none         = 0,   // outside tier scope (jewelry, boots, consumables, ...)
	melee_weapon = 1,
	bow          = 2,
	wand         = 3,
	body_armor   = 4,   // incl. hauberks/robes (Body, Arms, FullBody slots)
	helm         = 5,
	leggings     = 6,
	shield       = 7,
	cape         = 8
};
}

// Derive the tier item class from base item config. Mirrors the enchant
// route switch (Server/ItemManager.cpp request_item_upgrade_handler), with
// armor split by slot — capes are item_sub_type::armor in the item data,
// so Back is a slot case, not an accessory. Class shape only: droppability
// and the is_special_item unique roster are excluded at roll time, not here.
constexpr tier_item_class::tier_item_class derive_tier_item_class(
	item_sub_type::item_sub_type sub_type,
	weapon_class::weapon_class weapon,
	EquipPos equip_pos)
{
	if (sub_type == item_sub_type::accessory)
		return tier_item_class::none;          // rings, necklaces, pendants
	if (weapon == weapon_class::bow)
		return tier_item_class::bow;
	if (weapon == weapon_class::wand)
		return tier_item_class::wand;
	if (equip_pos == EquipPos::LeftHand)
		return tier_item_class::shield;        // shields are sub_type weapon; the slot decides
	if (sub_type == item_sub_type::weapon)
		return tier_item_class::melee_weapon;
	if (sub_type != item_sub_type::armor)
		return tier_item_class::none;

	switch (equip_pos)
	{
	case EquipPos::Head:     return tier_item_class::helm;
	case EquipPos::Body:
	case EquipPos::Arms:                       // hauberks, shirts
	case EquipPos::FullBody: return tier_item_class::body_armor;
	case EquipPos::Leggings: return tier_item_class::leggings;
	case EquipPos::Back:     return tier_item_class::cape;
	default:                 return tier_item_class::none;  // boots etc.
	}
}

} // namespace hb::shared::item
