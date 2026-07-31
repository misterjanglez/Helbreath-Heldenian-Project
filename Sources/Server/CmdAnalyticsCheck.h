// CmdAnalyticsCheck.h: the analytics starter pack's prover (#85, plan P4.3)
//
// P4.3 adds one thing to the server and one thing to the report: a count of
// kills, and a machine-readable statement of what each (item, monster) row is
// authored to yield. Everything the analytics conclude rests on those two, so
// both are proved here rather than assumed from a query that happened to return
// rows.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "ServerCommand.h"

class CmdAnalyticsCheck : public ServerCommand
{
public:
	const char* get_name() const override { return "analyticscheck"; }
	const char* GetDescription() const override { return "Prove the drop analytics' denominator and predicted side (#85)"; }
	const char* GetHelp() const override { return "Usage: analyticscheck\n  Proves the two things the drop-rate audit rests on.\n  (1) The DENOMINATOR: npc_kills aggregates by day and monster, accumulates\n      across flush windows rather than replacing, sums the killers' reputation\n      factors, refuses an NPC with no config, and survives a restart.\n  (2) The JOIN: a drop's birth row records its monster by NAME and a kill row\n      carries the same name, so the SQL in Scripts/analytics can join them with\n      no world database. Driven by running that join.\n  (3) The PREDICTED SIDE: the per-kill expectation dropodds publishes obeys the\n      expected-yield identity, on an ordinary table and on a guaranteed-head\n      one - the arithmetic #89 made non-obvious.\n  Everything runs against a scratch ledger, which is created and deleted here.\n  The live ledger is neither read for content nor written to.\n  Machine-readable lines: ANALYTICSCHECK <name> PASS|FAIL, then\n  ANALYTICSCHECK RESULT <passed> <total>.\n  Headless: Server --analyticscheck runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
