#pragma once

#include "ServerCommand.h"

class CmdItemLogCheck : public ServerCommand
{
public:
	const char* get_name() const override { return "itemlogcheck"; }
	const char* GetDescription() const override { return "Prove item_log()'s dual sink reaches the Provenance Ledger (ADR 0003)"; }
	const char* GetHelp() const override { return "Usage: itemlogcheck\n  Asserts the P2.2 contract: minting an Instanced item lands a birth row and a\n  creation event, a Counted one lands neither, every item_log() call appends a\n  ledger event keyed by Serial carrying the actor, the counterparty and where it\n  happened, the stored event_type is the ItemLogAction number itself, and the\n  three things that stop the TEXT sink - an item check_good_item() rejects, an\n  action its switch does not name, a client handle that resolves to nobody -\n  stop only that sink.\n  Asserts the GM mint contract (#104) through the real batch venue: a request\n  for N copies lands N birth rows and N GmMint events - one per copy, since one\n  event for the batch leaves the rest held by a player the ledger cannot name -\n  each naming the holder as actor with no counterparty and no detail, while the\n  operator line for the request records nothing by itself.\n  Redirects the ledger sink to a scratch database for the run and restores it\n  afterwards, so the live itemledger.db gains nothing; a few Serials are minted\n  and a few text-channel lines are written, both of which are the sinks working.\n  Machine-readable lines: ITEMLOGCHECK <check> <PASS|FAIL> [detail], then\n  ITEMLOGCHECK RESULT <passed> <total>. Free of absolute serials, timestamps,\n  ids and row counts so Windows and Linux output compares byte-for-byte.\n  Headless: Server --itemlogcheck runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
