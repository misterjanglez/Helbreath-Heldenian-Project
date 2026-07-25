#include "GameCmdClearNpc.h"
#include "Game.h"
#include "EntityManager.h"
#include "StringCompat.h"
#include <cstdio>

using namespace hb::shared::net;

bool GameCmdClearNpc::execute(CGame* game, int client_h, const char* args)
{
	if (game->m_client_list[client_h] == nullptr)
		return true;

	bool clear_all = false, rewards = false;
	if (args != nullptr && args[0] != '\0')
	{
		char tokens[3][16] = {};
		int count = sscanf(args, "%15s %15s %15s", tokens[0], tokens[1], tokens[2]);
		for (int t = 0; t < count; t++)
		{
			if (hb_stricmp(tokens[t], "all") == 0)
				clear_all = true;
			else if (hb_stricmp(tokens[t], "rewards") == 0)
				rewards = true;
			else
			{
				game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "Usage: /clearnpc [all] [rewards]");
				return true;
			}
		}
	}

	if (rewards)
	{
		int required = game->get_command_required_level("clearnpc.rewards");
		if (game->m_client_list[client_h]->m_admin_level < required)
		{
			char msg[96];
			std::snprintf(msg, sizeof(msg), "'rewards' requires admin level %d.", required);
			game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, msg);
			return true;
		}
	}

	int map_index = game->m_client_list[client_h]->m_map_index;
	int killed = game->m_entity_manager->kill_entities_on_map(map_index, clear_all, rewards ? client_h : 0);

	char buf[160];
	std::snprintf(buf, sizeof(buf), "Killed %d NPC(s) on %s%s.%s",
		killed, game->m_map_list[map_index]->m_name,
		rewards ? " with exp/drops" : "",
		clear_all ? " Static NPCs return on server restart." : "");
	game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, buf);

	return true;
}
