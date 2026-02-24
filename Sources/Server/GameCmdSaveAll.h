#pragma once

#include "GameChatCommand.h"
#include "AdminLevel.h"

class GameCmdSaveAll : public GameChatCommand
{
public:
	const char* get_name() const override { return "saveall"; }
	int get_default_level() const override { return hb::shared::admin::GameMaster; }
	bool execute(CGame* game, int client_h, const char* args) override;
};
