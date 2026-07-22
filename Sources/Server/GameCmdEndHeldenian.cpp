#include "GameCmdEndHeldenian.h"
#include "Game.h"
#include "WarManager.h"
#include "Log.h"
#include "ServerLogChannels.h"

using namespace hb::shared::net;

bool GameCmdEndHeldenian::execute(CGame* game, int client_h, const char* args)
{
	if (game->m_client_list[client_h] == nullptr)
		return true;

	if (game->m_is_heldenian_mode == false)
	{
		game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "No Heldenian is in progress.");
		return true;
	}

	game->m_war_manager->manual_end_heldenian_mode();

	game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "Heldenian ended.");
	hb::logger::log<hb::log_channel::commands>("GM Order({}): end Heldenian",
		game->m_client_list[client_h]->m_char_name);

	return true;
}
