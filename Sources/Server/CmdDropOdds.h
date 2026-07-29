#pragma once

#include "ServerCommand.h"

class CmdDropOdds : public ServerCommand
{
public:
	const char* get_name() const override { return "dropodds"; }
	const char* GetDescription() const override { return "Report effective per-kill drop odds per loot grade"; }
	const char* GetHelp() const override { return "Usage: dropodds [grade]\n  Resolves the whole stage-1 drop chain from the running config and prints,\n  per loot grade, the per-kill odds that an item drops at all, that it is\n  tier-eligible gear, and the tier it lands on - plus the stage-1 gear share\n  of every drop table in use, which is the term that dominates the product.\n  With a grade (1-5) it also lists that grade's monsters one by one.\n  Machine-readable lines: DROPODDS MODE|GRADE|SPLIT|TABLE ...\n  Headless: Server --dropodds [grade] runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
