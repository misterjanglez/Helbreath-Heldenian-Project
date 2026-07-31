// CmdBiographyCheck.h: the Biography + validator prover (#84, plan P4.2)
//
// A validator is a claim about what could not have happened, and a claim like
// that is only worth having if it is wrong in neither direction. One that misses
// an impossible history turns "we replayed the ledger" into false comfort; one
// that reports a legal history turns the whole tool into noise an operator stops
// reading — and the legal histories are the ones there are millions of.
//
// So the fixture is half lives that are entirely ordinary and must produce
// nothing, and half lives that could not have happened and must each produce
// exactly one finding.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "ServerCommand.h"

class CmdBiographyCheck : public ServerCommand
{
public:
	const char* get_name() const override { return "biographycheck"; }
	const char* GetDescription() const override { return "Prove the Biography + transition-machine contract (plan P4.2)"; }
	const char* GetHelp() const override { return "Usage: biographycheck\n  Asserts the P4.2 contract by planting nineteen lives in a scratch Provenance\n  Ledger - ordinary ones and impossible ones - and reading back what the replay\n  says about each.\n  Checks that ordinary custody is silent: a full life from mint to destruction,\n  a Warehouse round trip, a Trading Post trade whose delivery lands in a\n  WAREHOUSE, and a Use between a pickup and a drop that must not move the item.\n  Checks the two rules a wrong machine would break the widest. A sale is Sell,\n  then Deplete, then destroyed - so neither of the first two may be an exit, or\n  every sale in the database reads as an event after the item's death. And an\n  item that was minted and never placed may legally turn up anywhere, because\n  most creation venues never record where they put it.\n  Checks each violation class exactly once: an event after the item left the\n  world, a second exit, a pickup with no intervening drop, a Warehouse retrieval\n  while already carrying it, a drop of something already on the ground, a second\n  birth for one Serial, and events for a Serial with no birth row - which is\n  reported once for the Serial and suppresses its other findings.\n  Checks the crash excuse in all three directions: a violation across a crash\n  boundary is excused, the same violation across a CLEAN boundary is not, and a\n  structural violation across a crash boundary is not - a birth row and its\n  event ride one transaction, so no hole can produce either.\n  Checks the Biography itself: ordered by event id and not by timestamp, the\n  birth row read back whole, and a Serial nothing was ever recorded against\n  answered rather than treated as an error.\n  The scratch ledger is deleted afterwards; the live one is never opened.\n  Machine-readable lines: BIOGRAPHYCHECK <check> <PASS|FAIL> [detail], then\n  BIOGRAPHYCHECK RESULT <passed> <total>. Free of absolute serials, timestamps,\n  ids and row counts so Windows and Linux output compares byte-for-byte.\n  Headless: Server --biographycheck runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
