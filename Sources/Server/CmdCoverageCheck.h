// CmdCoverageCheck.h: the Counted-tier / coverage-audit prover (#81, #103, plan P3.3)
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "ServerCommand.h"

class CmdCoverageCheck : public ServerCommand
{
public:
	const char* get_name() const override { return "coveragecheck"; }
	const char* GetDescription() const override { return "Prove the Counted tier records flows and the audited transitions all emit (ADR 0003)"; }
	const char* GetHelp() const override { return "Usage: coveragecheck\n  Asserts the P3.3 trust gate: the Counted (stackable) tier records aggregate\n  item_flows rows, keyed by the SAME number an Instanced item writes to\n  item_events.event_type - there is deliberately no second taxonomy, so a Drop\n  is 2 in both tables.\n  Checks the flow contract: a stackable transition books a row and no event, an\n  Instanced one books an event and no flow, repeated moves of one item type\n  accumulate into a single row rather than one per move, distinct transitions\n  stay distinct rows, and the day column is today.\n  Checks quantity fidelity: the flow records what MOVED, not what the stack\n  held - a shop sale hands item_log the whole slot and takes part of it, so an\n  explicit quantity has to win over the item's own count.\n  Checks the merge exclusion: a stack merging into one the holder already had\n  books nothing at all, because nothing left the world; every other destruction\n  reason does book.\n  Checks the transitions this ticket wired that had no caller before it -\n  Warehouse Deposit and Retrieve, and the split-stack half of a Give - each land\n  their own action number.\n  Checks the outflow contract (#103): gold paid to an NPC books what LEFT the\n  stack, under one number per route - Buy for a purchase, Repair for a repair\n  bill, MagicLearn for a spell - so the money supply has both sides. A count\n  that RISES books nothing and no row is ever negative, because direction is a\n  property of the number and never of a stored sign. Spending the last coin\n  still books its route, beside the Deplete and destroyed rows an emptied stack\n  has always produced.\n  The ledger sink is redirected to a scratch database for the run and restored\n  afterwards, so the live itemledger.db gains nothing; a few Serials are minted,\n  which is the factory working.\n  Machine-readable lines: COVERAGECHECK <check> <PASS|FAIL> [detail], then\n  COVERAGECHECK RESULT <passed> <total>. Free of absolute serials, timestamps,\n  ids and row counts so Windows and Linux output compares byte-for-byte.\n  Headless: Server --coveragecheck runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
