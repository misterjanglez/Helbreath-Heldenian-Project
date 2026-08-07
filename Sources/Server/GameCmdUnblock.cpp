#include "GameCmdUnblock.h"
#include "Game.h"
#include <cstring>
#include <algorithm>
#include "StringCompat.h"

using namespace hb::shared::net;
bool GameCmdUnblock::execute(CGame* game, int client_h, const char* args)
{
	if (game->m_client_list[client_h] == nullptr)
		return true;

	if (args == nullptr || args[0] == '\0')
	{
		game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "Usage: /unblock CharName");
		return true;
	}

	char char_name[hb::shared::limits::CharNameLen];
	if (!parse_char_name(args, char_name))
		return true;

	// Find entry in block list by character name
	auto& blockList = game->m_client_list[client_h]->m_blocked_accounts_list;
	auto it = std::find_if(blockList.begin(), blockList.end(),
		[&](const std::pair<std::string, std::string>& entry) {
			return hb_stricmp(entry.second.c_str(), char_name) == 0;
		});

	if (it == blockList.end())
	{
		char msg[64] = {};
		std::snprintf(msg, sizeof(msg), "'%s' is not in your block list.", char_name);
		game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, msg);
		return true;
	}

	// Remove from set and list
	game->m_client_list[client_h]->m_blocked_accounts.erase(it->first);
	blockList.erase(it);
	game->m_client_list[client_h]->m_block_list_dirty = true;

	char msg[64] = {};
	std::snprintf(msg, sizeof(msg), "'%s' has been unblocked.", char_name);
	game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, msg);

	return true;
}
