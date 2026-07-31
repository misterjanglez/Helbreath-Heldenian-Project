// CmdTpEscrowCheck.h: the Trading Post escrow prover (#80, plan P3.2)
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "ServerCommand.h"

class CmdTpEscrowCheck : public ServerCommand
{
public:
	const char* get_name() const override { return "tpescrowcheck"; }
	const char* GetDescription() const override { return "Prove every Trading Post custody move reaches the Provenance Ledger (ADR 0001 + ADR 0003)"; }
	const char* GetHelp() const override { return "Usage: tpescrowcheck\n  Asserts the P3.2 contract by driving a real escrow flow: listing, offering,\n  rescinding, finalizing a Trade, delisting and voiding a deleted character all\n  append ledger events keyed by the Serial the escrow row carried, stored as the\n  ItemLogAction number itself. The two halves of a finalized Trade name their\n  counterparty; the moves that are one character and the board do not. Every\n  event carries the Listing id, and an Offer's carries its Offer id too.\n  Then the honesty checks: an item escrowed and returned comes back with the\n  same Serial, a Counted (stackable) item in a bundle records nothing, an actor\n  who is offline contributes their name and no location, and a delivery that\n  fails with the item already out of its owner's hands records the loss instead\n  of leaving the custody move as the last word.\n  Runs against a scratch tradingpost.db and a scratch ledger, with the character\n  save path disarmed, so neither the live board nor game.db is touched; a few\n  Serials are minted, which is the factory working.\n  Machine-readable lines: TPESCROWCHECK <check> <PASS|FAIL> [detail], then\n  TPESCROWCHECK RESULT <passed> <total>. Free of absolute serials, timestamps,\n  ids and row counts so Windows and Linux output compares byte-for-byte.\n  Headless: Server --tpescrowcheck runs this and exits."; }
	void execute(CGame* game, const char* args) override;
};
