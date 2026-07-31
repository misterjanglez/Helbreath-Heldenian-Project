#include "ItemReconciliation.h"

#include "Game.h"                 // the item config table, for instanced_item_lookup
#include "GameDatabase.h"         // stmt_scope
#include "Item.h"
#include "ItemLedgerStore.h"      // ledger_event, and ItemLogAction through it
#include "Log.h"
#include "StringCompat.h"
#include "sqlite3.h"

#include <algorithm>
#include <format>
#include <unordered_map>

namespace ItemLogAction = hb::server::net::ItemLogAction;
namespace ledger_event = hb::server::ledger_event;

namespace
{
	// Where a Serial is, as one of the two sides claims. The snapshot can only
	// ever say one of the first four; the ledger can say any of them.
	namespace place
	{
		enum place : int
		{
			nowhere = 0,
			inventory,      // a character's carried slots
			bank,           // a character's Warehouse
			escrow_listing, // a Trading Post Listing's bundle
			escrow_offer,   // an Offer's bundle
			ground,         // a map tile
			gone,           // it left the world
			unnamed,        // it moved to somebody the event did not name
		};
	}

	const char* place_name(int where)
	{
		switch (where) {
		case place::inventory:      return "inventory";
		case place::bank:           return "bank";
		case place::escrow_listing: return "listing";
		case place::escrow_offer:   return "offer";
		case place::ground:         return "ground";
		case place::gone:           return "gone";
		case place::unnamed:        return "unnamed-holder";
		default:                    return "unplaced";
		}
	}

	// Escrow is the board's custody, not the character's, so the two escrow
	// places compare equal to each other and to nothing else. A Listing whose
	// bundle the snapshot files under the Offer table would still be an
	// anomaly — but it would be a Trading Post bug, not a dupe, and this job
	// reports custody.
	bool is_escrow(int where)
	{
		return where == place::escrow_listing || where == place::escrow_offer;
	}

	struct holder
	{
		int         where = place::nowhere;
		std::string who;      // character name; the board's rows carry the party's
	};

	// The ledger's opinion about one Serial, folded from its last locating event.
	struct ledger_view
	{
		holder at;
		bool   has_instance = false;
	};

	// Every holder row the snapshot has for one Serial. One is the healthy case;
	// the vector is what makes the second one a finding rather than a silent
	// overwrite.
	struct snapshot_view
	{
		std::vector<holder> rows;
	};

	// --- The locating map -------------------------------------------------
	//
	// Which events say where an item ended up, and whose name to read off them.
	// Everything not named here is deliberately NOT locating: the birth row
	// (nothing holds a freshly minted item yet), Use and both upgrade outcomes
	// (the holder is unchanged, so the previous locating event is still the
	// answer), the run boundary (not an item event at all), and the six actions
	// that have a text-log arm and no caller. Excluding them is what stops a
	// player using a potion from erasing the ledger's memory of who picked it up.
	namespace locate
	{
		enum locate : int
		{
			not_locating = 0,
			actor_inventory,
			counterparty_inventory,
			actor_bank,
			to_escrow,
			to_ground,
			left_world,
		};
	}

	int locating_rule(int event_type)
	{
		switch (event_type) {
		// Into the actor's own hands.
		case ItemLogAction::get:        // 3  — picked up off the ground
		case ItemLogAction::Buy:        // 7  — bought from a shop
		case ItemLogAction::Retrieve:   // 9  — taken out of the Warehouse
		case ItemLogAction::GmMint:     // 40 — audited against the RECEIVING character
			return locate::actor_inventory;

		// Into the other party's hands. Both of these are the two-account
		// operations #82 made atomic, so the receiver is the counterparty column.
		case ItemLogAction::Give:       // 1
		case ItemLogAction::Exchange:   // 11
			return locate::counterparty_inventory;

		// Into the actor's Warehouse. Every escrow-out lands in a bank rather
		// than an inventory — deliver_to_bank is the single door out of escrow
		// (ADR 0001) and the actor of those events is the recipient, not the
		// party who gave the items up.
		case ItemLogAction::Deposit:    // 10
		case ItemLogAction::TpDelist:   // 34
		case ItemLogAction::TpRescind:  // 36
		case ItemLogAction::TpRefund:   // 37
		case ItemLogAction::TpTradeOut: // 38
		case ItemLogAction::TpTradeIn:  // 39
			return locate::actor_bank;

		// Into the board's custody.
		case ItemLogAction::TpList:     // 33
		case ItemLogAction::TpOffer:    // 35
			return locate::to_escrow;

		// Onto a map tile. Ground items are RAM-only, so this is the one ledger
		// state that legitimately has no snapshot row behind it.
		case ItemLogAction::Drop:       // 2
		case ItemLogAction::NewGenDrop: // 5
			return locate::to_ground;

		// Out of the world. Deplete and Sell are followed by a `destroyed` event
		// from the same funnel; they are listed anyway so a lost flush that
		// carried only the destruction still leaves the ledger with the right
		// answer.
		case ItemLogAction::Deplete:    // 4
		case ItemLogAction::Sell:       // 8
		case ledger_event::despawned:   // 101
		case ledger_event::destroyed:   // 102
			return locate::left_world;

		default:
			return locate::not_locating;
		}
	}

	// The same set as an SQL IN list, so the query returns only events that can
	// answer the question. Built from the enum values rather than typed out, so
	// a number that changes in one place cannot disagree with the other.
	std::string locating_event_list()
	{
		std::string list;
		for (int type = 1; type <= ledger_event::boundary; type++) {
			if (locating_rule(type) == locate::not_locating) continue;
			if (!list.empty()) list += ',';
			list += std::to_string(type);
		}
		return list;
	}

	// --- Reading rows ------------------------------------------------------
	// The shared reader (GameDatabase.h), tagged so a query that will not compile
	// names this job in the log. Named `query` locally because that is what every
	// call site below already calls it, and because the tag is a property of the
	// file rather than of the call.
	struct query : hb::server::sql_query
	{
		query(sqlite3* db, const std::string& sql) : sql_query(db, sql, "RECONCILE") {}
	};

	int64_t scalar(sqlite3* db, const char* sql, int64_t fallback)
	{
		return hb::server::sql_scalar(db, sql, fallback, "RECONCILE");
	}

	bool table_exists(sqlite3* db, const char* name)
	{
		return hb::server::sql_table_exists(db, name, "RECONCILE");
	}

	// A name for the report. Empty is what the schema stores as NULL, and the
	// two sides must render it the same way or every comparison against an
	// unnamed holder reads as a mismatch of spelling.
	const char* shown(const std::string& name)
	{
		return name.empty() ? "(none)" : name.c_str();
	}

	// Character names are COLLATE NOCASE in every table that holds one, so the
	// comparison has to be as well. Comparing them case-sensitively would report
	// a mismatch for the same player spelled two ways — which the database
	// itself considers one player.
	bool same_name(const std::string& a, const std::string& b)
	{
		return hb_stricmp(a.c_str(), b.c_str()) == 0;
	}
}

namespace hb::server
{
	const char* reconcile_class_name(int class_id)
	{
		switch (class_id) {
		case reconcile_class::held_twice:                  return "held_twice";
		case reconcile_class::held_but_gone:               return "held_but_gone";
		case reconcile_class::held_but_ground:             return "held_but_ground";
		case reconcile_class::unknown_serial:              return "unknown_serial";
		case reconcile_class::holder_mismatch:             return "holder_mismatch";
		case reconcile_class::missing_holder:              return "missing_holder";
		case reconcile_class::held_unrecorded:             return "held_unrecorded";
		case reconcile_class::serial_on_instanced_missing: return "serial_missing";
		case reconcile_class::serial_on_counted:           return "serial_on_counted";
		default:                                           return "unknown_class";
		}
	}

	bool reconcile_class_is_critical(int class_id)
	{
		switch (class_id) {
		case reconcile_class::held_twice:
		case reconcile_class::held_but_gone:
		case reconcile_class::held_but_ground:
		case reconcile_class::unknown_serial:
			return true;
		default:
			// The rest are states a live run can produce honestly: an online
			// player's inventory is RAM until it saves, so every transition since
			// the last save reads as a mismatch or a missing holder.
			return false;
		}
	}

	std::function<bool(int)> instanced_item_lookup(CGame& game)
	{
		return [&game](int item_id)
		{
			if (item_id < 0 || item_id >= config::MaxItemTypes) return false;
			const CItem* config = game.m_item_config_list[item_id];
			return config != nullptr && !config->is_stackable();
		};
	}

	int64_t reconcile_report::anomaly_total() const
	{
		int64_t total = 0;
		for (int i = 0; i < reconcile_class::count; i++) total += class_counts[i];
		return total;
	}

	int64_t reconcile_report::critical_total() const
	{
		int64_t total = 0;
		for (int i = 0; i < reconcile_class::count; i++) {
			if (reconcile_class_is_critical(i)) total += class_counts[i];
		}
		return total;
	}

	reconcile_report run_reconciliation(sqlite3* world, sqlite3* ledger,
		const reconcile_options& options)
	{
		reconcile_report report;

		if (world == nullptr || ledger == nullptr) {
			report.failure = "a database handle is null";
			return report;
		}

		// Named tables rather than a schema-version check: this job is expected
		// to be pointed at snapshots, including ones taken before whatever the
		// current version is, and it only reads columns that have existed since
		// the Serial arrived. What it cannot survive is a table that is not there.
		for (const char* name : { "character_items", "character_bank_items",
				"listing_items", "offer_items", "listings", "offers" }) {
			if (!table_exists(world, name)) {
				report.failure = std::format("'{}' is not a Game DB: no {} table",
					sqlite3_db_filename(world, "main") ? sqlite3_db_filename(world, "main") : "?",
					name);
				return report;
			}
		}
		for (const char* name : { "item_instances", "item_events" }) {
			if (!table_exists(ledger, name)) {
				report.failure = std::format("'{}' is not a Provenance Ledger: no {} table",
					sqlite3_db_filename(ledger, "main") ? sqlite3_db_filename(ledger, "main") : "?",
					name);
				return report;
			}
		}

		const auto keep = [&](int class_id, int64_t serial, std::string detail)
		{
			report.class_counts[class_id]++;
			if (options.sample_limit == 0
				|| static_cast<size_t>(report.class_counts[class_id]) <= options.sample_limit) {
				report.samples.push_back({ class_id, serial, std::move(detail) });
			}
		};

		// --- The snapshot side -----------------------------------------------
		// One pass over the four holder tables. The escrow tables join their
		// parent to name the party whose items are on the board — a Listing's
		// seller, an Offer's offerer — because "the board holds it" without
		// saying whose bundle it is answers half the dispute it exists to settle.
		//
		// Rows with serial 0 come back too: a Counted row is expected to have
		// none, but an Instanced one that has none cannot be reconciled at all,
		// and that is worth a finding rather than a silent omission from the scan.
		std::unordered_map<int64_t, snapshot_view> held;

		{
			query q(world,
				"SELECT serial, character_name, item_id, 1 FROM character_items"
				" UNION ALL"
				" SELECT serial, character_name, item_id, 2 FROM character_bank_items"
				" UNION ALL"
				" SELECT li.serial, l.seller_name, li.item_id, 3"
				"  FROM listing_items li JOIN listings l ON l.listing_id = li.listing_id"
				" UNION ALL"
				" SELECT oi.serial, o.offerer_name, oi.item_id, 4"
				"  FROM offer_items oi JOIN offers o ON o.offer_id = oi.offer_id;");
			if (!q) {
				report.failure = "the holder query would not compile against this Game DB";
				return report;
			}

			while (q.step()) {
				const int64_t serial = q.as_int(0);
				const std::string owner = q.as_text(1);
				const int item_id = static_cast<int>(q.as_int(2));
				const int source = static_cast<int>(q.as_int(3));

				const int where = (source == 1) ? place::inventory
					: (source == 2) ? place::bank
					: (source == 3) ? place::escrow_listing
					: place::escrow_offer;

				if (options.is_instanced) {
					const bool instanced = options.is_instanced(item_id);
					if (serial == 0 && instanced) {
						keep(reconcile_class::serial_on_instanced_missing, 0,
							std::format("item {} held by {} in {} carries no Serial",
								item_id, shown(owner), place_name(where)));
					}
					else if (serial != 0 && !instanced) {
						keep(reconcile_class::serial_on_counted, serial,
							std::format("stackable item {} held by {} in {} carries a Serial",
								item_id, shown(owner), place_name(where)));
					}
				}

				if (serial == 0) continue;

				report.held_rows++;
				held[serial].rows.push_back({ where, owner });
			}
		}
		report.held_serials = static_cast<int64_t>(held.size());

		// --- The ledger side --------------------------------------------------
		report.instances = scalar(ledger, "SELECT COUNT(*) FROM item_instances;", 0);
		report.high_water = scalar(ledger,
			"SELECT CAST(value AS INTEGER) FROM meta WHERE key='serial_high_water';", 0);
		report.high_water = std::max(report.high_water,
			scalar(ledger, "SELECT COALESCE(MAX(serial),0) FROM item_instances;", 0));

		std::unordered_map<int64_t, ledger_view> believed;

		{
			// Every birth row, so a held Serial with none is answerable. This is
			// the whole instance table in RAM — one int64 and a small struct per
			// item the server has ever minted.
			query q(ledger, "SELECT serial FROM item_instances;");
			while (q.step()) {
				believed[q.as_int(0)].has_instance = true;
			}
		}

		{
			// The last locating event per Serial. The inner GROUP BY is the whole
			// derivation: event_id is assigned in insertion order and the buffer
			// flushes in the order events were recorded, so the highest id for a
			// Serial is its latest transition. A correlated subquery would have
			// said the same thing one Serial at a time.
			query q(ledger, std::format(
				"SELECT e.serial, e.event_type, e.actor_char, e.counterparty_char"
				" FROM item_events e"
				" JOIN (SELECT serial, MAX(event_id) AS last_id FROM item_events"
				"       WHERE event_type IN ({}) AND serial != 0"
				"       GROUP BY serial) m"
				"  ON m.serial = e.serial AND m.last_id = e.event_id;",
				locating_event_list()));
			if (!q) {
				report.failure = "the holder-derivation query would not compile against this ledger";
				return report;
			}

			while (q.step()) {
				const int64_t serial = q.as_int(0);
				const int event_type = static_cast<int>(q.as_int(1));
				const std::string actor = q.as_text(2);
				const std::string counterparty = q.as_text(3);

				holder& at = believed[serial].at;
				switch (locating_rule(event_type)) {
				case locate::actor_inventory:
					at = { place::inventory, actor };
					break;
				case locate::counterparty_inventory:
					at = { place::inventory, counterparty };
					break;
				case locate::actor_bank:
					at = { place::bank, actor };
					break;
				case locate::to_escrow:
					// Which of the two escrow tables holds it is the board's
					// business; the ledger records the party and the transition.
					at = { event_type == ItemLogAction::TpList
						? place::escrow_listing : place::escrow_offer, actor };
					break;
				case locate::to_ground:
					at = { place::ground, {} };
					break;
				case locate::left_world:
					at = { place::gone, {} };
					break;
				default:
					break;
				}

				// A locating event that names nobody cannot be compared: the
				// client had already gone by the time the event was written, so
				// the ledger knows the item moved and not to whom. Its own state,
				// counted rather than guessed at — folding it into "never placed"
				// would say the opposite of what happened.
				if ((at.where == place::inventory || at.where == place::bank
						|| is_escrow(at.where)) && at.who.empty()) {
					at.where = place::unnamed;
					report.indeterminate++;
				}
			}
		}

		// --- The comparison ---------------------------------------------------
		for (const auto& [serial, snapshot] : held) {
			const auto found = believed.find(serial);
			const ledger_view* view = (found != believed.end()) ? &found->second : nullptr;

			if (view == nullptr || !view->has_instance) {
				keep(reconcile_class::unknown_serial, serial,
					serial > report.high_water
						? std::format("held by {} in {}, above the ledger high-water {}",
							shown(snapshot.rows.front().who),
							place_name(snapshot.rows.front().where), report.high_water)
						: std::format("held by {} in {}, no birth row",
							shown(snapshot.rows.front().who),
							place_name(snapshot.rows.front().where)));
				continue;
			}

			// The dupe. Reported before the holder comparison and instead of it:
			// with two rows claiming one object there is no single holder to
			// compare, and one finding per Serial is what makes the class count
			// read as "this many items exist twice".
			if (snapshot.rows.size() > 1) {
				std::string where;
				for (const auto& row : snapshot.rows) {
					if (!where.empty()) where += " + ";
					where += std::format("{}'s {}", shown(row.who), place_name(row.where));
				}
				keep(reconcile_class::held_twice, serial,
					std::format("{} rows: {}", snapshot.rows.size(), where));
				continue;
			}

			const holder& row = snapshot.rows.front();
			const holder& believed_at = view->at;

			switch (believed_at.where) {
			case place::gone:
				keep(reconcile_class::held_but_gone, serial,
					std::format("held by {} in {}, ledger says it left the world",
						shown(row.who), place_name(row.where)));
				break;

			case place::ground:
				keep(reconcile_class::held_but_ground, serial,
					std::format("held by {} in {}, ledger says it is on the ground",
						shown(row.who), place_name(row.where)));
				break;

			case place::nowhere:
				keep(reconcile_class::held_unrecorded, serial,
					std::format("held by {} in {}, no event ever placed it",
						shown(row.who), place_name(row.where)));
				break;

			case place::unnamed:
				// Already counted as indeterminate. The snapshot's answer is the
				// better one and there is nothing to contradict it.
				break;

			default:
			{
				const bool same_where = (row.where == believed_at.where)
					|| (is_escrow(row.where) && is_escrow(believed_at.where));
				if (!same_where || !same_name(row.who, believed_at.who)) {
					keep(reconcile_class::holder_mismatch, serial,
						std::format("held by {} in {}, ledger says {} in {}",
							shown(row.who), place_name(row.where),
							shown(believed_at.who), place_name(believed_at.where)));
				}
				break;
			}
			}
		}

		// The other direction: a Serial the ledger believes somebody holds, that
		// no row does. Ground, gone and unplaced are counted as context here
		// rather than accused — those are the three states in which having no
		// holder row is the correct answer.
		for (const auto& [serial, view] : believed) {
			if (held.find(serial) != held.end()) continue;

			switch (view.at.where) {
			case place::ground:  report.on_ground++;    break;
			case place::gone:    report.gone++;         break;
			case place::nowhere: report.never_placed++; break;
			case place::unnamed: /* already counted as indeterminate */ break;
			default:
				keep(reconcile_class::missing_holder, serial,
					std::format("ledger says {} holds it in {}, nothing does",
						shown(view.at.who), place_name(view.at.where)));
				break;
			}
		}

		// A stable order for the evidence: by class, then by Serial. The scan
		// walks two hash maps, so without this the same database would print its
		// findings in a different order on a different run — and a report an
		// operator cannot diff against yesterday's is worth much less.
		std::sort(report.samples.begin(), report.samples.end(),
			[](const reconcile_anomaly& a, const reconcile_anomaly& b)
			{
				if (a.class_id != b.class_id) return a.class_id < b.class_id;
				return a.serial < b.serial;
			});

		report.ok = true;
		return report;
	}
}
