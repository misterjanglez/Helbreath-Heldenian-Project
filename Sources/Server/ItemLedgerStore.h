// ItemLedgerStore.h: the Provenance Ledger storage (ADR 0003, plan P2.1)
//
// itemledger.db is the append-only biography of every item the server has ever
// created: one `item_instances` row per Instanced item, one `item_events` row per
// custody or state transition it undergoes, and daily aggregate `item_flows`
// counters for the Counted (stackable) tier that has no per-instance identity.
//
// Three properties shape the whole class:
//
//   1. It is a passive audit trail, not the system of record (D3). The in-RAM
//      world stays authoritative and snapshot saves stay how state persists, so
//      a ledger write may never be on the critical path of a game action and a
//      ledger failure may never fail one. Every recorder here therefore buffers
//      in RAM and returns void; only flush() can fail, and it fails loudly to a
//      log rather than to the caller.
//
//   2. It is the durable home of the Serial high-water mark. The allocator
//      (ItemProvenance.h) mints in RAM; this store is what makes the sequence
//      survive a restart. Boot recovery resumes at max(meta high-water,
//      MAX(item_instances.serial)) so neither a stale meta row nor a lost flush
//      can hand a live Serial out twice.
//
//   3. It is WAL from birth (D4). Not for write throughput — for readers: WAL
//      lets an external process query the live file while the server writes,
//      which is what makes the analytics and reconciliation tooling of P4
//      possible without ever stopping the world.
//
// Design contract: PLANS/ItemLedger_Plan.md P2.1, docs/adr/0003-item-provenance-ledger.md.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ServerMessages.h"   // ItemLogAction, for the event-band static_assert

struct sqlite3;
class CItem;

namespace hb::server
{
	// What one item_events row records.
	//
	// Values 1..99 are ItemLogAction (ServerMessages.h) passed straight through:
	// #78 turns the existing item_log() call sites into a dual sink, and a
	// translation table between two numberings would be one more thing that can
	// silently disagree. The ledger-only band starts at 100 — transitions with no
	// text-log equivalent, either because nothing logs them today (despawn) or
	// because they are facts about the ledger itself rather than about an item.
	//
	// The numbering is fixed here, in the ticket that creates the column, so the
	// later coverage tickets append rather than renumber. Stored values are
	// permanent world fact the moment real players exist.
	namespace ledger_event
	{
		enum ledger_event : int32_t
		{
			// 1..99: ItemLogAction values, unmapped.

			// An item was minted. Emitted from the factory by #78; the paired
			// item_instances row carries the origin and the birth location.
			created   = 100,

			// A ground item's timed cleanup removed it from the world (#79). No
			// actor — this is the despawn-attrition signal Loot 2.0 tuning wants.
			despawned = 101,

			// The item left the population permanently: depleted, broken by a
			// failed upgrade, sold out of the world, consumed by a turn-in (#79).
			destroyed = 102,

			// Not an item transition: a server-run boundary, written at boot with
			// serial 0. Batched flushing means a crash loses the last few seconds
			// of events, so a forensic query needs to see where those holes can
			// be. detail says whether the previous run ended cleanly, which is
			// what separates "restart" from "gap".
			boundary  = 103,
		};
	}

	// The band reservation, enforced rather than described. Stored event_type
	// values are permanent world fact once real players exist, so a collision
	// between the two numberings would make one number mean two transitions with
	// no way to tell them apart afterwards — and it would be invisible while
	// reviewing the file that caused it.
	// Both sides cast to their common storage type: they are separate unscoped
	// enums, and GCC warns on comparing one to the other even though the values
	// are what matter here.
	static_assert(static_cast<int32_t>(hb::server::net::ItemLogAction::max_action)
			< static_cast<int32_t>(ledger_event::created),
		"ItemLogAction has grown into the ledger-only event band; give the new "
		"action a value below ledger_event::created or move the band up.");

	// The `detail` column's encoder. That column is declared as event-specific
	// JSON by the schema, so the rule for turning a value into it belongs beside
	// the column rather than in whichever manager happened to need it first —
	// every later emitter (upgrade level, sale price, despawn reason) hits the
	// same rule. Hand-assembly is what these exist to prevent: one quote in an
	// NPC name out of a content file makes a row no reader can parse, and the
	// column is only worth having if every row in it parses.
	std::string detail_json(const char* key, const std::string& value);
	std::string detail_json(const char* key, int64_t value);

	// One item_instances row: the birth record of an Instanced item.
	struct ledger_instance
	{
		int64_t     serial     = 0;
		int32_t     item_id    = 0;
		int64_t     created_at = 0;   // unix seconds
		int32_t     origin_type = 0;  // item_origin value
		std::string origin_detail;    // npc name, crafter, GM name; empty => NULL
		std::string map;
		int32_t     x = 0;
		int32_t     y = 0;
		uint8_t     tier = 0;         // Item Tiers byte at creation (0 = Untiered)
	};

	// One item_events row. Every text field is empty-means-NULL: a despawn has no
	// actor, a pickup has no counterparty, and storing "" instead of NULL would
	// make those queries lie.
	struct ledger_event_record
	{
		int64_t     serial     = 0;   // 0 only for a boundary event
		int64_t     at         = 0;   // unix seconds
		int32_t     event_type = 0;   // ledger_event or ItemLogAction
		std::string actor_account;
		std::string actor_char;
		std::string counterparty_char;
		std::string map;
		int32_t     x = 0;
		int32_t     y = 0;
		std::string detail;           // event-specific JSON
	};

	// The Provenance Ledger store. One per server, owned by CGame, opened at boot
	// and held until shutdown.
	//
	// Recording is a RAM append; nothing reaches disk until flush(), which writes
	// the whole buffer plus the Serial high-water in one transaction. That is the
	// point: an item transition costs the game loop a vector push, and a crash can
	// only ever lose a whole flush window, never half of one.
	class item_ledger_store
	{
	public:
		item_ledger_store() = default;
		~item_ledger_store();

		item_ledger_store(const item_ledger_store&) = delete;
		item_ledger_store& operator=(const item_ledger_store&) = delete;

		// Open (creating if absent) itemledger.db, ensure the schema in WAL mode,
		// run Serial recovery, and buffer the run-boundary event. Call once at
		// startup. False on connection or schema failure.
		bool open(const std::string& path);

		// Flush, stamp the clean-shutdown marker, and close. Idempotent.
		//
		// `final_high_water` is the allocator's finishing position. It is a
		// parameter rather than something the store reads for itself because the
		// allocator belongs to the minting factory (ItemProvenance.h, ADR 0003),
		// and holding a pointer to it here would dangle: ~CGame deletes
		// ItemManager in its body, while this store is a unique_ptr member
		// destroyed afterwards. The default of 0 is therefore what the destructor
		// can honestly claim — flush the buffer, leave the durable mark alone —
		// and CGame::quit passes the real value.
		void close(int64_t final_high_water = 0);

		bool is_open() const { return m_db != nullptr; }
		sqlite3* handle() const { return m_db; }

		// The Serial the allocator must resume above, as recovered at open(). 0 on
		// a fresh database, which is exactly what a fresh allocator already is.
		int64_t recovered_high_water() const { return m_recovered_high_water; }

		// --- Recording (never fails, never blocks) ---------------------------
		// An Instanced item's birth record plus its `created` event, from one
		// call: the two always come as a pair, and a pair that can be half-made
		// is a gap. Ignored for an unserialed (Counted) item — those are the
		// flow counters' business.
		//
		// `map` may be null and x/y zero. A mint does not know where the item will
		// end up — the caller places it immediately afterwards (see the m_origin
		// comment on CItem) — so the minting factory can emit the birth record
		// with no location and let the following drop/pickup event supply it. The
		// schema makes map/x/y nullable for exactly that reason, which is what
		// keeps #78 wiring creation at one funnel instead of 45 placement sites.
		// Everything after the item therefore defaults to "not known here", which
		// is what the minting funnel passes; #79 supplies them at the venues that
		// do know.
		void record_mint(const CItem& item, const char* origin_detail = nullptr,
			const char* map = nullptr, int x = 0, int y = 0);

		// One transition of an already-minted item. Ignored when serial == 0, so
		// a caller that reaches this with a Counted item silently records nothing
		// rather than writing an event pointing at no instance.
		//
		// An `at` left at 0 is stamped with the current time here. That is where
		// the rule belongs: the dual sink (#78) records from inside the game
		// action, so "when" is always the moment of the call, and a call site
		// that forgot to say so would land at the epoch — ordering before every
		// event ever recorded. Setting `at` explicitly still wins, which is both
		// how record_mint keeps a birth row and its creation event on one
		// timestamp and what a replay of known history would need.
		//
		// By value: the record holds six strings and goes straight into the
		// buffer, so callers that build one on the spot move it in rather than
		// paying a heap copy per event on a path the class promises is a push.
		void record_event(ledger_event_record event);

		// A day/type/sink aggregate for the Counted tier. Accumulated in RAM by
		// key, so a thousand potions bought in one window cost one row, not one
		// row per purchase. Negative qty is legal (a sink can be expressed either
		// as its own flow_type or as a negation, and the ledger does not judge).
		void record_flow(int32_t item_id, int32_t flow_type, int64_t qty);

		// --- Flushing --------------------------------------------------------
		// Write everything buffered plus `high_water` into one transaction. True
		// when the buffer reached disk (including the trivially-true empty case).
		// On failure the buffer is KEPT: the next flush retries it, because an
		// audit log that discards what it could not write is worse than one that
		// writes it late. Pass high_water <= 0 to leave the meta row alone.
		bool flush(int64_t high_water);

		// Cadence hook for the game tick. Flushes when the configured interval has
		// elapsed or the buffer has grown past the size trigger; cheap otherwise.
		void process_flush(uint32_t now_ms, int64_t high_water);

		// Flush cadence, from server_config.timing (data, not code). A
		// non-positive interval disables the timer, leaving only the size trigger
		// and the explicit flushes on save-all and shutdown.
		void set_flush_cadence(int interval_ms, int event_threshold);

		// Buffered rows not yet on disk — what a crash right now would cost.
		size_t pending_count() const;

	private:
		// `path` only names the file in the mismatch message — the store is
		// instantiated more than once (ledgercheck drives a scratch database), so
		// a hardcoded name would point an operator at the wrong file.
		bool ensure_schema(const std::string& path);

		// The recovery rule, against what is on disk right now:
		// max(meta.serial_high_water, MAX(item_instances.serial)). The two sources
		// fail in opposite directions — the meta row can be stale because a crash
		// lost the flush that would have advanced it, and the instance table can
		// be behind because those same rows were in that lost flush — so the
		// higher of them is the only value neither hole can undercut.
		int64_t compute_recovery_high_water() const;

		// A meta row as a string, empty when absent; as an int64, 0 when absent
		// or unparseable.
		std::string read_meta(const char* key) const;
		int64_t read_meta_int(const char* key) const;
		bool write_meta(const char* key, const std::string& value);

		// The per-flush inserts. Each returns false on the first *transient* SQL
		// error so flush() can roll the whole transaction back rather than commit
		// a partial window.
		//
		// A row the schema rejects outright is a different case: it can never
		// succeed, and because the buffer is retried whole it would stop every
		// later event from ever reaching disk. Those rows are dropped and counted
		// in `dropped` instead, so one bad row costs one row rather than the rest
		// of the run's audit trail.
		bool write_instances(int& dropped);
		bool write_events(int& dropped);
		bool write_flows(int& dropped);

		// A yyyymmdd day number for the flow key, from local time — flow rows are
		// read by humans tuning drop rates, so the day boundary that matters is
		// theirs, not UTC's. Memoized per second: every stackable that moves asks
		// for this, and a timezone conversion to produce a number that changes
		// once a day is the one thing on the record path that is not a push.
		int32_t flow_day() const;

		sqlite3* m_db = nullptr;

		std::vector<ledger_instance>     m_instances;
		std::vector<ledger_event_record> m_events;

		// (day, item_id, flow_type) -> qty, accumulated until flush. A map keeps
		// the flush's UPSERT count proportional to distinct item types rather
		// than to the number of stackables that moved.
		struct flow_key
		{
			int32_t day;
			int32_t item_id;
			int32_t flow_type;

			bool operator<(const flow_key& other) const
			{
				if (day != other.day) return day < other.day;
				if (item_id != other.item_id) return item_id < other.item_id;
				return flow_type < other.flow_type;
			}
		};
		std::map<flow_key, int64_t> m_flows;

		int64_t m_recovered_high_water = 0;
		int64_t m_written_high_water = 0;   // last value actually persisted

		// flow_day()'s memo: the day number and the unix second it was derived
		// from, so repeat calls within the same second skip the conversion.
		mutable int32_t m_flow_day = 0;
		mutable int64_t m_flow_day_at = 0;

		// Cadence, owned by server_config.timing (ServerConfig.h) — the numbers
		// live there and nowhere else, so a second set of defaults here cannot
		// drift from them. Both zero means "explicit flushes only", which is the
		// safe direction for a store nobody configured.
		int      m_flush_interval_ms = 0;
		size_t   m_flush_event_threshold = 0;
		uint32_t m_last_flush_ms = 0;
		bool     m_flushed_once = false;    // force one flush on the first tick

		// The last flush attempt failed and its buffer is still queued. Without
		// this the size trigger stays armed by the very rows it could not write,
		// and process_flush retries the whole batch on every tick — 50 times a
		// second, each attempt replaying every buffered row and forcing a
		// synchronous error-log write. Backing off to the interval keeps the retry
		// promise without turning one bad write into a busy loop.
		bool     m_flush_failed = false;

		// Rows the schema refused, for the life of the process. Non-zero means the
		// ledger is knowingly incomplete — which is worth surviving (the
		// alternative is a stalled writer and no events at all) but is never
		// normal, so it is kept where an operator and the provers can both see it.
		int64_t  m_dropped_rows = 0;

	public:
		// How many rows the schema has rejected since startup. 0 in a healthy run.
		int64_t dropped_rows() const { return m_dropped_rows; }
	};
}
