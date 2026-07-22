#include "GameCmdBeginApocalypse.h"
#include "Game.h"
#include "WarManager.h"
#include "Log.h"
#include "ServerLogChannels.h"

using namespace hb::shared::net;

bool GameCmdBeginApocalypse::execute(CGame* game, int client_h, const char* args)
{
	if (game->m_client_list[client_h] == nullptr)
		return true;

	if (game->m_is_apocalypse_mode)
	{
		game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "Apocalypse is already in progress.");
		return true;
	}

	if (game->m_is_crusade_mode || game->m_is_heldenian_mode)
	{
		game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "Another war event is in progress.");
		return true;
	}

	game->m_war_manager->local_start_apocalypse(GameClock::GetTimeMS());

	game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "Apocalypse started. The gates are open.");
	hb::logger::log<hb::log_channel::commands>("GM Order({}): begin apocalypse",
		game->m_client_list[client_h]->m_char_name);

	return true;
}
