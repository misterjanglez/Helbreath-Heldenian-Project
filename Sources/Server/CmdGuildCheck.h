#pragma once

#include "ServerCommand.h"

class CmdGuildCheck : public ServerCommand
{
public:
	const char* get_name() const override { return "guildcheck"; }
	const char* GetDescription() const override { return "Prove the guild engine contract (lifecycle gates, membership index, hydration - #121)"; }
	const char* GetHelp() const override { return "Usage: guildcheck\n  Asserts the #121 guild-engine contract by driving the real policy entry\n  points with synthetic clients against a scratch guilds.db (the live store\n  and the ledger sink are swapped out for the run and restored after):\n  every create gate refuses on its own rung (level, charisma, citizenship,\n  home town, crusade, name rules, taken names) and a clean create founds,\n  hydrates and indexes the master; join/leave requests queue only with the\n  ticket, consume it on filing (never on refusal), and any holder of the\n  matching permission bit decides them; approving a join retires the\n  character's other applications; Kennedy self-exit demotes Officers on the\n  way out, refuses the master, costs the ticket plus 300 exp clamped at\n  zero (never wrapping the unsigned counter); kick works by permission bit\n  on targets strictly below the actor's rank, online or offline; promote/\n  demote and transfer-mastership move ranks, masks and the master's CHR\n  floor correctly; disband is master-only behind the typed name, pays the\n  Treasury remainder to the master and clears every online member through\n  the index; and login/logout hydration keeps the guid->handles index true.\n  The scratch databases are deleted afterwards; the live guilds.db is not\n  touched.\n  Machine-readable lines: GUILDCHECK <check> <PASS|FAIL> [detail], then\n  GUILDCHECK RESULT <passed> <total>. Free of timestamps and absolute ids so\n  Windows and Linux output compares byte-for-byte.\n  Headless: Server --guildcheck runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
