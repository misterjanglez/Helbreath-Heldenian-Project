// LegacyRollStrategy.cpp: the legacy Roll strategy (Tiers 3-A)
//
// Verbatim extraction of ItemManager::generate_item_attributes (the
// Tiers 2-C pool-driven path) plus its two roll helpers. Behavior is
// unchanged through the seam — the rollsmoke harness is the parity
// evidence across this move.
//
//////////////////////////////////////////////////////////////////////

#include "LegacyRollStrategy.h"
#include "Game.h"
#include "ItemManager.h"
#include "Item.h"
#include <cstdlib>
#include <vector>

using namespace hb::shared::item;

namespace hb::server
{

static int roll_attribute_value(int min_val, int max_val)
{
	if (max_val <= 0) return 0;
	if (min_val >= max_val) return min_val;

	// Weighted roll biased toward lower values within [min_val, max_val]
	static const int weights[] = { 10000, 7400, 5000, 3000, 2000, 1000, 500, 400, 300, 200, 100, 70, 30 };

	int range = max_val - min_val + 1;
	int total = 0;
	for (int i = 0; i < range; i++)
		total += (i < 13) ? weights[i] : weights[12];

	int roll = rand() % total;
	int cumulative = 0;
	for (int i = 0; i < range; i++)
	{
		cumulative += (i < 13) ? weights[i] : weights[12];
		if (roll < cumulative) return min_val + i;
	}
	return min_val;
}

// Weighted pick over one pool slot's entries. Legacy roll semantics:
// rand() % total-weight, cumulative walk in load order.
static uint8_t pick_pool_modifier(const std::vector<attribute_pool_entry_config>& entries,
	int total_weight)
{
	if (total_weight <= 0) return modifier_id::empty;
	int roll = rand() % total_weight;
	for (const auto& entry : entries)
	{
		roll -= entry.weight;
		if (roll < 0) return entry.modifier_id;
	}
	return modifier_id::empty;
}

bool legacy_roll_strategy::roll(CItem& item, const roll_context&)
{
	// Rollability gate unchanged from the hardcoded-table era: only three
	// effect types roll (is_legacy_rollable_effect_type). Some pool-assigned
	// gear (bows, Xelima-class weapons) carries other effect types and stays
	// attribute-less until a curation pass (Tiers 2-E) decides otherwise.
	const auto effect_type = item.get_item_effect_type();
	if (!is_legacy_rollable_effect_type(effect_type)) return false;
	const bool weapon_family = (effect_type != ItemEffectType::Defense);

	if (item.m_attribute_pool_id <= 0) return false;   // no pool: the item never rolls (spec §2)
	const auto* pool = m_game.get_tier_config().find_attribute_pool(item.m_attribute_pool_id);
	if (pool == nullptr) return false;

	uint8_t primaryType = pick_pool_modifier(pool->prefix_entries, pool->prefix_total_weight);
	if (primaryType == modifier_id::empty) return false;

	int primaryValue = roll_attribute_value(m_game.m_modifier_min_value[primaryType],
		m_game.m_modifier_max_value[primaryType]);
	// Legacy halving for the two charge-style armor prefixes (dead zero kept
	// until drift fix 3-F).
	if (primaryType == modifier_id::mana_converting || primaryType == modifier_id::crit_chance)
		primaryValue = primaryValue / 2;

	uint8_t secondaryType = modifier_id::empty;
	int secondaryValue = 0;
	if (rand() % 100 < pool->secondary_chance)
	{
		secondaryType = pick_pool_modifier(pool->secondary_entries, pool->secondary_total_weight);
		if (secondaryType != modifier_id::empty)
			secondaryValue = roll_attribute_value(m_game.m_modifier_min_value[secondaryType],
				m_game.m_modifier_max_value[secondaryType]);
	}

	// Armor never dyes from its roll; weapon-family colors ride the prefix row.
	item.m_instance.item_color = weapon_family
		? (char)m_game.m_modifier_weapon_color[primaryType] : 0;
	item.set_custom_made(false);
	item.set_prefix(primaryType, static_cast<uint8_t>(primaryValue));
	item.set_secondary(secondaryType, static_cast<uint8_t>(secondaryValue));
	item.set_enchant_bonus(0);

	m_game.m_item_manager->apply_modifier_derived_stats(&item);
	// New item: current durability should equal max durability
	item.m_instance.cur_durability = item.m_durability;
	return true;
}

} // namespace hb::server
