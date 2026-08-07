#include "ItemLedgerStore.h"

#include "AccountSqliteStore.h"   // VerifySqliteSchemaVersion
#include "Item.h"
#include "Log.h"
#include "ServerLogChannels.h"
#include "TimeUtils.h"
#include "json.hpp"               // the detail column's encoder
#include "sqlite3.h"

// Single source of truth for the itemledger.db schema version: spliced into the
// DDL stamp and passed to the VerifySqliteSchemaVersion gate. Bump on any ledger
// schema change — stale dev DBs are refused, never migrated (D6).
// v2 (#85): npc_kills, the denominator every observed drop rate is divided by.
// v3 (#122): npc_kills grows title_factor_sum, the Huntmaster tier-shift's own
//            denominator (tier mix, not drop rate).
#define ITEM_LEDGER_SCHEMA_VERSION "3"

#include <chrono>
#include <charconv>
#include <format>
#include <utility>

using hb::log_channel;

namespace
{
	bool exec_sql(sqlite3* db, const char* sql)
	{
		char* err = nullptr;
		if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
			hb::logger::error("[LEDGER] SQLite exec failed: {}", err ? err : "unknown");
			sqlite3_free(err);
			return false;
		}
		return true;
	}

	// Empty string means NULL, for every nullable text column in the schema. A
	// despawn has no actor and a pickup has no counterparty; writing "" there
	// would make `WHERE actor_char IS NULL` miss them and `GROUP BY actor_char`
	// invent a nameless player.
	bool bind_text_or_null(sqlite3_stmt* stmt, int idx, const std::string& value)
	{
		if (value.empty()) {
			return sqlite3_bind_null(stmt, idx) == SQLITE_OK;
		}
		return sqlite3_bind_text(stmt, idx, value.c_str(),
			static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
	}

	// One insert statement driven over one buffer. All three tables share this
	// ritual and only the SQL and the bind list differ, so those are what the
	// callers supply. `describe` names the offending row in the error line; it is
	// only called on a failure path.
	//
	// It tells two kinds of failure apart.
	//
	// A transient failure — the file is locked, the disk hiccupped — is worth
	// retrying, so it fails the whole transaction and the caller keeps the buffer.
	//
	// A constraint violation is not. It will fail identically on every future
	// attempt, and the buffer is only ever retried as a whole, so one poisoned row
	// stops **every** later event from reaching disk: the ledger goes silent, the
	// buffer grows without bound, and the only sign is one error line repeating on
	// the flush interval. That is a worse outcome than losing the offending row,
	// because the rows behind it are the ones nobody has seen yet.
	//
	// So a rejected row is dropped and loudly named, and the batch carries on.
	// `dropped` is counted rather than swallowed: a rejection means something
	// upstream produced a row the schema says cannot exist — a re-issued Serial,
	// say — and that is a defect report, not routine housekeeping.
	template <typename Rows, typename Bind, typename Describe>
	bool write_batch(sqlite3* db, const char* label, const char* sql,
		const Rows& rows, Bind bind, Describe describe, int& dropped)
	{
		if (rows.empty()) {
			return true;
		}

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
			hb::logger::error("[LEDGER] {} insert prepare failed: {}", label, sqlite3_errmsg(db));
			return false;
		}

		bool ok = true;
		for (const auto& row : rows) {
			sqlite3_reset(stmt);
			sqlite3_clear_bindings(stmt);
			bind(stmt, row);

			const int rc = sqlite3_step(stmt);
			if (rc == SQLITE_DONE) {
				continue;
			}

			if (rc == SQLITE_CONSTRAINT) {
				hb::logger::error("[LEDGER] {} row REJECTED and dropped - {}: {}. "
					"The schema says this row cannot exist, so retrying it would "
					"stall every event behind it; something upstream produced it.",
					label, describe(row), sqlite3_errmsg(db));
				++dropped;
				continue;
			}

			hb::logger::error("[LEDGER] {} insert failed for {}: {}",
				label, describe(row), sqlite3_errmsg(db));
			ok = false;
			break;
		}

		sqlite3_finalize(stmt);
		return ok;
	}
}

namespace hb::server
{
	// One-field detail objects, which is every detail the ledger writes today.
	// nlohmann is already vendored and already used by the server (ServerConfig),
	// so the escaping rule is the library's rather than a second hand-rolled one.
	std::string detail_json(const char* key, const std::string& value)
	{
		return nlohmann::json::object({ { key, value } }).dump();
	}

	std::string detail_json(const char* key, int64_t value)
	{
		return nlohmann::json::object({ { key, value } }).dump();
	}

	std::string detail_json(std::initializer_list<std::pair<const char*, int64_t>> fields)
	{
		nlohmann::json object = nlohmann::json::object();
		for (const auto& field : fields)
			object[field.first] = field.second;
		return object.dump();
	}

	item_ledger_store::~item_ledger_store()
	{
		close();
	}

	bool item_ledger_store::open(const std::string& path)
	{
		if (m_db != nullptr) {
			return true;
		}

		if (sqlite3_open(path.c_str(), &m_db) != SQLITE_OK) {
			hb::logger::error("[LEDGER] sqlite3_open failed for {}: {}", path,
				m_db ? sqlite3_errmsg(m_db) : "unknown");
			if (m_db != nullptr) {
				sqlite3_close(m_db);
				m_db = nullptr;
			}
			return false;
		}

		sqlite3_busy_timeout(m_db, 1000);

		// WAL from birth (D4). The reason is readers, not writers: under WAL an
		// external process can query the live file while the server writes, which
		// is what lets the P4 reconciliation and analytics tooling run against a
		// running world. synchronous=NORMAL is the matching half — the ledger
		// already accepts losing the final flush window, so paying for an fsync
		// per commit buys nothing it does not already tolerate losing.
		//
		// journal_mode returns a row; sqlite3_exec with a null callback discards
		// it, so the value is read back explicitly below rather than assumed.
		if (!exec_sql(m_db, "PRAGMA journal_mode = WAL;")
			|| !exec_sql(m_db, "PRAGMA synchronous = NORMAL;")) {
			close();
			return false;
		}

		if (!ensure_schema(path)) {
			close();
			return false;
		}

		// A file that silently stayed in rollback-journal mode would look fine
		// until an external reader blocked the server, so the mode is verified
		// rather than trusted.
		{
			sqlite3_stmt* stmt = nullptr;
			std::string mode;
			if (sqlite3_prepare_v2(m_db, "PRAGMA journal_mode;", -1, &stmt, nullptr) == SQLITE_OK) {
				if (sqlite3_step(stmt) == SQLITE_ROW) {
					const unsigned char* text = sqlite3_column_text(stmt, 0);
					if (text != nullptr) mode = reinterpret_cast<const char*>(text);
				}
				sqlite3_finalize(stmt);
			}
			if (mode != "wal") {
				hb::logger::error("[LEDGER] journal_mode is '{}', not WAL - external readers "
					"would block the server", mode.empty() ? "unknown" : mode);
				close();
				return false;
			}
		}

		m_recovered_high_water = compute_recovery_high_water();
		m_written_high_water = m_recovered_high_water;

		// The run boundary. Batched flushing means a crash loses the tail of the
		// previous run's events, so forensics needs to see where those holes can
		// be — and whether this restart follows a clean stop or a crash, which is
		// the difference between "no events are missing here" and "some may be".
		const std::string previous = read_meta("clean_shutdown");
		const char* previous_state = "none";
		if (previous == "1") previous_state = "clean";
		else if (previous == "0") previous_state = "crash";

		ledger_event_record boundary;
		boundary.serial = 0;
		boundary.at = hb::time::unix_now();
		boundary.event_type = ledger_event::boundary;
		boundary.detail = nlohmann::json::object({
			{ "boot", 1 }, { "prev_shutdown", previous_state } }).dump();
		record_event(std::move(boundary));

		// Marked crashed for the duration of the run; close() sets it back. The
		// write is immediate rather than buffered, because a value that only
		// reaches disk on the first flush would report a crash in the window it
		// exists to describe.
		if (!write_meta("clean_shutdown", "0")) {
			hb::logger::warn("[LEDGER] could not stamp the run marker - a clean stop may "
				"later read as a crash");
		}

		// The cadence is echoed because the store's own defaults are zero — the
		// numbers come from server_config.timing and nowhere else. A boot line
		// reading "flush every 0ms" is how an operator finds a config that lost
		// the keys, instead of discovering it as events that never reached disk.
		hb::logger::log("[LEDGER] {} open (WAL); serials resume above {}; previous shutdown: {}; "
			"flush every {}ms or {} row(s)",
			path, m_recovered_high_water, previous_state,
			m_flush_interval_ms, m_flush_event_threshold);
		return true;
	}

	void item_ledger_store::close(int64_t final_high_water)
	{
		if (m_db == nullptr) {
			return;
		}

		// Last chance for the buffer. A shutdown that dropped it would put a
		// guaranteed hole at the end of every clean run.
		flush(final_high_water);

		if (!write_meta("clean_shutdown", "1")) {
			hb::logger::warn("[LEDGER] could not stamp the clean-shutdown marker");
		}

		sqlite3_close(m_db);
		m_db = nullptr;
	}

	bool item_ledger_store::ensure_schema(const std::string& path)
	{
		const sqlite_schema_state schema_state =
			VerifySqliteSchemaVersion(m_db, path.c_str(), ITEM_LEDGER_SCHEMA_VERSION);
		if (schema_state == sqlite_schema_state::mismatch) {
			return false;
		}
		if (schema_state == sqlite_schema_state::current) {
			return true;
		}

		// Fresh database: the full schema at the current version, exactly as the
		// design contract specifies it (PLANS/ItemLedger_Plan.md, Schema section).
		//
		// No foreign key from item_events.serial to item_instances.serial, and
		// that is deliberate: serial 0 is the boundary event's sentinel, and a
		// crash can leave an event whose instance row was in the lost flush.
		// Refusing to record such an event would hide the very gap it documents.
		const char* schema =
			"BEGIN;"
			"CREATE TABLE IF NOT EXISTS meta ("
			" key TEXT PRIMARY KEY,"
			" value TEXT NOT NULL"
			");"
			"INSERT INTO meta(key, value) VALUES('schema_version','" ITEM_LEDGER_SCHEMA_VERSION "');"
			"INSERT INTO meta(key, value) VALUES('serial_high_water','0');"
			"CREATE TABLE IF NOT EXISTS item_instances ("
			" serial        INTEGER PRIMARY KEY,"
			" item_id       INTEGER NOT NULL,"
			" created_at    INTEGER NOT NULL,"
			" origin_type   INTEGER NOT NULL,"
			" origin_detail TEXT,"
			" map           TEXT,"
			" x             INTEGER,"
			" y             INTEGER,"
			" tier          INTEGER NOT NULL DEFAULT 0"
			");"
			"CREATE TABLE IF NOT EXISTS item_events ("
			" event_id          INTEGER PRIMARY KEY,"
			" serial            INTEGER NOT NULL,"
			" at                INTEGER NOT NULL,"
			" event_type        INTEGER NOT NULL,"
			" actor_account     TEXT,"
			" actor_char        TEXT,"
			" counterparty_char TEXT,"
			" map               TEXT,"
			" x                 INTEGER,"
			" y                 INTEGER,"
			" detail            TEXT"
			");"
			"CREATE INDEX IF NOT EXISTS idx_events_serial ON item_events(serial, at);"
			"CREATE INDEX IF NOT EXISTS idx_events_actor  ON item_events(actor_char, at);"
			"CREATE INDEX IF NOT EXISTS idx_events_type   ON item_events(event_type, at);"
			"CREATE TABLE IF NOT EXISTS item_flows ("
			" day       INTEGER NOT NULL,"
			" item_id   INTEGER NOT NULL,"
			" flow_type INTEGER NOT NULL,"
			" qty       INTEGER NOT NULL,"
			" PRIMARY KEY (day, item_id, flow_type)"
			");"
			// The denominator (#85, plan P4.3). Every figure dropodds prints is a
			// chance PER KILL, so an observed rate has nothing to divide by until
			// kills are counted — and nothing in the server counted them
			// (CClient::m_enemy_kill_count is per character and per nothing else).
			//
			// Aggregated by day and monster like item_flows, and for the same
			// reason: a busy world kills thousands of the same monster and the
			// analytics only ever read the total.
			//
			// rep_factor_sum is the sum of the killers' reputation multipliers,
			// stored beside the count rather than bucketed by rating, because that
			// layer multiplies LINEARLY on gear and unique rows (#88). The expected
			// number of drops of one such row is therefore
			// `rep_factor_sum x authored_ppb_at_rating_0`, exactly, with no
			// per-rating breakdown to store or to join against. Ordinary rows
			// ignore it and divide by `kills`.
			//
			// npc_name is stored beside the id, and that denormalisation is the
			// whole reason the SQL half of the analytics works at all: a drop's
			// birth row records the monster by NAME (origin_detail, which a
			// Biography shows a human), while a kill is keyed by npc_config id.
			// The ledger holds no name-to-id table and must not need one — it is
			// readable on its own, never against a copy of gamedata.db — so
			// without the name here the two halves could not be joined without a
			// world file. Where one name covers several configs (14 Catapults, 3
			// Guards) both sides pool identically, which is the honest reading.
			"CREATE TABLE IF NOT EXISTS npc_kills ("
			" day            INTEGER NOT NULL,"
			" npc_id         INTEGER NOT NULL,"
			" npc_name       TEXT    NOT NULL,"
			" kills          INTEGER NOT NULL,"
			" rep_factor_sum REAL    NOT NULL,"
			" title_factor_sum REAL  NOT NULL,"   // Huntmaster tier-shift (#122)
			" PRIMARY KEY (day, npc_id)"
			");"
			"CREATE INDEX IF NOT EXISTS idx_kills_name ON npc_kills(npc_name);"
			"COMMIT;";

		return exec_sql(m_db, schema);
	}

	int64_t item_ledger_store::compute_recovery_high_water() const
	{
		if (m_db == nullptr) {
			return 0;
		}

		const int64_t from_meta = read_meta_int("serial_high_water");

		int64_t from_instances = 0;
		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(m_db, "SELECT MAX(serial) FROM item_instances;",
			-1, &stmt, nullptr) == SQLITE_OK) {
			if (sqlite3_step(stmt) == SQLITE_ROW
				&& sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
				from_instances = sqlite3_column_int64(stmt, 0);
			}
			sqlite3_finalize(stmt);
		}
		else {
			hb::logger::error("[LEDGER] MAX(serial) query failed: {}", sqlite3_errmsg(m_db));
		}

		return from_meta > from_instances ? from_meta : from_instances;
	}

	std::string item_ledger_store::read_meta(const char* key) const
	{
		std::string value;
		if (m_db == nullptr) {
			return value;
		}

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(m_db, "SELECT value FROM meta WHERE key = ?;",
			-1, &stmt, nullptr) != SQLITE_OK) {
			return value;
		}
		sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
		if (sqlite3_step(stmt) == SQLITE_ROW) {
			const unsigned char* text = sqlite3_column_text(stmt, 0);
			if (text != nullptr) value = reinterpret_cast<const char*>(text);
		}
		sqlite3_finalize(stmt);
		return value;
	}

	int64_t item_ledger_store::read_meta_int(const char* key) const
	{
		const std::string raw = read_meta(key);
		if (raw.empty()) {
			return 0;
		}

		int64_t value = 0;
		const auto result = std::from_chars(raw.data(), raw.data() + raw.size(), value);
		if (result.ec != std::errc{} || result.ptr != raw.data() + raw.size()) {
			hb::logger::warn("[LEDGER] meta.{} is not a number ('{}') - reading it as 0",
				key, raw);
			return 0;
		}
		return value;
	}

	bool item_ledger_store::write_meta(const char* key, const std::string& value)
	{
		if (m_db == nullptr) {
			return false;
		}

		sqlite3_stmt* stmt = nullptr;
		if (sqlite3_prepare_v2(m_db,
			"INSERT INTO meta(key, value) VALUES(?, ?)"
			" ON CONFLICT(key) DO UPDATE SET value = excluded.value;",
			-1, &stmt, nullptr) != SQLITE_OK) {
			hb::logger::error("[LEDGER] meta upsert prepare failed: {}", sqlite3_errmsg(m_db));
			return false;
		}
		sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
		const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
		if (!ok) {
			hb::logger::error("[LEDGER] meta upsert failed for {}: {}", key, sqlite3_errmsg(m_db));
		}
		sqlite3_finalize(stmt);
		return ok;
	}

	//////////////////////////////////////////////////////////////////////
	// Recording
	//////////////////////////////////////////////////////////////////////

	void item_ledger_store::record_mint(const CItem& item, const char* origin_detail,
		const char* map, int x, int y)
	{
		// A Counted item has no identity to record a birth for (D2); its arrival
		// is a flow counter, and the caller that wants it counted says so with
		// record_flow. Silently ignoring it here keeps the factory's call site
		// free of a stackability test it would otherwise have to repeat.
		if (m_db == nullptr || item.m_serial == 0) {
			return;
		}

		const int64_t at = hb::time::unix_now();

		ledger_instance instance;
		instance.serial = item.m_serial;
		instance.item_id = item.m_id_num;
		instance.created_at = at;
		instance.origin_type = item.m_origin;
		if (origin_detail != nullptr) instance.origin_detail = origin_detail;
		if (map != nullptr) instance.map = map;
		instance.x = x;
		instance.y = y;
		instance.tier = item.get_tier();
		m_instances.push_back(std::move(instance));

		// The birth row and the creation event are made together because they
		// are one fact. A biography that starts with a custody transfer and no
		// creation looks exactly like a duped item.
		//
		// Through record_event rather than straight into the buffer, so the
		// guards that protect that buffer have one door instead of two: `at` is
		// set here on purpose, to the same second the instance row carries.
		ledger_event_record created;
		created.serial = item.m_serial;
		created.at = at;
		created.event_type = ledger_event::created;
		if (map != nullptr) created.map = map;
		created.x = x;
		created.y = y;
		record_event(std::move(created));
	}

	void item_ledger_store::record_event(ledger_event_record event)
	{
		if (m_db == nullptr) {
			return;
		}

		// Serial 0 is the boundary sentinel and belongs to this class alone. An
		// event arriving from a call site with no Serial is a Counted item that
		// took the Instanced path — recording it would attach a transition to no
		// instance, which reads as an orphan to Reconciliation.
		if (event.serial == 0 && event.event_type != ledger_event::boundary) {
			return;
		}

		// Unstamped means now. The dual sink (#78) records from inside the game
		// action, so "when" is always the moment of the call, and leaving each
		// call site to say so would put a clock read in the game loop and give a
		// forgotten one the epoch. A caller that does set `at` keeps it: replaying
		// a known time is the only reason to want that column not to be now.
		if (event.at == 0) {
			event.at = hb::time::unix_now();
		}

		m_events.push_back(std::move(event));
	}

	void item_ledger_store::record_flow(int32_t item_id, int32_t flow_type, int64_t qty)
	{
		if (m_db == nullptr || qty == 0) {
			return;
		}

		m_flows[flow_key{ flow_day(), item_id, flow_type }] += qty;
	}

	void item_ledger_store::record_kill(int32_t npc_id, const char* npc_name, double rep_factor,
		double title_factor)
	{
		if (m_db == nullptr) {
			return;
		}

		// An NPC that was never spawned from a config has none, and a kill keyed on
		// -1 would pool every such death into one row that names no monster. The
		// analytics divide by this number, so a row that cannot be attributed is
		// worse than an absent one.
		if (npc_id < 0) {
			return;
		}

		kill_tally& tally = m_kills[kill_key{ flow_day(), npc_id }];
		tally.kills += 1;
		tally.rep_factor_sum += rep_factor;
		tally.title_factor_sum += title_factor;

		// Written once per key rather than on every death: the name cannot change
		// for a given config id, and this runs on every monster kill in the world.
		if (tally.npc_name.empty() && npc_name != nullptr) {
			tally.npc_name = npc_name;
		}
	}

	size_t item_ledger_store::pending_count() const
	{
		return m_instances.size() + m_events.size() + m_flows.size() + m_kills.size();
	}

	int32_t item_ledger_store::flow_day() const
	{
		// Every stackable that moves lands here, and the answer changes once a
		// day, so the timezone conversion is memoized against the second it was
		// derived from. Still exact — a new second is always re-derived.
		const int64_t now = hb::time::unix_now();
		if (m_flow_day != 0 && m_flow_day_at == now) {
			return m_flow_day;
		}

		const hb::time::local_time local = hb::time::local_time::now();
		m_flow_day = local.date_key();
		m_flow_day_at = now;
		return m_flow_day;
	}

	//////////////////////////////////////////////////////////////////////
	// Flushing
	//////////////////////////////////////////////////////////////////////

	bool item_ledger_store::flush(int64_t high_water)
	{
		if (m_db == nullptr) {
			return false;
		}

		const bool advance_high_water = high_water > m_written_high_water;
		if (m_instances.empty() && m_events.empty() && m_flows.empty() && m_kills.empty()
			&& !advance_high_water) {
			return true;
		}

		if (!exec_sql(m_db, "BEGIN IMMEDIATE;")) {
			return false;
		}

		int dropped = 0;
		bool ok = write_instances(dropped) && write_events(dropped) && write_flows(dropped)
			&& write_kills(dropped);

		// The high-water mark rides the same transaction as the rows it covers.
		// Persisting it separately could leave a mark above instances that never
		// landed — recoverable (recovery takes the max) but it would waste the
		// gap; persisting it late is the dangerous direction and this forbids it.
		if (ok && advance_high_water) {
			ok = write_meta("serial_high_water", std::to_string(high_water));
		}

		if (ok) {
			ok = exec_sql(m_db, "COMMIT;");
		}

		if (!ok) {
			exec_sql(m_db, "ROLLBACK;");
			// The buffer is kept on purpose. Dropping it would turn one failed
			// write into a permanent hole in the audit trail, and the next flush
			// costs nothing extra to retry it.
			//
			// The flag is what stops that retry becoming a busy loop: the rows it
			// could not write keep the size trigger armed, so without it
			// process_flush would replay the whole batch on every 20 ms tick. Only
			// logged on the first failure of a run for the same reason — the
			// logger flushes to disk synchronously.
			if (!m_flush_failed) {
				hb::logger::error("[LEDGER] flush failed - {} row(s) held for the next attempt; "
					"retrying on the flush interval", pending_count());
			}
			m_flush_failed = true;
			return false;
		}

		if (m_flush_failed) {
			hb::logger::log("[LEDGER] flush recovered - {} held row(s) written", pending_count());
			m_flush_failed = false;
		}

		// Counted for the life of the process, not just this window: a rejection
		// is a defect signal, and one line per occurrence scrolls away while the
		// running total is what an operator can still see an hour later.
		if (dropped > 0) {
			m_dropped_rows += dropped;
			hb::logger::error("[LEDGER] {} row(s) rejected by the schema this flush "
				"({} since start) - the ledger is now INCOMPLETE and the cause is "
				"upstream of it", dropped, m_dropped_rows);
		}

		if (advance_high_water) {
			m_written_high_water = high_water;
		}

		hb::logger::log<log_channel::items_misc>(
			"[LEDGER] flushed {} instance(s), {} event(s), {} flow row(s), {} kill row(s); "
			"high-water {}",
			m_instances.size(), m_events.size(), m_flows.size(), m_kills.size(),
			m_written_high_water);

		m_instances.clear();
		m_events.clear();
		m_flows.clear();
		m_kills.clear();
		return true;
	}

	bool item_ledger_store::write_instances(int& dropped)
	{
		return write_batch(m_db, "instance",
			"INSERT INTO item_instances"
			" (serial, item_id, created_at, origin_type, origin_detail, map, x, y, tier)"
			" VALUES (?,?,?,?,?,?,?,?,?);",
			m_instances,
			[](sqlite3_stmt* stmt, const ledger_instance& row)
			{
				int col = 1;
				sqlite3_bind_int64(stmt, col++, row.serial);
				sqlite3_bind_int(stmt, col++, row.item_id);
				sqlite3_bind_int64(stmt, col++, row.created_at);
				sqlite3_bind_int(stmt, col++, row.origin_type);
				bind_text_or_null(stmt, col++, row.origin_detail);
				bind_text_or_null(stmt, col++, row.map);
				sqlite3_bind_int(stmt, col++, row.x);
				sqlite3_bind_int(stmt, col++, row.y);
				sqlite3_bind_int(stmt, col++, row.tier);
			},
			[](const ledger_instance& row)
			{
				return std::format("serial {}", row.serial);
			}, dropped);
	}

	bool item_ledger_store::write_events(int& dropped)
	{
		return write_batch(m_db, "event",
			"INSERT INTO item_events"
			" (serial, at, event_type, actor_account, actor_char, counterparty_char,"
			"  map, x, y, detail)"
			" VALUES (?,?,?,?,?,?,?,?,?,?);",
			m_events,
			[](sqlite3_stmt* stmt, const ledger_event_record& row)
			{
				int col = 1;
				sqlite3_bind_int64(stmt, col++, row.serial);
				sqlite3_bind_int64(stmt, col++, row.at);
				sqlite3_bind_int(stmt, col++, row.event_type);
				bind_text_or_null(stmt, col++, row.actor_account);
				bind_text_or_null(stmt, col++, row.actor_char);
				bind_text_or_null(stmt, col++, row.counterparty_char);
				bind_text_or_null(stmt, col++, row.map);
				sqlite3_bind_int(stmt, col++, row.x);
				sqlite3_bind_int(stmt, col++, row.y);
				bind_text_or_null(stmt, col++, row.detail);
			},
			[](const ledger_event_record& row)
			{
				return std::format("serial {} type {}", row.serial, row.event_type);
			}, dropped);
	}

	bool item_ledger_store::write_flows(int& dropped)
	{
		// Accumulate rather than replace: a day's row is the sum of every window
		// that touched it, and the buffer only ever holds this window's delta.
		return write_batch(m_db, "flow",
			"INSERT INTO item_flows (day, item_id, flow_type, qty) VALUES (?,?,?,?)"
			" ON CONFLICT(day, item_id, flow_type) DO UPDATE SET qty = qty + excluded.qty;",
			m_flows,
			[](sqlite3_stmt* stmt, const std::pair<const flow_key, int64_t>& row)
			{
				sqlite3_bind_int(stmt, 1, row.first.day);
				sqlite3_bind_int(stmt, 2, row.first.item_id);
				sqlite3_bind_int(stmt, 3, row.first.flow_type);
				sqlite3_bind_int64(stmt, 4, row.second);
			},
			[](const std::pair<const flow_key, int64_t>& row)
			{
				return std::format("day {} item {} type {}",
					row.first.day, row.first.item_id, row.first.flow_type);
			}, dropped);
	}

	bool item_ledger_store::write_kills(int& dropped)
	{
		// Both columns accumulate, for the same reason the flow qty does: the
		// buffer holds this window's delta and the row holds the day's total. They
		// have to move together — a kills count advanced without its reputation
		// sum would silently reprice every gear row that day.
		return write_batch(m_db, "kill",
			"INSERT INTO npc_kills (day, npc_id, npc_name, kills, rep_factor_sum,"
			" title_factor_sum)"
			" VALUES (?,?,?,?,?,?)"
			" ON CONFLICT(day, npc_id) DO UPDATE SET"
			" kills = kills + excluded.kills,"
			" rep_factor_sum = rep_factor_sum + excluded.rep_factor_sum,"
			" title_factor_sum = title_factor_sum + excluded.title_factor_sum;",
			m_kills,
			[](sqlite3_stmt* stmt, const std::pair<const kill_key, kill_tally>& row)
			{
				sqlite3_bind_int(stmt, 1, row.first.day);
				sqlite3_bind_int(stmt, 2, row.first.npc_id);
				sqlite3_bind_text(stmt, 3, row.second.npc_name.c_str(),
					static_cast<int>(row.second.npc_name.size()), SQLITE_TRANSIENT);
				sqlite3_bind_int64(stmt, 4, row.second.kills);
				sqlite3_bind_double(stmt, 5, row.second.rep_factor_sum);
				sqlite3_bind_double(stmt, 6, row.second.title_factor_sum);
			},
			[](const std::pair<const kill_key, kill_tally>& row)
			{
				return std::format("day {} npc {}", row.first.day, row.first.npc_id);
			}, dropped);
	}

	void item_ledger_store::set_flush_cadence(int interval_ms, int event_threshold)
	{
		m_flush_interval_ms = interval_ms;
		m_flush_event_threshold = event_threshold > 0 ? static_cast<size_t>(event_threshold) : 0;
	}

	void item_ledger_store::process_flush(uint32_t now_ms, int64_t high_water)
	{
		if (m_db == nullptr) {
			return;
		}

		// The size trigger exists so a busy world does not hold an unbounded
		// buffer between ticks — under load it is what keeps the crash window
		// small, and the timer is what keeps a quiet world from holding events
		// for hours.
		//
		// Disarmed while a flush is failing: the held rows are what push the
		// buffer over the threshold, so leaving it armed would retry the entire
		// batch fifty times a second for as long as the write kept failing. The
		// interval still retries it, which is all the promise requires.
		const bool over_threshold = m_flush_event_threshold > 0
			&& !m_flush_failed
			&& pending_count() >= m_flush_event_threshold;

		const bool interval_elapsed = m_flush_interval_ms > 0
			&& (now_ms - m_last_flush_ms) >= static_cast<uint32_t>(m_flush_interval_ms);

		if (m_flushed_once && !over_threshold && !interval_elapsed) {
			return;
		}

		m_flushed_once = true;
		m_last_flush_ms = now_ms;
		flush(high_water);
	}
}
