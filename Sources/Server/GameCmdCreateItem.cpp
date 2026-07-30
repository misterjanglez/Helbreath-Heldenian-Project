#include "GameCmdCreateItem.h"
#include "Game.h"
#include "ItemManager.h"
#include "Item.h"
#include "Item/ItemEnums.h"
#include "GmMintSpec.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

using namespace hb::shared::net;
using namespace hb::server::config;

namespace
{

constexpr const char* usage_text =
	"Usage: /createitem <item_id> [amount] [tier] [mod_id:value[:value2] ...]";

// Highest tiered mint this command will make in one go. A tiered mint sends
// one item notification per copy (they are distinct instances, unlike a
// stack), so the chat venue keeps the tester menu's ceiling.
constexpr int max_tiered_amount = 10;

} // namespace

bool GameCmdCreateItem::execute(CGame* game, int client_h, const char* args)
{
	if (game->m_client_list[client_h] == nullptr)
		return true;

	const std::vector<std::string> tokens = hb::server::split_args(args);

	int item_id = 0, amount = 1;
	if (tokens.empty() || std::sscanf(tokens[0].c_str(), "%d", &item_id) != 1)
	{
		game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, usage_text);
		return true;
	}
	if (tokens.size() > 1 && std::sscanf(tokens[1].c_str(), "%d", &amount) != 1)
	{
		game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, usage_text);
		return true;
	}

	if (item_id < 0 || item_id >= MaxItemTypes || game->m_item_config_list[item_id] == nullptr)
	{
		game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "Invalid item ID.");
		return true;
	}

	// Attributed mint (Item Tiers 3-G): a tier plus up to four modifier
	// slots, stated by the GM and gated by the running mode's Roll strategy.
	// This is the venue that can attempt an ILLEGAL mint, which is how the
	// legality gate gets exercised without a client that knows the rules.
	if (tokens.size() > 2)
	{
		hb::shared::item::item_attribute_data requested;
		const std::string complaint = hb::server::parse_mint_spec(tokens, 2, requested);
		if (!complaint.empty())
		{
			game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, complaint.c_str());
			game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, usage_text);
			return true;
		}

		game->gm_mint_items(client_h, item_id,
			std::clamp(amount, 1, max_tiered_amount), requested);
		return true;
	}

	if (amount < 1) amount = 1;
	bool is_gold = (item_id == hb::shared::item::ItemId::Gold);
	if (!is_gold && amount > 1000) amount = 1000;

	const char* item_name = game->m_item_config_list[item_id]->m_name;
	bool true_stack = game->m_item_config_list[item_id]->is_stackable() || is_gold;

	int created = 0;

	if (true_stack)
	{
		// True stacks: single item with count = amount (arrows, materials, gold)
		CItem* item = game->m_item_manager->create_item(item_name, hb::server::item_origin::gm_mint,
			game->m_item_manager->birth_at(client_h));
		if (item != nullptr)
		{
			item->m_instance.count = amount;
			int erase_req = 0;
			if (game->m_item_manager->add_client_item_list(client_h, item, &erase_req))
			{
				game->m_item_manager->send_item_notify_msg(client_h, Notify::ItemObtained, item, 0);
				created = amount;
				game->m_item_manager->item_log(hb::server::net::ItemLogAction::GmMint,
					client_h, created, item);
				// Merged into an existing stack: the contents live on in the slot
				// it merged into, so this frees a Counted husk and records nothing.
				if (erase_req == 1)
					game->m_item_manager->destroy_item(item,
						hb::server::destroy_reason::merged, client_h);
			}
			else
			{
				game->m_item_manager->destroy_item(item,
					hb::server::destroy_reason::discarded, client_h);
				game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "Inventory full.");
				return true;
			}
		}
		else
		{
			// The factory already released the partially built item.
			game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "Failed to create item.");
			return true;
		}
	}
	else
	{
		// Soft-linked items: individual items, one bulk notification
		created = game->m_item_manager->add_client_bulk_item_list(client_h, item_name, amount);
	}

	char buf[128];
	std::snprintf(buf, sizeof(buf), "Created %d x %s (ID: %d).", created, item_name, item_id);
	game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, buf);

	return true;
}
