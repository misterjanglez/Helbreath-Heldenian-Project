#include "GameCmdThunder.h"
#include "Game.h"
#include "WarManager.h"
#include "Log.h"
#include "ServerLogChannels.h"

using namespace hb::shared::net;

bool GameCmdThunder::execute(CGame* game, int client_h, const char* args)
{
	if (game->m_client_list[client_h] == nullptr)
		return true;

	game->m_war_manager->unleash_abaddon_thunder();
	game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "Lightning storm unleashed on the boss map.");
	hb::logger::log<hb::log_channel::commands>("GM Order({}): thunder",
		game->m_client_list[client_h]->m_char_name);

	return true;
}
