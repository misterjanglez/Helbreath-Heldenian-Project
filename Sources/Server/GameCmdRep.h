// GameCmdRep.h: the player-to-player reputation vote (#88).
//
// The original dispatched `/rep+ ` and `/rep- ` straight into
// CGame::SetPlayerReputation. Our port kept the handler and lost the two
// entry points, which left rating a one-way ratchet downward — the combat
// penalties could subtract from it, but nothing could ever add.
//
// Player commands, so get_default_level() stays 0: what gates a vote is the
// voter's own character level, cooldown and PK record, checked inside the
// handler, not an admin rank.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "GameChatCommand.h"

class GameCmdRepUp : public GameChatCommand
{
public:
	const char* get_name() const override { return "rep+"; }
	bool execute(CGame* game, int client_h, const char* args) override;
};

class GameCmdRepDown : public GameChatCommand
{
public:
	const char* get_name() const override { return "rep-"; }
	bool execute(CGame* game, int client_h, const char* args) override;
};
