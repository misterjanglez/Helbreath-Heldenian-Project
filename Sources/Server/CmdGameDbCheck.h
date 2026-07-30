#pragma once

#include "ServerCommand.h"

class CmdGameDbCheck : public ServerCommand
{
public:
	const char* get_name() const override { return "gamedbcheck"; }
	const char* GetDescription() const override { return "Prove the Persistence v8 contract (one game.db, ADR 0004 + Serial columns)"; }
	const char* GetHelp() const override { return "Usage: gamedbcheck\n  Asserts the P1.2 contract against the live game.db and a scratch copy of the\n  v8 schema: the live file is WAL with synchronous=NORMAL and foreign keys on,\n  a fresh database stamps schema_version 8, a pre-v8 stamp and a surviving\n  accounts/ directory are both refused, character names are globally unique and\n  case-insensitively so, item Serials survive a save/load round trip in both\n  the inventory and the Warehouse, deleting a character cascades its rows away,\n  block lists are scoped to their owning account, the statement cache compiles\n  one statement per distinct SQL, and a nested transaction rolls back alone\n  while its enclosing sweep still commits.\n  The scratch database and its probe accounts directory are deleted afterwards;\n  the live world is only read.\n  Machine-readable lines: GAMEDBCHECK <check> <PASS|FAIL> [detail], then\n  GAMEDBCHECK RESULT <passed> <total>. Free of absolute serials, timestamps and\n  row counts so Windows and Linux output compares byte-for-byte.\n  Headless: Server --gamedbcheck runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
