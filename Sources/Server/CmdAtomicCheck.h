// CmdAtomicCheck.h: the multi-account atomicity prover (#82, plan P3.4)
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "ServerCommand.h"

class CmdAtomicCheck : public ServerCommand
{
public:
	const char* get_name() const override { return "atomiccheck"; }
	const char* GetDescription() const override { return "Prove no multi-account operation spans two commits (ADR 0004)"; }
	const char* GetHelp() const override { return "Usage: atomiccheck\n  Asserts the P3.4 contract: an operation that changes two accounts is ONE\n  game.db transaction, so it either happens completely or not at all.\n  Checks the primitive: several characters saved together are all written or\n  none of them is, and a handle whose client has gone drops out of the batch\n  instead of failing it.\n  Checks the Exchange and the Give: after a real trade the item is on the\n  receiver's SAVED inventory and off the giver's - which before this could only\n  become true at two unrelated later moments, one of which a crash could skip.\n  Checks Trading Post escrow, by making the database refuse mid-operation with a\n  trigger and reading back what survived. An escrow-in that cannot commit leaves\n  the character holding the items and the board with no Listing; an escrow-out\n  that cannot commit leaves the Listing standing and the Warehouse empty, in\n  memory as well as on disk; a finalized Trade that fails late moves nothing at\n  all - not the Listing, not one Offer, not one Warehouse row.\n  Checks that nothing was recorded for a move that rolled back: the ledger is\n  append-only, so a custody event written for a rewound transaction would name\n  the wrong holder forever.\n  Escrow runs against a scratch world - a whole game.db with the v9 schema, since\n  the escrow tables live there now. The Exchange and Give checks drive the real\n  save path, so they write two probe accounts into the live game.db and delete\n  them again; the run refuses to start if either name is already taken.\n  Machine-readable lines: ATOMICCHECK <check> <PASS|FAIL> [detail], then\n  ATOMICCHECK RESULT <passed> <total>. Free of absolute serials, timestamps,\n  ids and row counts so Windows and Linux output compares byte-for-byte.\n  Headless: Server --atomiccheck runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
