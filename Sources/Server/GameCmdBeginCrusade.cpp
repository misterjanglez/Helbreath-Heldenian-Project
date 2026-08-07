#include "GameCmdBeginCrusade.h"
#include "Game.h"
#include "WarManager.h"
#include "Log.h"
#include "ServerLogChannels.h"

using namespace hb::shared::net;

bool GameCmdBeginCrusade::execute(CGame* game, int client_h, const char* args)
{
	if (game->m_client_list[client_h] == nullptr)
		return true;

	if (game->m_is_crusade_mode)
	{
		game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "A crusade is already in progress.");
		return true;
	}

	if (game->m_war_manager->war_event_active())
	{
		game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "Another war event is in progress.");
		return true;
	}

	// Starts now — the scheduler's once-per-day guard applies only to scheduled starts.
	game->m_war_manager->start_crusade_mode();

	game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "Crusade started.");
	hb::logger::log<hb::log_channel::commands>("GM Order({}): begin crusade",
		game->m_client_list[client_h]->m_char_name);

	return true;
}
