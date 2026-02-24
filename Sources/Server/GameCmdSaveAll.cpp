#include "GameCmdSaveAll.h"
#include "Game.h"
#include "Log.h"
#include "ServerLogChannels.h"
#include <cstdio>

using namespace hb::shared::net;

bool GameCmdSaveAll::execute(CGame* game, int client_h, const char* args)
{
	if (game->m_client_list[client_h] == nullptr)
		return true;

	int count = game->save_all_players();

	char buf[64];
	std::snprintf(buf, sizeof(buf), "Saved %d player(s).", count);
	game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, buf);

	hb::logger::log<hb::log_channel::commands>("saveall (in-game): {} saved {} player(s)",
		game->m_client_list[client_h]->m_char_name, count);

	return true;
}
