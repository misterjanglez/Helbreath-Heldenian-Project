// ItemReconciliation.h: the snapshot-vs-ledger comparison (ADR 0003, plan P4.1)
//
// The Provenance Ledger is a passive audit trail, not the system of record (D3).
// The in-RAM world stays authoritative and snapshot saves stay how state
// persists. That redundancy is the whole point: two independent records of who
// holds what, written by different code at different moments, must agree — and
// **where they disagree is a dupe**. Without Serials, dupe detection is
// statistical inference. With them it is this file.
//
// The comparison has two sides:
//
//   1. **The snapshot side** — every Serial that game.db says somebody holds:
//      character_items (inventory), character_bank_items (Warehouse), and since
//      v9 the two escrow tables, listing_items and offer_items. All four carry a
//      `serial` column with an index built for this query.
//
//   2. **The ledger side** — the holder derived from each Serial's last
//      *locating* event. Most events say where an item ended up; some (Use, a
//      failed upgrade, the birth row itself) say nothing about custody, so they
//      are excluded rather than allowed to mask the last real move.
//
// Everything here works on two raw `sqlite3*` handles and nothing else. That is
// deliberate: the tool points them at the live files, the prover points them at
// a scratch pair with anomalies planted by hand, and neither needs a booted
// world. The one game fact the comparison wants — whether an item type is
// stackable — arrives as an injected predicate for the same reason.
//
// Design contract: PLANS/ItemLedger_Plan.md P4.1, docs/adr/0003-item-provenance-ledger.md.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct sqlite3;
class CGame;

namespace hb::server
{
	// What one finding is. The numbering is internal — the machine output names
	// classes by string, because an operator reading a report should not have to
	// hold a table in their head, and a class added later must not renumber the
	// ones a previous report already named.
	namespace reconcile_class
	{
		enum reconcile_class : int
		{
			// One Serial in two holder rows at once. THE dupe: an Instanced item
			// is one object, so two rows claiming it means one of them was
			// written by a path that copied rather than moved.
			held_twice = 0,

			// Somebody holds it and the ledger says it left the world —
			// destroyed, depleted, sold, or despawned off the ground. Either the
			// exit was recorded for an item that survived it, or the item was
			// recreated out of an exit.
			held_but_gone,

			// Somebody holds it and the ledger says it is lying on the ground. An
			// item cannot be in a bag and on the floor.
			held_but_ground,

			// A held Serial with no birth row at all. Identity from nowhere: the
			// item was minted outside the factory, or the ledger lost the flush
			// that carried its birth. A Serial above the recovered high-water is
			// this class with the strongest possible tell, and says so in detail.
			unknown_serial,

			// Both sides name a holder and they disagree, or they agree on the
			// character and disagree on the container. Warn rather than critical:
			// an online player's inventory is RAM until it saves, so a live run
			// produces these for every transition since the last save.
			holder_mismatch,

			// The ledger says a character or the board holds it and no row does.
			// Same live-run caveat, from the other direction.
			missing_holder,

			// It has a birth row, somebody holds it, and no event ever said so.
			// The ledger cannot answer "who has this" for it — which is the one
			// question it exists to answer.
			held_unrecorded,

			// A held row whose item type is Instanced and whose Serial is 0. An
			// item that should carry identity and does not cannot be reconciled
			// at all, so it is the one anomaly that hides every other.
			serial_on_instanced_missing,

			// The mirror: a stackable holding a Serial. Merge and split dissolve
			// identity (D2), so a Counted row with one has an identity that the
			// next stack merge will silently destroy.
			serial_on_counted,

			count
		};
	}

	// The stable machine name of a class, and whether it is a certain fault
	// rather than something a live run can produce honestly.
	const char* reconcile_class_name(int class_id);
	bool reconcile_class_is_critical(int class_id);

	// One finding, already formatted. `serial` is 0 only for the two tier
	// classes, which are about a row that carries no identity.
	struct reconcile_anomaly
	{
		int         class_id = 0;
		int64_t     serial = 0;
		std::string detail;
	};

	struct reconcile_options
	{
		// Findings kept per class. The counts are always exact — this bounds the
		// evidence printed, not the scan — because a world that has gone wrong
		// tends to go wrong in thousands, and a console that scrolls for a minute
		// is a report nobody reads. 0 keeps everything.
		size_t sample_limit = 20;

		// item_id -> is this type Instanced (non-stackable)? Empty disables the
		// two tier classes rather than guessing: a report that invents a
		// stackability it does not know would accuse every row in the database.
		std::function<bool(int)> is_instanced;
	};

	struct reconcile_report
	{
		// False when the scan could not run at all (a missing table, a handle
		// that is not a world or a ledger); `failure` says which.
		bool        ok = false;
		std::string failure;

		// --- What was scanned ---
		int64_t held_rows = 0;      // holder rows carrying a non-zero Serial
		int64_t held_serials = 0;   // distinct Serials among them
		int64_t instances = 0;      // item_instances rows
		int64_t high_water = 0;     // the ledger's durable Serial mark

		// --- Ledger states that are NOT anomalies, as context ---
		// A Serial the ledger last saw on the ground. Normal while a world is
		// running (ground items are map tiles, never persisted); after a clean
		// shutdown it should be zero, because world_shutdown despawns them.
		int64_t on_ground = 0;
		// Minted and never moved: a birth row and no locating event, held by
		// nobody. Every prover that mints makes these. A GM batch used to make
		// them too — one GmMint event for N copies — until #104 gave minting an
		// event per copy, so a batch now leaves none of these behind.
		int64_t never_placed = 0;
		// The ledger says it left the world and nothing holds it. The healthy
		// end state of most items.
		int64_t gone = 0;
		// A locating event whose holder name is empty (the client had already
		// gone when the event was written), so the ledger has an opinion it
		// cannot express. Not compared against the snapshot.
		int64_t indeterminate = 0;

		int64_t class_counts[reconcile_class::count] = {};
		std::vector<reconcile_anomaly> samples;

		int64_t anomaly_total() const;
		int64_t critical_total() const;
	};

	// Run the comparison. `world` is a game.db connection (v9 layout), `ledger`
	// an itemledger.db connection; read-only handles are expected and nothing
	// here writes. Neither may be null.
	//
	// Both databases are read whole into RAM — one row per held item and one per
	// Serial the ledger has an opinion about. That is a few MB for a world with
	// a hundred thousand items, and it buys the one thing a pure-SQL comparison
	// could not do without a temp table in one of the two files: this tool must
	// be able to run against a read-only snapshot of a live server.
	reconcile_report run_reconciliation(sqlite3* world, sqlite3* ledger,
		const reconcile_options& options);

	// The Instanced/Counted rule (D2) read off the loaded item configs, in the
	// shape reconcile_options wants: an item type is Instanced iff it is
	// non-stackable. A factory rather than a lambda written out at each call
	// site, because the tool and the prover that proves the tool must ask this
	// of the same table — two copies of the rule is two answers waiting to
	// disagree, and the one that disagrees quietly is the tool.
	//
	// The injection point stays: the comparison itself never learns what a CGame
	// is, which is what lets it run against a snapshot from another world.
	std::function<bool(int)> instanced_item_lookup(CGame& game);
}
