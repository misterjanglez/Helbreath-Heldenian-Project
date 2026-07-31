#pragma once

#include "ServerCommand.h"

class CmdDropOdds : public ServerCommand
{
public:
	const char* get_name() const override { return "dropodds"; }
	const char* GetDescription() const override { return "Report effective per-kill drop odds per loot grade"; }
	const char* GetHelp() const override { return "Usage: dropodds [grade] | dropodds npc <id|name> | dropodds rows [id|name]\n  Prints, per loot grade, the ABSOLUTE per-kill odds that a stage-1 drop\n  yields anything, yields an item, yields tier-eligible gear, and the tier\n  it lands on - read straight off the drop tables, with the generosity\n  multiplier stack shown. There is no chain left to multiply.\n  With a grade (1-5) it also lists that grade's monsters one by one.\n  'dropodds npc <id|name>' lists BOTH stages of one monster row by row as\n  \"1 in N kills\", including what is left over for nothing.\n  'dropodds rows [id|name]' prints the same per-(item, monster) rates as\n  machine lines and nothing else - the PREDICTED side of the drop-rate audit\n  (Scripts/drop_audit.py). Capture it at rating 0: the tool applies the\n  reputation layer itself, from the ledger's summed factors.\n  Machine-readable lines: DROPODDS MODE|GRADE|SPLIT|TABLE, and under 'rows'\n  DROPODDS MODE|NPC|SLOT|ROW|ROWS ...\n  Headless: Server --dropodds [grade] runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
