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
//   - A GM mint makes N items in one command, and until #104 recorded one event
//     for the batch. The other N-1 copies had a birth row, a Serial and a holder,
//     and nothing in the ledger saying who that holder was — which is the state
//     a duped item is in. Nothing about the log file looks wrong when this
//     happens, because the line that is there is correct.
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

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <vector>

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

	// How many copies the batch mint asks for. Three is the smallest number that
	// tells the two failure modes apart: one event for the batch, and one event
	// per copy. Two would also do it, but a run that recorded the first and the
	// last would look identical to a correct one.
	constexpr int batch_copies = 3;

	// A probe client is born with nothing, and carrying capacity is strength and
	// level (`max_load`), so the batch venue's weight gate would refuse every
	// copy and leave the counts below comparing zero against zero.
	constexpr int probe_str = 100;

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

	// The operator door of a GM mint, on its own. It writes the trade-channel
	// line for a whole request and records nothing — asserted below against the
	// subject's own Serial, so a venue that called only this one would leave a
	// visible hole rather than a half-recorded item. The second call is a handle
	// that resolves to nobody: no actor, no line.
	const bool mint_line = items.log_gm_mint(actor_h, batch_copies, subject.get());
	const bool mint_line_absent = items.log_gm_mint(absent_h, batch_copies, subject.get());

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

	//----------------------------------------------------------------------
	// The batch mints, through the venues rather than through the entry point.
	// Calling the ledger door by hand N times would prove the door works and
	// say nothing about whether a venue opens it N times, which is the whole of
	// #104. Both venues are driven: the two GM creation commands share
	// add_client_bulk_item_list for a non-stackable batch, and the tester menu
	// and /createitem's attributed form share mint_gm_items.
	//----------------------------------------------------------------------

	// Carrying capacity, or both venues refuse every copy on weight and the
	// counts below compare zero against zero.
	game->m_client_list[actor_h]->m_str = probe_str;

	// The Serials of the probe item the actor is holding. Read off the holder
	// rather than off the ledger: the question is whether the ledger can name a
	// holder for every copy somebody is carrying, so asking the ledger which
	// Serials it knows about would let it answer itself.
	auto held_copies = [&]()
	{
		std::vector<int64_t> serials;
		for (int i = 0; i < hb::shared::limits::MaxItems; i++)
		{
			const CItem* held = game->m_client_list[actor_h]->m_item_list[i];
			if (held != nullptr && held->m_id_num == instanced_id && held->m_serial != 0)
				serials.push_back(held->m_serial);
		}
		return serials;
	};

	const int bulk_created = items.add_client_bulk_item_list(actor_h,
		game->m_item_config_list[instanced_id]->m_name, batch_copies);
	const std::vector<int64_t> bulk_serials = held_copies();

	// A plain request — no tier, no lines — is legal in both roll modes, so what
	// the tiered venue proves here does not depend on which mode the world
	// booted in. `error` is checked through the count it returns.
	std::string mint_error;
	const int tiered_created = items.mint_gm_items(actor_h, instanced_id, batch_copies,
		hb::shared::item::item_attribute_data{}, mint_error);

	// Serials are monotonic, so the second venue's copies are the ones above the
	// high mark the first one left — cheaper than diffing two inventories, and
	// it does not assume anything about which slots either venue picked.
	const int64_t bulk_mark = bulk_serials.empty()
		? 0 : *std::max_element(bulk_serials.begin(), bulk_serials.end());
	std::vector<int64_t> tiered_serials;
	for (const int64_t serial : held_copies())
		if (serial > bulk_mark) tiered_serials.push_back(serial);

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
	auto text_of = [&](const char* column, int type, int64_t serial = 0)
	{
		return hb::server::event_text(db, column, type,
			serial != 0 ? serial : subject->m_serial);
	};
	auto is_null = [&](const char* column, int type, int64_t serial = 0)
	{
		return number_of(std::string(column) + " IS NULL", type, serial) == 1;
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

	// The two doors of a mint, and what each of them is NOT. The operator line
	// went out for a request the ledger knows nothing about, and the subject's
	// Serial has no mint event: the text channel cannot stand in for the ledger,
	// which is what makes a venue that forgets one of the two doors visible.
	tally.record("gm_mint_line_is_not_an_event",
		mint_line
		&& mint_line_absent == false
		&& count_of(ItemLogAction::GmMint) == 0);

	// #104: one event per copy, not one per request. Every copy a venue puts in
	// the inventory has its own birth row, so a batch recorded once leaves the
	// rest of them held by a player the ledger cannot name — `held_unrecorded`,
	// which is the anomaly class this whole subsystem exists to make impossible.
	// Both venues are counted the same way, because they answer the same
	// question and a copy of the arithmetic could pass for one and not the other.
	auto every_copy_recorded = [&](const std::vector<int64_t>& serials, int created)
	{
		if (created != batch_copies || static_cast<int>(serials.size()) != batch_copies)
			return false;

		int64_t events = 0;
		int64_t births = 0;
		for (const int64_t serial : serials)
		{
			events += count_of(ItemLogAction::GmMint, serial);
			births += probe_scalar(db, std::format(
				"SELECT COUNT(*) FROM item_instances WHERE serial={};", serial).c_str());
		}
		return events == batch_copies && births == batch_copies;
	};

	tally.record("gm_mint_event_per_copy_bulk",
		every_copy_recorded(bulk_serials, bulk_created));

	tally.record("gm_mint_event_per_copy_tiered",
		every_copy_recorded(tiered_serials, tiered_created));

	// What one of those events says. A mint has no counterparty — the character
	// the copy landed on is the actor, which is also what makes GmMint locating
	// — and no detail: the batch size used to be recorded there as {"qty":N},
	// and with a row per copy the rows are the count.
	const int64_t first_copy = bulk_serials.empty() ? 0 : bulk_serials.front();
	tally.record("gm_mint_names_the_holder",
		first_copy != 0
		&& text_of("actor_char", ItemLogAction::GmMint, first_copy) == actor_char
		&& is_null("counterparty_char", ItemLogAction::GmMint, first_copy)
		&& is_null("detail", ItemLogAction::GmMint, first_copy));

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
