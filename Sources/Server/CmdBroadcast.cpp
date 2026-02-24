#include "CmdBroadcast.h"
#include "ServerConsole.h"
#include "Game.h"
#include "Log.h"
#include "ServerLogChannels.h"

void CmdBroadcast::execute(CGame* game, const char* args)
{
	if (args == nullptr || args[0] == '\0')
	{
		hb::console::error("Usage: broadcast <message>");
		return;
	}

	game->broadcast_server_message(args);

	hb::console::success("Broadcast sent: {}", args);
	hb::logger::log<hb::log_channel::commands>("broadcast: {}", args);
}
