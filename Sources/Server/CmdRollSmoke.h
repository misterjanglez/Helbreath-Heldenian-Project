#pragma once

#include "ServerCommand.h"

class CmdRollSmoke : public ServerCommand
{
public:
	const char* get_name() const override { return "rollsmoke"; }
	const char* GetDescription() const override { return "Roll-distribution smoke for item attribute generation"; }
	const char* GetHelp() const override { return "Usage: rollsmoke <item_id> [count]\n  Rolls attribute generation <count> times (default 100000) on a fresh\n  instance of the item and prints modifier/value/color histograms.\n  Machine-readable lines: ROLLSMOKE <P|S|C> <id> <value> <count>.\n  Headless: Server --rollsmoke <item_id> [count] runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
