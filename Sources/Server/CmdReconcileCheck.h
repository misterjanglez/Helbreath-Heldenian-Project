// CmdReconcileCheck.h: the Reconciliation prover (#83, plan P4.1)
//
// The detector is the last line of the audit: everything from #75 to #82 exists
// so that this comparison can be trusted, and a detector that misses a dupe is
// worse than none — it turns "we checked" into evidence of innocence. So the
// contract is proved the only way a detector's can be: by planting one of every
// anomaly in a scratch world and asserting it finds exactly those and nothing
// else.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "ServerCommand.h"

class CmdReconcileCheck : public ServerCommand
{
public:
	const char* get_name() const override { return "reconcilecheck"; }
	const char* GetDescription() const override { return "Prove the Reconciliation contract (the dupe detector, plan P4.1)"; }
	const char* GetHelp() const override { return "Usage: reconcilecheck\n  Asserts the P4.1 contract by building a scratch world and a scratch ledger\n  that disagree in every way they can, then reading back what the comparison\n  says about them.\n  Checks that healthy custody is silent: an item in an inventory, in a\n  Warehouse, in a Listing and in an Offer, each where its last event says it is,\n  produces nothing.\n  Checks the holder derivation: an Exchange hands the item to the counterparty\n  and not the actor, every Trading Post escrow-out lands in the recipient's\n  WAREHOUSE, a Use after a pickup does not erase who picked it up, and holder\n  names compare the way the database collates them - case-insensitively.\n  Checks each anomaly class exactly once: one Serial in two inventories, a held\n  Serial the ledger destroyed, a held Serial the ledger dropped on the ground, a\n  held Serial with no birth row, a holder the ledger disagrees with, a live\n  Serial nobody holds, a held Serial no event ever placed, an Instanced row\n  carrying no Serial, and a stackable row carrying one.\n  Checks that the three states with no holder are counted and not accused -\n  on the ground, out of the world, and never placed - and that an event naming\n  nobody is counted as indeterminate rather than guessed at.\n  Checks that capping the printed evidence does not cap the counts, and that\n  the whole scan runs against READ-ONLY handles, which is what lets it be\n  pointed at a live server's files.\n  Both scratch databases are deleted afterwards; the live world is not read.\n  Machine-readable lines: RECONCILECHECK <check> <PASS|FAIL> [detail], then\n  RECONCILECHECK RESULT <passed> <total>. Free of absolute serials, timestamps,\n  ids and row counts so Windows and Linux output compares byte-for-byte.\n  Headless: Server --reconcilecheck runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
