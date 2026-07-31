// CmdCoverageCheck.cpp: the Counted-tier / coverage-audit prover (P3.3, #81)
//
// This is the trust gate for Phase 4. Everything the forensic tooling will do —
// reconciliation, Biographies, drop-rate analytics — rests on the claim that
// every item-mutation path emits, so that claim needs evidence rather than a
// reading of the code.
//
// Two halves, and the second is what #79 and #80 deliberately left:
//
//   - The Instanced tier was already wired. What is asserted here is that it
//     stayed that way: an item with a Serial books an event and NO flow row.
//   - The Counted tier recorded nothing at all. `record_flow` existed and had no
//     caller in the whole server, so a gold drop, a potion drunk and a stack of
//     arrows sold were invisible in both sinks.
//
// The taxonomy question that held it up is settled here by NOT answering it:
// `flow_type` stores the same number the Instanced path stores in
// `item_events.event_type`. A second numbering would have been a second set of
// permanent world-fact values to keep from drifting against the first — the
// exact failure the 1..99 pass-through band was built to prevent for events —
// and it would have bought nothing, because every flow the plan asked for
// (dropped, picked_up, despawned, consumed, shop_bought, shop_sold) already had
// a number. So the checks below assert LITERALS: a Drop flow is 2 because
// ItemLogAction::Drop is 2, and if either ever moves, every row already written
// means something else.
//
// The trap this prover exists to not fall into: `init_item_attr` leaves
// `m_instance.count` at 0, and a zero-quantity flow is deliberately not
// recorded. A probe item taken straight from the factory therefore proves
// nothing about flows — it takes the same path as a broken implementation. Every
// stackable probe below is given an explicit count for that reason.
//
// As with the other provers the ledger sink is redirected to a scratch database
// and restored on every exit path, so the live itemledger.db gains nothing. The
// machine-readable lines carry no serials, ids, timestamps or row counts: the
// Linux gate compares them against the Windows run byte for byte.
//
// Design contract: PLANS/ItemLedger_Plan.md P3.3, docs/adr/0003-item-provenance-ledger.md.
//
//////////////////////////////////////////////////////////////////////

#include "CmdCoverageCheck.h"

#include "CheckTally.h"
#include "Client.h"
#include "Game.h"
#include "Item.h"
#include "ItemLedgerStore.h"
#include "ItemManager.h"
#include "ItemProvenance.h"
#include "Packet/SharedPackets.h"
#include "ServerConsole.h"
#include "ServerMessages.h"
#include "sqlite3.h"

#include <cstdint>
#include <ctime>
#include <format>
#include <memory>
#include <string>

using namespace hb::shared::net;   // MsgId, for the retrieve packet below

namespace destroy_reason = hb::server::destroy_reason;
namespace despawn_reason = hb::server::despawn_reason;
namespace item_origin = hb::server::item_origin;
namespace ledger_event = hb::server::ledger_event;
namespace ItemLogAction = hb::server::net::ItemLogAction;
using hb::server::check_tally;
using hb::server::find_free_handle;
using hb::server::find_probe_item;
using hb::server::item_ledger_store;
using hb::server::probe_client;
using hb::server::probe_scalar;

namespace
{
	constexpr const char* probe_db = "coveragecheck_probe.db";

	constexpr const char* actor_account = "probeacct";
	constexpr const char* actor_char    = "ProbeCov";
	constexpr const char* actor_map     = "probemap";
	constexpr int actor_x = 13;
	constexpr int actor_y = 26;

	// A second character, so the Give checks have somewhere to give to.
	constexpr const char* peer_char = "ProbePeer";

	// Stack sizes. Distinct primes so a check that summed the wrong pair, or read
	// one stack's count where it meant another's, cannot land on the right total
	// by coincidence.
	constexpr int64_t drop_qty     = 7;
	constexpr int64_t drop_qty_2   = 11;
	constexpr int64_t pickup_qty   = 13;
	constexpr int64_t held_qty     = 100;   // the whole slot a sale is taken out of
	constexpr int64_t sold_qty     = 5;     // what the sale actually moved
	constexpr int64_t merge_qty    = 17;
	constexpr int64_t exit_qty     = 19;
	constexpr int64_t despawn_qty  = 23;

	// Stored numbers as literals. These are permanent world fact the moment real
	// players exist, and a check that queried with the same symbol it stored
	// would still pass if both were renumbered together.
	constexpr int drop_flow_number     = 2;
	constexpr int get_flow_number      = 3;
	constexpr int sell_flow_number     = 8;
	constexpr int retrieve_event_number = 9;
	constexpr int deposit_event_number  = 10;
	constexpr int give_event_number     = 1;
	constexpr int despawned_number      = 101;
	constexpr int destroyed_number      = 102;
	static_assert(static_cast<int>(ItemLogAction::Drop) == drop_flow_number,
		"ItemLogAction::Drop moved: every drop flow already written means something else now.");
	static_assert(static_cast<int>(ItemLogAction::get) == get_flow_number,
		"ItemLogAction::get moved: every pickup flow already written means something else now.");
	static_assert(static_cast<int>(ItemLogAction::Sell) == sell_flow_number,
		"ItemLogAction::Sell moved: every shop-sale flow already written means something else now.");
	static_assert(static_cast<int>(ItemLogAction::Retrieve) == retrieve_event_number,
		"ItemLogAction::Retrieve moved: every Warehouse retrieval row means something else now.");
	static_assert(static_cast<int>(ItemLogAction::Deposit) == deposit_event_number,
		"ItemLogAction::Deposit moved: every Warehouse deposit row means something else now.");
	static_assert(static_cast<int>(ItemLogAction::Give) == give_event_number,
		"ItemLogAction::Give moved: every hand-over row already written means something else now.");
	static_assert(static_cast<int>(ledger_event::despawned) == despawned_number,
		"ledger_event::despawned moved: every despawn row already written means something else now.");
	static_assert(static_cast<int>(ledger_event::destroyed) == destroyed_number,
		"ledger_event::destroyed moved: every destruction row already written means something else now.");

	// Today as yyyymmdd, local time — the same rule item_ledger_store::flow_day
	// uses. Recomputed here rather than exposed from the store so the check is
	// evidence about the stored value rather than a restatement of it.
	int32_t today_key()
	{
		const std::time_t now = std::time(nullptr);
		std::tm tm{};
#ifdef _WIN32
		localtime_s(&tm, &now);
#else
		localtime_r(&now, &tm);
#endif
		return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
	}

	// A stackable probe with a real count on it. The count is the entire point:
	// a flow of zero is deliberately not recorded, so a probe left at the
	// factory's default would take the same path as a broken implementation.
	CItem* counted_probe(ItemManager& items, int item_id, int64_t count)
	{
		CItem* item = items.create_item(item_id, item_origin::none);
		if (item != nullptr) item->m_instance.count = static_cast<uint64_t>(count);
		return item;
	}
}

void CmdCoverageCheck::execute(CGame* game, const char* args)
{
	(void)args;

	check_tally tally("COVERAGECHECK", "coveragecheck");

	if (game == nullptr || game->m_item_manager == nullptr
		|| game->m_item_ledger_store == nullptr || !game->m_item_ledger_store->is_open())
	{
		hb::console::error("coveragecheck: the live ledger is not open.");
		return;
	}

	ItemManager& items = *game->m_item_manager;

	const int instanced_id = find_probe_item(game, false);
	const int counted_id = find_probe_item(game, true);
	if (items.is_valid_item_id(instanced_id) == false
		|| items.is_valid_item_id(counted_id) == false)
	{
		hb::console::error("coveragecheck: no usable probe item ids.");
		return;
	}

	const int actor_h = find_free_handle(game);
	const int peer_h = (actor_h < 0) ? -1 : find_free_handle(game, actor_h + 1);
	if (G_pIOPool == nullptr || actor_h < 0 || peer_h < 0)
	{
		hb::console::error("coveragecheck: no free client handles for the probes.");
		return;
	}

	//----------------------------------------------------------------------
	// Redirect the sink. Everything below records into the scratch file.
	//----------------------------------------------------------------------

	hb::server::ledger_sink_swap swap(game, probe_db);
	if (!swap.ok())
	{
		hb::console::error("coveragecheck: could not create the scratch ledger '{}'.", probe_db);
		return;
	}
	item_ledger_store& ledger = swap.scratch();

	const probe_client actor(game, actor_h, actor_account, actor_char, actor_map, actor_x, actor_y);
	const probe_client peer(game, peer_h, actor_account, peer_char, actor_map, actor_x, actor_y);

	using item_ptr = std::unique_ptr<CItem>;

	//----------------------------------------------------------------------
	// The Counted tier: transitions become aggregate flows.
	//----------------------------------------------------------------------

	// Two drops of the same item type, so accumulation into ONE row is provable
	// rather than assumed.
	item_ptr dropped{ counted_probe(items, counted_id, drop_qty) };
	item_ptr dropped2{ counted_probe(items, counted_id, drop_qty_2) };
	item_ptr picked{ counted_probe(items, counted_id, pickup_qty) };
	if (dropped == nullptr || dropped2 == nullptr || picked == nullptr)
	{
		hb::console::error("coveragecheck: item creation failed for a Counted probe.");
		return;
	}

	const bool counted_unserialed = (dropped->m_serial == 0);

	items.item_log(ItemLogAction::Drop, actor_h, -1, dropped.get());
	items.item_log(ItemLogAction::Drop, actor_h, -1, dropped2.get());
	items.item_log(ItemLogAction::get, actor_h, 0, picked.get());

	//----------------------------------------------------------------------
	// Quantity fidelity: what moved, not what the stack held.
	//----------------------------------------------------------------------

	// The shape of a shop sale: item_log is handed the whole slot, and only part
	// of it was sold. Without the explicit quantity this books `held_qty`.
	item_ptr sold{ counted_probe(items, counted_id, held_qty) };
	if (sold == nullptr)
	{
		hb::console::error("coveragecheck: item creation failed for the sale probe.");
		return;
	}
	items.item_log(ItemLogAction::Sell, actor_h, -1, sold.get(), false, sold_qty);

	//----------------------------------------------------------------------
	// The merge exclusion, and the exits that are not merges.
	//----------------------------------------------------------------------

	// A merge frees a husk whose contents live on in the stack it joined, so
	// nothing left the world and nothing may be booked. Every gold pickup takes
	// this path, so a flow here would double-count the entire currency supply.
	CItem* merged_raw = counted_probe(items, counted_id, merge_qty);
	items.destroy_item(merged_raw, destroy_reason::merged, actor_h);
	const bool merge_nulled = (merged_raw == nullptr);

	// Any other reason is a real exit and does book.
	CItem* exited_raw = counted_probe(items, counted_id, exit_qty);
	items.destroy_item(exited_raw, destroy_reason::consumed, actor_h);

	// A ground stackable timing out: attrition, and the one exit with no actor.
	CItem* rotted_raw = counted_probe(items, counted_id, despawn_qty);
	items.despawn_item(rotted_raw, despawn_reason::expired, actor_map, actor_x, actor_y);

	//----------------------------------------------------------------------
	// The Instanced tier is untouched by any of it.
	//----------------------------------------------------------------------

	item_ptr instanced{ items.create_item(instanced_id, item_origin::npc_drop) };
	if (instanced == nullptr)
	{
		hb::console::error("coveragecheck: item creation failed for the Instanced probe.");
		return;
	}
	const int64_t instanced_serial = instanced->m_serial;
	const bool instanced_serialed = (instanced_serial != 0);

	items.item_log(ItemLogAction::Drop, actor_h, -1, instanced.get());

	// An explicit quantity is meaningless for an item that has identity, and must
	// not turn into a flow row for it.
	items.item_log(ItemLogAction::get, actor_h, 0, instanced.get(), false, sold_qty);

	//----------------------------------------------------------------------
	// The transitions this ticket wired. All three had a text-sink switch arm
	// and no caller anywhere in the server before it, so they were silent in
	// both sinks: a Warehouse deposit, a Warehouse retrieval, and the
	// split-stack half of a Give.
	//----------------------------------------------------------------------

	// Driven through the REAL doors, not by calling item_log with the action.
	// The latter would pass whether or not the doors were ever wired — it would
	// only prove that item_log can carry a number, which was never in question.
	// So the deposit goes through set_item_to_bank_item and the retrieval
	// through request_retrieve_item_handler, exactly as a player's packet does.
	CItem* banked_raw = items.create_item(instanced_id, item_origin::npc_drop);
	item_ptr handed{ items.create_item(instanced_id, item_origin::npc_drop) };
	if (banked_raw == nullptr || handed == nullptr)
	{
		hb::console::error("coveragecheck: item creation failed for a transition probe.");
		return;
	}
	const int64_t banked_serial = banked_raw->m_serial;
	const int64_t handed_serial = handed->m_serial;

	// The Warehouse now owns banked_raw on success; the probe_client destructor
	// frees whatever is left in its bank list.
	const bool deposited = items.set_item_to_bank_item(actor_h, banked_raw,
		ItemManager::bank_deposit::by_character);

	// ...and straight back out of slot 0, through the packet handler.
	hb::net::PacketRequestRetrieveItem retrieve{};
	retrieve.header.msg_id = MsgId::RequestRetrieveItem;
	retrieve.header.msg_type = 0;
	retrieve.item_slot = 0;
	items.request_retrieve_item_handler(actor_h,
		reinterpret_cast<char*>(&retrieve));

	items.item_log(ItemLogAction::Give, actor_h, peer_h, handed.get());

	//----------------------------------------------------------------------
	// Flush and read the columns a forensic query would read.
	//----------------------------------------------------------------------

	ledger.flush(items.serial_high_water());
	sqlite3* db = ledger.handle();

	const int32_t day = today_key();

	auto flow_qty = [&](int flow_type)
	{
		return probe_scalar(db, std::format(
			"SELECT qty FROM item_flows WHERE day={} AND item_id={} AND flow_type={}",
			day, counted_id, flow_type).c_str());
	};
	auto flow_rows = [&](int flow_type)
	{
		return probe_scalar(db, std::format(
			"SELECT COUNT(*) FROM item_flows WHERE day={} AND item_id={} AND flow_type={}",
			day, counted_id, flow_type).c_str());
	};
	auto instanced_flow_rows = [&]()
	{
		return probe_scalar(db, std::format(
			"SELECT COUNT(*) FROM item_flows WHERE item_id={}", instanced_id).c_str());
	};
	auto event_count = [&](int event_type, int64_t serial)
	{
		return hb::server::event_scalar(db, "COUNT(*)", event_type, serial);
	};
	auto event_text = [&](const char* column, int event_type, int64_t serial)
	{
		return hb::server::event_text(db, column, event_type, serial);
	};

	//----------------------------------------------------------------------
	// Counted tier
	//----------------------------------------------------------------------

	tally.record("counted_item_has_no_serial", counted_unserialed);

	// The headline: a stackable transition is recorded at all. Before this
	// ticket record_flow had no caller and this row did not exist.
	tally.record("counted_transition_books_flow", flow_rows(drop_flow_number) == 1);

	// Two drops of one item type accumulate into one row rather than one row per
	// move — the reason flows are a map keyed by (day, item, type).
	tally.record("counted_flows_accumulate",
		flow_qty(drop_flow_number) == drop_qty + drop_qty_2);

	// Distinct transitions stay distinct rows: a pickup is not a drop.
	tally.record("counted_types_stay_distinct", flow_qty(get_flow_number) == pickup_qty);

	// flow_type IS the event number, and the whole decision was to not mint a
	// second taxonomy — so the stored value is READ BACK here, keyed on the
	// pickup's own quantity, rather than filtered on. Every other check selects
	// `WHERE flow_type=<literal>`, which can only ever confirm its own premise:
	// it would pass just as well against a private flow numbering that happened
	// to use 3 for something else.
	tally.record("flow_type_is_the_event_number",
		probe_scalar(db, std::format(
			"SELECT flow_type FROM item_flows WHERE day={} AND item_id={} AND qty={}",
			day, counted_id, pickup_qty).c_str()) == get_flow_number);

	// A Counted item has no identity, so it must never reach item_events — a row
	// there would point at an instance that does not exist.
	tally.record("counted_books_no_event",
		probe_scalar(db, "SELECT COUNT(*) FROM item_events WHERE serial=0 "
			"AND event_type<>103") == 0);

	// ... and no birth row either.
	tally.record("counted_books_no_instance_row",
		probe_scalar(db, std::format(
			"SELECT COUNT(*) FROM item_instances WHERE item_id={}", counted_id).c_str()) == 0);

	tally.record("flow_day_is_today",
		probe_scalar(db, std::format(
			"SELECT COUNT(*) FROM item_flows WHERE day<>{}", day).c_str()) == 0);

	//----------------------------------------------------------------------
	// Quantity fidelity
	//----------------------------------------------------------------------

	// The sale booked what moved. Had the funnel read the item's own count this
	// would be held_qty — a stack of 100 arrows reported as a sale of 100.
	tally.record("explicit_qty_wins_over_stack", flow_qty(sell_flow_number) == sold_qty);

	//----------------------------------------------------------------------
	// Exits
	//----------------------------------------------------------------------

	// A real exit books. (day, item_id, flow_type) is the primary key, so this
	// can only ever be 0 or 1 — it proves the row exists, nothing about its size.
	tally.record("exit_books_flow", flow_rows(destroyed_number) == 1);

	// ...and the merge did NOT contribute to it. This is the load-bearing half:
	// both destructions share one row, so a merge that booked would be invisible
	// to a row count and would show up only here, as exit_qty + merge_qty. Every
	// gold pickup in the game takes the merge path.
	tally.record("merge_books_no_flow", flow_qty(destroyed_number) == exit_qty);
	tally.record("merge_nulls_caller_pointer", merge_nulled);

	// Despawn is its own exit and its own number.
	tally.record("despawn_books_flow", flow_qty(despawned_number) == despawn_qty);

	//----------------------------------------------------------------------
	// Instanced tier unaffected
	//----------------------------------------------------------------------

	tally.record("instanced_item_has_serial", instanced_serialed);
	tally.record("instanced_books_event", event_count(drop_flow_number, instanced_serial) == 1);

	// The other half of the boundary, and the one a naive implementation gets
	// wrong: an item with identity must never also be counted in aggregate, or
	// every Instanced transition is recorded twice in two different shapes.
	tally.record("instanced_books_no_flow", instanced_flow_rows() == 0);
	tally.record("instanced_ignores_explicit_qty",
		event_count(get_flow_number, instanced_serial) == 1);

	//----------------------------------------------------------------------
	// The newly-wired transitions
	//----------------------------------------------------------------------

	tally.record("deposit_accepted", deposited);

	// Both keyed on the SAME Serial: the item that went into the Warehouse is the
	// item that came back out, which is also what makes this a custody chain
	// rather than two unrelated events.
	tally.record("deposit_emits", event_count(deposit_event_number, banked_serial) == 1);
	tally.record("retrieve_emits", event_count(retrieve_event_number, banked_serial) == 1);

	// Exactly one of each. The retrieve handler has two success branches — merge
	// into an existing stack, and take a free slot — and an emission added to
	// both without noticing they are alternatives would double-count.
	tally.record("deposit_emits_once",
		probe_scalar(db, std::format(
			"SELECT COUNT(*) FROM item_events WHERE event_type IN ({},{})",
			deposit_event_number, retrieve_event_number).c_str()) == 2);

	tally.record("give_emits", event_count(give_event_number, handed_serial) == 1);

	// A Give names who received it — the column a dispute is settled with.
	tally.record("give_records_counterparty",
		event_text("counterparty_char", give_event_number, handed_serial) == peer_char);

	//----------------------------------------------------------------------
	// The sink goes back before anything else runs.
	//----------------------------------------------------------------------

	swap.restore();
	tally.record("live_sink_restored",
		game->m_item_ledger_store != nullptr && game->m_item_ledger_store->is_open());

	tally.report();
}
