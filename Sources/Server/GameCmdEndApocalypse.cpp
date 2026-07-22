#include "GameCmdEndApocalypse.h"
#include "Game.h"
#include "WarManager.h"
#include "Log.h"
#include "ServerLogChannels.h"

using namespace hb::shared::net;

bool GameCmdEndApocalypse::execute(CGame* game, int client_h, const char* args)
{
	if (game->m_client_list[client_h] == nullptr)
		return true;

	if (game->m_is_apocalypse_mode == false)
	{
		game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "No apocalypse is in progress.");
		return true;
	}

	game->m_war_manager->global_end_apocalypse_mode();

	game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "Apocalypse ended.");
	hb::logger::log<hb::log_channel::commands>("GM Order({}): end apocalypse",
		game->m_client_list[client_h]->m_char_name);

	return true;
}
