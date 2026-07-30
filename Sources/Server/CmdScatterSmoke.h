#pragma once

#include "ServerCommand.h"

class CmdScatterSmoke : public ServerCommand
{
public:
	const char* get_name() const override { return "scattersmoke"; }
	const char* GetDescription() const override { return "Yield and composition smoke for a monster's drop slots"; }
	const char* GetHelp() const override { return "Usage: scattersmoke <npc_id> [kills] [stage]\n  Rolls <kills> (default 100000) drops for the monster's stage-<stage>\n  table (default 2) and prints the observed yield distribution and\n  per-item composition. Seeded, and drawn without\n  std::uniform_int_distribution, so the output is byte-identical on\n  Windows and Linux.\n  Machine-readable lines: SCATTERSMOKE YIELD <n> <kills>, SCATTERSMOKE\n  ITEM <id> <count>, SCATTERSMOKE TOTALS <items> <gold_piles> <gold>.\n  Headless: Server --scattersmoke <npc_id> [kills] [stage] runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
