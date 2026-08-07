#include "GameCmdBanGuild.h"
#include "Game.h"
#include "GuildManager.h"
#include <cstring>

using namespace hb::shared::net;

// The engine owns every gate (kick bit, same guild, below-rank, the master);
// this command only parses the name and translates the result into the
// generic notice line — no guild notify family exists until #123.
bool GameCmdBanGuild::execute(CGame* game, int client_h, const char* args)
{
	if (game->m_client_list[client_h] == nullptr)
		return true;

	if (args == nullptr || args[0] == '\0')
	{
		game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "Usage: /banguild CharName");
		return true;
	}

	char char_name[hb::shared::limits::CharNameLen];
	if (!parse_char_name(args, char_name))
		return true;

	// The result -> message table, on the generic notice arm (#123 will put
	// the result code itself on the wire instead of these strings).
	const char* message;
	switch (game->m_guild_manager->kick_member(client_h, char_name))
	{
	case hb::server::guild_result::ok:
		message = "Guild member banned."; break;
	case hb::server::guild_result::not_in_guild:
		message = "You are not in a guild."; break;
	case hb::server::guild_result::no_permission:
		message = "You cannot ban guild members."; break;
	case hb::server::guild_result::target_not_found:
		message = "No such member in your guild."; break;
	case hb::server::guild_result::target_is_master:
	case hb::server::guild_result::target_not_below_rank:
		message = "You cannot ban that member."; break;
	default:
		message = "Guild ban failed."; break;
	}
	game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, message);
	return true;
}
