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
#include <algorithm>
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
	// The two charge-style armor prefixes roll on the shared 1..13 ladder and
	// are halved onto their own 1..7 band. Retail's `v / 2` put a seventh of
	// that ladder on a dead zero — a rolled line that did nothing at all;
	// `(v + 1) / 2`, floored at 1, is the retail-correct halving (spec §6, §13).
	if (primaryType == modifier_id::mana_converting || primaryType == modifier_id::crit_chance)
		primaryValue = std::max(1, (primaryValue + 1) / 2);

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

std::string legacy_roll_strategy::mint(CItem& item, const item_attribute_data& requested)
{
	// Legacy items carry no tier and exactly two positional lines. Slots
	// [2]/[3] and the tier byte are the tiered strategy's vocabulary — a
	// request carrying them came from a client talking to the wrong mode.
	if (requested.tier != 0)
		return "tiers do not exist in legacy mode";
	if (requested.modifiers[2].type != 0 || requested.modifiers[3].type != 0)
		return "legacy mode carries a prefix and a secondary only";

	item.set_custom_made(requested.custom_made != 0);
	item.set_prefix(requested.modifiers[0].type, requested.modifiers[0].value);
	item.set_secondary(requested.modifiers[1].type, requested.modifiers[1].value);
	item.set_enchant_bonus(requested.enchant_bonus);
	m_game.m_item_manager->apply_modifier_derived_stats(&item);

	// Prefix dye tint, unified palette weapon indices 16-21. Deliberately
	// ungated on item kind (fork issue #2): a GM-minted prefixed special is a
	// tester tool and the tint is what marks it as non-base. This mapping is
	// the creator's own, not m_modifier_weapon_color's — the two disagree and
	// reconciling them is issue #2's call, not this cycle's.
	switch (item.get_prefix_type())
	{
	case modifier_id::agile:         item.m_instance.item_color = 16; break;
	case modifier_id::light:         item.m_instance.item_color = 16; break;
	case modifier_id::strong:        item.m_instance.item_color = 16; break;
	case modifier_id::poisoning:     item.m_instance.item_color = 17; break;
	case modifier_id::critical:      item.m_instance.item_color = 18; break;
	case modifier_id::spell_success: item.m_instance.item_color = 18; break;
	case modifier_id::sharp:         item.m_instance.item_color = 19; break;
	case modifier_id::righteous:     item.m_instance.item_color = 20; break;
	case modifier_id::ancient:       item.m_instance.item_color = 21; break;
	default: break;
	}
	return {};
}

} // namespace hb::server
