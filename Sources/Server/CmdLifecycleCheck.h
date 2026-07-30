// CmdLifecycleCheck.h: the lifecycle-coverage prover (#79, plan P3.1)
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "ServerCommand.h"

class CmdLifecycleCheck : public ServerCommand
{
public:
	const char* get_name() const override { return "lifecyclecheck"; }
	const char* GetDescription() const override { return "Prove birth context, destruction and ground despawn all reach the Provenance Ledger (ADR 0003)"; }
	const char* GetHelp() const override { return "Usage: lifecyclecheck\n  Asserts the P3.1 contract: a creation venue's birth context reaches the birth\n  row (map, x, y and the NPC name in origin_detail), a venue with none leaves\n  those columns NULL rather than claiming a location, and a loot drop's row\n  carries the tier the item actually rolled - create_loot_item rolls before it\n  mints, which is the whole reason it exists.\n  Then the exits: destroy_item lands one destroyed event carrying its reason and\n  actor and nulls the caller's pointer, a Counted item records nothing, a shop\n  purchase reaches the ledger as ItemLogAction::Buy's own number, and the ground\n  paths - placement stamping the clock, expiry taking only what is due, tile\n  overflow pushing the oldest off the end - each land a despawned event with no\n  actor and the tile's own location.\n  The ground checks run against a real map tile and put it back as they found\n  it. The ledger sink is redirected to a scratch database for the run and\n  restored afterwards, so the live itemledger.db gains nothing; a few Serials\n  are minted, which is the factory working.\n  Machine-readable lines: LIFECYCLECHECK <check> <PASS|FAIL> [detail], then\n  LIFECYCLECHECK RESULT <passed> <total>. Free of absolute serials, timestamps,\n  ids and row counts so Windows and Linux output compares byte-for-byte.\n  Headless: Server --lifecyclecheck runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
