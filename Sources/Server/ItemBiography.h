// ItemBiography.h: one Serial's ordered history, and whether it is possible (ADR 0003, plan P4.2)
//
// #83 asked the ledger where an item is now. This asks how it got there, and
// whether the route it took could have happened.
//
// Two readers over the same machine:
//
//   1. **The Biography** — one Serial's events in order, each carrying the place
//      the item was in afterwards. This is the ownership-dispute settler: an
//      operator holding a Serial can read who had it, when, and what moved it.
//
//   2. **The validator** — the same replay run over every Serial at once,
//      reporting only the steps the machine says are impossible. An item cannot
//      be picked up twice with nobody dropping it in between, and nothing can
//      happen to it after it left the world.
//
// One machine serves both on purpose. A biography whose place column disagreed
// with the validator's verdict would be two answers about the same events, and
// the one an operator reads is the one that would be wrong.
//
// **Unplaced is a wildcard, and that is the whole reason this reports so little
// on a healthy world.** The minting funnel does not know where a venue puts an
// item — it cannot, the venue sets that afterwards (see the m_origin comment on
// CItem) — so a crafted sword's ledger reads `created` and then nothing until
// somebody drops or trades it. Demanding a placement event would accuse every
// craft, quest and fishing item in the database. So from `unborn` or `limbo`
// every transition is legal, and the machine only starts holding the ledger to
// its word once an event has actually said where the item is.
//
// **Divergence from ItemReconciliation, deliberate.** That file maps `Sell` and
// `Deplete` to "left the world"; here they move nothing. The two answer
// different questions. Reconciliation asks who holds the item and hedges against
// a lost destruction row, so treating a sale as an exit is the safe direction.
// This asks whether a transition was legal, and the real emission order is
// `Sell` -> `Deplete` -> `destroyed` (ItemManager::item_deplete_handler): make
// the sale terminal and every sale in the database reads as an event after the
// item's death.
//
// Like ItemReconciliation this works on a raw `sqlite3*` and nothing else — the
// tool points it at the live file read-only, the prover points it at a scratch
// one — but only ONE handle, because legality is a property of the ledger alone.
// No snapshot, no world, no booted server.
//
// Design contract: PLANS/ItemLedger_Plan.md P4.2, docs/adr/0003-item-provenance-ledger.md.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ItemLedgerStore.h"   // ledger_instance, and the event numbering

struct sqlite3;

namespace hb::server
{
	// Where an item is between two events. Not the same vocabulary as
	// ItemReconciliation's `place`: that one answers "whose row should hold
	// this", so it distinguishes a Listing's bundle from an Offer's and folds
	// every exit into one. This one answers "what may happen next", and for that
	// the two escrow tables behave identically while `unborn` and `limbo` do not.
	namespace ledger_place
	{
		enum ledger_place : int
		{
			// No `created` event has been seen. The starting state of every
			// replay, including one for a Serial whose birth row is missing.
			unborn = 0,

			// Minted, and no event has said where it went. Every venue that does
			// not record a placement leaves items here, which is why this is a
			// wildcard source rather than a state with rules.
			limbo,

			carried,   // a character's inventory
			banked,    // a character's Warehouse
			escrow,    // the Trading Post board's custody, either table
			ground,    // a map tile
			gone,      // it left the world; terminal

			count,

			// Not places: what a rule says about a place.
			any       = -1,   // this event does not care where the item was
			unchanged = -2,   // this event does not move it
		};
	}

	// The stable machine name. `limbo` prints as "unplaced" — the state is about
	// the ledger not having been told, and an operator reading "limbo" would
	// reasonably think it described the item.
	const char* ledger_place_name(int place);

	// What one impossible step is. Ordered most-certain first, matching the
	// severity split below.
	namespace ledger_violation
	{
		enum ledger_violation : int
		{
			// Something happened to the item after it left the world. Covers the
			// plan's "destroyed once" rule as its own case — a second exit is an
			// event after the first one. Either the exit was recorded for an item
			// that survived it, or the item was rebuilt out of its own death.
			event_after_exit = 0,

			// It was already in somebody's custody, and something put it into
			// custody again with nobody having let go. The picked-up-twice class,
			// and the dupe shape the ledger can see on its own: picked up while
			// carried, retrieved from the Warehouse while carrying it, minted
			// into hands that already held it, escrowed what was in the bank.
			//
			// Give and Exchange are excluded by construction: a hand-off moves an
			// item between two holders in one event, which is not the same thing.
			acquired_while_held,

			// The event expected it somewhere it was not, and afterwards it is in
			// no more custody than before — dropped what was already on the
			// ground, sold what was in the Warehouse, delivered out of an escrow
			// it was never in.
			//
			// A warning rather than a fault. This is the shape a hole makes: lose
			// the flush that carried a Drop and the ledger still believes the item
			// is held, so the next event about it lands from the wrong place.
			released_when_absent,

			// Two `created` events for one Serial. The allocator hands each value
			// out once (ItemProvenance.h), so a second birth means either the
			// durable high-water went backwards or two items were minted onto one
			// identity.
			double_creation,

			// Events for a Serial with no birth row. Identity from nowhere, and
			// the ledger cannot say what the item even is. Counted once per
			// Serial rather than once per event, so the class count reads as
			// "this many items have no origin".
			orphan_event,

			count,

			// Not a class: what a legal step reports.
			none = -1,
		};
	}

	const char* ledger_violation_name(int class_id);

	// A name column, as a report prints it. Empty is what the schema stores as
	// NULL, and NULL means the ledger genuinely did not know — the client had
	// already gone when the event was written. Rendered as a word so it cannot be
	// mistaken for a field the report forgot to fill in, and shared so a
	// Biography and the validator cannot say it two ways about one event.
	const char* ledger_actor_name(const std::string& name);

	// Whether a class is a certain fault rather than something a hole can make.
	// Same split as the Reconciliation job's, for the same reason: a report that
	// sums everything into one number is one an operator stops reading.
	bool ledger_violation_is_critical(int class_id);

	// The stable name of an event number, for a report a human reads. Covers the
	// ItemLogAction band and the ledger's own; anything else prints as
	// `event_<n>` rather than as a guess, because an unnamed number in a
	// biography is a number somebody has to go and look up either way.
	std::string ledger_event_name(int event_type);

	// One step of the machine: what the event did to the place, and what was
	// wrong with it. `required` is the place the event expected, kept so the
	// report can say what it wanted rather than only that it was disappointed.
	struct ledger_transition
	{
		int from      = ledger_place::unborn;
		int to        = ledger_place::unborn;
		int required  = ledger_place::any;
		int violation = ledger_violation::none;
	};

	// Apply one event to one place. Pure, total, and the only place the legal
	// transition table lives.
	ledger_transition ledger_step(int place, int event_type);

	// --- The Biography ------------------------------------------------------
	// One event of one Serial's history, as it is read back plus what the replay
	// made of it.
	struct biography_step
	{
		int64_t     event_id   = 0;
		int64_t     at         = 0;   // unix seconds
		int32_t     event_type = 0;
		std::string actor_account;
		std::string actor_char;
		std::string counterparty_char;
		std::string map;
		int32_t     x = 0;
		int32_t     y = 0;
		std::string detail;

		// What the machine made of it: where the item was, where it ended up,
		// where the event expected it, and what was wrong. The machine's own
		// output type rather than a copy of its fields, so a biography and the
		// validator cannot describe one event two ways.
		ledger_transition move;

		// A crash boundary sits between this event and the previous one, so the
		// events that would have explained the step may never have reached disk.
		// Still shown — the operator should see the step — but not counted as a
		// fault.
		bool excused = false;
	};

	struct biography
	{
		bool        ok = false;
		std::string failure;

		int64_t serial = 0;

		// The birth row. `has_instance` false means the Serial has no origin on
		// record; `birth` is then untouched rather than zero-filled-and-plausible.
		bool            has_instance = false;
		ledger_instance birth;

		std::vector<biography_step> steps;

		int     final_place = ledger_place::unborn;
		int64_t violations  = 0;   // excused steps not counted
	};

	// One Serial's ordered history. Ordered by event_id and never by `at`: the
	// timestamp is whole seconds and a pickup followed immediately by a drop
	// shares one, while event_id is assigned in the order the buffer recorded
	// them (ItemLedgerStore.h) and therefore always has an answer.
	//
	// A Serial with no rows at all is not a failure — `ok` is true, `steps` is
	// empty and `has_instance` is false. "Nothing was ever recorded about this
	// Serial" is a finding, and one the caller should be able to print.
	biography read_biography(sqlite3* ledger, int64_t serial);

	// --- The validator ------------------------------------------------------
	struct validate_options
	{
		// Violations kept per class. Counts stay exact; this bounds the printed
		// evidence only, because a ledger that has gone wrong tends to go wrong
		// in thousands. 0 keeps everything.
		size_t sample_limit = 20;
	};

	struct ledger_violation_record
	{
		int         class_id = 0;
		int64_t     serial = 0;
		std::string detail;
	};

	struct validate_report
	{
		bool        ok = false;
		std::string failure;

		// --- What was replayed ---
		int64_t serials   = 0;   // distinct Serials: birth rows plus event-only ones
		int64_t events    = 0;   // item events replayed; boundaries are not items
		int64_t instances = 0;   // birth rows

		// --- Where they ended, as context rather than as findings ---
		int64_t ended       = 0;  // left the world, the healthy end state
		int64_t in_world    = 0;  // held, banked, escrowed or lying on the ground
		int64_t never_moved = 0;  // minted, and no event ever placed it

		// --- What a crash explains ---
		int64_t gaps        = 0;  // run boundaries whose previous run did not stop cleanly
		int64_t gap_excused = 0;  // steps a gap accounts for, not counted as violations

		int64_t class_counts[ledger_violation::count] = {};
		std::vector<ledger_violation_record> samples;

		int64_t violation_total() const;
		int64_t critical_total() const;
	};

	// Replay every Serial in the ledger. `ledger` is an itemledger.db connection;
	// a read-only handle is expected and nothing here writes.
	//
	// One ordered pass over item_events, holding one small record per Serial. The
	// alternative — a query per Serial — would be correct and would also read the
	// events table once per item the server has ever minted.
	validate_report run_validation(sqlite3* ledger, const validate_options& options);
}
