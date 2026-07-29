// CmdRollSmoke.cpp: roll-distribution smoke harness (Tiers 2-C)
//
// Repeatedly runs the boot-selected Roll strategy on a fresh instance
// of one item and histograms the outcome. Used to prove drop-roll parity
// across roll-code changes (2-C data wiring, 3-A seam extraction, 3-B
// tiered strategy): capture a baseline before the change, re-run after,
// compare the ROLLSMOKE lines.
//
// Legacy mode: rollsmoke <item_id> [count] — P/S/C histograms, unchanged
// format since 2-C.
// Tiered mode: rollsmoke <item_id> [count] [grade] — tier histogram (T),
// per-tier modifier value histograms (M, and N for pair second halves),
// plus a per-item legality audit (count == tier, Bucket law, min-tier
// ladder, band/window bounds) so a smoke run is also structural evidence.
//
//////////////////////////////////////////////////////////////////////

#include "CmdRollSmoke.h"
#include "ServerConsole.h"
#include "Game.h"
#include "ItemManager.h"
#include "Item.h"
#include "TierConfigStore.h"
#include "TierConfigValidator.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace hb::shared::item;

namespace
{

void rollsmoke_tiered(CGame* game, int item_id, int count, int grade)
{
	const hb::server::tier_config& config = game->get_tier_config();
	if (config.find_loot_grade(static_cast<uint8_t>(grade)) == nullptr)
	{
		hb::console::error("Unknown loot grade: {}.", grade);
		return;
	}

	// [tier][modifier id][stored value] counters; tier index 1..tier_count.
	std::vector<int> values((tier_count + 1) * 256 * 256);
	std::vector<int> values2((tier_count + 1) * 256 * 256);
	int tier_counts[tier_count + 1] = {};
	int no_roll = 0;
	int violations = 0;

	for (int i = 0; i < count; i++)
	{
		CItem item;
		if (game->m_item_manager->init_item_attr(&item, item_id) == false)
		{
			hb::console::error("init_item_attr failed for item ID {}.", item_id);
			return;
		}
		hb::server::roll_context context;
		context.loot_grade = static_cast<uint8_t>(grade);
		context.tier_rolls = true;
		if (game->get_roll_strategy().roll(item, context) == false)
		{
			no_roll++;
			continue;
		}

		const item_attribute_data& attributes = item.get_attributes();
		// The shared structural gate (also the 3-G GM-mint gate): count ==
		// tier, Bucket law, min-tier ladder, band/window bounds.
		std::string violation = hb::server::validate_tiered_instance(config, attributes);
		if (!violation.empty())
		{
			if (violations < 10)
				hb::console::error("rollsmoke VIOLATION: {}", violation);
			violations++;
		}

		const int tier = attributes.tier <= tier_count ? attributes.tier : 0;
		tier_counts[tier]++;
		if (tier == 0) continue;
		for (const auto& mod : attributes.modifiers)
		{
			if (mod.type == 0) continue;
			values[(tier * 256 + mod.type) * 256 + mod.value]++;
			if (mod.value2 != 0)
				values2[(tier * 256 + mod.type) * 256 + mod.value2]++;
		}
	}

	hb::console::write("rollsmoke: item {} ({}) x{} rolls, grade {}, {} produced no attributes, {} legality violations",
		item_id, game->m_item_config_list[item_id]->m_name, count, grade, no_roll, violations);

	for (int tier = 1; tier <= tier_count; tier++)
		if (tier_counts[tier] > 0)
			hb::console::write("ROLLSMOKE T {} {}", tier, tier_counts[tier]);

	auto dump = [](const char* slot, const std::vector<int>& hist)
	{
		for (int tier = 1; tier <= tier_count; tier++)
			for (int id = 0; id < 256; id++)
				for (int value = 0; value < 256; value++)
					if (int n = hist[(tier * 256 + id) * 256 + value]; n > 0)
						hb::console::write("ROLLSMOKE {} {} {} {} {}", slot, tier, id, value, n);
	};
	dump("M", values);
	dump("N", values2);
}

} // namespace

void CmdRollSmoke::execute(CGame* game, const char* args)
{
	int item_id = 0;
	int count = 100000;
	int grade = loot_grade::boss;   // tiered default: every tier reachable
	if (args == nullptr || std::sscanf(args, "%d %d %d", &item_id, &count, &grade) < 1)
	{
		hb::console::error("Usage: rollsmoke <item_id> [count] [grade]");
		return;
	}
	if (count < 1) count = 1;
	if (count > 5000000) count = 5000000;

	if (item_id < 0 || item_id >= hb::server::config::MaxItemTypes ||
		game->m_item_config_list[item_id] == nullptr)
	{
		hb::console::error("Invalid item ID: {}.", item_id);
		return;
	}

	if (game->get_tier_config().item_system == item_system_mode::tiered)
	{
		rollsmoke_tiered(game, item_id, count, grade);
		return;
	}

	// [modifier id][value] counters, indexed id * 256 + value (both uint8 on
	// the wire); C tallies the rolled item_color per primary id
	std::vector<int> primary(256 * 256), secondary(256 * 256), color(256 * 256);
	int no_roll = 0;

	for (int i = 0; i < count; i++)
	{
		CItem item;
		if (game->m_item_manager->init_item_attr(&item, item_id) == false)
		{
			hb::console::error("init_item_attr failed for item ID {}.", item_id);
			return;
		}
		if (game->get_roll_strategy().roll(item, hb::server::roll_context{}) == false)
		{
			no_roll++;
			continue;
		}
		primary[item.get_prefix_type() * 256 + item.get_prefix_value()]++;
		secondary[item.get_secondary_type() * 256 + item.get_secondary_value()]++;
		color[item.get_prefix_type() * 256 + (uint8_t)item.m_instance.item_color]++;
	}

	hb::console::write("rollsmoke: item {} ({}) x{} rolls, {} produced no attributes",
		item_id, game->m_item_config_list[item_id]->m_name, count, no_roll);

	auto dump = [count](const char* slot, const std::vector<int>& hist)
	{
		for (int id = 0; id < 256; id++)
		{
			int total = 0;
			for (int value = 0; value < 256; value++)
				total += hist[id * 256 + value];
			if (total == 0) continue;
			hb::console::write("  {} id {:3}: {:8} ({:.2f}%)", slot, id, total,
				100.0 * total / count);
			for (int value = 0; value < 256; value++)
				if (hist[id * 256 + value] > 0)
					hb::console::write("ROLLSMOKE {} {} {} {}", slot, id, value, hist[id * 256 + value]);
		}
	};
	dump("P", primary);
	dump("S", secondary);
	dump("C", color);
}
