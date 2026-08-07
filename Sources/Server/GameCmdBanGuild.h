#pragma once

#include "GameChatCommand.h"

// /banguild <name> — the §3.1 moderation fallback (#121). A player command
// (level 0): the real gate is the guild kick permission bit, checked by the
// engine, not an admin level. §3.1.1 makes moderation UI-first; this command
// stays as the slash fallback.
class GameCmdBanGuild : public GameChatCommand
{
public:
	const char* get_name() const override { return "banguild"; }
	bool execute(CGame* game, int client_h, const char* args) override;
};
