#include "GameCmdEndCrusade.h"
#include "Game.h"
#include "WarManager.h"
#include "Log.h"
#include "ServerLogChannels.h"
#include <cstdio>

using namespace hb::shared::net;

bool GameCmdEndCrusade::execute(CGame* game, int client_h, const char* args)
{
	if (game->m_client_list[client_h] == nullptr)
		return true;

	if (game->m_is_crusade_mode == false)
	{
		game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "No crusade is in progress.");
		return true;
	}

	int winner = 0;
	if (args != nullptr && args[0] != '\0')
	{
		if (sscanf(args, "%d", &winner) != 1 || winner < 0 || winner > 2)
		{
			game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "Usage: /endcrusade [winner: 0=none 1=aresden 2=elvine]");
			return true;
		}
	}

	game->m_war_manager->manual_end_crusade_mode(winner);

	char buf[64];
	std::snprintf(buf, sizeof(buf), "Crusade ended (winner side: %d).", winner);
	game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, buf);
	hb::logger::log<hb::log_channel::commands>("GM Order({}): end crusade (winner {})",
		game->m_client_list[client_h]->m_char_name, winner);

	return true;
}
