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
// A third half arrived with #103, and it is the half that makes the money supply
// answerable: the Counted tier booked inflows but not outflows. Gold leaving a
// player's hands to an NPC reduces the stack through `set_item_count_by_id`, and
// that call carried no transition — so a purchase, a repair bill and a spell
// were unrecorded while every route gold took INTO the world was counted. The
// checks below assert the funnel books what it removes, under one number per
// route, always positive.
//
// #105 closed the same hole in the slot-keyed sibling: `set_item_count` booked
// only when a stack emptied, so the reagents a recipe took out of a stack it
// did not empty — the common case of every craft — were unrecorded. Those
// checks walk one reagent stack through every caller shape: a consumption
// books `Make` for the decrease, and a flow_none decrease, a rise and an
// already-applied decrease book nothing.
//
// #109 folded the last hand-rolled copy of that setter into it: arrow
// consumption in CombatManager inlined the decrement-or-deplete body, so a
// fired arrow booked nothing and — because the copy decremented BEFORE calling
// deplete — the quiver-emptying shot handed the exit bookings a count-0 stack,
// which the zero-quantity guard drops. An emptied quiver left no Deplete row
// and no destroyed row, where every other emptied stack in the game books
// both. The checks below fire a quiver dry one arrow at a time and assert the
// ammunition number: `Use` books each shot's decrease, accumulates across
// shots, and the last arrow books its exit rows for a count of one — the
// stack as it stood when deplete took it, not after.
//
// #110 corrected the opposite failure — not a missing booking but a phantom
// one. The split piece a rejected Give frees was `discarded`, which books a
// destroyed flow for the piece's whole count, even though the refusal had
// just restored every unit to the giver's stack: an exit in the ledger, no
// exit in the world, mintable by handing part of a stack to any NPC that
// cannot receive it. The reject check below drives the real handler against
// a synthetic refuser and asserts the round trip books nothing at all.
//
// #111 closed the product side of the loop #105 opened. Reagents booked their
// Make exits while a stackable product entered the world unbooked — created by
// the factory, handed to the inventory, no flow on the whole path — so a
// crafted batch eventually exited a world it never entered. The factory cannot
// book it (every venue sets the count after create_item returns, the same #81
// arithmetic the trap note below describes), so the venues book through one
// named door, record_created_flow, once the count is real. The checks below
// drive that door with the shapes that matter: a finished batch books
// `created` for the batch quantity and books it exactly once — the husk its
// merge frees adds nothing — while an Instanced product and a factory-fresh
// count of 0 book no row at all.
//
// What these checks do not prove, and cannot cheaply: that each VENUE names the
// right route. The parameter is required rather than defaulted, so a venue
// cannot omit a transition — that much is a compile error — but a venue naming
// `Buy` where it meant `Repair` is a review matter, and the venue-to-number
// table is in PLANS/ItemLedger_Plan.md for that review to check against.
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
#include "Map.h"
#include "Npc.h"
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

	// The purse the outflow checks (#103) spend from, and the three payments it
	// makes. On a SECOND stackable id, because spending a stack to nothing books
	// the same exit rows the checks above measure exactly.
	constexpr int64_t purse_qty    = 211;
	constexpr int64_t buy_qty      = 29;
	constexpr int64_t repair_qty   = 31;
	constexpr int64_t study_qty    = 37;
	constexpr int64_t refund_qty   = 41;   // a count that RISES, which books nothing

	// The reagent stack the slot-keyed checks (#105) consume, on a THIRD
	// stackable id for the reason the purse is on a second: emptying it books
	// exit rows, and a shared id would add them to another group's total.
	constexpr int64_t reagent_qty   = 101;
	constexpr int64_t consume_qty   = 43;   // a partial decrement, which must book Make
	constexpr int64_t elsewhere_qty = 47;   // a decrease whose venue already booked (flow_none)
	constexpr int64_t rise_qty      = 53;   // a count that RISES, which books nothing
	constexpr int64_t applied_qty   = 3;    // a decrease the caller applied first (delta 0)

	// The quiver the ammunition checks (#109) fire dry, on a FOURTH stackable
	// id: its last arrow books exit rows, and a shared id would land them in
	// another group's total. One more unused prime, so a sum that mixed groups
	// cannot land right by coincidence.
	constexpr int64_t quiver_qty    = 59;

	// The stack a rejected Give (#110) splits and gets back, on a FIFTH id kept
	// clear of every group above — its assertion is that the id books NO row of
	// any type, which only a virgin id can make. Two more unused primes.
	constexpr int64_t reject_stack_qty = 67;
	constexpr int64_t reject_give_qty  = 61;

	// The finished batch a craft venue books (#111), on a SIXTH id so its
	// `created` inflow is the only row the id holds. One more unused prime.
	constexpr int64_t craft_batch_qty = 71;

	// Stored numbers as literals. These are permanent world fact the moment real
	// players exist, and a check that queried with the same symbol it stored
	// would still pass if both were renumbered together.
	constexpr int drop_flow_number     = 2;
	constexpr int get_flow_number      = 3;
	constexpr int buy_flow_number      = 7;
	constexpr int sell_flow_number     = 8;
	constexpr int make_flow_number     = 13;
	constexpr int study_flow_number    = 16;
	constexpr int repair_flow_number   = 17;
	constexpr int use_flow_number      = 32;
	constexpr int retrieve_event_number = 9;
	constexpr int deposit_event_number  = 10;
	constexpr int give_event_number     = 1;
	constexpr int despawned_number      = 101;
	constexpr int destroyed_number      = 102;
	constexpr int created_number        = 100;
	static_assert(static_cast<int>(ItemLogAction::Drop) == drop_flow_number,
		"ItemLogAction::Drop moved: every drop flow already written means something else now.");
	static_assert(static_cast<int>(ItemLogAction::get) == get_flow_number,
		"ItemLogAction::get moved: every pickup flow already written means something else now.");
	static_assert(static_cast<int>(ItemLogAction::Sell) == sell_flow_number,
		"ItemLogAction::Sell moved: every shop-sale flow already written means something else now.");
	static_assert(static_cast<int>(ItemLogAction::Buy) == buy_flow_number,
		"ItemLogAction::Buy moved: every gold-spent-at-a-shop flow means something else now.");
	static_assert(static_cast<int>(ItemLogAction::MagicLearn) == study_flow_number,
		"ItemLogAction::MagicLearn moved: every spell-payment flow means something else now.");
	static_assert(static_cast<int>(ItemLogAction::Make) == make_flow_number,
		"ItemLogAction::Make moved: every reagent-consumption flow means something else now.");
	static_assert(static_cast<int>(ItemLogAction::Repair) == repair_flow_number,
		"ItemLogAction::Repair moved: every repair-bill flow already written means something else now.");
	static_assert(static_cast<int>(ItemLogAction::Use) == use_flow_number,
		"ItemLogAction::Use moved: every ammunition-consumption flow means something else now.");
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
	static_assert(static_cast<int>(ledger_event::created) == created_number,
		"ledger_event::created moved: every mint flow already written means something else now.");

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

	// A second stackable for the outflow checks (#103): spending a purse to
	// nothing books the exit rows the merge checks measure to the coin, so the
	// two groups cannot share an item id.
	const int spend_id = find_probe_item(game, true, counted_id + 1);

	// And a third for the slot-keyed checks (#105), disjoint from both groups
	// above for the reason the purse is disjoint from the first.
	const int craft_id = find_probe_item(game, true, spend_id + 1);

	// And a fourth for the ammunition checks (#109), disjoint again.
	const int arrow_id = find_probe_item(game, true, craft_id + 1);

	// And a fifth for the rejected Give (#110), disjoint so that "books no row
	// at all" stays a statement about the reject path alone.
	const int reject_id = find_probe_item(game, true, arrow_id + 1);

	// A sixth for the created inflow (#111), and a seventh that must end the
	// run virgin: it takes the no-book shape (a factory-fresh count of 0
	// through the created door), and "books nothing" is a statement only a
	// clean id can make.
	const int craftout_id = find_probe_item(game, true, reject_id + 1);
	const int blank_id = find_probe_item(game, true, craftout_id + 1);
	if (items.is_valid_item_id(instanced_id) == false
		|| items.is_valid_item_id(counted_id) == false
		|| items.is_valid_item_id(spend_id) == false
		|| items.is_valid_item_id(craft_id) == false
		|| items.is_valid_item_id(arrow_id) == false
		|| items.is_valid_item_id(reject_id) == false
		|| items.is_valid_item_id(craftout_id) == false
		|| items.is_valid_item_id(blank_id) == false)
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
	// Outflows: the sink half of the currency loop (#103).
	//
	// Money entering the world was booked from the start; money leaving a
	// player's hands to an NPC was not, because a payment reduces the stack
	// through set_item_count_by_id and that call carried no transition. So a
	// purchase, a repair bill and a spell were invisible while every inflow was
	// counted, and "is the money supply diverging" had only one side.
	//
	// Driven through set_item_count_by_id rather than through record_flow, for
	// the same reason #81 drove its Warehouse checks through the real handlers:
	// calling the recorder proves only that the recorder records. What is at
	// issue is whether the funnel books what it removes.
	//
	// One purse, spent by all three routes. It survives every payment but the
	// last, so the exits it eventually books land on `spend_id` and leave the
	// merge checks above measuring their own rows.
	//----------------------------------------------------------------------

	// Slot 0 of a synthetic client, the same placement the other provers use
	// (place_in_inventory, CmdAtomicCheck.cpp) — a probe_client starts with every
	// slot null and nothing above this fills one. Not add_client_item_list: that
	// door weighs the stack against the carrier's max load, and a probe has no
	// strength or level, so a purse worth carrying never fits. The purse is setup
	// for the checks below, not evidence for them — what is under test is
	// set_item_count_by_id — so buying the real door's weight formula here would
	// only make the outcome depend on balance data.
	//
	// The inventory owns it from here; the probe_client destructor frees whatever
	// is left in its item list, as it does for the banked probe below.
	CClient* carrier = game->m_client_list[actor_h];
	const bool slot_was_free = (carrier->m_item_list[0] == nullptr);
	if (slot_was_free) carrier->m_item_list[0] = counted_probe(items, spend_id, purse_qty);

	// Asserted rather than assumed: a check added above that parks something in
	// slot 0 would otherwise leave these checks measuring the wrong stack.
	const bool purse_held = slot_was_free && carrier->m_item_list[0] != nullptr;

	// Absolute counts, because that is what the call takes and what the checks
	// below are written against. Deriving them by subtraction would leave a
	// reader hand-simulating a sequence to learn what any one payment booked.
	constexpr int64_t after_buy    = purse_qty - buy_qty;      // 211 - 29 = 182
	constexpr int64_t after_repair = after_buy - repair_qty;   // 182 - 31 = 151
	constexpr int64_t after_study  = after_repair - study_qty; // 151 - 37 = 114
	constexpr int64_t last_coins   = after_study + refund_qty; // 114 + 41 = 155

	const short spend_item = static_cast<short>(spend_id);

	items.set_item_count_by_id(actor_h, spend_item, after_buy, ItemLogAction::Buy);
	items.set_item_count_by_id(actor_h, spend_item, after_repair, ItemLogAction::Repair);
	items.set_item_count_by_id(actor_h, spend_item, after_study, ItemLogAction::MagicLearn);

	// A count that RISES. Nothing may be booked: quantities are positive and
	// direction is a property of the number, so a gain has no outflow to record
	// — the venue that granted it books its own inflow, as a shop sale does.
	items.set_item_count_by_id(actor_h, spend_item, last_coins, ItemLogAction::Buy);

	// And the purse spent to the last coin. The stack empties, so this also
	// books Deplete and destroyed for the whole remainder — the route flow still
	// has to fire, or "gold spent on repairs" would omit exactly the repairs
	// that cleaned a player out.
	items.set_item_count_by_id(actor_h, spend_item, 0, ItemLogAction::Repair);

	//----------------------------------------------------------------------
	// The slot-keyed sibling (#105): reagents consumed by crafting.
	//
	// set_item_count had the hole its by-id sibling above was cured of: it
	// booked only when a stack emptied, so a recipe taking 10 reagents out of
	// a stack of 50 recorded nothing. Driven through set_item_count for the
	// reason the purse drives through set_item_count_by_id — what is at issue
	// is whether the funnel books what it removes.
	//
	// One reagent stack in slot 1 of the same carrier, walked through every
	// caller shape: a real consumption (Make), a decrease whose venue already
	// booked it (flow_none, the shop-sale shape), a count that rises (the
	// Warehouse-merge shape), a decrease the caller applied before the call
	// (a shape no live caller takes since #105 converted the splits to pass
	// targets — pinned here), and the consumption that empties the stack.
	//----------------------------------------------------------------------

	const bool reagent_slot_free = (carrier->m_item_list[1] == nullptr);
	if (reagent_slot_free) carrier->m_item_list[1] = counted_probe(items, craft_id, reagent_qty);
	const bool reagent_held = reagent_slot_free && carrier->m_item_list[1] != nullptr;

	// Absolute counts, as with the purse above.
	constexpr int64_t after_consume   = reagent_qty - consume_qty;       // 101 - 43 = 58
	constexpr int64_t after_elsewhere = after_consume - elsewhere_qty;   // 58 - 47 = 11
	constexpr int64_t after_rise      = after_elsewhere + rise_qty;      // 11 + 53 = 64
	constexpr int64_t after_applied   = after_rise - applied_qty;        // 64 - 3 = 61

	items.set_item_count(actor_h, 1, after_consume, ItemLogAction::Make);
	items.set_item_count(actor_h, 1, after_elsewhere, ItemManager::flow_none);
	items.set_item_count(actor_h, 1, after_rise, ItemLogAction::Make);

	// The pre-applied shape: a caller that mutates the count first and passes
	// the already-current value gets a delta of zero, so even a real flow type
	// books nothing. No live caller takes this shape — #105 converted the
	// splits that did to pass targets — but the funnel's answer to it is
	// permanent arithmetic, so it stays pinned in case one reappears.
	if (reagent_held) carrier->m_item_list[1]->m_instance.count = static_cast<uint64_t>(after_applied);
	items.set_item_count(actor_h, 1, after_applied, ItemLogAction::Make);

	// And the consumption that empties the stack: Make for the remainder,
	// beside the exit rows every emptied stack books.
	items.set_item_count(actor_h, 1, 0, ItemLogAction::Make);

	//----------------------------------------------------------------------
	// Ammunition (#109): the last hand-rolled copy of the setter, folded in.
	//
	// Arrow consumption in CombatManager reimplemented the body inline —
	// decrement, deplete at zero, notify otherwise — minus the booking, and
	// with the decrement BEFORE the deplete call, so the quiver-emptying shot
	// handed the exit bookings a count-0 stack and the zero-quantity guard
	// dropped them: an emptied quiver left neither a Deplete row nor a
	// destroyed row. Driven through set_item_count with the caller's real
	// shape — one arrow per shot, target count, `Use`, dry to the last arrow —
	// for the reason the reagent checks drive through it: what is at issue is
	// the door's own booking under the ammunition number. The combat caller's
	// own choices — the sweep-shot exclusion, the notify byte (wire-only, so
	// no check can read it), the weight recalc — are reviewed by reading.
	//----------------------------------------------------------------------

	const bool quiver_slot_free = (carrier->m_item_list[2] == nullptr);
	if (quiver_slot_free) carrier->m_item_list[2] = counted_probe(items, arrow_id, quiver_qty);
	const bool quiver_held = quiver_slot_free && carrier->m_item_list[2] != nullptr;

	// Every shot removes exactly one, which is what makes the accumulated Use
	// row equal the quiver: a door that booked the target count would sum to
	// 1711, one that booked what the stack held to 1770 — both off-total. The
	// last iteration passes 0 and empties the stack, arrow in hand. The false
	// is the combat site's notify byte, passed for fidelity only — it never
	// reaches the ledger.
	for (int64_t left = quiver_qty; left > 0; --left)
		items.set_item_count(actor_h, 2, static_cast<uint64_t>(left - 1),
			ItemLogAction::Use, false);

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
	// The rejected Give (#110): the exit that never happened.
	//
	// give_item_handler splits off a piece, the NPC refuses, the stack is
	// restored to its full count, and the piece is freed. Until #110 that
	// free said `discarded` — a destroyed flow for the piece's whole count,
	// booked while the player kept every unit, so the exit totals drifted
	// upward with ordinary play (hand five arrows to a shopkeeper). The
	// honest reason is `merged`: the piece's contents live on in the stack
	// they were restored to, the exact shape the merge exclusion was built
	// for.
	//
	// Driven through the REAL give_item_handler, for the reason the deposit
	// drives through set_item_to_bank_item: what is at issue is the reason
	// the call site passes, and only the door knows it. On the fifth id the
	// assertion can be the sharpest in the file — NO flow row of any type —
	// and every wrong turn through the handler breaks it: a revert to
	// `discarded` books destroyed, missing the refuser drops the piece on
	// the ground and books Drop. The handler's early returns are excluded by
	// construction (the probe is init-complete, the slot holds a stackable,
	// the amount is positive and less than the stack).
	//
	// The refuser is synthetic, like the probe clients: a bare CNpc in a
	// free slot, parked on the target tile and removed after. The reject
	// branch reads its name and its config id — anything but the Warehouse
	// Keeper's 58 refuses — and never needs the spawn machinery. Last of
	// the drives, so the tile juggling can disturb nothing after it; the
	// tile's prior owner is put back, as the atomicity prover's Give does,
	// because map 0 is live.
	//----------------------------------------------------------------------

	// Slot 3: slot 0 is taken again by now — the Warehouse retrieve above
	// lands its item in the first free slot, which the emptied purse had
	// left open.
	const bool reject_slot_free = (carrier->m_item_list[3] == nullptr);
	if (reject_slot_free) carrier->m_item_list[3] = counted_probe(items, reject_id, reject_stack_qty);
	const bool reject_held = reject_slot_free && carrier->m_item_list[3] != nullptr;

	int refuser_h = 0;
	if (reject_held && game->m_npc_list != nullptr)
		for (int i = 1; i < hb::server::config::MaxNpcs; i++)
			if (game->m_npc_list[i] == nullptr) { refuser_h = i; break; }

	CMap* reject_map = game->m_map_list[0];
	int64_t reject_count_after = -1;

	if (reject_held && refuser_h != 0 && reject_map != nullptr)
	{
		// A CClient starts at map index -1; this is the one drive that walks
		// a path dereferencing m_map_list[m_map_index], so it gets a real
		// map (the atomicity prover's make_playable shape). Not restored:
		// unlike the tile below, the probe dies with the run.
		carrier->m_map_index = 0;

		game->m_npc_list[refuser_h] = new CNpc("Probe");
		// The constructor's -1 would already refuse; set explicitly so the
		// check cannot come to hang on a constructor default.
		game->m_npc_list[refuser_h]->m_npc_config_id = 1;

		const short reject_x = static_cast<short>(actor_x + 1);
		const short reject_y = static_cast<short>(actor_y);

		short prior_owner = 0;
		char prior_class = 0;
		reject_map->get_owner(&prior_owner, &prior_class, reject_x, reject_y);
		reject_map->set_owner(static_cast<short>(refuser_h),
			hb::shared::owner_class::Npc, reject_x, reject_y);

		items.give_item_handler(actor_h, 3, static_cast<int>(reject_give_qty),
			reject_x, reject_y, 0, nullptr);

		reject_map->set_owner(prior_owner, prior_class, reject_x, reject_y);
		delete game->m_npc_list[refuser_h];
		game->m_npc_list[refuser_h] = nullptr;

		if (carrier->m_item_list[3] != nullptr)
			reject_count_after = static_cast<int64_t>(carrier->m_item_list[3]->m_instance.count);
	}

	//----------------------------------------------------------------------
	// The created inflow (#111): a finished stackable enters the world.
	//
	// Driven through record_created_flow — the door itself, as the outflow
	// checks drive set_item_count_by_id — in the sequence every venue runs:
	// the two-argument door sets the batch count on the factory-fresh item
	// and books, the product is handed on, the merge husk freed `merged`.
	// The husk free is the case the ticket pins: the venue booked before the
	// merge, so the free must add nothing, or every crafted batch that landed
	// on an existing stack would enter the world twice.
	//----------------------------------------------------------------------

	CItem* crafted_raw = items.create_item(craftout_id, item_origin::craft);
	const bool crafted_made = (crafted_raw != nullptr);
	if (crafted_made)
	{
		items.record_created_flow(*crafted_raw, static_cast<uint64_t>(craft_batch_qty));
		items.destroy_item(crafted_raw, destroy_reason::merged, actor_h);
	}

	// The two no-book shapes. A venue that calls before setting the count
	// holds the factory-fresh 0, and the zero-quantity guard keeps the row out
	// — the same arithmetic that keeps a husk merge silent. An Instanced
	// product declines on its serial: its birth is the factory's created
	// EVENT, and a flow beside it would record the same birth twice in two
	// shapes. The Instanced drive has no tally line of its own — it runs
	// before the flush, so instanced_books_no_flow below is its assertion,
	// now covering the created door along with every older route.
	item_ptr countless{ items.create_item(blank_id, item_origin::craft) };
	if (countless != nullptr) items.record_created_flow(*countless);
	items.record_created_flow(*instanced);

	//----------------------------------------------------------------------
	// Flush and read the columns a forensic query would read.
	//----------------------------------------------------------------------

	ledger.flush(items.serial_high_water());
	sqlite3* db = ledger.handle();

	const int32_t day = today_key();

	auto flow_qty = [&](int flow_type)
	{
		return hb::server::flow_scalar(db, "qty", day, counted_id, flow_type);
	};
	auto flow_rows = [&](int flow_type)
	{
		return hb::server::flow_scalar(db, "COUNT(*)", day, counted_id, flow_type);
	};
	auto spend_flow_qty = [&](int flow_type)
	{
		return hb::server::flow_scalar(db, "qty", day, spend_id, flow_type);
	};
	auto spend_flow_rows = [&](int flow_type)
	{
		return hb::server::flow_scalar(db, "COUNT(*)", day, spend_id, flow_type);
	};
	auto craft_flow_qty = [&](int flow_type)
	{
		return hb::server::flow_scalar(db, "qty", day, craft_id, flow_type);
	};
	auto craft_flow_rows = [&](int flow_type)
	{
		return hb::server::flow_scalar(db, "COUNT(*)", day, craft_id, flow_type);
	};
	auto arrow_flow_qty = [&](int flow_type)
	{
		return hb::server::flow_scalar(db, "qty", day, arrow_id, flow_type);
	};
	auto arrow_flow_rows = [&](int flow_type)
	{
		return hb::server::flow_scalar(db, "COUNT(*)", day, arrow_id, flow_type);
	};
	// Any flow row at all for an id, whatever its day or type. Two callers,
	// two different claims of total absence: the Instanced probe must never
	// reach the aggregate table, and the rejected Give's id must end the run
	// virgin.
	auto any_flow_rows = [&](int item_id)
	{
		return probe_scalar(db, std::format(
			"SELECT COUNT(*) FROM item_flows WHERE item_id={}", item_id).c_str());
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
	// Outflows (#103)
	//----------------------------------------------------------------------

	tally.record("purse_held", purse_held);

	// The headline: a payment to an NPC is recorded at all. Before this ticket
	// set_item_count_by_id carried no transition and this row did not exist.
	tally.record("outflow_books_flow", spend_flow_rows(buy_flow_number) == 1);

	// Read once and asserted twice — the purchase row is what both the quantity
	// check and the gain check below turn on, and nothing writes to it between
	// them (every row reaches disk in the single flush above).
	const int64_t booked_buy = spend_flow_qty(buy_flow_number);

	// ...and it books what LEFT. The two numbers a wrong implementation reaches
	// for are both present and both wrong: the count after the payment
	// (after_buy) and the stack it came out of (purse_qty).
	tally.record("outflow_books_what_left", booked_buy == buy_qty);

	// One number per route, so the three payments are three rows. This is the
	// half that makes the ledger answer "where does the money go" rather than
	// only "how much of it is gone".
	tally.record("outflow_routes_stay_distinct",
		spend_flow_qty(study_flow_number) == study_qty
		&& spend_flow_rows(repair_flow_number) == 1);

	// The stored number is READ BACK rather than filtered on, for the reason the
	// pickup check above gives: every other query here confirms its own premise.
	// Keyed on the spell payment, whose quantity is unique on this item id.
	tally.record("outflow_number_is_the_action_number",
		probe_scalar(db, std::format(
			"SELECT flow_type FROM item_flows WHERE day={} AND item_id={} AND qty={}",
			day, spend_id, study_qty).c_str()) == study_flow_number);

	// A count that rose added nothing. The whole sign convention rests on this:
	// direction is a property of the number, so an outflow recorder that booked
	// a gain would be recording a sink that never happened.
	tally.record("gain_books_no_outflow", booked_buy == buy_qty);

	// Quantities are positive everywhere, in every row the run produced — the
	// convention stated once, checked against the whole file rather than against
	// the rows this ticket happens to write.
	tally.record("no_flow_is_negative",
		probe_scalar(db, "SELECT COUNT(*) FROM item_flows WHERE qty<0") == 0);

	// Spending the last coin still books the route. The stack empties on that
	// payment, which sends it down item_deplete_handler — a path that books its
	// own exit rows and would happily have swallowed the payment with them.
	tally.record("emptied_purse_books_route",
		spend_flow_qty(repair_flow_number) == repair_qty + last_coins);

	// ...and the exit rows are still there beside it. They answer a different
	// question — the slot emptied, the item left the world — and a reader adding
	// them to the route numbers would count the same coins twice, which is why
	// this is asserted rather than left to be discovered.
	tally.record("emptied_purse_books_exit_too",
		spend_flow_qty(destroyed_number) == last_coins);

	//----------------------------------------------------------------------
	// The slot-keyed sibling (#105)
	//----------------------------------------------------------------------

	tally.record("reagent_held", reagent_held);

	// The headline: a partial consumption is recorded at all. Before this
	// ticket set_item_count booked only the stack-emptying case, and this row
	// did not exist.
	tally.record("reagent_consumption_books_flow",
		craft_flow_rows(make_flow_number) == 1);

	// Read once, asserted under three names below; every row reached disk in
	// the single flush above, so the three share one fact.
	const int64_t booked_make = craft_flow_qty(make_flow_number);

	// It books the DECREASE, both times the count fell by this route — the
	// partial consumption and the one that emptied the stack. The numbers a
	// wrong implementation lands on are all distinct from 104: booking the
	// target count gives 58, booking what the stack held gives 162, and each
	// of the three no-book shapes below adds its own prime.
	tally.record("reagent_books_the_decrease",
		booked_make == consume_qty + after_applied);

	// flow_none books nothing even though the count fell — the shop-sale
	// shape, whose Sell already carried the amount. Nothing may land under 0:
	// the sentinel is a statement about the call site, never a stored number.
	tally.record("flow_none_books_nothing", craft_flow_rows(0) == 0);

	// A rise books nothing, as with the purse's refund: had it booked, Make
	// would be 53 (or 11) high.
	tally.record("reagent_gain_books_nothing",
		booked_make == consume_qty + after_applied);

	// A decrease the caller already applied books nothing more — the delta
	// reads zero. Nothing in the tree takes this shape since #105 converted
	// the splits to pass targets; had the pass-through booked, Make would be
	// 3 high.
	tally.record("applied_decrease_books_nothing",
		booked_make == consume_qty + after_applied);

	// The emptying consumption books its exit rows beside the route, exactly
	// as the purse's last payment does.
	tally.record("emptied_reagent_books_exit_too",
		craft_flow_qty(destroyed_number) == after_applied);

	//----------------------------------------------------------------------
	// Ammunition (#109)
	//----------------------------------------------------------------------

	tally.record("quiver_held", quiver_held);

	// The headline: a fired arrow is recorded at all. Before this ticket the
	// combat site booked nothing anywhere, and ammunition — the highest-volume
	// Counted consumable in the game — was absent from the consumption picture
	// the tier had for reagents and gold.
	tally.record("fired_arrow_books_use", arrow_flow_rows(use_flow_number) == 1);

	// Fifty-nine single-arrow decreases accumulate to the quiver, in one row.
	tally.record("arrow_shots_accumulate",
		arrow_flow_qty(use_flow_number) == quiver_qty);

	// The stored number is READ BACK rather than filtered on, as with the
	// pickup and the spell payment above. Keyed on the accumulated Use row,
	// whose quantity is unique on this item id.
	tally.record("use_number_is_the_action_number",
		probe_scalar(db, std::format(
			"SELECT flow_type FROM item_flows WHERE day={} AND item_id={} AND qty={}",
			day, arrow_id, quiver_qty).c_str()) == use_flow_number);

	// The last arrow books its exit rows for a count of ONE — the stack as it
	// stood when deplete took it. The hand-rolled copy decremented first, so
	// deplete read a count of 0, the zero-quantity guard dropped both bookings,
	// and an emptied quiver was the one emptied stack in the game with no
	// destroyed row. A door that regressed to that shape books no row at all
	// here, not a row of the wrong size.
	tally.record("emptied_quiver_books_exit_too",
		arrow_flow_qty(destroyed_number) == 1);

	//----------------------------------------------------------------------
	// The rejected Give (#110)
	//----------------------------------------------------------------------

	tally.record("reject_probe_held", reject_held);

	// The player kept the stack: the refusal restored every unit the split
	// took out. Read from the slot, not assumed — a drive that never reached
	// the reject branch would leave a different number here (6 if the piece
	// was never restored, -1 if the slot emptied) before it could leave the
	// right one.
	tally.record("rejected_give_returns_the_stack",
		reject_count_after == reject_stack_qty);

	// And the ledger recorded NOTHING for the whole round trip: not the
	// split (flow_none), not the restore (a rise), not the freed piece
	// (merged). Until #110 this id held a destroyed flow of reject_give_qty
	// — an exit row for items still in the giver's slot, which is exactly
	// the drift this ticket exists to stop.
	tally.record("rejected_give_books_nothing", any_flow_rows(reject_id) == 0);

	//----------------------------------------------------------------------
	// The created inflow (#111)
	//----------------------------------------------------------------------

	tally.record("crafted_probe_made", crafted_made);

	// The headline: a finished stackable's entry is recorded at all. Before
	// this ticket the shop-sale payout was the only created flow in the
	// server, and a crafted batch entered the world without a row.
	tally.record("crafted_stack_books_created",
		hb::server::flow_scalar(db, "COUNT(*)", day, craftout_id, created_number) == 1);

	// ...for the batch quantity, exactly once. The drive freed the merge husk
	// after booking, so a door that booked the free again lands on 142, and a
	// venue that booked the factory default instead of the batch lands on no
	// row at all.
	tally.record("crafted_stack_books_the_batch",
		hb::server::flow_scalar(db, "qty", day, craftout_id, created_number) == craft_batch_qty);

	// The count-first contract: a factory-fresh 0 through the created door
	// books nothing, so a venue that forgets the count reproduces the hole
	// this ticket closed rather than filing zeros under it. The id must end
	// the run untouched by ANY row for the same reason the rejected Give's
	// must. The created door's other no-op — an Instanced item — has no line
	// here: its drive ran above, so instanced_books_no_flow below carries it.
	tally.record("factory_count_books_no_created", any_flow_rows(blank_id) == 0);

	//----------------------------------------------------------------------
	// Instanced tier unaffected
	//----------------------------------------------------------------------

	tally.record("instanced_item_has_serial", instanced_serialed);
	tally.record("instanced_books_event", event_count(drop_flow_number, instanced_serial) == 1);

	// The other half of the boundary, and the one a naive implementation gets
	// wrong: an item with identity must never also be counted in aggregate, or
	// every Instanced transition is recorded twice in two different shapes.
	// Since #111 the routes under this assertion include the created door —
	// the drive above pushed the Instanced probe through record_created_flow.
	tally.record("instanced_books_no_flow", any_flow_rows(instanced_id) == 0);
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
