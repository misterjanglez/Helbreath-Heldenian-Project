// CmdRollSmoke.cpp: roll-distribution smoke harness (Tiers 2-C)
//
// Repeatedly runs ItemManager::generate_item_attributes on a fresh instance
// of one item and histograms the outcome. Used to prove drop-roll parity
// across roll-code changes (2-C data wiring, 3-B tiered strategy): capture a
// baseline before the change, re-run after, compare the ROLLSMOKE lines.
//
//////////////////////////////////////////////////////////////////////

#include "CmdRollSmoke.h"
#include "ServerConsole.h"
#include "Game.h"
#include "ItemManager.h"
#include "Item.h"
#include <cstdio>
#include <vector>

void CmdRollSmoke::execute(CGame* game, const char* args)
{
	int item_id = 0;
	int count = 100000;
	if (args == nullptr || std::sscanf(args, "%d %d", &item_id, &count) < 1)
	{
		hb::console::error("Usage: rollsmoke <item_id> [count]");
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
		if (game->m_item_manager->generate_item_attributes(&item) == false)
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
