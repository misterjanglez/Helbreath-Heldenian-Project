#pragma once

#include "ServerCommand.h"

class CmdReload : public ServerCommand
{
public:
	const char* get_name() const override { return "reload"; }
	const char* GetDescription() const override { return "Reload config tables from database or JSON"; }
	const char* GetHelp() const override { return "Usage: reload <items|magic|skills|npcs|shops|config|formulas|colors|tiers|all>\n  Reloads configuration at runtime.\n  config: reloads server_config.json (balance, timing, combat).\n  formulas: reloads formula tables from gamedata.db, pushes to clients.\n  items/magic/skills/npcs: reloads from gamedata.db, pushes to clients.\n  shops: reloads NPC shop inventories (server-side only).\n  tiers: reloads all tier + legacy-pool tables transactionally - the\n    candidate is validated first and rejected on any error, leaving the\n    running config untouched. The item_system mode itself is restart-only."; }
	void execute(CGame* game, const char* args) override;
};
