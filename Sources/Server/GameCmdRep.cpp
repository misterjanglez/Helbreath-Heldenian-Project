#include "GameCmdRep.h"
#include "Game.h"

// 1 rates up, 0 rates down — the original's encoding, preserved so the
// handler reads the same as the archived source it was ported from.
bool GameCmdRepUp::execute(CGame* game, int client_h, const char* args)
{
	game->set_player_reputation(client_h, args, 1);
	return true;
}

bool GameCmdRepDown::execute(CGame* game, int client_h, const char* args)
{
	game->set_player_reputation(client_h, args, 0);
	return true;
}
