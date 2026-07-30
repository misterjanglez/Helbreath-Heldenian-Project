#pragma once

#include "ServerCommand.h"

class CmdSerialCheck : public ServerCommand
{
public:
	const char* get_name() const override { return "serialcheck"; }
	const char* GetDescription() const override { return "Prove the Serial identity contract (minting factory, ADR 0003)"; }
	const char* GetHelp() const override { return "Usage: serialcheck [instanced_item_id] [stackable_item_id]\n  Exercises ItemManager's minting factory and asserts the P1.1 contract:\n  Instanced (non-stackable) items get a unique monotonic Serial, Counted\n  (stackable) items stay unserialed, origins are recorded, restore carries a\n  stored Serial without minting and lifts the allocator past it, and an\n  in-place transform keeps the original identity. Item ids default to the\n  first non-stackable and first stackable rows in the config.\n  Nothing enters the world; every probe item is released.\n  Machine-readable lines: SERIALCHECK <check> <PASS|FAIL> [detail], then\n  SERIALCHECK RESULT <passed> <total>. Deliberately free of absolute Serial\n  numbers so Windows and Linux output compares byte-for-byte.\n  Headless: Server --serialcheck [ids] runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
