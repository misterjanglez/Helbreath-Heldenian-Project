#include "ItemBiography.h"

#include "GameDatabase.h"         // sql_query, sql_table_exists
#include "ServerMessages.h"       // ItemLogAction
#include "json.hpp"               // the boundary event's detail
#include "sqlite3.h"

#include <algorithm>
#include <format>
#include <unordered_map>
#include <unordered_set>

namespace ItemLogAction = hb::server::net::ItemLogAction;
namespace ledger_event = hb::server::ledger_event;
namespace ledger_place = hb::server::ledger_place;
namespace ledger_violation = hb::server::ledger_violation;

namespace
{
	// The shared reader, tagged for this job.
	struct query : hb::server::sql_query
	{
		query(sqlite3* db, const std::string& sql) : sql_query(db, sql, "BIOGRAPHY") {}
	};

	// --- The legal transition table ---------------------------------------
	//
	// One row per event that means something about custody. `required` is the
	// place the event expects the item to already be in; `result` is where it
	// leaves it. Everything absent from the switch is a state change that says
	// nothing about custody — Use, Repair, both upgrade outcomes, and the six
	// actions that have a text-log arm and no caller — so it may happen wherever
	// the item is and moves it nowhere.
	//
	// This is the same set of events ItemReconciliation calls "locating", read
	// for a different purpose: that file wants the holder each event implies,
	// this one wants the holder each event *presupposes*. The one place the two
	// deliberately disagree is Sell and Deplete — see the header.
	struct rule
	{
		int  required;
		int  result;

		// A hand-off moves the item between two holders in a single event, so it
		// is the one shape that legitimately puts an item into custody while
		// somebody already has it. Excluded from the acquired-while-held class
		// for that reason and no other.
		bool hand_off;
	};

	rule rule_for(int event_type)
	{
		switch (event_type) {
		// Birth. The only rule with an exact starting state, because a Serial is
		// minted once (ItemProvenance.h) and a second one is a finding rather
		// than a move.
		case ledger_event::created:      return { ledger_place::unborn,  ledger_place::limbo,     false };

		// Out of nothing and into the world. `required` is `limbo` for all three,
		// which — since unplaced is a wildcard source — means "the item must not
		// already be somewhere". Buying, GM-minting or spawning an item that is
		// already held is the thing these catch.
		case ItemLogAction::NewGenDrop:  return { ledger_place::limbo,   ledger_place::ground,    false };
		case ItemLogAction::Buy:         return { ledger_place::limbo,   ledger_place::carried,   false };
		case ItemLogAction::GmMint:      return { ledger_place::limbo,   ledger_place::carried,   false };

		// The ground, both ways.
		case ItemLogAction::Drop:        return { ledger_place::carried, ledger_place::ground,    false };
		case ItemLogAction::get:         return { ledger_place::ground,  ledger_place::carried,   false };

		// The Warehouse, both ways.
		case ItemLogAction::Deposit:     return { ledger_place::carried, ledger_place::banked,    false };
		case ItemLogAction::Retrieve:    return { ledger_place::banked,  ledger_place::carried,   false };

		// Hand to hand. The receiving character is the counterparty column, which
		// is what the Biography prints; the place is unchanged because both ends
		// of a hand-off are an inventory.
		case ItemLogAction::Give:        return { ledger_place::carried, ledger_place::carried,   true  };
		case ItemLogAction::Exchange:    return { ledger_place::carried, ledger_place::carried,   true  };

		// Into the board's custody, and back out of it. Every escrow-out lands in
		// a Warehouse — deliver_to_bank is the single door out of escrow (ADR
		// 0001) — so none of the five returns to an inventory.
		case ItemLogAction::TpList:      return { ledger_place::carried, ledger_place::escrow,    false };
		case ItemLogAction::TpOffer:     return { ledger_place::carried, ledger_place::escrow,    false };
		case ItemLogAction::TpDelist:
		case ItemLogAction::TpRescind:
		case ItemLogAction::TpRefund:
		case ItemLogAction::TpTradeOut:
		case ItemLogAction::TpTradeIn:   return { ledger_place::escrow,  ledger_place::banked,    false };

		// The slot emptied — which is NOT the item leaving the world. The real
		// order a shop sale writes is Sell, then Deplete, then destroyed
		// (ItemManager::item_deplete_handler), so treating either of these as an
		// exit would report the destruction that follows it as an event after the
		// item's death — on every sale in the database.
		case ItemLogAction::Sell:
		case ItemLogAction::Deplete:     return { ledger_place::carried, ledger_place::unchanged, false };

		// The two real exits. A despawn is a ground item timing out, so it has a
		// starting place; a destruction can reach an item anywhere, including in
		// escrow (ADR 0001's delivery failures) and on the ground (a fire spell).
		case ledger_event::despawned:    return { ledger_place::ground,  ledger_place::gone,      false };
		case ledger_event::destroyed:    return { ledger_place::any,     ledger_place::gone,      false };

		default:                         return { ledger_place::any,     ledger_place::unchanged, false };
		}
	}

	// A place in which somebody has the item. The ground is not one of these: an
	// item lying on a tile is in the world and in nobody's custody, which is why
	// two events placing it there is a lost drop rather than a duplication.
	bool is_custody(int place)
	{
		return place == ledger_place::carried
			|| place == ledger_place::banked
			|| place == ledger_place::escrow;
	}

	// --- Reading the run boundaries ---------------------------------------
	//
	// The ledger flushes in windows, so a crash loses the tail of one (D4,
	// ItemLedgerStore.h). The events that vanish are ordinary moves, and losing a
	// Drop is enough to make the pickup that follows it read as the strongest
	// finding this file has. Without this the first crash on a live server would
	// produce a page of critical violations describing nothing.
	//
	// The boundary event written at every boot says whether the previous run
	// stopped cleanly. The ones that say it did not are the only places a hole
	// can be.
	std::vector<int64_t> load_gap_boundaries(sqlite3* ledger)
	{
		std::vector<int64_t> gaps;

		query rows(ledger, std::format(
			"SELECT event_id, detail FROM item_events WHERE event_type = {} ORDER BY event_id;",
			static_cast<int>(ledger_event::boundary)));

		while (rows.step()) {
			// Parsed rather than searched for a substring: the column is declared
			// as JSON and a boundary whose detail does not parse is a boundary
			// this cannot make claims about, which is the safe direction — an
			// unreadable marker excuses nothing.
			const nlohmann::json detail =
				nlohmann::json::parse(rows.as_text(1), nullptr, false);
			if (detail.is_discarded() || !detail.is_object()) continue;

			const auto previous = detail.find("prev_shutdown");
			if (previous != detail.end() && previous->is_string()
				&& previous->get<std::string>() == "crash") {
				gaps.push_back(rows.as_int(0));
			}
		}

		return gaps;
	}

	// Whether a hole sits between two of one Serial's events. `previous_id` of 0
	// means this is the Serial's first event: nothing was lost between it and an
	// event that does not exist, so a first-event violation is never excused.
	bool gap_between(const std::vector<int64_t>& gaps, int64_t previous_id, int64_t event_id)
	{
		if (previous_id <= 0 || gaps.empty()) return false;
		const auto after = std::upper_bound(gaps.begin(), gaps.end(), previous_id);
		return after != gaps.end() && *after < event_id;
	}

	// Which classes a hole can account for. Only the ones whose verdict depends
	// on the machine's memory of where the item was, because that memory is
	// exactly what a lost flush corrupts.
	//
	// The two structural classes are never excused: a birth row and its `created`
	// event are buffered together and written in one transaction
	// (ItemLedgerStore::flush), so no hole can produce a Serial that was born
	// twice or an event about a Serial that was never born at all.
	bool excusable(int class_id)
	{
		return class_id == ledger_violation::event_after_exit
			|| class_id == ledger_violation::acquired_while_held
			|| class_id == ledger_violation::released_when_absent;
	}

	// The two tables this reads. Named rather than version-checked, because the
	// tool is expected to be pointed at snapshots — including ones older than
	// whatever the current schema version is.
	bool is_a_ledger(sqlite3* ledger, std::string& failure)
	{
		for (const char* table : { "item_instances", "item_events" }) {
			if (hb::server::sql_table_exists(ledger, table, "BIOGRAPHY")) continue;

			const char* name = sqlite3_db_filename(ledger, "main");
			failure = std::format("'{}' is not a Provenance Ledger: no {} table",
				name != nullptr ? name : "?", table);
			return false;
		}
		return true;
	}

	// The row a biography_step is built from, in the order read_event_columns
	// expects. Beside the reader so the column list and the code that indexes it
	// cannot drift apart.
	constexpr const char* event_columns =
		"event_id, at, event_type, actor_account, actor_char, counterparty_char,"
		" map, x, y, detail";

	void read_event_columns(const query& row, hb::server::biography_step& step)
	{
		step.event_id          = row.as_int(0);
		step.at                = row.as_int(1);
		step.event_type        = static_cast<int32_t>(row.as_int(2));
		step.actor_account     = row.as_text(3);
		step.actor_char        = row.as_text(4);
		step.counterparty_char = row.as_text(5);
		step.map               = row.as_text(6);
		step.x                 = static_cast<int32_t>(row.as_int(7));
		step.y                 = static_cast<int32_t>(row.as_int(8));
		step.detail            = row.as_text(9);
	}

	// One violation, said in a sentence. Written once because the validator
	// prints these and the Biography prints the same judgement about the same
	// event — two wordings would be two claims.
	std::string describe(int event_type, const std::string& actor,
		const hb::server::ledger_transition& move, int exit_event)
	{
		const std::string name = hb::server::ledger_event_name(event_type);
		const char* who = hb::server::ledger_actor_name(actor);

		switch (move.violation) {
		case ledger_violation::event_after_exit:
			return std::format("{} by {} after the item left the world by {}",
				name, who, hb::server::ledger_event_name(exit_event));

		case ledger_violation::double_creation:
			return std::format("a second {}; the first had left it {}",
				name, hb::server::ledger_place_name(move.from));

		default:
			return std::format("{} by {} from {}, expected {}",
				name, who, hb::server::ledger_place_name(move.from),
				hb::server::ledger_place_name(move.required));
		}
	}
}

namespace hb::server
{
	const char* ledger_place_name(int place)
	{
		switch (place) {
		case ledger_place::unborn:  return "unborn";
		// Not "limbo": the state is about the ledger never having been told where
		// the item went, and an operator reading "limbo" would reasonably think it
		// described the item rather than the record.
		case ledger_place::limbo:   return "unplaced";
		case ledger_place::carried: return "inventory";
		case ledger_place::banked:  return "warehouse";
		case ledger_place::escrow:  return "escrow";
		case ledger_place::ground:  return "ground";
		case ledger_place::gone:    return "gone";
		case ledger_place::any:     return "anywhere";
		default:                    return "unchanged";
		}
	}

	const char* ledger_actor_name(const std::string& name)
	{
		return name.empty() ? "(nobody)" : name.c_str();
	}

	const char* ledger_violation_name(int class_id)
	{
		switch (class_id) {
		case ledger_violation::event_after_exit:     return "event_after_exit";
		case ledger_violation::acquired_while_held:  return "acquired_while_held";
		case ledger_violation::released_when_absent: return "released_when_absent";
		case ledger_violation::double_creation:      return "double_creation";
		case ledger_violation::orphan_event:         return "orphan_event";
		default:                                     return "unknown_class";
		}
	}

	bool ledger_violation_is_critical(int class_id)
	{
		switch (class_id) {
		case ledger_violation::event_after_exit:
		case ledger_violation::acquired_while_held:
		case ledger_violation::double_creation:
		case ledger_violation::orphan_event:
			return true;

		default:
			// released_when_absent alone. It is the shape a hole makes from the
			// other direction: the ledger believed the item was somewhere it had
			// already left, and afterwards it is in no more custody than before.
			return false;
		}
	}

	std::string ledger_event_name(int event_type)
	{
		switch (event_type) {
		case ItemLogAction::Give:           return "Give";
		case ItemLogAction::Drop:           return "Drop";
		case ItemLogAction::get:            return "get";
		case ItemLogAction::Deplete:        return "Deplete";
		case ItemLogAction::NewGenDrop:     return "NewGenDrop";
		case ItemLogAction::Buy:            return "Buy";
		case ItemLogAction::Sell:           return "Sell";
		case ItemLogAction::Retrieve:       return "Retrieve";
		case ItemLogAction::Deposit:        return "Deposit";
		case ItemLogAction::Exchange:       return "Exchange";
		case ItemLogAction::SkillLearn:     return "SkillLearn";
		case ItemLogAction::Make:           return "Make";
		case ItemLogAction::SummonMonster:  return "SummonMonster";
		case ItemLogAction::Poisoned:       return "Poisoned";
		case ItemLogAction::MagicLearn:     return "MagicLearn";
		case ItemLogAction::Repair:         return "Repair";
		case ItemLogAction::UpgradeFail:    return "UpgradeFail";
		case ItemLogAction::UpgradeSuccess: return "UpgradeSuccess";
		case ItemLogAction::Use:            return "Use";
		case ItemLogAction::TpList:         return "TpList";
		case ItemLogAction::TpDelist:       return "TpDelist";
		case ItemLogAction::TpOffer:        return "TpOffer";
		case ItemLogAction::TpRescind:      return "TpRescind";
		case ItemLogAction::TpRefund:       return "TpRefund";
		case ItemLogAction::TpTradeOut:     return "TpTradeOut";
		case ItemLogAction::TpTradeIn:      return "TpTradeIn";
		case ItemLogAction::GmMint:         return "GmMint";
		case ItemLogAction::GuildDonation:  return "GuildDonation";
		case ItemLogAction::GuildTreasury:  return "GuildTreasury";
		case ledger_event::created:         return "created";
		case ledger_event::despawned:       return "despawned";
		case ledger_event::destroyed:       return "destroyed";
		case ledger_event::boundary:        return "boundary";
		// A number the build does not know. Printed as itself rather than as
		// "unknown", because the number is the only thing that identifies it and
		// a reader needs it to go and look the row up.
		default:                            return std::format("event_{}", event_type);
		}
	}

	ledger_transition ledger_step(int place, int event_type)
	{
		const rule applies = rule_for(event_type);

		ledger_transition move;
		move.from     = place;
		move.to       = place;
		move.required = applies.required;
		move.violation = ledger_violation::none;

		// Terminal first, and before the rule is consulted at all. Once the item
		// has left the world nothing may happen to it, which is also how the
		// plan's "destroyed once" rule is enforced — a second exit is an event
		// after the first one.
		if (place == ledger_place::gone) {
			move.violation = ledger_violation::event_after_exit;
			return move;
		}

		if (event_type == ledger_event::created) {
			if (place != ledger_place::unborn) {
				// Already alive. The place is left as it was rather than reset:
				// whatever the item was actually doing, a spurious birth row does
				// not move it, and the replay should carry on from the truth.
				move.violation = ledger_violation::double_creation;
				return move;
			}
			move.to = ledger_place::limbo;
			return move;
		}

		// Unplaced is a wildcard source. Most creation venues never record where
		// they put the item (see the header), so an item that has only ever been
		// minted may legally turn up anywhere; the machine starts holding the
		// ledger to its word from the first event that says where the item is.
		const bool unplaced = (place == ledger_place::unborn || place == ledger_place::limbo);
		const bool legal = unplaced
			|| applies.required == ledger_place::any
			|| applies.required == place;

		// The event's own answer wins even when it was impossible to get here.
		// The alternative — refusing to move an item whose arrival was illegal —
		// would make one bad row misreport every event after it, turning a single
		// finding into a cascade.
		move.to = (applies.result == ledger_place::unchanged) ? place : applies.result;

		if (!legal) {
			move.violation = (!applies.hand_off && is_custody(place) && is_custody(move.to))
				? ledger_violation::acquired_while_held
				: ledger_violation::released_when_absent;
		}

		return move;
	}

	// --- The Biography ------------------------------------------------------
	biography read_biography(sqlite3* ledger, int64_t serial)
	{
		biography life;
		life.serial = serial;

		if (ledger == nullptr) {
			life.failure = "the ledger handle is null";
			return life;
		}
		if (!is_a_ledger(ledger, life.failure)) {
			return life;
		}

		{
			query birth(ledger, std::format(
				"SELECT serial, item_id, created_at, origin_type, COALESCE(origin_detail,''),"
				" COALESCE(map,''), COALESCE(x,0), COALESCE(y,0), tier"
				" FROM item_instances WHERE serial = {};", serial));
			if (birth.step()) {
				life.has_instance      = true;
				life.birth.serial      = birth.as_int(0);
				life.birth.item_id     = static_cast<int32_t>(birth.as_int(1));
				life.birth.created_at  = birth.as_int(2);
				life.birth.origin_type = static_cast<int32_t>(birth.as_int(3));
				life.birth.origin_detail = birth.as_text(4);
				life.birth.map         = birth.as_text(5);
				life.birth.x           = static_cast<int32_t>(birth.as_int(6));
				life.birth.y           = static_cast<int32_t>(birth.as_int(7));
				life.birth.tier        = static_cast<uint8_t>(birth.as_int(8));
			}
		}

		const std::vector<int64_t> gaps = load_gap_boundaries(ledger);

		query events(ledger, std::format(
			"SELECT {} FROM item_events WHERE serial = {} ORDER BY event_id;",
			event_columns, serial));
		if (!events) {
			life.failure = "the event query would not compile against this ledger";
			return life;
		}

		int     place = ledger_place::unborn;
		int64_t previous_id = 0;

		while (events.step()) {
			biography_step step;
			read_event_columns(events, step);

			step.move = ledger_step(place, step.event_type);
			step.excused = excusable(step.move.violation)
				&& gap_between(gaps, previous_id, step.event_id);

			if (step.move.violation != ledger_violation::none && !step.excused) {
				life.violations++;
			}

			place = step.move.to;
			previous_id = step.event_id;
			life.steps.push_back(std::move(step));
		}

		life.final_place = place;
		life.ok = true;
		return life;
	}

	// --- The validator ------------------------------------------------------
	int64_t validate_report::violation_total() const
	{
		int64_t total = 0;
		for (int i = 0; i < ledger_violation::count; i++) total += class_counts[i];
		return total;
	}

	int64_t validate_report::critical_total() const
	{
		int64_t total = 0;
		for (int i = 0; i < ledger_violation::count; i++) {
			if (ledger_violation_is_critical(i)) total += class_counts[i];
		}
		return total;
	}

	validate_report run_validation(sqlite3* ledger, const validate_options& options)
	{
		validate_report report;

		if (ledger == nullptr) {
			report.failure = "the ledger handle is null";
			return report;
		}
		if (!is_a_ledger(ledger, report.failure)) {
			return report;
		}

		const auto keep = [&](int class_id, int64_t serial, std::string detail)
		{
			report.class_counts[class_id]++;
			if (options.sample_limit == 0
				|| static_cast<size_t>(report.class_counts[class_id]) <= options.sample_limit) {
				report.samples.push_back({ class_id, serial, std::move(detail) });
			}
		};

		// Every birth row, so an event about a Serial with no origin is
		// answerable. One int64 per item the server has ever minted.
		std::unordered_set<int64_t> born;
		{
			query births(ledger, "SELECT serial FROM item_instances;");
			while (births.step()) born.insert(births.as_int(0));
		}
		report.instances = static_cast<int64_t>(born.size());

		const std::vector<int64_t> gaps = load_gap_boundaries(ledger);
		report.gaps = static_cast<int64_t>(gaps.size());

		// One record per Serial, carried across a single ordered pass. The
		// alternative — a query per Serial — reads the events table once per item
		// the server has ever minted.
		struct replay
		{
			int     place = ledger_place::unborn;
			int64_t previous_id = 0;
			int64_t events = 0;
			int32_t exit_event = 0;    // what took it out of the world, for the report
			bool    orphan = false;
			bool    orphan_reported = false;
		};
		std::unordered_map<int64_t, replay> lives;

		// Four columns rather than the Biography's ten. This pass reads every
		// event the server has ever recorded, and the six it does not need are
		// six string allocations per row for text nothing prints — the map, the
		// detail JSON, the account and the counterparty all belong to a report
		// about one item, not to a verdict about all of them.
		//
		// Serial 0 is the run boundary's sentinel and not an item; it is read
		// above for the gap list and excluded here so it cannot be replayed as
		// one very long-lived object.
		query events(ledger,
			"SELECT event_id, event_type, COALESCE(actor_char,''), serial"
			" FROM item_events WHERE serial != 0 ORDER BY event_id;");
		if (!events) {
			report.failure = "the event query would not compile against this ledger";
			return report;
		}

		while (events.step()) {
			const int64_t event_id = events.as_int(0);
			const int event_type = static_cast<int>(events.as_int(1));
			const std::string actor = events.as_text(2);
			const int64_t serial = events.as_int(3);

			replay& life = lives[serial];
			if (life.events == 0) {
				life.orphan = born.find(serial) == born.end();
			}
			life.events++;
			report.events++;

			const ledger_transition move = ledger_step(life.place, event_type);

			// An orphan is reported once, as the Serial's own finding, and its
			// transitions are not additionally accused. One cause, one finding:
			// a Serial with no origin will usually break the machine too, and
			// counting both would make the class that matters harder to see.
			if (life.orphan) {
				if (!life.orphan_reported) {
					life.orphan_reported = true;
					keep(ledger_violation::orphan_event, serial,
						std::format("first event {} by {}, no birth row",
							ledger_event_name(event_type), ledger_actor_name(actor)));
				}
			}
			else if (move.violation != ledger_violation::none) {
				if (excusable(move.violation)
					&& gap_between(gaps, life.previous_id, event_id)) {
					report.gap_excused++;
				}
				else {
					keep(move.violation, serial,
						describe(event_type, actor, move, life.exit_event));
				}
			}

			if (move.to == ledger_place::gone && life.place != ledger_place::gone) {
				life.exit_event = static_cast<int32_t>(event_type);
			}
			life.place = move.to;
			life.previous_id = event_id;
		}

		// --- The census -------------------------------------------------------
		for (const auto& [serial, life] : lives) {
			if (life.place == ledger_place::gone) report.ended++;
			else if (life.place == ledger_place::unborn || life.place == ledger_place::limbo)
				report.never_moved++;
			else report.in_world++;
		}

		// A birth row whose `created` event never reached disk has no entry
		// above. It is still a Serial the ledger knows about, and it has still
		// never moved.
		int64_t silent_births = 0;
		for (int64_t serial : born) {
			if (lives.find(serial) == lives.end()) silent_births++;
		}
		report.never_moved += silent_births;
		report.serials = static_cast<int64_t>(lives.size()) + silent_births;

		// A stable order for the evidence: by class, then by Serial. The pass
		// walks a hash map, so without this the same file would print its
		// findings in a different order on a different run — and a report an
		// operator cannot diff against yesterday's is worth much less.
		std::sort(report.samples.begin(), report.samples.end(),
			[](const ledger_violation_record& a, const ledger_violation_record& b)
			{
				if (a.class_id != b.class_id) return a.class_id < b.class_id;
				return a.serial < b.serial;
			});

		report.ok = true;
		return report;
	}
}
