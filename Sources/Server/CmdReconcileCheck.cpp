// CmdReconcileCheck.cpp: the Reconciliation prover (#83, plan P4.1)
//
// Every other prover in this epic drives the real game paths and reads the rows
// they leave. This one cannot: the states it has to detect are states the game
// is not able to produce. There is no sequence of legal actions that puts one
// Serial in two inventories — if there were, the bug would be there and not
// here. So the fixture is planted with SQL, and what is under test is the
// comparison rather than the code that fills the tables.
//
// That makes the fixture itself the load-bearing part, and it is built to one
// rule: **one planted cause per class, and every class planted.** A detector is
// only trustworthy if it is silent on healthy custody as well as loud on broken
// custody, so half the fixture is items that are exactly where they should be —
// including the four that exercise the derivation rules most easily got wrong
// (an Exchange's counterparty, an escrow-out landing in a Warehouse rather than
// an inventory, a Use that must not erase who picked the item up, and a holder
// name in the wrong case).
//
//////////////////////////////////////////////////////////////////////

#include "CmdReconcileCheck.h"

#include "AccountSqliteStore.h"   // EnsureGameSchema
#include "CheckTally.h"
#include "Game.h"
#include "GameDatabase.h"
#include "Item.h"
#include "ItemLedgerStore.h"
#include "ItemReconciliation.h"
#include "Log.h"
#include "ServerConsole.h"
#include "ServerLogChannels.h"
#include "sqlite3.h"

#include <cstring>
#include <filesystem>
#include <format>
#include <string>

using hb::log_channel;
using hb::server::check_tally;
using hb::server::game_database;
using hb::server::reconcile_report;
namespace reconcile_class = hb::server::reconcile_class;
namespace ItemLogAction = hb::server::net::ItemLogAction;
namespace ledger_event = hb::server::ledger_event;

namespace
{
	constexpr const char* probe_world_db = "reconcilecheck_world.db";
	constexpr const char* probe_ledger_db = "reconcilecheck_ledger.db";

	// Two characters and the account they hang on. Names stay inside the 10-char
	// buffers the character columns are read into elsewhere.
	constexpr const char* probe_account = "reconacct";
	constexpr const char* char_a = "reconone";
	constexpr const char* char_b = "recontwo";

	// The planted Serials. Far above anything a dev world holds, and each one is
	// a distinct cause so a failure names itself. Nothing prints them — the
	// machine lines carry check names only, which is what keeps the Windows and
	// Linux output byte-identical.
	enum probe_serial : int64_t
	{
		s_healthy_inventory = 900000001,
		s_healthy_bank      = 900000002,
		s_healthy_listing   = 900000003,
		s_healthy_offer     = 900000004,
		s_use_after_pickup  = 900000005,
		s_exchanged         = 900000006,
		s_escrow_out        = 900000007,
		s_name_case         = 900000008,
		s_held_twice        = 900000010,
		s_held_but_gone     = 900000011,
		s_held_but_gone_2   = 900000012,
		s_held_but_ground   = 900000013,
		s_holder_mismatch   = 900000014,
		s_missing_holder    = 900000015,
		s_held_unrecorded   = 900000016,
		s_on_counted        = 900000017,
		s_on_ground         = 900000020,
		s_gone              = 900000021,
		s_never_placed      = 900000022,
		s_indeterminate     = 900000023,
		// The unknown Serial is deliberately the highest number in the fixture
		// and gets no birth row, so it is also above the ledger's high-water —
		// which is the strongest form this class takes and the one the detail
		// line is expected to name.
		s_unknown           = 900009999,
	};

	bool exec(sqlite3* db, const std::string& sql)
	{
		char* err = nullptr;
		if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) == SQLITE_OK) return true;
		hb::logger::error("[RECONCILECHECK] {} <- {}", err ? err : "unknown", sql);
		sqlite3_free(err);
		return false;
	}

	// A character the item tables' foreign keys will accept. Only the columns
	// with no default are named; everything else the schema fills in.
	bool seed_probe_character(sqlite3* db, const char* name)
	{
		return exec(db, std::format(
			"INSERT OR IGNORE INTO characters(character_name, account_name, created_at,"
			" level, exp, map_name, map_x, map_y, hp, mp, sp,"
			" str, vit, dex, intl, mag, chr, gender, skin, hairstyle, haircolor, underwear)"
			" VALUES('{}','{}','t',1,0,'default',10,10,1,1,1,10,10,10,10,10,10,1,1,1,1,1);",
			name, probe_account));
	}

	// One holder row. `table` picks inventory or Warehouse; the two escrow tables
	// key on their parent instead and get their own helper below.
	bool plant_held(sqlite3* db, const char* table, const char* owner, int slot,
		int item_id, int64_t serial)
	{
		// The carried slots are the one holder table with a position and an
		// equipped flag. Derived from the table name rather than taken as a
		// second parameter, so the two cannot be given contradicting answers.
		const bool inventory = (std::strcmp(table, "character_items") == 0);
		return exec(db, std::format(
			"INSERT INTO {}(character_name, slot, item_id, serial, count,"
			" touch_effect_type, touch_effect_value1, touch_effect_value2, touch_effect_value3,"
			" item_color, spec_effect_value1, spec_effect_value2, spec_effect_value3,"
			" cur_durability{})"
			" VALUES('{}',{},{},{},1,0,0,0,0,0,0,0,0,100{});",
			table, inventory ? ", pos_x, pos_y, is_equipped" : "",
			owner, slot, item_id, serial, inventory ? ", 0, 0, 0" : ""));
	}

	bool plant_escrow(sqlite3* db, const char* table, const char* key_column, int64_t key,
		int slot, int item_id, int64_t serial)
	{
		return exec(db, std::format(
			"INSERT INTO {}({}, slot, item_id, serial, count,"
			" touch_effect_type, touch_effect_value1, touch_effect_value2, touch_effect_value3,"
			" item_color, spec_effect_value1, spec_effect_value2, spec_effect_value3,"
			" cur_durability)"
			" VALUES({},{},{},{},1,0,0,0,0,0,0,0,0,100);",
			table, key_column, key, slot, item_id, serial));
	}

	bool plant_instance(sqlite3* db, int64_t serial, int item_id)
	{
		return exec(db, std::format(
			"INSERT INTO item_instances(serial, item_id, created_at, origin_type)"
			" VALUES({},{},1000,1);", serial, item_id));
	}

	// One ledger event. `at` is supplied so the order inside the fixture is
	// fixed, but the derivation reads event_id and not the clock — two events
	// written in the same second still have an order, which is the property a
	// timestamp could not give it.
	bool plant_event(sqlite3* db, int64_t serial, int event_type,
		const char* actor, const char* counterparty)
	{
		const auto quoted = [](const char* name)
		{
			return name == nullptr ? std::string("NULL") : std::format("'{}'", name);
		};
		return exec(db, std::format(
			"INSERT INTO item_events(serial, at, event_type, actor_char, counterparty_char)"
			" VALUES({},1000,{},{},{});",
			serial, event_type, quoted(actor), quoted(counterparty)));
	}

	// --- Reading the report back ------------------------------------------
	// Whether a class fired for exactly one Serial, and that it is the planted
	// one. Both halves matter: a detector that reports the right count for the
	// wrong item is not a detector.
	bool only(const reconcile_report& report, int class_id, int64_t serial)
	{
		if (report.class_counts[class_id] != 1) return false;
		for (const auto& sample : report.samples) {
			if (sample.class_id == class_id) return sample.serial == serial;
		}
		return false;
	}

	// Whether nothing at all was said about this Serial. The silence half of the
	// contract, which is the half a noisy detector fails.
	bool silent(const reconcile_report& report, int64_t serial)
	{
		for (const auto& sample : report.samples) {
			if (sample.serial == serial) return false;
		}
		return true;
	}

	size_t samples_in(const reconcile_report& report, int class_id)
	{
		size_t found = 0;
		for (const auto& sample : report.samples) {
			if (sample.class_id == class_id) found++;
		}
		return found;
	}

	std::string detail_of(const reconcile_report& report, int class_id, int64_t serial)
	{
		for (const auto& sample : report.samples) {
			if (sample.class_id == class_id && sample.serial == serial) return sample.detail;
		}
		return {};
	}

	// The world the fixture lives in. Built once and read three times: with the
	// evidence uncapped, with it capped, and through a read-only connection.
	bool build_probe_world(sqlite3* world, int instanced_id, int counted_id)
	{
		if (!hb::server::seed_probe_account(world, probe_account)) return false;
		if (!seed_probe_character(world, char_a)) return false;
		if (!seed_probe_character(world, char_b)) return false;

		if (!exec(world, std::format(
			"INSERT INTO listings(listing_id, seller_name, seller_account, seller_nation,"
			" created_at, expires_at) VALUES(1,'{}','{}',1,1000,9000);",
			char_a, probe_account))) return false;
		if (!exec(world, std::format(
			"INSERT INTO offers(offer_id, listing_id, offerer_name, offerer_account, created_at)"
			" VALUES(1,1,'{}','{}',1000);", char_b, probe_account))) return false;

		struct held_row { const char* table; const char* owner; int64_t serial; int item_id; };
		const held_row rows[] = {
			{ "character_items",      char_a, s_healthy_inventory, instanced_id },
			{ "character_bank_items", char_a, s_healthy_bank,      instanced_id },
			{ "character_items",      char_a, s_use_after_pickup,  instanced_id },
			// The Exchange gave it to B, so B is where it must be found.
			{ "character_items",      char_b, s_exchanged,         instanced_id },
			// Escrow-out delivers to a Warehouse, never to an inventory.
			{ "character_bank_items", char_b, s_escrow_out,        instanced_id },
			{ "character_items",      char_a, s_name_case,         instanced_id },
			// The dupe: one Serial, two holders.
			{ "character_items",      char_a, s_held_twice,        instanced_id },
			{ "character_items",      char_b, s_held_twice,        instanced_id },
			{ "character_items",      char_a, s_held_but_gone,     instanced_id },
			{ "character_items",      char_b, s_held_but_gone_2,   instanced_id },
			{ "character_items",      char_a, s_held_but_ground,   instanced_id },
			{ "character_items",      char_a, s_holder_mismatch,   instanced_id },
			{ "character_items",      char_a, s_held_unrecorded,   instanced_id },
			{ "character_items",      char_a, s_unknown,           instanced_id },
			// An Instanced item carrying no identity at all.
			{ "character_items",      char_a, 0,                   instanced_id },
			// A stackable carrying one.
			{ "character_items",      char_a, s_on_counted,        counted_id },
		};

		int slot = 0;
		for (const auto& row : rows) {
			if (!plant_held(world, row.table, row.owner, slot++, row.item_id, row.serial)) {
				return false;
			}
		}

		return plant_escrow(world, "listing_items", "listing_id", 1, 0,
				instanced_id, s_healthy_listing)
			&& plant_escrow(world, "offer_items", "offer_id", 1, 0,
				instanced_id, s_healthy_offer);
	}

	bool build_probe_ledger(sqlite3* ledger, int instanced_id, int counted_id)
	{
		struct planted { int64_t serial; int item_id; };
		const planted instances[] = {
			{ s_healthy_inventory, instanced_id }, { s_healthy_bank, instanced_id },
			{ s_healthy_listing, instanced_id },   { s_healthy_offer, instanced_id },
			{ s_use_after_pickup, instanced_id },  { s_exchanged, instanced_id },
			{ s_escrow_out, instanced_id },        { s_name_case, instanced_id },
			{ s_held_twice, instanced_id },        { s_held_but_gone, instanced_id },
			{ s_held_but_gone_2, instanced_id },   { s_held_but_ground, instanced_id },
			{ s_holder_mismatch, instanced_id },   { s_missing_holder, instanced_id },
			{ s_held_unrecorded, instanced_id },   { s_on_counted, counted_id },
			{ s_on_ground, instanced_id },         { s_gone, instanced_id },
			{ s_never_placed, instanced_id },      { s_indeterminate, instanced_id },
		};
		for (const auto& row : instances) {
			if (!plant_instance(ledger, row.serial, row.item_id)) return false;
		}

		struct planted_event { int64_t serial; int type; const char* actor; const char* other; };
		const planted_event events[] = {
			// Healthy custody, one rule each.
			{ s_healthy_inventory, ItemLogAction::get,        char_a,  nullptr },
			{ s_healthy_bank,      ItemLogAction::Deposit,    char_a,  nullptr },
			{ s_healthy_listing,   ItemLogAction::TpList,     char_a,  nullptr },
			{ s_healthy_offer,     ItemLogAction::TpOffer,    char_b,  nullptr },
			// A pickup, then a Use. Use says nothing about custody, so the
			// pickup must still be the answer.
			{ s_use_after_pickup,  ItemLogAction::get,        char_a,  nullptr },
			{ s_use_after_pickup,  ItemLogAction::Use,        char_a,  nullptr },
			// An Exchange hands it to the counterparty, not to the actor.
			{ s_exchanged,         ItemLogAction::get,        char_a,  nullptr },
			{ s_exchanged,         ItemLogAction::Exchange,   char_a,  char_b  },
			// Escrow-out: the actor is the recipient and the destination is a
			// Warehouse. Getting either half wrong makes a healthy trade look
			// like a mismatch, which is the failure that would make an operator
			// stop reading the report.
			{ s_escrow_out,        ItemLogAction::TpTradeIn,  char_b,  char_a  },
			// The holder in the case the database itself considers equal.
			{ s_name_case,         ItemLogAction::get,        "RECONONE", nullptr },
			// The anomalies.
			{ s_held_twice,        ItemLogAction::get,        char_a,  nullptr },
			{ s_held_but_gone,     ledger_event::destroyed,   char_a,  nullptr },
			{ s_held_but_gone_2,   ledger_event::destroyed,   char_b,  nullptr },
			{ s_held_but_ground,   ItemLogAction::Drop,       char_a,  nullptr },
			{ s_holder_mismatch,   ItemLogAction::get,        char_b,  nullptr },
			{ s_missing_holder,    ItemLogAction::get,        char_a,  nullptr },
			{ s_on_counted,        ItemLogAction::get,        char_a,  nullptr },
			// The three states that legitimately have no holder, plus the one
			// that cannot say who its holder is.
			{ s_on_ground,         ItemLogAction::Drop,       char_a,  nullptr },
			{ s_gone,              ledger_event::despawned,   nullptr, nullptr },
			{ s_indeterminate,     ItemLogAction::get,        nullptr, nullptr },
		};
		for (const auto& row : events) {
			if (!plant_event(ledger, row.serial, row.type, row.actor, row.other)) return false;
		}

		// s_held_unrecorded and s_never_placed get no event at all: a birth row
		// and nothing else is what a mint the venue never followed up looks like.
		return true;
	}
}

void CmdReconcileCheck::execute(CGame* game, const char* args)
{
	const int instanced_id = hb::server::find_probe_item(game, false);
	const int counted_id = hb::server::find_probe_item(game, true);
	if (instanced_id < 0 || counted_id < 0) {
		hb::console::error("reconcilecheck: the item config has no usable {} item.",
			instanced_id < 0 ? "non-stackable" : "stackable");
		return;
	}

	check_tally tally("RECONCILECHECK", "reconcilecheck");

	game_database world;
	hb::server::item_ledger_store ledger;

	// Both closes and both removals, on every exit. Written once because there
	// are four ways out of this command and a scratch database left behind by
	// one of them is read by the next run as its own fixture — which is the
	// failure that makes a prover pass for the wrong reason. Both closes are
	// idempotent, so the success path just calls this too.
	const auto cleanup = [&]()
	{
		ledger.close();
		world.close();
		hb::server::remove_probe_db(probe_world_db);
		hb::server::remove_probe_db(probe_ledger_db);
	};

	// A run that died half way through must not leave rows for this one to read
	// as its own, so the way in is the way out.
	hb::server::remove_probe_db(probe_world_db);
	hb::server::remove_probe_db(probe_ledger_db);

	if (!world.open(probe_world_db) || !EnsureGameSchema(world.handle(), probe_world_db)) {
		hb::console::error("reconcilecheck: could not create the scratch world '{}'.",
			probe_world_db);
		cleanup();
		return;
	}

	// The ledger's own store lays the schema down, so the fixture cannot drift
	// from the real table definitions — the one thing a hand-written CREATE
	// TABLE in a prover is guaranteed to do eventually.
	if (!ledger.open(probe_ledger_db)) {
		hb::console::error("reconcilecheck: could not create the scratch ledger '{}'.",
			probe_ledger_db);
		cleanup();
		return;
	}

	if (!build_probe_world(world.handle(), instanced_id, counted_id)
		|| !build_probe_ledger(ledger.handle(), instanced_id, counted_id)) {
		hb::console::error("reconcilecheck: could not plant the fixture.");
		cleanup();
		return;
	}

	hb::server::reconcile_options options;
	options.sample_limit = 0;   // every finding, so the fixture can be read whole
	options.is_instanced = hb::server::instanced_item_lookup(*game);

	const reconcile_report report =
		hb::server::run_reconciliation(world.handle(), ledger.handle(), options);

	tally.record("scan_completed", report.ok, report.ok ? "" : report.failure);

	// --- Healthy custody is silent ---------------------------------------
	tally.record("healthy_inventory_is_silent", silent(report, s_healthy_inventory));
	tally.record("healthy_warehouse_is_silent", silent(report, s_healthy_bank));
	tally.record("healthy_listing_is_silent", silent(report, s_healthy_listing));
	tally.record("healthy_offer_is_silent", silent(report, s_healthy_offer));

	// --- The derivation rules most easily got wrong -----------------------
	tally.record("use_does_not_move_the_holder", silent(report, s_use_after_pickup));
	tally.record("exchange_hands_it_to_the_counterparty", silent(report, s_exchanged));
	tally.record("escrow_out_lands_in_the_warehouse", silent(report, s_escrow_out));
	tally.record("holder_names_compare_case_insensitively", silent(report, s_name_case));

	// --- One of every anomaly class ---------------------------------------
	tally.record("dupe_found_one_serial_two_holders",
		only(report, reconcile_class::held_twice, s_held_twice));
	tally.record("held_but_gone_found",
		report.class_counts[reconcile_class::held_but_gone] == 2
		&& !detail_of(report, reconcile_class::held_but_gone, s_held_but_gone).empty());
	tally.record("held_but_ground_found",
		only(report, reconcile_class::held_but_ground, s_held_but_ground));
	tally.record("unknown_serial_found",
		only(report, reconcile_class::unknown_serial, s_unknown));
	tally.record("unknown_serial_names_the_high_water",
		detail_of(report, reconcile_class::unknown_serial, s_unknown)
			.find("high-water") != std::string::npos);
	tally.record("holder_mismatch_found",
		only(report, reconcile_class::holder_mismatch, s_holder_mismatch));
	tally.record("missing_holder_found",
		only(report, reconcile_class::missing_holder, s_missing_holder));
	tally.record("held_unrecorded_found",
		only(report, reconcile_class::held_unrecorded, s_held_unrecorded));
	tally.record("instanced_row_without_a_serial_found",
		report.class_counts[reconcile_class::serial_on_instanced_missing] == 1);
	tally.record("stackable_row_with_a_serial_found",
		only(report, reconcile_class::serial_on_counted, s_on_counted));

	// --- The states with no holder are counted, not accused ---------------
	tally.record("on_ground_is_counted_not_accused",
		report.on_ground == 1 && silent(report, s_on_ground));
	tally.record("left_world_is_counted_not_accused",
		report.gone == 1 && silent(report, s_gone));
	tally.record("never_placed_is_counted_not_accused",
		report.never_placed == 1 && silent(report, s_never_placed));
	tally.record("unnamed_holder_is_counted_not_accused",
		report.indeterminate == 1 && silent(report, s_indeterminate));

	// --- The totals -------------------------------------------------------
	// Ten findings from nine classes, four of which are the ones a healthy
	// world cannot produce. Written as a total rather than re-listing the
	// classes: this is what catches a tenth class firing on a fixture that was
	// only meant to trip nine.
	tally.record("anomaly_total_matches_the_fixture", report.anomaly_total() == 10);
	tally.record("critical_total_matches_the_fixture", report.critical_total() == 5);

	// --- Capping the evidence does not cap the counts ---------------------
	{
		hb::server::reconcile_options capped = options;
		capped.sample_limit = 1;
		const reconcile_report short_report =
			hb::server::run_reconciliation(world.handle(), ledger.handle(), capped);

		tally.record("cap_limits_the_evidence",
			samples_in(short_report, reconcile_class::held_but_gone) == 1);
		tally.record("cap_does_not_limit_the_count",
			short_report.class_counts[reconcile_class::held_but_gone] == 2);
	}

	// --- It runs against read-only handles --------------------------------
	// The claim that makes this job usable at all: an operator points it at a
	// live server's files. If any part of the scan wrote, SQLite would refuse
	// here and the report would come back short.
	{
		sqlite3* ro_world = nullptr;
		sqlite3* ro_ledger = nullptr;
		const bool opened =
			sqlite3_open_v2(probe_world_db, &ro_world, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK
			&& sqlite3_open_v2(probe_ledger_db, &ro_ledger, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK;

		reconcile_report ro_report;
		if (opened) {
			ro_report = hb::server::run_reconciliation(ro_world, ro_ledger, options);
		}
		tally.record("read_only_handles_give_the_same_answer",
			opened && ro_report.ok && ro_report.anomaly_total() == report.anomaly_total());

		if (ro_world) sqlite3_close(ro_world);
		if (ro_ledger) sqlite3_close(ro_ledger);
	}

	// --- It refuses what is not a world or not a ledger -------------------
	// Both directions, because the two handles are the same type and swapping
	// them is the mistake an operator makes at the command line. A run that
	// carried on would report every item in the world as unknown.
	{
		const reconcile_report swapped =
			hb::server::run_reconciliation(ledger.handle(), world.handle(), options);
		tally.record("refuses_a_ledger_where_the_world_goes",
			!swapped.ok && !swapped.failure.empty());

		const reconcile_report no_ledger =
			hb::server::run_reconciliation(world.handle(), world.handle(), options);
		tally.record("refuses_a_world_where_the_ledger_goes",
			!no_ledger.ok && !no_ledger.failure.empty());
	}

	cleanup();

	tally.record("scratch_removed",
		!std::filesystem::exists(probe_world_db) && !std::filesystem::exists(probe_ledger_db));

	tally.report();

	hb::logger::log<log_channel::commands>("reconcilecheck: {}/{} checks passed",
		tally.passed(), tally.total());
}
