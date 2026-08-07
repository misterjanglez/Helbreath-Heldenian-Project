#include "GameCmdBeginHeldenian.h"
#include "Game.h"
#include "WarManager.h"
#include "Log.h"
#include "ServerLogChannels.h"
#include <cstdio>

using namespace hb::shared::net;
using namespace hb::server::config;

bool GameCmdBeginHeldenian::execute(CGame* game, int client_h, const char* args)
{
	if (game->m_client_list[client_h] == nullptr)
		return true;

	if (game->m_is_heldenian_mode)
	{
		game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "Heldenian is already in progress.");
		return true;
	}

	if (game->m_war_manager->war_event_active())
	{
		game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "Another war event is in progress.");
		return true;
	}

	// Default: keep the current mode type, falling back to battlefield.
	int type = (game->m_heldenian_mode_type == heldenian_battle::castle_siege)
		? heldenian_battle::castle_siege : heldenian_battle::battlefield;
	if (args != nullptr && args[0] != '\0')
	{
		if (sscanf(args, "%d", &type) != 1 || !heldenian_battle::is_valid(type))
		{
			game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "Usage: /beginheldenian [type: 1=battlefield 2=castle siege]");
			return true;
		}
	}

	game->m_war_manager->start_heldenian_mode(type);

	char buf[64];
	std::snprintf(buf, sizeof(buf), "Heldenian started (type %d).", type);
	game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, buf);
	hb::logger::log<hb::log_channel::commands>("GM Order({}): begin Heldenian (type {})",
		game->m_client_list[client_h]->m_char_name, type);

	return true;
}
