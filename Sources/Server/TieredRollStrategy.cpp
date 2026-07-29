// TieredRollStrategy.cpp: the tiered Roll strategy (Tiers 3-B)
//
// First-drop pipeline (spec §8): when the stage-1 table yields
// tier-eligible gear, roll Loot grade -> Tier -> Bucket-law modifier
// selection -> Band/Tier-curve values. Second drops and non-drop venues
// never tier-roll. The item's stored stats are never mutated — tier
// effects derive at equip time (3-C).
//
// Design contract: PLANS/ItemTiers_Plan.md §3, §4, §6-§8.
//
//////////////////////////////////////////////////////////////////////

#include "TieredRollStrategy.h"
#include "Game.h"
#include "Item.h"
#include "Log.h"
#include "TierConfigStore.h"
#include "TierConfigValidator.h"
#include <algorithm>
#include <iterator>
#include <vector>

using namespace hb::shared::item;

namespace hb::server
{

namespace
{

// One weighted pick: sum -> uniform draw -> subtractive walk, returning
// the picked index (last index on a zero-total pathology). `weight_of`
// maps an element to its integer weight.
template <typename Range, typename WeightOf>
int pick_weighted(const Range& range, WeightOf weight_of, std::mt19937& rng)
{
	int total = 0;
	for (const auto& entry : range) total += weight_of(entry);
	if (total <= 0) return static_cast<int>(std::size(range)) - 1;

	int roll = std::uniform_int_distribution<int>(0, total - 1)(rng);
	int index = 0;
	for (const auto& entry : range)
	{
		roll -= weight_of(entry);
		if (roll < 0) return index;
		index++;
	}
	return index - 1;
}

} // namespace

// §1 scope classes only, with the named-unique roster excluded explicitly —
// Kloness-class weapons pass the class check but must never roll (spec §8
// implementation note).
bool is_tier_scope_gear(const CItem& item)
{
	if (derive_tier_item_class(item.get_item_sub_type(), item.get_weapon_class(),
		item.get_equip_pos()) == tier_item_class::none) return false;
	return !is_special_item(item.m_id_num);
}

// Grade -> Tier: one weighted pick over the grade's four tier weights.
// Staircase zero-weights are the hard gate (vermin can only land Common);
// the validator guarantees at least one weight is positive.
uint8_t tiered_roll_strategy::roll_tier(const loot_grade_config& grade)
{
	int weights[tier_count];
	for (uint8_t tier = 1; tier <= tier_count; tier++)
		weights[tier - 1] = grade.tier_weight(tier);
	return static_cast<uint8_t>(pick_weighted(weights,
		[](int weight) { return weight; }, m_rng) + 1);
}

// One value roll: the tier picks a probability curve, never value
// legality (spec §6.1) — every tier rolls the full band (or its per-tier
// window when data sets one). Returns the stored wire byte
// (display units ÷ multiplier).
uint8_t tiered_roll_strategy::roll_modifier_value(const tier_config& config,
	const modifier_catalog_config& modifier, uint8_t tier, const tier_curve_config& curve)
{
	if (modifier.multiplier == 0) return 0;   // value-less line (Agile's fixed -1)

	const modifier_window range = modifier.display_range(tier);
	const int stored_min = range.min / modifier.multiplier;
	const int stored_max = range.max / modifier.multiplier;
	if (stored_max <= stored_min) return static_cast<uint8_t>(stored_min);

	// Band max comes only from the explicit cap chance (spec §7): the tier
	// curve's denominator, overridden per (modifier, tier) for the tails
	// (pair halves, ALL STATS).
	int cap_den = curve.cap_chance_den;
	for (const auto& row : config.curve_overrides)
		if (row.modifier_id == modifier.modifier_id && row.tier == tier)
		{
			cap_den = row.cap_chance_den;
			break;
		}
	if (std::uniform_int_distribution<int>(1, cap_den)(m_rng) == 1)
		return static_cast<uint8_t>(stored_max);

	// Otherwise sample the piecewise-linear CDF through the curve anchors
	// (p50, p90, p99 are normalized band positions) and map the position
	// strictly below band max.
	const double positions[] = { 0.0, curve.p50, curve.p90, curve.p99, 1.0 };
	const double cumulative[] = { 0.0, 0.5, 0.9, 0.99, 1.0 };
	const double u = std::uniform_real_distribution<double>(0.0, 1.0)(m_rng);
	double position = 1.0;
	for (int i = 0; i < 4; i++)
	{
		if (u > cumulative[i + 1]) continue;
		position = positions[i] + (u - cumulative[i])
			* (positions[i + 1] - positions[i]) / (cumulative[i + 1] - cumulative[i]);
		break;
	}
	const int width = stored_max - stored_min;
	const int offset = std::min(static_cast<int>(position * width), width - 1);
	return static_cast<uint8_t>(stored_min + offset);
}

bool tiered_roll_strategy::roll(CItem& item, const roll_context& context)
{
	// Only the stage-1 first drop tier-rolls (spec §8). Second drops stay
	// the unique/special venue; GM minting joins in 3-G.
	if (!context.tier_rolls) return false;

	if (!is_tier_scope_gear(item)) return false;

	// The class the gate just accepted, needed again to weight the roll.
	const auto item_class = derive_tier_item_class(item.get_item_sub_type(),
		item.get_weapon_class(), item.get_equip_pos());

	const tier_config& config = m_game.get_tier_config();
	const loot_grade_config* grade = config.find_loot_grade(context.loot_grade);
	if (grade == nullptr) return false;

	const uint8_t tier = roll_tier(*grade);

	const tier_curve_config* curve = config.find_tier_curve(tier);
	if (curve == nullptr) return false;   // validator guarantees the four tier curves

	// Bucket law: weighted picks from the class's eligible modifiers with
	// the min-tier ladder applied; a pick retires its whole Bucket, so
	// repeats and same-Bucket stacking are structurally impossible.
	struct candidate
	{
		const modifier_catalog_config* modifier;
		int weight;
	};
	std::vector<candidate> candidates;
	for (const auto& row : config.eligibility)
	{
		if (row.item_class != item_class) continue;
		const modifier_catalog_config* modifier = config.find_modifier(row.modifier_id);
		if (modifier == nullptr || modifier->min_tier > tier) continue;
		candidates.push_back({ modifier, row.weight });
	}

	item_attribute_data attributes;
	uint8_t rolled = 0;
	while (rolled < tier && !candidates.empty())
	{
		const modifier_catalog_config* chosen = candidates[pick_weighted(candidates,
			[](const candidate& entry) { return entry.weight; }, m_rng)].modifier;

		item_modifier& slot = attributes.modifiers[rolled];
		slot.type = chosen->modifier_id;
		slot.value = roll_modifier_value(config, *chosen, tier, *curve);
		// Archetype pairs roll two independent halves; ALL STATS is one
		// shared N (slot.value), so its value2 stays 0 (spec §4.4).
		if (chosen->effect_id == effect_id::add_attribute_pair)
			slot.value2 = roll_modifier_value(config, *chosen, tier, *curve);
		rolled++;

		std::erase_if(candidates, [bucket_id = chosen->bucket_id](const candidate& entry)
			{ return entry.modifier->bucket_id == bucket_id; });
	}

	if (rolled == 0) return false;
	if (rolled < tier)
		// Data gap the boot validator's fillability check should have
		// caught; keep the §3 invariant (tiered => count == tier) by
		// degrading the tier to the count actually rolled.
		hb::logger::error("tiered roll: item {} class {} filled only {}/{} buckets - check modifier_eligibility",
			item.m_id_num, static_cast<int>(item_class), static_cast<int>(rolled), static_cast<int>(tier));

	attributes.tier = rolled;
	// Fresh drop: custom_made and enchant_bonus stay 0; item_color stays
	// untouched — tiered gear never tints (spec §11).
	item.set_attributes(attributes);
	return true;
}

std::string tiered_roll_strategy::mint(CItem& item, const item_attribute_data& requested)
{
	const tier_config& config = m_game.get_tier_config();

	// A plain mint — no tier, no lines — is legal on anything: base gear,
	// consumables, quest props. Everything below is about tiered instances.
	if (requested.tier == 0)
	{
		if (requested.modifier_count() != 0)
			return "modifiers require a tier (1-4)";
	}
	else
	{
		// The same gate the roll applies, split back into its two halves so
		// the GM learns which rule refused the spec.
		if (!is_tier_scope_gear(item))
			return is_special_item(item.m_id_num)
				? "named uniques never carry a tier"
				: "this item is outside tier scope and cannot carry a tier";

		// The one structural audit, shared with the roll smoke harness: tier
		// 1..4, count == tier, one modifier per Bucket, min-tier ladder,
		// values inside the band/window. Per-class eligibility is deliberately
		// NOT a gate — it weights what may roll, and a GM minting an off-class
		// line is a legitimate test instrument.
		std::string violation = validate_tiered_instance(config, requested);
		if (!violation.empty()) return violation;
	}

	// Stored stats never mutate for a tiered item (spec §6.5 / cycle 3-C):
	// no apply_modifier_derived_stats, no dye tint (§11) — every effect the
	// lines carry derives at equip time from the same catalog rows.
	item.set_attributes(requested);
	return {};
}

} // namespace hb::server
