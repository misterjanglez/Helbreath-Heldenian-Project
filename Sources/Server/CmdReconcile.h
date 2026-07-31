// CmdReconcile.h: the Reconciliation job (#83, plan P4.1)
//
// The dupe detector. Everything before this ticket built the two records — the
// snapshot the world saves and the ledger the world writes beside it — and this
// is what compares them. See ItemReconciliation.h for the comparison itself;
// this file is the operator's door to it.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "ServerCommand.h"

class CmdReconcile : public ServerCommand
{
public:
	const char* get_name() const override { return "reconcile"; }
	const char* GetDescription() const override { return "Compare held items against the Provenance Ledger (the dupe detector, ADR 0003)"; }
	const char* GetHelp() const override { return "Usage: reconcile [game_db] [ledger_db] [limit]\n  Compares every Serial the Game DB says somebody holds - inventories,\n  Warehouses and both Trading Post escrow tables - against the holder the\n  Provenance Ledger derives from each Serial's last custody event. Where the\n  two disagree is a dupe: one Serial in two inventories, a held Serial the\n  ledger believes destroyed or lying on the ground, a live Serial nobody holds.\n  Reports each class with a count and up to `limit` examples (default 20, 0 for\n  all). Nothing is written to either database.\n  With no arguments it reads the live game.db and itemledger.db, flushing the\n  ledger's buffer first so the file holds every event recorded so far. Name two\n  paths to read a snapshot pair instead - a copy taken while the server runs is\n  a valid target, which is what WAL is for.\n  Run `saveall` first on a live world: a logged-in character's inventory is in\n  memory until it saves, so every transition since its last save reads as a\n  holder mismatch. Those classes are reported as warnings for that reason; the\n  four critical classes cannot be produced by an unsaved player.\n  Machine-readable lines: RECONCILE CLASS <class> <count>, RECONCILE ANOMALY\n  <class> <serial> <detail>, RECONCILE INFO <name> <count>, then RECONCILE\n  RESULT <anomalies> <critical> <serials>.\n  Headless: Server --reconcile [paths] runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
