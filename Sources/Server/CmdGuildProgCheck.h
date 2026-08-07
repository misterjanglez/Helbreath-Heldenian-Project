#pragma once

#include "ServerCommand.h"

class CmdGuildProgCheck : public ServerCommand
{
public:
	const char* get_name() const override { return "guildprogcheck"; }
	const char* GetDescription() const override { return "Prove the guild progression contract (donations, levels, caps, titles, treasury - #122)"; }
	const char* GetHelp() const override { return "Usage: guildprogcheck\n  Asserts the #122 guild-progression contract. The dataset half runs the\n  gamedata loader + fail-fast validator against a scratch config database:\n  a coherent curve loads clean, and a hole, a nonzero Level-1 requirement,\n  a decreasing column, an unknown or missing or non-boolean knob, an empty\n  curve and a world level above the curve max are each refused. The engine\n  half stages a synthetic three-level curve on CGame (restored after) and\n  drives the real entry points with synthetic clients against a scratch\n  guilds.db + ledger: the Donation lanes gate on the enable switch, the\n  minimums and the donor's own balance (a negative contribution included),\n  burn the live counter, accrue lifetime + guild counters in one\n  transaction and climb Levels only when all three lanes cross (upward\n  only, over-donation carrying); the member cap and Officer capacity read\n  the curve; Titles claim first-come-first-served inside curve slot counts\n  (Commander pinned at one behind its eligibility bit, one Title per\n  member), drop, strip under title_manage, release on losing Commander\n  eligibility and with membership, and auto-release on the inactivity\n  sweep (online holders ride the AFK clock and are touched while active;\n  offline holders ride last_seen_at); the Treasury deposits/withdraws with\n  its log and never moves a burn counter; and the bonus seams\n  (client_holds_title, the repair discount) read the cache and the switch\n  correctly. The scratch files are deleted afterwards; the live guilds.db\n  and gamedata.db are not touched.\n  Machine-readable lines: GUILDPROGCHECK <check> <PASS|FAIL> [detail], then\n  GUILDPROGCHECK RESULT <passed> <total>. Free of timestamps and absolute\n  ids so Windows and Linux output compares byte-for-byte.\n  Headless: Server --guildprogcheck runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
