// CmdTpEscrowCheck.cpp: Trading Post escrow prover (Item Provenance Ledger P3.2, #80)
//
// Escrow is the transition the ledger is least able to survive losing, and the
// one where it was blindest. Under ADR 0001 a listed item is physically removed
// from its owner: the CItem is destroyed on the way in and rebuilt on the way
// out, so between those two moments the item exists only as database rows and
// its Serial is the whole of its identity. Until #80 the Trading Post wrote to a
// text channel and nothing else, which meant the one place in the economy where
// an item legitimately crosses accounts was the one place a Biography went
// silent — and a Serial that vanishes at one holder and reappears at another
// with nothing in between is indistinguishable from a dupe.
//
// So this drives the real flow rather than the emitters, because the failures
// worth catching live in the plumbing between them:
//
//   - A Serial dropped on the round trip. The item that comes back out of a
//     Listing has to be the same instance that went in. If escrow re-minted, the
//     ledger would show one item destroyed and an unrelated one created, which
//     is exactly what Reconciliation reports as a loss plus a duplication.
//   - A counterparty that is missing or wrong. "Who did I get this from" is the
//     question a trade dispute is, and a finalized Trade is the only escrow move
//     that has an answer — every other one is a character and the board.
//   - A delivery that fails and says nothing. ADR 0001 orders every step to fail
//     as loss rather than duplication, so losses are a designed outcome; an
//     audit trail that recorded the custody move and stayed silent about the
//     loss would name a holder who has not had the item for months.
//
// Three things are moved aside for the run, and all three go back:
//
//   - the ledger sink, redirected to a scratch database (an append-only log
//     cannot have test rows deleted afterwards, so the sink moves instead);
//   - the Trading Post store, which is a local instance on a scratch file rather
//     than the game's, so the live board gains nothing;
//   - g_login, nulled. Both escrow-in and online delivery force a character save
//     through it, and a synthetic client saved that way would write a probe
//     character into the LIVE game.db. Both call sites already guard on it being
//     null, so removing it removes exactly the writes and nothing else.
//
// The scratch board is created fresh, which is what makes the Listing and Offer
// ids in the expected `detail` strings deterministic (both columns are
// AUTOINCREMENT). They are asserted, never printed: the machine-readable lines
// carry no serials, ids, timestamps or row counts, because the Linux gate
// compares them against the Windows run byte for byte.
//
// Design contract: PLANS/ItemLedger_Plan.md P3.2, docs/adr/0003-item-provenance-ledger.md,
// docs/adr/0001-trading-post-physical-escrow.md.
//
//////////////////////////////////////////////////////////////////////

#include "CmdTpEscrowCheck.h"

#include "AccountSqliteStore.h"
#include "CheckTally.h"
#include "Client.h"
#include "Game.h"
#include "Item.h"
#include "ItemLedgerStore.h"
#include "ItemManager.h"
#include "ItemProvenance.h"
#include "LoginServer.h"
#include "Packet/PacketTradingPost.h"
#include "ServerConsole.h"
#include "ServerMessages.h"
#include "TradingPostStore.h"
#include "sqlite3.h"

#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace destroy_reason = hb::server::destroy_reason;
namespace item_origin = hb::server::item_origin;
namespace ledger_event = hb::server::ledger_event;
namespace ItemLogAction = hb::server::net::ItemLogAction;
using hb::server::check_tally;
using hb::server::find_free_handle;
using hb::server::find_probe_item;
using hb::server::item_ledger_store;
using hb::server::probe_client;
using hb::server::probe_scalar;
using hb::server::probe_text;
using hb::server::trading_post_store;

namespace
{
	constexpr const char* probe_ledger_db = "tpescrowcheck_probe.db";
	constexpr const char* probe_board_db  = "tpescrowcheck_board.db";

	// Every one of these has to fit CClient's buffers: character, account and map
	// names are all char[11], and a name that overflows is silently truncated on
	// the way in — so a probe with a longer one would compare its untruncated
	// constant against the server's shortened copy and fail everywhere at once,
	// looking exactly like an emitter that never ran.
	constexpr const char* seller_account  = "tpacctA";
	constexpr const char* seller_char     = "TpSeller";
	constexpr const char* offerer_account = "tpacctB";
	constexpr const char* offerer_char    = "TpBuyer";
	constexpr const char* probe_map       = "tpmap";
	constexpr int probe_x = 67;
	constexpr int probe_y = 41;

	// Stored numbers written out as literals. event_type values are permanent
	// world fact the moment real players exist, so a check that queried with the
	// same enum it recorded with would still pass if both were renumbered
	// together — which is the one thing that must never happen.
	constexpr int list_event_number       = 33;
	constexpr int delist_event_number     = 34;
	constexpr int offer_event_number      = 35;
	constexpr int rescind_event_number    = 36;
	constexpr int trade_out_event_number  = 38;
	constexpr int trade_in_event_number   = 39;
	constexpr int destroyed_event_number  = 102;
	static_assert(static_cast<int>(ItemLogAction::TpList) == list_event_number,
		"ItemLogAction::TpList moved: every escrow row already written means something else now.");
	static_assert(static_cast<int>(ItemLogAction::TpDelist) == delist_event_number,
		"ItemLogAction::TpDelist moved: every escrow row already written means something else now.");
	static_assert(static_cast<int>(ItemLogAction::TpOffer) == offer_event_number,
		"ItemLogAction::TpOffer moved: every escrow row already written means something else now.");
	static_assert(static_cast<int>(ItemLogAction::TpRescind) == rescind_event_number,
		"ItemLogAction::TpRescind moved: every escrow row already written means something else now.");
	static_assert(static_cast<int>(ItemLogAction::TpTradeOut) == trade_out_event_number,
		"ItemLogAction::TpTradeOut moved: every escrow row already written means something else now.");
	static_assert(static_cast<int>(ItemLogAction::TpTradeIn) == trade_in_event_number,
		"ItemLogAction::TpTradeIn moved: every escrow row already written means something else now.");
	static_assert(static_cast<int>(ledger_event::destroyed) == destroyed_event_number,
		"ledger_event::destroyed moved: every destruction row already written means something else now.");

	// Removes the character-save path for the run and puts it back however this
	// command returns. See the file header: escrow-in and online delivery both
	// force a save, and a synthetic client saved that way lands in the live
	// game.db. Both call sites already treat a null g_login as "do not save".
	struct login_hold
	{
		LoginServer* live = g_login;
		login_hold() { g_login = nullptr; }
		~login_hold() { g_login = live; }

		login_hold(const login_hold&) = delete;
		login_hold& operator=(const login_hold&) = delete;
	};

	// A minted probe item holding `count` of itself. The count has to be set
	// here because init_item_attr copies the config and leaves it at zero —
	// every creation venue supplies its own — and an escrow bundle asking for one
	// of a stack of zero is rejected as invalid before anything is recorded.
	CItem* mint_probe(ItemManager& items, int item_id, uint64_t count)
	{
		CItem* item = items.create_item(item_id, item_origin::gm_mint);
		if (item != nullptr) item->m_instance.count = count;
		return item;
	}

	// Puts an item the caller owns into an inventory slot and hands back its
	// Serial. The client takes ownership: ~CClient frees whatever is still in the
	// slot, and everything escrowed leaves the slot before then anyway.
	int64_t place_in_inventory(CGame* game, int client_h, int slot, CItem* item)
	{
		CClient* client = game->m_client_list[client_h];
		client->m_item_list[slot] = item;
		client->m_is_item_equipped[slot] = false;
		return item->m_serial;
	}

	// True when a Warehouse holds an item with this Serial. The round-trip
	// question: escrow is a custody move, so the instance that comes back out has
	// to be the one that went in.
	bool bank_holds_serial(CGame* game, int client_h, int64_t serial)
	{
		if (serial == 0) return false;
		const CClient* client = game->m_client_list[client_h];
		if (client == nullptr) return false;
		for (int i = 0; i < hb::shared::limits::MaxBankItems; i++)
			if (client->m_item_in_bank_list[i] != nullptr
				&& client->m_item_in_bank_list[i]->m_serial == serial) return true;
		return false;
	}

	// One escrow bundle as the wire would describe it.
	hb::net::TpEscrowSlot escrow_slot(int slot, int amount)
	{
		hb::net::TpEscrowSlot s{};
		s.inv_slot = static_cast<uint8_t>(slot);
		s.amount = amount;
		return s;
	}
}

void CmdTpEscrowCheck::execute(CGame* game, const char* args)
{
	(void)args;

	check_tally tally("TPESCROWCHECK", "tpescrowcheck");

	if (game == nullptr || game->m_item_manager == nullptr
		|| game->m_item_ledger_store == nullptr || !game->m_item_ledger_store->is_open())
	{
		hb::console::error("tpescrowcheck: the live ledger is not open.");
		return;
	}

	ItemManager& items = *game->m_item_manager;

	const int instanced_id = find_probe_item(game, false);
	const int counted_id = find_probe_item(game, true);
	if (items.is_valid_item_id(instanced_id) == false
		|| items.is_valid_item_id(counted_id) == false)
	{
		hb::console::error("tpescrowcheck: no usable probe item ids.");
		return;
	}

	const int seller_h = find_free_handle(game, 1);
	const int offerer_h = seller_h > 0 ? find_free_handle(game, seller_h + 1) : -1;
	if (G_pIOPool == nullptr || seller_h < 0 || offerer_h < 0)
	{
		hb::console::error("tpescrowcheck: no free client handles for the probes.");
		return;
	}

	// The offline half of the run delivers to a character who is not logged in,
	// which routes through the account store. That path is only safe — and the
	// loss it is meant to prove only happens — if the probe name resolves to
	// nobody. Checked rather than assumed, because the alternative is writing
	// Warehouse rows into a real player's account.
	{
		char account[32];
		std::memset(account, 0, sizeof(account));
		if (ResolveCharacterToAccount(seller_char, account, sizeof(account)))
		{
			hb::console::error("tpescrowcheck: '{}' is a real character - refusing to run.", seller_char);
			return;
		}
	}

	//----------------------------------------------------------------------
	// Move the three things aside. Everything below records into the scratch
	// ledger, trades on a scratch board, and saves nobody.
	//----------------------------------------------------------------------

	hb::server::ledger_sink_swap swap(game, probe_ledger_db);
	if (!swap.ok())
	{
		hb::console::error("tpescrowcheck: could not create the scratch ledger '{}'.", probe_ledger_db);
		return;
	}
	item_ledger_store& ledger = swap.scratch();

	// Its own store on its own file, rather than the game's: nothing inside
	// trading_post_store reaches for CGame's instance, so a local one is the
	// whole isolation. Removed on the way in as well as out, so a run that died
	// half way through cannot leave this one a board with rows already on it —
	// which is also what keeps the ids below deterministic.
	hb::server::remove_probe_db(probe_board_db);
	struct board_cleanup
	{
		~board_cleanup() { hb::server::remove_probe_db(probe_board_db); }
	} board_guard;

	trading_post_store board;
	board.set_game(game);
	if (!board.open(probe_board_db))
	{
		hb::console::error("tpescrowcheck: could not create the scratch board '{}'.", probe_board_db);
		return;
	}

	const login_hold no_saves;

	const probe_client seller_probe(game, seller_h, seller_account, seller_char,
		probe_map, probe_x, probe_y);
	const probe_client offerer_probe(game, offerer_h, offerer_account, offerer_char,
		probe_map, probe_x, probe_y);

	//----------------------------------------------------------------------
	// Phase 1 — a full trade: list, offer, rescind, offer again, finalize.
	//----------------------------------------------------------------------

	CItem* listed = mint_probe(items, instanced_id, 1);
	CItem* stack = mint_probe(items, counted_id, 10);
	CItem* first_offer = mint_probe(items, instanced_id, 1);
	CItem* second_offer = mint_probe(items, instanced_id, 1);
	if (listed == nullptr || stack == nullptr || first_offer == nullptr || second_offer == nullptr)
	{
		hb::console::error("tpescrowcheck: could not mint the probe items.");
		return;
	}

	const int64_t listed_serial = place_in_inventory(game, seller_h, 0, listed);
	place_in_inventory(game, seller_h, 1, stack);
	const int64_t first_offer_serial = place_in_inventory(game, offerer_h, 0, first_offer);
	const int64_t second_offer_serial = place_in_inventory(game, offerer_h, 1, second_offer);

	// A bundle of one Instanced item and part of a stack. The stack is the
	// Counted half of D2 and carries no Serial, so it must record nothing at all
	// — not a row with serial 0, which would point at no instance.
	const hb::net::TpEscrowSlot listing_bundle[] = { escrow_slot(0, 1), escrow_slot(1, 5) };
	int64_t listing_id = 0;
	uint16_t result = 0;
	const bool listed_ok = board.create_listing(seller_h, "probe", listing_bundle, 2,
		listing_id, result) && result == hb::net::TpResultCode::Ok;

	const hb::net::TpEscrowSlot first_offer_bundle[] = { escrow_slot(0, 1) };
	int64_t first_offer_id = 0;
	const bool offered_ok = board.place_offer(offerer_h, static_cast<int32_t>(listing_id),
		first_offer_bundle, 1, first_offer_id, result) && result == hb::net::TpResultCode::Ok;

	const bool rescinded_ok = board.refund_offer(first_offer_id, ItemLogAction::TpRescind);

	const hb::net::TpEscrowSlot second_offer_bundle[] = { escrow_slot(1, 1) };
	int64_t second_offer_id = 0;
	const bool reoffered_ok = board.place_offer(offerer_h, static_cast<int32_t>(listing_id),
		second_offer_bundle, 1, second_offer_id, result) && result == hb::net::TpResultCode::Ok;

	const bool finalized_ok = board.finalize(listing_id, second_offer_id, result)
		&& result == hb::net::TpResultCode::Ok;

	//----------------------------------------------------------------------
	// Phase 2 — a deleted character's escrowed bundle. Never delivered and
	// never returned, so it is an exit rather than a custody move.
	//----------------------------------------------------------------------

	CItem* voided = mint_probe(items, instanced_id, 1);
	if (voided == nullptr)
	{
		hb::console::error("tpescrowcheck: could not mint the void probe item.");
		return;
	}
	const int64_t voided_serial = place_in_inventory(game, seller_h, 0, voided);

	const hb::net::TpEscrowSlot void_bundle[] = { escrow_slot(0, 1) };
	int64_t void_listing_id = 0;
	board.create_listing(seller_h, "probe", void_bundle, 1, void_listing_id, result);
	board.void_character(seller_char);

	//----------------------------------------------------------------------
	// Phase 3 — an offline actor and a delivery that cannot land. The Seller
	// logs out between listing and delisting, so the return has a character to
	// name and no place to name, and the account store has never heard of them.
	//----------------------------------------------------------------------

	CItem* stranded = mint_probe(items, instanced_id, 1);
	if (stranded == nullptr)
	{
		hb::console::error("tpescrowcheck: could not mint the offline probe item.");
		return;
	}
	const int64_t stranded_serial = place_in_inventory(game, seller_h, 0, stranded);

	const hb::net::TpEscrowSlot stranded_bundle[] = { escrow_slot(0, 1) };
	int64_t stranded_listing_id = 0;
	board.create_listing(seller_h, "probe", stranded_bundle, 1, stranded_listing_id, result);

	// The Seller goes offline. The listing outlives them, which is the whole
	// point of a board.
	game->m_client_list[seller_h]->m_is_init_complete = false;

	board.delist(stranded_listing_id, result);

	if (!ledger.flush(0))
	{
		hb::console::error("tpescrowcheck: could not flush the scratch ledger.");
		return;
	}

	//----------------------------------------------------------------------
	// Read the window back and check what each column says.
	//----------------------------------------------------------------------

	sqlite3* db = ledger.handle();

	auto count_of = [&](int type, int64_t serial)
	{
		return hb::server::event_scalar(db, "COUNT(*)", type, serial);
	};
	auto number_of = [&](const char* expr, int type, int64_t serial)
	{
		return hb::server::event_scalar(db, expr, type, serial);
	};
	auto text_of = [&](const char* column, int type, int64_t serial)
	{
		return hb::server::event_text(db, column, type, serial);
	};
	auto is_null = [&](const char* column, int type, int64_t serial)
	{
		return number_of((std::string(column) + " IS NULL").c_str(), type, serial) == 1;
	};
	auto total_of = [&](int type)
	{
		return probe_scalar(db, std::format(
			"SELECT COUNT(*) FROM item_events WHERE event_type={};", type).c_str());
	};
	// Rows belonging to one transition rather than to one item. The detail column
	// is the only thing that identifies which Listing an event came from, which
	// is exactly the question a Counted item cannot be asked directly: it has no
	// Serial to key on, so "it recorded nothing" can only be shown by counting
	// what the bundle it travelled in did record. The JSON carries no single
	// quotes, so it needs no escaping to sit inside a SQL literal.
	auto rows_for = [&](int type, const std::string& detail)
	{
		return probe_scalar(db, std::format(
			"SELECT COUNT(*) FROM item_events WHERE event_type={} AND detail='{}';",
			type, detail).c_str());
	};

	// The ids are AUTOINCREMENT on a board created empty a moment ago, so these
	// are the strings the detail column must hold. Asserted, never printed.
	const std::string listing_detail = std::format("{{\"listing\":{}}}", listing_id);
	const std::string offer_detail = std::format("{{\"listing\":{},\"offer\":{}}}",
		listing_id, first_offer_id);
	const std::string trade_detail = std::format("{{\"listing\":{},\"offer\":{}}}",
		listing_id, second_offer_id);
	const std::string stranded_detail = std::format("{{\"listing\":{}}}", stranded_listing_id);
	const std::string delivery_lost_detail = std::format("{{\"reason\":\"{}\"}}",
		destroy_reason::name(destroy_reason::delivery_lost));
	const std::string character_deleted_detail = std::format("{{\"reason\":\"{}\"}}",
		destroy_reason::name(destroy_reason::character_deleted));

	//--- Custody in --------------------------------------------------------

	// The event exists, it is keyed by the Serial the escrow row carried, and its
	// type is the ItemLogAction number itself rather than anything translated.
	tally.record("list_emits_keyed_by_serial",
		listed_ok && listed_serial != 0 && count_of(list_event_number, listed_serial) == 1);

	// Whose it was and where they stood. Escrow-in is the half of the flow with
	// an online actor, so this is where the location columns get to be true.
	tally.record("list_records_the_seller",
		text_of("actor_char", list_event_number, listed_serial) == seller_char
		&& text_of("actor_account", list_event_number, listed_serial) == seller_account
		&& text_of("map", list_event_number, listed_serial) == probe_map
		&& number_of("x", list_event_number, listed_serial) == probe_x
		&& number_of("y", list_event_number, listed_serial) == probe_y);

	// Listing is one character and the board. Naming a counterparty here would
	// invent a transfer between two players that has not happened yet.
	tally.record("list_has_no_counterparty",
		is_null("counterparty_char", list_event_number, listed_serial));

	// The Listing id is the thread a whole trade hangs off: one query on this
	// column returns both bundles, every offerer and both directions.
	tally.record("list_detail_names_the_listing",
		text_of("detail", list_event_number, listed_serial) == listing_detail);

	// The bundle held a stack as well, and stackables have no identity to key an
	// event on (D2). Their aggregate flow counters are #81's; what matters here
	// is that the Counted half produced no row at all rather than a row pointing
	// at no instance. Counted against the run's total, because "no row for
	// serial 0" is not something a query keyed on a Serial can ask.
	tally.record("counted_item_records_nothing", rows_for(list_event_number, listing_detail) == 1);

	tally.record("offer_detail_names_listing_and_offer",
		offered_ok && count_of(offer_event_number, first_offer_serial) == 1
		&& text_of("detail", offer_event_number, first_offer_serial) == offer_detail);

	//--- Custody out, same character ---------------------------------------

	tally.record("rescind_returns_to_the_offerer",
		rescinded_ok && count_of(rescind_event_number, first_offer_serial) == 1
		&& text_of("actor_char", rescind_event_number, first_offer_serial) == offerer_char
		&& text_of("detail", rescind_event_number, first_offer_serial) == offer_detail);

	// The round trip. An item pulled out of inventory, written to a database and
	// rebuilt has to be the same instance: a re-mint here would read as one item
	// destroyed and an unrelated one created, which is a loss plus a duplication
	// in the one report the ledger exists to produce.
	tally.record("escrow_round_trip_keeps_the_serial",
		bank_holds_serial(game, offerer_h, first_offer_serial));

	//--- The trade ---------------------------------------------------------

	// The two events that have a counterparty, and the only two that do. Each
	// names the character on the other end of the exchange, which is the answer
	// to the question a trade dispute is.
	tally.record("trade_out_names_the_winner",
		finalized_ok && count_of(trade_out_event_number, second_offer_serial) == 1
		&& text_of("actor_char", trade_out_event_number, second_offer_serial) == seller_char
		&& text_of("counterparty_char", trade_out_event_number, second_offer_serial) == offerer_char
		&& text_of("detail", trade_out_event_number, second_offer_serial) == trade_detail);

	tally.record("trade_in_names_the_seller",
		count_of(trade_in_event_number, listed_serial) == 1
		&& text_of("actor_char", trade_in_event_number, listed_serial) == offerer_char
		&& text_of("counterparty_char", trade_in_event_number, listed_serial) == seller_char
		&& text_of("detail", trade_in_event_number, listed_serial) == trade_detail);

	// End to end, in one Biography: the item the Seller listed is now in the
	// winner's Warehouse under the Serial it was born with.
	tally.record("traded_item_reaches_the_winner",
		bank_holds_serial(game, offerer_h, listed_serial));

	//--- A deleted character's bundle --------------------------------------

	// An exit, with a reason of its own. Rolled into delivery_lost, every
	// character deletion would read as an outage.
	tally.record("void_destroys_with_its_own_reason",
		count_of(destroyed_event_number, voided_serial) == 1
		&& text_of("detail", destroyed_event_number, voided_serial) == character_deleted_detail);

	// It went in through the front door and left by destruction. A return event
	// here would say a character who no longer exists got their items back.
	tally.record("void_records_no_custody_move",
		count_of(list_event_number, voided_serial) == 1
		&& count_of(delist_event_number, voided_serial) == 0);

	//--- An offline actor and a delivery that cannot land ------------------

	tally.record("delist_emits_for_an_offline_seller",
		count_of(delist_event_number, stranded_serial) == 1
		&& text_of("detail", delist_event_number, stranded_serial) == stranded_detail);

	// A name is the honest half. An escrowed item is returned to a character,
	// not to a place, so an actor who is not logged in contributes who they are
	// and nothing about where — rather than a 0,0 a query would read as the
	// top-left corner of a map.
	tally.record("offline_actor_has_a_name_and_no_place",
		text_of("actor_char", delist_event_number, stranded_serial) == seller_char
		&& is_null("actor_account", delist_event_number, stranded_serial)
		&& is_null("map", delist_event_number, stranded_serial));

	// The delivery had nowhere to go, and the escrow row was already gone. The
	// custody move is still true; what makes the pair honest is the exit beside
	// it, because otherwise the Seller stays this item's recorded holder forever.
	tally.record("unreachable_delivery_records_the_loss",
		count_of(destroyed_event_number, stranded_serial) == 1
		&& text_of("detail", destroyed_event_number, stranded_serial) == delivery_lost_detail);

	// And nothing else left. Across an entire trade — six bundles moving between
	// two accounts and a board — the only exits are the two this deliberately
	// caused. A third would be an item destroyed by a custody move that was
	// supposed to preserve it, which is the failure this whole prover is for.
	tally.record("no_spurious_exits", total_of(destroyed_event_number) == 2);

	//----------------------------------------------------------------------
	// Put the live sink back. Restored by hand rather than left to the guard so
	// that it can be asserted: everything above wrote into a file this command
	// is about to delete, and a run that ended without moving the sink back
	// would leave the world logging into nothing — silently, because the ledger
	// promises never to fail a game action.
	//----------------------------------------------------------------------

	swap.restore();

	const std::string restored_file = probe_text(
		game->m_item_ledger_store != nullptr ? game->m_item_ledger_store->handle() : nullptr,
		"SELECT file FROM pragma_database_list WHERE name='main';");

	tally.record("live_sink_restored",
		game->m_item_ledger_store != nullptr
		&& game->m_item_ledger_store->is_open()
		&& restored_file.find(probe_ledger_db) == std::string::npos);

	board.close();
	hb::server::remove_probe_db(probe_ledger_db);
	hb::server::remove_probe_db(probe_board_db);
	tally.record("scratch_removed",
		!std::filesystem::exists(probe_ledger_db) && !std::filesystem::exists(probe_board_db));

	tally.report(std::format("instanced item {}, counted item {}", instanced_id, counted_id));
}
