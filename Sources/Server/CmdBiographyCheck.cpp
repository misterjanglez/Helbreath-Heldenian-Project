// CmdBiographyCheck.cpp: the Biography + validator prover (#84, plan P4.2)
//
// Like the Reconciliation prover before it, the fixture is planted with SQL
// rather than driven through the game — the histories under test are histories
// the game cannot produce, and if it could the bug would be there and not here.
//
// The fixture is built to one rule: **every ordinary life must produce nothing,
// and every impossible life must produce exactly one thing.** Both halves are
// load-bearing. The second half is what a validator is for; the first half is
// what decides whether anybody keeps running it, because a report with a false
// finding in it is indistinguishable from a report with a real one.
//
// **Plant order is part of the fixture.** event_id is assigned in insertion
// order, the replay reads it in that order, and the crash-excuse tests turn on
// which side of a boundary row an event lands. The three blocks below are
// therefore a sequence and not a set.
//
//////////////////////////////////////////////////////////////////////

#include "CmdBiographyCheck.h"

#include "CheckTally.h"
#include "Game.h"
#include "ItemBiography.h"
#include "ItemLedgerStore.h"
#include "Log.h"
#include "ServerConsole.h"
#include "ServerLogChannels.h"
#include "sqlite3.h"

#include <filesystem>
#include <format>
#include <string>

using hb::log_channel;
using hb::server::biography;
using hb::server::check_tally;
using hb::server::validate_report;
namespace ItemLogAction = hb::server::net::ItemLogAction;
namespace ledger_event = hb::server::ledger_event;
namespace ledger_place = hb::server::ledger_place;
namespace ledger_violation = hb::server::ledger_violation;

namespace
{
	constexpr const char* probe_ledger_db = "biographycheck_ledger.db";
	constexpr const char* probe_empty_db = "biographycheck_empty.db";

	// Two characters. Names stay inside the 10-char buffers the character
	// columns are read into elsewhere, so the fixture cannot become the reason a
	// check fails.
	constexpr const char* char_a = "biogone";
	constexpr const char* char_b = "biogtwo";

	// The planted Serials. Far above anything a dev world holds, and each one is
	// a distinct cause so a failure names itself. Nothing prints them — the
	// machine lines carry check names only, which is what keeps the Windows and
	// Linux output byte-identical.
	enum probe_serial : int64_t
	{
		// --- Lives that must produce nothing ---
		s_legal_life     = 940000001,  // mint to destruction, every legal step
		s_unplaced_drop  = 940000002,  // minted, never placed, then dropped
		s_use_no_move    = 940000003,  // a Use between a pickup and a drop
		s_sold           = 940000004,  // Sell, Deplete, destroyed — the divergence
		s_escrow         = 940000005,  // a trade delivered into a Warehouse

		// --- One impossible life per class ---
		s_after_exit     = 940000010,  // a Use after the item was destroyed
		s_exit_twice     = 940000011,  // destroyed twice
		s_picked_twice   = 940000012,  // picked up with no intervening drop
		s_retrieve_held  = 940000013,  // retrieved from the Warehouse while carrying it
		s_drop_on_ground = 940000014,  // dropped what was already on the ground
		s_double_birth   = 940000015,  // two births for one Serial
		s_orphan         = 940000016,  // events with no birth row

		// --- The census ---
		s_never_moved    = 940000020,
		s_in_world       = 940000021,
		s_ended          = 940000022,

		// --- The Biography's own contract ---
		s_order          = 940000023,  // events whose timestamps disagree with their order
		s_absent         = 940009999,  // nothing recorded against it at all

		// --- The crash excuse, all three directions ---
		s_clean_gap      = 940000030,  // the same violation across a CLEAN boundary
		s_crash_gap      = 940000031,  // a transition violation across a crash boundary
		s_struct_gap     = 940000032,  // a structural violation across a crash boundary
	};

	// What the fixture is worth, written out so an unintended twentieth finding
	// has something to fail against. reconcilecheck's rule: a total is what
	// catches a class firing on a fixture that was only meant to trip the ones
	// below it.
	constexpr int64_t expect_serials      = 19;
	constexpr int64_t expect_instances    = 18;   // every Serial but the orphan
	constexpr int64_t expect_violations   = 9;
	constexpr int64_t expect_critical     = 8;    // all but the one released_when_absent
	constexpr int64_t expect_ended        = 5;
	constexpr int64_t expect_in_world     = 11;
	constexpr int64_t expect_never_moved  = 3;

	bool exec(sqlite3* db, const std::string& sql)
	{
		char* error = nullptr;
		if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error) == SQLITE_OK) return true;
		hb::logger::error("[BIOGRAPHYCHECK] {} <- {}", error != nullptr ? error : "unknown", sql);
		sqlite3_free(error);
		return false;
	}

	bool plant_instance(sqlite3* db, int64_t serial, int item_id, int origin, int tier)
	{
		return exec(db, std::format(
			"INSERT INTO item_instances(serial, item_id, created_at, origin_type, origin_detail,"
			" map, x, y, tier) VALUES({},{},1000,{},'Orc Warrior','abaddon',120,330,{});",
			serial, item_id, origin, tier));
	}

	// One event. `at` is supplied so the ordering check can make the clock
	// disagree with the order; the replay reads event_id and not the clock, which
	// is the property that check exists to prove.
	bool plant_event(sqlite3* db, int64_t serial, int event_type,
		const char* actor, const char* counterparty, int64_t at)
	{
		const auto quoted = [](const char* name)
		{
			return name == nullptr ? std::string("NULL") : std::format("'{}'", name);
		};
		return exec(db, std::format(
			"INSERT INTO item_events(serial, at, event_type, actor_char, counterparty_char)"
			" VALUES({},{},{},{},{});",
			serial, at, event_type, quoted(actor), quoted(counterparty)));
	}

	// A run boundary, in the shape ItemLedgerStore writes one. `crashed` is the
	// whole point: only a boundary whose previous run did not stop cleanly marks
	// a place where events can be missing.
	bool plant_boundary(sqlite3* db, bool crashed)
	{
		return exec(db, std::format(
			"INSERT INTO item_events(serial, at, event_type, detail)"
			" VALUES(0,2000,{},'{{\"boot\":1,\"prev_shutdown\":\"{}\"}}');",
			static_cast<int>(ledger_event::boundary), crashed ? "crash" : "clean"));
	}

	struct step_spec { int64_t serial; int type; const char* actor; const char* other; int64_t at; };

	bool plant_all(sqlite3* db, const step_spec* steps, size_t count)
	{
		for (size_t i = 0; i < count; i++) {
			if (!plant_event(db, steps[i].serial, steps[i].type,
				steps[i].actor, steps[i].other, steps[i].at)) {
				return false;
			}
		}
		return true;
	}

	// --- Reading the report back ------------------------------------------
	// Whether a class fired for exactly one Serial, and that it is the planted
	// one. Both halves matter: a validator that reports the right count for the
	// wrong item is not a validator.
	bool only(const validate_report& report, int class_id, int64_t serial)
	{
		if (report.class_counts[class_id] != 1) return false;
		for (const auto& sample : report.samples) {
			if (sample.class_id == class_id) return sample.serial == serial;
		}
		return false;
	}

	bool accused(const validate_report& report, int class_id, int64_t serial)
	{
		for (const auto& sample : report.samples) {
			if (sample.class_id == class_id && sample.serial == serial) return true;
		}
		return false;
	}

	// Whether nothing at all was said about this Serial. The silence half of the
	// contract, which is the half a noisy validator fails.
	bool silent(const validate_report& report, int64_t serial)
	{
		for (const auto& sample : report.samples) {
			if (sample.serial == serial) return false;
		}
		return true;
	}

	size_t samples_in(const validate_report& report, int class_id)
	{
		size_t found = 0;
		for (const auto& sample : report.samples) {
			if (sample.class_id == class_id) found++;
		}
		return found;
	}

	// --- The fixture -------------------------------------------------------
	bool build_probe_ledger(sqlite3* ledger, int item_id)
	{
		namespace origin = hb::server::item_origin;

		struct birth { int64_t serial; int origin_type; int tier; };
		const birth births[] = {
			{ s_legal_life,    origin::npc_drop, 2 }, { s_unplaced_drop,  origin::craft,    0 },
			{ s_use_no_move,   origin::npc_drop, 0 }, { s_sold,           origin::shop_buy, 0 },
			{ s_escrow,        origin::shop_buy, 1 }, { s_after_exit,     origin::shop_buy, 0 },
			{ s_exit_twice,    origin::shop_buy, 0 }, { s_picked_twice,   origin::npc_drop, 0 },
			{ s_retrieve_held, origin::shop_buy, 0 }, { s_drop_on_ground, origin::npc_drop, 0 },
			{ s_double_birth,  origin::craft,    0 }, { s_never_moved,    origin::craft,    0 },
			{ s_in_world,      origin::shop_buy, 0 }, { s_ended,          origin::shop_buy, 0 },
			{ s_order,         origin::craft,    0 }, { s_clean_gap,      origin::npc_drop, 0 },
			{ s_crash_gap,     origin::npc_drop, 0 }, { s_struct_gap,     origin::craft,    0 },
			// s_orphan gets none: events about a Serial the ledger never recorded
			// a birth for is the whole class.
		};
		for (const auto& row : births) {
			if (!plant_instance(ledger, row.serial, item_id, row.origin_type, row.tier)) {
				return false;
			}
		}

		// --- Block A: everything that does not straddle a boundary ----------
		const step_spec block_a[] = {
			// A whole legal life, every rule in the table exercised once: minted,
			// dropped by an NPC, picked up, banked, taken back, listed, delivered
			// out of the trade into a Warehouse, taken out, handed to somebody,
			// dropped, picked up again, and destroyed.
			{ s_legal_life, ledger_event::created,    nullptr, nullptr, 1000 },
			{ s_legal_life, ItemLogAction::NewGenDrop, nullptr, nullptr, 1001 },
			{ s_legal_life, ItemLogAction::get,        char_a,  nullptr, 1002 },
			{ s_legal_life, ItemLogAction::Deposit,    char_a,  nullptr, 1003 },
			{ s_legal_life, ItemLogAction::Retrieve,   char_a,  nullptr, 1004 },
			{ s_legal_life, ItemLogAction::TpList,     char_a,  nullptr, 1005 },
			{ s_legal_life, ItemLogAction::TpTradeIn,  char_b,  char_a,  1006 },
			{ s_legal_life, ItemLogAction::Retrieve,   char_b,  nullptr, 1007 },
			{ s_legal_life, ItemLogAction::Give,       char_b,  char_a,  1008 },
			{ s_legal_life, ItemLogAction::Drop,       char_a,  nullptr, 1009 },
			{ s_legal_life, ItemLogAction::get,        char_b,  nullptr, 1010 },
			{ s_legal_life, ledger_event::destroyed,   char_b,  nullptr, 1011 },

			// Minted by a venue that records no placement, then dropped out of an
			// inventory nothing ever said it was in. The commonest shape in a real
			// ledger, and it must be silent or the tool is unusable.
			{ s_unplaced_drop, ledger_event::created, nullptr, nullptr, 1000 },
			{ s_unplaced_drop, ItemLogAction::Drop,   char_a,  nullptr, 1001 },

			// A Use says nothing about custody, so the item is still carried when
			// the drop comes.
			{ s_use_no_move, ledger_event::created,    nullptr, nullptr, 1000 },
			{ s_use_no_move, ItemLogAction::NewGenDrop, nullptr, nullptr, 1001 },
			{ s_use_no_move, ItemLogAction::get,        char_a,  nullptr, 1002 },
			{ s_use_no_move, ItemLogAction::Use,        char_a,  nullptr, 1003 },
			{ s_use_no_move, ItemLogAction::Drop,       char_a,  nullptr, 1004 },

			// The sale. Sell and Deplete both empty the slot and neither is the
			// exit — the destroyed that follows them is legal, and a machine that
			// treated either as terminal would report every sale ever made.
			{ s_sold, ledger_event::created,  nullptr, nullptr, 1000 },
			{ s_sold, ItemLogAction::Buy,     char_a,  nullptr, 1001 },
			{ s_sold, ItemLogAction::Sell,    char_a,  nullptr, 1002 },
			{ s_sold, ItemLogAction::Deplete, char_a,  nullptr, 1003 },
			{ s_sold, ledger_event::destroyed, char_a, nullptr, 1004 },

			// Escrow out is a delivery into a WAREHOUSE, never into an inventory,
			// so the Retrieve that follows it is the legal next step.
			{ s_escrow, ledger_event::created,   nullptr, nullptr, 1000 },
			{ s_escrow, ItemLogAction::Buy,      char_a,  nullptr, 1001 },
			{ s_escrow, ItemLogAction::TpOffer,  char_a,  nullptr, 1002 },
			{ s_escrow, ItemLogAction::TpRefund, char_a,  nullptr, 1003 },
			{ s_escrow, ItemLogAction::Retrieve, char_a,  nullptr, 1004 },

			// One impossible life per class.
			{ s_after_exit, ledger_event::created,   nullptr, nullptr, 1000 },
			{ s_after_exit, ItemLogAction::Buy,      char_a,  nullptr, 1001 },
			{ s_after_exit, ledger_event::destroyed, char_a,  nullptr, 1002 },
			{ s_after_exit, ItemLogAction::Use,      char_a,  nullptr, 1003 },

			{ s_exit_twice, ledger_event::created,   nullptr, nullptr, 1000 },
			{ s_exit_twice, ItemLogAction::Buy,      char_a,  nullptr, 1001 },
			{ s_exit_twice, ledger_event::destroyed, char_a,  nullptr, 1002 },
			{ s_exit_twice, ledger_event::destroyed, char_a,  nullptr, 1003 },

			{ s_picked_twice, ledger_event::created,    nullptr, nullptr, 1000 },
			{ s_picked_twice, ItemLogAction::NewGenDrop, nullptr, nullptr, 1001 },
			{ s_picked_twice, ItemLogAction::get,        char_a,  nullptr, 1002 },
			{ s_picked_twice, ItemLogAction::get,        char_b,  nullptr, 1003 },

			{ s_retrieve_held, ledger_event::created,   nullptr, nullptr, 1000 },
			{ s_retrieve_held, ItemLogAction::Buy,      char_a,  nullptr, 1001 },
			{ s_retrieve_held, ItemLogAction::Retrieve, char_a,  nullptr, 1002 },

			{ s_drop_on_ground, ledger_event::created,    nullptr, nullptr, 1000 },
			{ s_drop_on_ground, ItemLogAction::NewGenDrop, nullptr, nullptr, 1001 },
			{ s_drop_on_ground, ItemLogAction::Drop,       char_a,  nullptr, 1002 },

			{ s_double_birth, ledger_event::created, nullptr, nullptr, 1000 },
			{ s_double_birth, ledger_event::created, nullptr, nullptr, 1001 },

			// No birth row, and a transition violation inside it. The orphan must
			// be reported once for the Serial and the violation must NOT also be
			// reported — one cause, one finding.
			{ s_orphan, ItemLogAction::get,  char_a, nullptr, 1000 },
			{ s_orphan, ItemLogAction::get,  char_b, nullptr, 1001 },
			{ s_orphan, ItemLogAction::Drop, char_b, nullptr, 1002 },

			// The census.
			{ s_never_moved, ledger_event::created, nullptr, nullptr, 1000 },
			{ s_in_world,    ledger_event::created, nullptr, nullptr, 1000 },
			{ s_in_world,    ItemLogAction::Buy,    char_a,  nullptr, 1001 },
			{ s_ended,       ledger_event::created, nullptr, nullptr, 1000 },
			{ s_ended,       ItemLogAction::Buy,    char_a,  nullptr, 1001 },
			{ s_ended,       ledger_event::destroyed, char_a, nullptr, 1002 },

			// Timestamps that run backwards against the insertion order. Read by
			// the clock these come out reversed; read by event_id they come out as
			// planted, which is what the Biography promises.
			{ s_order, ledger_event::created, nullptr, nullptr, 3000 },
			{ s_order, ItemLogAction::Drop,   char_a,  nullptr, 2000 },
			{ s_order, ItemLogAction::get,    char_a,  nullptr, 1000 },

			// The first half of each crash-excuse life. Their second events land
			// on the far side of a boundary, below.
			{ s_clean_gap,  ledger_event::created,    nullptr, nullptr, 1000 },
			{ s_clean_gap,  ItemLogAction::NewGenDrop, nullptr, nullptr, 1001 },
			{ s_clean_gap,  ItemLogAction::get,        char_a,  nullptr, 1002 },
			{ s_crash_gap,  ledger_event::created,    nullptr, nullptr, 1000 },
			{ s_crash_gap,  ItemLogAction::NewGenDrop, nullptr, nullptr, 1001 },
			{ s_crash_gap,  ItemLogAction::get,        char_a,  nullptr, 1002 },
			{ s_struct_gap, ledger_event::created,    nullptr, nullptr, 1000 },
		};
		if (!plant_all(ledger, block_a, std::size(block_a))) return false;

		// --- Block B: across a CLEAN boundary -------------------------------
		// A clean stop loses nothing, so the violation on the far side of it is
		// still a violation. This is what stops the excuse from forgiving every
		// restart.
		if (!plant_boundary(ledger, false)) return false;
		const step_spec block_b[] = {
			{ s_clean_gap, ItemLogAction::get, char_b, nullptr, 3000 },
		};
		if (!plant_all(ledger, block_b, std::size(block_b))) return false;

		// --- Block C: across a CRASH boundary -------------------------------
		// A crash loses the tail of a flush window, so the Drop that would have
		// explained the second pickup may never have reached disk. The transition
		// violation is excused; the structural one is not, because a birth row and
		// its event are written in one transaction and no hole can split them.
		if (!plant_boundary(ledger, true)) return false;
		const step_spec block_c[] = {
			{ s_crash_gap,  ItemLogAction::get,    char_b,  nullptr, 4000 },
			{ s_struct_gap, ledger_event::created, nullptr, nullptr, 4000 },
		};
		return plant_all(ledger, block_c, std::size(block_c));
	}
}

void CmdBiographyCheck::execute(CGame* game, const char* args)
{
	const int item_id = hb::server::find_probe_item(game, false);
	if (item_id < 0) {
		hb::console::error("biographycheck: the item config has no usable non-stackable item.");
		return;
	}

	check_tally tally("BIOGRAPHYCHECK", "biographycheck");

	hb::server::item_ledger_store ledger;
	sqlite3* empty = nullptr;

	// Every close and every removal, on every exit. Written once because there
	// are several ways out of this command and a scratch database left behind by
	// one of them is read by the next run as its own fixture — which is the
	// failure that makes a prover pass for the wrong reason.
	const auto cleanup = [&]()
	{
		ledger.close();
		if (empty != nullptr) { sqlite3_close(empty); empty = nullptr; }
		hb::server::remove_probe_db(probe_ledger_db);
		hb::server::remove_probe_db(probe_empty_db);
	};

	// A run that died half way through must not leave rows for this one to read
	// as its own, so the way in is the way out.
	hb::server::remove_probe_db(probe_ledger_db);
	hb::server::remove_probe_db(probe_empty_db);

	// The store lays its own schema down, so the fixture cannot drift from the
	// real table definitions — the one thing a hand-written CREATE TABLE in a
	// prover is guaranteed to do eventually.
	if (!ledger.open(probe_ledger_db)) {
		hb::console::error("biographycheck: could not create the scratch ledger '{}'.",
			probe_ledger_db);
		cleanup();
		return;
	}

	if (!build_probe_ledger(ledger.handle(), item_id)) {
		hb::console::error("biographycheck: could not plant the fixture.");
		cleanup();
		return;
	}

	hb::server::validate_options options;
	options.sample_limit = 0;   // every finding, so the fixture can be read whole

	const validate_report report = hb::server::run_validation(ledger.handle(), options);
	tally.record("replay_completed", report.ok, report.ok ? "" : report.failure);

	// --- Ordinary lives are silent ----------------------------------------
	tally.record("full_legal_life_is_silent", silent(report, s_legal_life));
	tally.record("unplaced_item_may_go_anywhere", silent(report, s_unplaced_drop));
	tally.record("use_does_not_move_the_item", silent(report, s_use_no_move));
	tally.record("sell_and_deplete_are_not_exits", silent(report, s_sold));
	tally.record("escrow_out_lands_in_the_warehouse", silent(report, s_escrow));
	tally.record("census_lives_are_silent",
		silent(report, s_never_moved) && silent(report, s_in_world) && silent(report, s_ended));
	tally.record("backwards_timestamps_are_not_a_violation", silent(report, s_order));

	// --- One of every violation class -------------------------------------
	tally.record("event_after_exit_found",
		accused(report, ledger_violation::event_after_exit, s_after_exit));
	tally.record("second_exit_is_an_event_after_exit",
		accused(report, ledger_violation::event_after_exit, s_exit_twice)
		&& report.class_counts[ledger_violation::event_after_exit] == 2);
	tally.record("picked_up_twice_found",
		accused(report, ledger_violation::acquired_while_held, s_picked_twice));
	tally.record("retrieved_while_carrying_found",
		accused(report, ledger_violation::acquired_while_held, s_retrieve_held));
	tally.record("dropped_what_was_on_the_ground_found",
		only(report, ledger_violation::released_when_absent, s_drop_on_ground));
	tally.record("double_birth_found",
		accused(report, ledger_violation::double_creation, s_double_birth));
	tally.record("orphan_event_found",
		only(report, ledger_violation::orphan_event, s_orphan));

	// One cause, one finding. The orphan's second pickup is an
	// acquired_while_held too, and reporting both would make the class that
	// matters — a Serial with no origin — harder to pick out of the noise.
	tally.record("orphan_suppresses_its_other_findings",
		report.class_counts[ledger_violation::acquired_while_held] == 3
		&& !accused(report, ledger_violation::acquired_while_held, s_orphan));

	// --- The crash excuse, all three directions ---------------------------
	tally.record("crash_boundary_excuses_a_transition",
		silent(report, s_crash_gap) && report.gap_excused == 1);
	tally.record("clean_boundary_excuses_nothing",
		accused(report, ledger_violation::acquired_while_held, s_clean_gap));
	tally.record("crash_boundary_excuses_no_structural_violation",
		accused(report, ledger_violation::double_creation, s_struct_gap)
		&& report.class_counts[ledger_violation::double_creation] == 2);
	tally.record("only_crash_boundaries_count_as_gaps", report.gaps == 1);

	// --- Severity ----------------------------------------------------------
	tally.record("released_when_absent_is_the_only_warning",
		!hb::server::ledger_violation_is_critical(ledger_violation::released_when_absent)
		&& hb::server::ledger_violation_is_critical(ledger_violation::event_after_exit)
		&& hb::server::ledger_violation_is_critical(ledger_violation::acquired_while_held)
		&& hb::server::ledger_violation_is_critical(ledger_violation::double_creation)
		&& hb::server::ledger_violation_is_critical(ledger_violation::orphan_event));

	// --- The totals --------------------------------------------------------
	// Written as totals rather than by re-listing the classes: this is what
	// catches a sixth class firing on a fixture that was only meant to trip five.
	tally.record("violation_total_matches_the_fixture",
		report.violation_total() == expect_violations);
	tally.record("critical_total_matches_the_fixture",
		report.critical_total() == expect_critical);
	tally.record("every_serial_was_replayed",
		report.serials == expect_serials && report.instances == expect_instances);
	tally.record("census_counts_where_they_ended",
		report.ended == expect_ended && report.in_world == expect_in_world
		&& report.never_moved == expect_never_moved);

	// --- Capping the evidence does not cap the counts ---------------------
	{
		hb::server::validate_options capped = options;
		capped.sample_limit = 1;
		const validate_report short_report =
			hb::server::run_validation(ledger.handle(), capped);

		tally.record("cap_limits_the_evidence",
			samples_in(short_report, ledger_violation::event_after_exit) == 1);
		tally.record("cap_does_not_limit_the_count",
			short_report.class_counts[ledger_violation::event_after_exit] == 2);
	}

	// --- The Biography -----------------------------------------------------
	{
		const biography life = hb::server::read_biography(ledger.handle(), s_order);
		tally.record("biography_reads_in_event_order",
			life.ok && life.steps.size() == 3
			&& life.steps[0].event_type == ledger_event::created
			&& life.steps[1].event_type == ItemLogAction::Drop
			&& life.steps[2].event_type == ItemLogAction::get);
		tally.record("biography_tracks_the_place",
			life.ok && life.steps.size() == 3
			&& life.steps[0].move.to == ledger_place::limbo
			&& life.steps[1].move.to == ledger_place::ground
			&& life.steps[2].move.to == ledger_place::carried
			&& life.final_place == ledger_place::carried);
	}
	{
		const biography life = hb::server::read_biography(ledger.handle(), s_legal_life);
		tally.record("biography_reads_the_birth_row",
			life.ok && life.has_instance
			&& life.birth.item_id == item_id && life.birth.tier == 2
			&& life.birth.origin_type == hb::server::item_origin::npc_drop
			&& life.birth.origin_detail == "Orc Warrior"
			&& life.birth.map == "abaddon");
		tally.record("biography_agrees_with_the_validator", life.ok && life.violations == 0);
	}
	{
		const biography life = hb::server::read_biography(ledger.handle(), s_orphan);
		tally.record("biography_says_when_there_is_no_birth_row",
			life.ok && !life.has_instance && life.steps.size() == 3);
	}
	{
		// A Serial nothing was ever recorded against is an answer, not a failure.
		// A GM chasing a dispute types Serials by hand, and "no such thing" has to
		// be distinguishable from "the tool broke".
		const biography life = hb::server::read_biography(ledger.handle(), s_absent);
		tally.record("biography_answers_for_a_serial_with_nothing",
			life.ok && !life.has_instance && life.steps.empty()
			&& life.final_place == ledger_place::unborn);
	}
	{
		const biography life = hb::server::read_biography(ledger.handle(), s_crash_gap);
		tally.record("biography_marks_an_excused_step_without_counting_it",
			life.ok && life.violations == 0 && life.steps.size() == 4
			&& life.steps[3].excused
			&& life.steps[3].move.violation == ledger_violation::acquired_while_held);
	}

	// --- It runs against a read-only handle -------------------------------
	// The claim that makes this job usable at all: an operator points it at a
	// live server's file. If any part of the replay wrote, SQLite would refuse
	// here and the report would come back short.
	{
		sqlite3* read_only = nullptr;
		const bool opened = sqlite3_open_v2(probe_ledger_db, &read_only,
			SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK;

		validate_report same;
		if (opened) same = hb::server::run_validation(read_only, options);

		tally.record("read_only_handle_gives_the_same_answer",
			opened && same.ok && same.violation_total() == report.violation_total());

		if (read_only != nullptr) sqlite3_close(read_only);
	}

	// --- It refuses what is not a ledger ----------------------------------
	// The mistake an operator makes at a command line where several database
	// paths sit next to each other. A run that carried on would report a healthy
	// world as having no items at all.
	{
		const bool created = sqlite3_open_v2(probe_empty_db, &empty,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK
			&& exec(empty, "CREATE TABLE something(x INTEGER);");

		const validate_report refused = hb::server::run_validation(empty, options);
		tally.record("refuses_a_file_that_is_not_a_ledger",
			created && !refused.ok && !refused.failure.empty());

		const biography life = hb::server::read_biography(empty, s_legal_life);
		tally.record("biography_refuses_a_file_that_is_not_a_ledger",
			created && !life.ok && !life.failure.empty());

		tally.record("refuses_a_null_handle",
			!hb::server::run_validation(nullptr, options).ok
			&& !hb::server::read_biography(nullptr, s_legal_life).ok);
	}

	cleanup();

	tally.record("scratch_removed",
		!std::filesystem::exists(probe_ledger_db) && !std::filesystem::exists(probe_empty_db));

	tally.report();

	hb::logger::log<log_channel::commands>("biographycheck: {}/{} checks passed",
		tally.passed(), tally.total());
}
