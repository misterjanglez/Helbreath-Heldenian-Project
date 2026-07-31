// CmdItemLogCheck.cpp: dual-sink prover (Item Provenance Ledger P2.2, #78)
//
// #78 did not add a logging call anywhere. It changed what the two existing
// item_log() overloads do, which means the way it fails is not a crash or a
// missing line — it is a ledger that looks complete and quietly is not. Three
// specific holes are possible and none of them are visible by reading a log
// file:
//
//   - The text sink drops items check_good_item() does not consider worth a
//     line. That whitelist is roughly sixty ids; if the ledger inherited it,
//     the audit trail would be missing exactly the ordinary items most disputes
//     are about, and the file would still look busy.
//   - The text sink returns early on an action its switch does not name and on
//     a client handle that no longer resolves. Both are ordinary in a running
//     world, and an event lost that way leaves a Serial whose biography skips a
//     custody change — indistinguishable from a dupe afterwards.
//   - GmMint's second argument is the minted quantity, not a handle. Reading it
//     as a counterparty would either name the wrong player in the audit trail
//     or index the client array with a count.
//
// So this proves the payload rather than the plumbing: that the rows land, and
// that each column says what a forensic query will later assume it says.
//
// The run redirects the ledger sink to a scratch database and restores it on
// every exit path, so the live itemledger.db gains nothing — an append-only
// audit log cannot be cleaned up after a test, which is why the sink moves
// instead of the rows being deleted. Two synthetic clients are installed in
// free handles for the duration: without an actor, "the actor is recorded"
// could only be tested through its NULL path, which is the half that does not
// matter. The Serials this mints and the text-channel lines it writes are left
// where they fall — both are the sinks doing their job.
//
// The machine-readable lines carry no serials, ids, timestamps or row counts:
// the Linux gate compares them against the Windows run byte for byte.
//
// Design contract: PLANS/ItemLedger_Plan.md P2.2, docs/adr/0003-item-provenance-ledger.md.
//
//////////////////////////////////////////////////////////////////////

#include "CmdItemLogCheck.h"

#include "CheckTally.h"
#include "Client.h"
#include "Game.h"
#include "Item.h"
#include "ItemLedgerStore.h"
#include "ItemManager.h"
#include "ItemProvenance.h"
#include "ServerConsole.h"
#include "ServerMessages.h"
#include "sqlite3.h"

#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <string>

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

namespace
{
	constexpr const char* probe_db = "itemlogcheck_probe.db";

	constexpr const char* actor_account = "probeacct";
	constexpr const char* actor_char    = "ProbeOne";
	constexpr const char* other_char    = "ProbeTwo";
	constexpr const char* probe_map     = "probemap";
	constexpr int probe_x = 101;
	constexpr int probe_y = 202;

	// A quote inside the NPC name on purpose: the detail column is JSON, and one
	// unescaped quote from a content file makes a row no reader can parse.
	constexpr const char* probe_npc_expected = "{\"npc\":\"Probe\\\"Slime\"}";

	// ItemLogAction::Drop's stored number, written out as a literal rather than
	// taken from the enum. The passthrough check reads this back out of the file,
	// and a check that queried with the same enum it recorded with would still
	// pass if both sides were renumbered together — which is the one thing that
	// must not happen, because stored event_type values are permanent world fact
	// once real players exist (ItemLedgerStore.h).
	constexpr int drop_event_number = 2;
	static_assert(static_cast<int>(ItemLogAction::Drop) == drop_event_number,
		"ItemLogAction::Drop moved: every ledger row already written means something else now.");

}

void CmdItemLogCheck::execute(CGame* game, const char* args)
{
	(void)args;

	check_tally tally("ITEMLOGCHECK", "itemlogcheck");

	if (game == nullptr || game->m_item_manager == nullptr
		|| game->m_item_ledger_store == nullptr || !game->m_item_ledger_store->is_open())
	{
		hb::console::error("itemlogcheck: the live ledger is not open.");
		return;
	}

	ItemManager& items = *game->m_item_manager;

	const int instanced_id = find_probe_item(game, false);
	const int counted_id = find_probe_item(game, true);
	const int unlogged_id = hb::server::find_unlogged_probe_item(game);
	if (items.is_valid_item_id(instanced_id) == false
		|| items.is_valid_item_id(counted_id) == false
		|| items.is_valid_item_id(unlogged_id) == false)
	{
		hb::console::error("itemlogcheck: no usable probe item ids.");
		return;
	}

	const int actor_h = find_free_handle(game, 1);
	const int other_h = actor_h > 0 ? find_free_handle(game, actor_h + 1) : -1;
	const int absent_h = other_h > 0 ? find_free_handle(game, other_h + 1) : -1;
	if (G_pIOPool == nullptr || actor_h < 0 || other_h < 0 || absent_h < 0)
	{
		hb::console::error("itemlogcheck: no free client handles for the probes.");
		return;
	}

	//----------------------------------------------------------------------
	// Redirect the sink. Everything below records into the scratch file.
	//----------------------------------------------------------------------

	hb::server::ledger_sink_swap swap(game, probe_db);
	if (!swap.ok())
	{
		hb::console::error("itemlogcheck: could not create the scratch ledger '{}'.", probe_db);
		return;
	}
	item_ledger_store& ledger = swap.scratch();

	const probe_client actor(game, actor_h, actor_account, actor_char, probe_map, probe_x, probe_y);
	const probe_client other(game, other_h, actor_account, other_char, probe_map, probe_x, probe_y);

	//----------------------------------------------------------------------
	// Creation. The Counted half is observed in the buffer, because "nothing
	// was recorded" is the one thing a query of the file cannot tell apart
	// from "the flush is still pending".
	//----------------------------------------------------------------------

	using item_ptr = std::unique_ptr<CItem>;

	item_ptr subject{ items.create_item(instanced_id, item_origin::npc_drop) };

	// A Counted item has no identity to be born with (D2), so the factory funnel
	// must stay silent for it rather than claim an instance stack merges dissolve.
	const size_t before_counted = ledger.pending_count();
	item_ptr counted{ items.create_item(counted_id, item_origin::shop_buy) };
	const bool counted_silent = ledger.pending_count() == before_counted;

	item_ptr unlogged{ items.create_item(unlogged_id, item_origin::npc_drop) };
	if (subject == nullptr || counted == nullptr || unlogged == nullptr)
	{
		hb::console::error("itemlogcheck: create_item failed for a probe item.");
		return;
	}

	// ...and stays silent for its transitions too, for the same reason.
	const size_t before_counted_event = ledger.pending_count();
	items.item_log(ItemLogAction::Drop, actor_h, -1, counted.get());
	const bool counted_event_silent = ledger.pending_count() == before_counted_event;

	//----------------------------------------------------------------------
	// The transitions. Return values are captured where the TEXT sink is
	// expected to decline, because "the ledger recorded it anyway" is only
	// evidence if the thing it recorded past actually happened.
	//----------------------------------------------------------------------

	const bool drop_logged = items.item_log(ItemLogAction::Drop, actor_h, -1, subject.get());
	const bool give_logged = items.item_log(ItemLogAction::Give, actor_h, other_h, subject.get());

	// GmMint's second handle is the minted quantity. Passing a live client handle
	// as that quantity is the sharp version of this check: if the special case is
	// ever "tidied up", the counterparty column fills in with a real name instead
	// of staying NULL, and this goes red rather than looking plausible.
	const bool mint_logged = items.item_log(ItemLogAction::GmMint, actor_h, other_h, subject.get());

	// An action the text switch has no case for. It returns false, and the
	// transition still happened.
	const bool unknown_logged = items.item_log(ItemLogAction::Use, actor_h, 0, subject.get());

	// A handle that resolves to nobody — an ordinary race in a running world.
	const bool absent_logged = items.item_log(ItemLogAction::Deplete, absent_h, 0, subject.get());

	// The name overload: an NPC drop, then the same call for an item the text
	// whitelist skips. (Whether the subject item is itself on that whitelist is a
	// content question, so only the second one asserts a return value.)
	char npc_name[] = "Probe\"Slime";
	items.item_log(ItemLogAction::NewGenDrop, 0, npc_name, subject.get());
	const bool unlogged_logged = items.item_log(ItemLogAction::NewGenDrop, 0, npc_name, unlogged.get());

	if (!ledger.flush(0))
	{
		hb::console::error("itemlogcheck: could not flush the scratch ledger.");
		return;
	}

	//----------------------------------------------------------------------
	// Read the window back and check what each column says.
	//----------------------------------------------------------------------

	sqlite3* db = ledger.handle();

	// Every check below reads one projection off one event of one Serial, which
	// is what event_scalar/event_text are; these only add this prover's default
	// of "the subject item". The serials are predicates, never output — the
	// machine lines have to compare across platforms.
	auto number_of = [&](const std::string& expr, int type, int64_t serial = 0)
	{
		return hb::server::event_scalar(db, expr.c_str(), type,
			serial != 0 ? serial : subject->m_serial);
	};
	auto count_of = [&](int type, int64_t serial = 0)
	{
		return number_of("COUNT(*)", type, serial);
	};
	auto text_of = [&](const char* column, int type)
	{
		return hb::server::event_text(db, column, type, subject->m_serial);
	};
	auto is_null = [&](const char* column, int type)
	{
		return number_of(std::string(column) + " IS NULL", type) == 1;
	};

	// A birth row and its creation event are one fact and arrive together. A
	// biography that opens with a custody transfer and no creation is exactly
	// what a duped item looks like.
	tally.record("factory_mints_birth_row",
		subject->m_serial != 0
		&& probe_scalar(db, std::format("SELECT COUNT(*) FROM item_instances WHERE serial={}"
			" AND item_id={} AND origin_type={};",
			subject->m_serial, subject->m_id_num,
			static_cast<int>(item_origin::npc_drop)).c_str()) == 1
		&& count_of(ledger_event::created) == 1);

	tally.record("counted_no_birth_row", counted_silent);

	// The event is keyed by the Serial — the join every forensic query this
	// exists for starts from — and its type is the ItemLogAction number itself
	// (1..99), with the ledger's own band above it. A translation table between
	// two numberings would be one more thing that can silently disagree, so
	// there isn't one: this queries the literal rather than the enum it was
	// recorded with, which is what makes the number, not just the round trip,
	// the thing being asserted.
	tally.record("event_keyed_by_serial", drop_logged && count_of(drop_event_number) == 1);

	// Who did it and where they were standing. Without these the ledger records
	// that something happened to an item and cannot say by whom, which is the
	// question disputes are.
	tally.record("actor_recorded",
		text_of("actor_char", ItemLogAction::Drop) == actor_char
		&& text_of("actor_account", ItemLogAction::Drop) == actor_account
		&& text_of("map", ItemLogAction::Drop) == probe_map
		&& number_of("x", ItemLogAction::Drop) == probe_x
		&& number_of("y", ItemLogAction::Drop) == probe_y);

	// The other half of a custody transfer.
	tally.record("counterparty_recorded",
		give_logged
		&& count_of(ItemLogAction::Give) == 1
		&& text_of("counterparty_char", ItemLogAction::Give) == other_char);

	// A pickup has no counterparty and a despawn has no actor. Writing "" there
	// instead of NULL would make `WHERE counterparty_char IS NULL` miss them and
	// `GROUP BY` invent a nameless player.
	tally.record("absent_columns_are_null",
		is_null("counterparty_char", ItemLogAction::Drop)
		&& is_null("detail", ItemLogAction::Drop));

	// Nobody on the call path sets `at`; the store stamps it. An event at the
	// epoch orders before every other event ever recorded.
	tally.record("timestamp_stamped",
		probe_scalar(db, std::format("SELECT COUNT(*) FROM item_events WHERE at <= 0"
			" AND serial={};", subject->m_serial).c_str()) == 0);

	// The three ways the text sink declines. Each returned false — the filters
	// still work — and each still left a ledger row.
	tally.record("text_filter_not_applied_to_ledger",
		unlogged_logged == false
		&& count_of(ItemLogAction::NewGenDrop, unlogged->m_serial) == 1);

	tally.record("unknown_action_recorded",
		unknown_logged == false && count_of(ItemLogAction::Use) == 1);

	tally.record("missing_client_recorded",
		absent_logged == false
		&& count_of(ItemLogAction::Deplete) == 1
		&& is_null("actor_char", ItemLogAction::Deplete)
		&& is_null("map", ItemLogAction::Deplete));

	tally.record("counted_no_event", counted_event_silent);

	// GmMint's quantity stayed a quantity.
	tally.record("gm_mint_qty_is_not_a_counterparty",
		mint_logged
		&& count_of(ItemLogAction::GmMint) == 1
		&& text_of("detail", ItemLogAction::GmMint) == std::format("{{\"qty\":{}}}", other_h)
		&& is_null("counterparty_char", ItemLogAction::GmMint));

	// The NPC that dropped it, escaped so the column stays parseable JSON. This
	// is the only birth context an NPC drop has until #79 puts the location and
	// the rolled tier on the birth row itself.
	tally.record("npc_drop_detail_recorded",
		count_of(ItemLogAction::NewGenDrop) == 1
		&& text_of("detail", ItemLogAction::NewGenDrop) == probe_npc_expected);

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
		&& restored_file.find(probe_db) == std::string::npos);

	hb::server::remove_probe_db(probe_db);
	tally.record("scratch_removed", !std::filesystem::exists(probe_db));

	tally.report(std::format("instanced item {}, counted item {}, unlogged item {}",
		instanced_id, counted_id, unlogged_id));
}
