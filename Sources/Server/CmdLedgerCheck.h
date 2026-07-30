#pragma once

#include "ServerCommand.h"

class CmdLedgerCheck : public ServerCommand
{
public:
	const char* get_name() const override { return "ledgercheck"; }
	const char* GetDescription() const override { return "Prove the Provenance Ledger contract (itemledger.db, ADR 0003)"; }
	const char* GetHelp() const override { return "Usage: ledgercheck\n  Exercises the live itemledger.db and asserts the P2.1 contract: the file is\n  in WAL mode so external readers never block the server, recording buffers in\n  RAM without touching disk, one flush writes the whole window in a single\n  transaction, a failed flush keeps its buffer instead of dropping the events,\n  Counted items and orphan events are refused, flow rows accumulate rather than\n  overwrite, the boot boundary event is present, and the Serial high-water mark\n  is durable and recovers as max(meta, MAX(item_instances.serial)).\n  Writes to a scratch serial range far above the live allocator and deletes\n  every probe row afterwards; the live world is left untouched.\n  Machine-readable lines: LEDGERCHECK <check> <PASS|FAIL> [detail], then\n  LEDGERCHECK RESULT <passed> <total>. Free of absolute serials, timestamps and\n  row counts so Windows and Linux output compares byte-for-byte.\n  Headless: Server --ledgercheck runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
