#pragma once

#include "GameChatCommand.h"
#include "AdminLevel.h"

class GameCmdEndHeldenian : public GameChatCommand
{
public:
	const char* get_name() const override { return "endheldenian"; }
	int get_default_level() const override { return hb::shared::admin::Developer; }
	bool execute(CGame* game, int client_h, const char* args) override;
};
