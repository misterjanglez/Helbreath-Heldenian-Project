#pragma once

#include "ServerCommand.h"

class CmdGuildDbCheck : public ServerCommand
{
public:
	const char* get_name() const override { return "guilddbcheck"; }
	const char* GetDescription() const override { return "Prove the guild store contract (guilds.db schema, invariants, boot validation - #120)"; }
	const char* GetHelp() const override { return "Usage: guilddbcheck\n  Asserts the #120 guild-store contract against the live guilds.db and a\n  scratch copy of the v1 schema: the live file is WAL with foreign keys on and\n  stamped v1; a fresh database builds the schema and re-running the ensure is\n  harmless (idempotent, data preserved); any other stamp is refused (fresh\n  start, no migrations); creating a guild seeds the master membership, the\n  three per-rank permission masks at the plan 3.1.1 defaults and an empty\n  Treasury in one transaction; the name rules (1-20 chars, letters/digits/\n  space/'_'/'-', case-insensitive uniqueness), one-guild-per-character,\n  master-rank and succession invariants, the join/leave queue rules, Treasury\n  overdraw refusal with a logged withdrawal, the one-Title-per-member slot\n  rules and disband cascade all hold; and the boot validator catches the\n  hand-corrupted states the API refuses to create.\n  The scratch databases are deleted afterwards; the live guilds.db is only read.\n  Machine-readable lines: GUILDDBCHECK <check> <PASS|FAIL> [detail], then\n  GUILDDBCHECK RESULT <passed> <total>. Free of timestamps and absolute ids so\n  Windows and Linux output compares byte-for-byte.\n  Headless: Server --guilddbcheck runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
