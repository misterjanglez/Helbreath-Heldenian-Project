#pragma once

#include "ServerCommand.h"

class CmdMintCheck : public ServerCommand
{
public:
	const char* get_name() const override { return "mintcheck"; }
	const char* GetDescription() const override { return "Run a GM mint spec through the mode's legality gate"; }
	const char* GetHelp() const override { return "Usage: mintcheck <item_id> <tier> [mod_id:value[:value2] ...]\n  Runs the spec through the boot-selected Roll strategy's mint gate on a\n  fresh instance of the item and prints the verdict. Nothing is created and\n  no player is needed - this is the legality half of the GM creator alone.\n  Machine-readable line: MINTCHECK <item_id> <tier> <OK|REJECT> [reason].\n  Headless: Server --mintcheck <item_id> <tier> [mods...] runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
