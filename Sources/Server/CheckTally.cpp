#include "CheckTally.h"

#include "Game.h"
#include "Item.h"

namespace hb::server
{
	int find_probe_item(CGame* game, bool want_stackable)
	{
		for (int i = 0; i < hb::server::config::MaxItemTypes; i++)
		{
			CItem* config = game->m_item_config_list[i];
			if (config == nullptr) continue;
			if (config->is_stackable() == want_stackable) return i;
		}
		return -1;
	}
}
