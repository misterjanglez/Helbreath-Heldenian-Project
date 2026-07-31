// CmdItemHistory.h: the Biography query (#84, plan P4.2)
//
// The ownership-dispute settler. Given a Serial, it prints where the item came
// from and everything that has happened to it since, in order, with the place it
// was in after each step. See ItemBiography.h for the replay; this file is the
// operator's door to it.
//
// The Serial comes from somewhere else — `reconcile` names them, and so does
// `ledgervalidate`. This command deliberately does not search by character or by
// item type: the ledger's own vocabulary calls a Biography one Serial's history,
// and a query that guessed which of a player's twelve identical rings was meant
// would settle disputes by coin toss.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "ServerCommand.h"

class CmdItemHistory : public ServerCommand
{
public:
	const char* get_name() const override { return "itemhistory"; }
	const char* GetDescription() const override { return "Print one Serial's Biography from the Provenance Ledger (ADR 0003)"; }
	const char* GetHelp() const override { return "Usage: itemhistory <serial> [ledger_db]\n  Prints the birth record of one Instanced item and every custody or state\n  event recorded against it, oldest first, with the place the item was in after\n  each one - unplaced, inventory, warehouse, escrow, ground or gone.\n  Ordered by event id and never by timestamp: the clock is whole seconds and a\n  pickup followed straight away by a drop shares one, while the id is assigned\n  in the order the events were recorded.\n  Steps the legal-transition machine rejects are marked. A step a crash boundary\n  can account for is marked EXCUSED instead and is not counted - a crash loses\n  the tail of a flush window, so the move that would have explained it may never\n  have reached disk.\n  With no path it reads the live itemledger.db, flushing the buffer first so the\n  file holds every event recorded so far. Name a path to read a snapshot\n  instead - a copy taken while the server runs is a valid target. The file is\n  opened READ-ONLY either way.\n  Serials come from `reconcile` and `ledgervalidate`, which name them; this\n  command does not search by player or by item type.\n  Machine-readable lines: ITEMHISTORY BIRTH <serial> <item_id> <tier> <origin>,\n  ITEMHISTORY EVENT <n> <event> <place_after> <actor> <counterparty> <verdict>,\n  then ITEMHISTORY RESULT <serial> <events> <place> <violations>.\n  Headless: Server --itemhistory <serial> runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
