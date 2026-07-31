// CmdLedgerValidate.h: the state-machine validator (#84, plan P4.2)
//
// `reconcile` compares the ledger against the world and needs both files. This
// needs only the ledger: it replays every Serial through the legal-transition
// machine and reports the steps that could not have happened. An item cannot be
// picked up twice with nobody dropping it in between, and nothing can happen to
// it after it left the world — those are facts about the record itself, provable
// without a world to check it against.
//
// See ItemBiography.h for the machine; this file is the operator's door to it.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "ServerCommand.h"

class CmdLedgerValidate : public ServerCommand
{
public:
	const char* get_name() const override { return "ledgervalidate"; }
	const char* GetDescription() const override { return "Replay every Serial through the legal-transition machine (ADR 0003)"; }
	const char* GetHelp() const override { return "Usage: ledgervalidate [ledger_db] [limit]\n  Replays every Serial in the Provenance Ledger through the transition machine\n  and reports only the steps it says are impossible: an event after the item\n  left the world (which is also how `destroyed once` is enforced), an item taken\n  into somebody's custody while it was already in custody with nobody having let\n  go, a second birth for one Serial, and events for a Serial with no birth row.\n  A fifth class - released from a place it was not in - is reported as a warning,\n  because that is the shape a lost flush makes.\n  Needs only the ledger. Where `reconcile` compares two records and needs both\n  files, this checks one record against itself.\n  Steps a crash boundary can account for are counted as excused rather than\n  accused: a crash loses the tail of a flush window, so the move that would have\n  explained the step may never have reached disk.\n  Reports each class with a count and up to `limit` examples (default 20, 0 for\n  all), then a census of where every Serial ended up. Nothing is written.\n  With no path it reads the live itemledger.db, flushing the buffer first. Name\n  a path to read a snapshot instead. The file is opened READ-ONLY either way.\n  A large `never moved` count is expected, not a fault: most creation venues do\n  not record where they put the item, so a Serial with only a birth row is\n  normal. `itemhistory <serial>` prints any one of them in full.\n  Machine-readable lines: LEDGERVALIDATE CLASS <class> <count>, LEDGERVALIDATE\n  VIOLATION <class> <serial> <detail>, LEDGERVALIDATE INFO <name> <count>, then\n  LEDGERVALIDATE RESULT <violations> <critical> <serials>.\n  Headless: Server --ledgervalidate [path] runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
