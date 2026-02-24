#include "CmdSaveAll.h"
#include "ServerConsole.h"
#include "Game.h"
#include "Log.h"
#include "ServerLogChannels.h"

void CmdSaveAll::execute(CGame* game, const char* args)
{
	int count = game->save_all_players();

	hb::console::success("Saved {} player(s)", count);
	hb::logger::log<hb::log_channel::commands>("saveall: saved {} player(s)", count);
}
