// CmdMintCheck.cpp: GM mint legality prover (Tiers 3-G)
//
// The mint gate without the mint: builds a fresh instance of one item,
// runs an operator-typed attribute spec through the boot-selected Roll
// strategy's `mint`, and prints accept/reject. Nothing enters an
// inventory and no client is involved, so the spec §10 legality rules
// (count == tier, one modifier per Bucket, min-tier ladder, Bands, tier
// scope) are provable headlessly on both platforms — the same role
// rollsmoke plays for the drop roll.
//
//////////////////////////////////////////////////////////////////////

#include "CmdMintCheck.h"
#include "ServerConsole.h"
#include "Game.h"
#include "GmMintSpec.h"
#include "ItemManager.h"
#include "Item.h"
#include "RollStrategy.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace hb::shared::item;

void CmdMintCheck::execute(CGame* game, const char* args)
{
	const std::vector<std::string> tokens = hb::server::split_args(args);

	int item_id = 0;
	if (tokens.size() < 2 || std::sscanf(tokens[0].c_str(), "%d", &item_id) != 1)
	{
		hb::console::error("Usage: mintcheck <item_id> <tier> [mod_id:value[:value2] ...]");
		return;
	}

	if (item_id < 0 || item_id >= hb::server::config::MaxItemTypes
		|| game->m_item_config_list[item_id] == nullptr)
	{
		hb::console::error("Invalid item ID: {}.", item_id);
		return;
	}

	item_attribute_data requested;
	const std::string complaint = hb::server::parse_mint_spec(tokens, 1, requested);
	if (!complaint.empty())
	{
		hb::console::error("{}", complaint);
		return;
	}

	CItem item;
	if (game->m_item_manager->init_item_attr(&item, item_id) == false)
	{
		hb::console::error("init_item_attr failed for item ID {}.", item_id);
		return;
	}

	const std::string violation = game->get_roll_strategy().mint(item, requested);
	if (violation.empty())
	{
		hb::console::success("mintcheck: {} ({}) tier {} x{} modifiers - LEGAL",
			item_id, game->m_item_config_list[item_id]->m_name,
			(int)requested.tier, (int)requested.modifier_count());
		hb::console::write("MINTCHECK {} {} OK", item_id, (int)requested.tier);
		return;
	}

	hb::console::error("mintcheck: {} ({}) tier {} - REJECTED: {}",
		item_id, game->m_item_config_list[item_id]->m_name,
		(int)requested.tier, violation);
	hb::console::write("MINTCHECK {} {} REJECT {}", item_id, (int)requested.tier, violation);
}
