// GuildSqliteStore.cpp: guilds.db schema and the integrity-preserving primitives (#120)
//
// See the header for the contract. Two disciplines shape every function here:
//
//   * Multi-row operations are one transaction. A guild is never on disk
//     without its master, masks and Treasury; a withdrawal is never taken
//     without its log row; a succession never half-happens. txn_scope on this
//     store's own connection gives rollback-on-early-return for free.
//
//   * Refusals are pre-checked for clean semantics, with the schema's
//     constraints (UNIQUE, CHECK, foreign keys) as the backstop for whatever a
//     future caller finds a way around. A constraint trip is logged and comes
//     back false, never half-applied.
//
// Rank and Title-kind legality is deliberately code (GuildDefs.h) rather than
// DDL CHECKs: ADR 0006 promises a later fourth rank without a schema change,
// so the schema constrains only what is structural forever — name length,
// side, non-negative gold, positive withdrawals.
//
//////////////////////////////////////////////////////////////////////

#include "GuildSqliteStore.h"

#include <format>

#include "AccountSqliteStore.h"   // VerifySqliteSchemaVersion, the shared stale-schema gate
#include "Log.h"
#include "sqlite3.h"
#include "StringCompat.h"

using namespace hb::shared::guild;

namespace
{
	// Single source of truth for the guilds.db schema version. Bump on any
	// schema change (fresh start — stale files are refused, never migrated).
	constexpr const char* guilds_db_schema_version = "1";

	bool exec_sql(sqlite3* db, const char* sql)
	{
		char* err = nullptr;
		if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK)
		{
			hb::logger::error("[GUILDDB] exec failed: {}", err ? err : "unknown");
			sqlite3_free(err);
			return false;
		}
		return true;
	}

	// Bind helpers so the call sites read as a column list. Text is TRANSIENT:
	// every caller passes buffers it may rewrite before the step.
	bool bind_text(sqlite3_stmt* stmt, int idx, const char* value)
	{
		return sqlite3_bind_text(stmt, idx, value, -1, SQLITE_TRANSIENT) == SQLITE_OK;
	}

	std::string column_string(sqlite3_stmt* stmt, int col)
	{
		const unsigned char* text = sqlite3_column_text(stmt, col);
		return text != nullptr ? reinterpret_cast<const char*>(text) : "";
	}

	// Column order contract for every guilds / guild_members SELECT below.
	void read_guild_row(sqlite3_stmt* stmt, hb::server::guild_record& out)
	{
		out.guid        = sqlite3_column_int64(stmt, 0);
		out.name        = column_string(stmt, 1);
		out.side        = sqlite3_column_int(stmt, 2);
		out.master_name = column_string(stmt, 3);
		out.created_at  = sqlite3_column_int64(stmt, 4);
		out.xp          = sqlite3_column_int64(stmt, 5);
		out.level       = sqlite3_column_int(stmt, 6);
	}

	void read_member_row(sqlite3_stmt* stmt, hb::server::guild_member_record& out)
	{
		out.guild_guid           = sqlite3_column_int64(stmt, 0);
		out.char_name            = column_string(stmt, 1);
		out.rank                 = sqlite3_column_int(stmt, 2);
		out.joined_at            = sqlite3_column_int64(stmt, 3);
		out.donated_gold         = sqlite3_column_int64(stmt, 4);
		out.donated_ek           = sqlite3_column_int64(stmt, 5);
		out.donated_contribution = sqlite3_column_int64(stmt, 6);
	}

	void read_request_row(sqlite3_stmt* stmt, hb::server::guild_request_record& out)
	{
		out.request_id = sqlite3_column_int64(stmt, 0);
		out.guild_guid = sqlite3_column_int64(stmt, 1);
		out.char_name  = column_string(stmt, 2);
		out.kind       = sqlite3_column_int(stmt, 3);
		out.created_at = sqlite3_column_int64(stmt, 4);
	}

	void read_title_row(sqlite3_stmt* stmt, hb::server::guild_title_record& out)
	{
		out.guild_guid   = sqlite3_column_int64(stmt, 0);
		out.kind         = sqlite3_column_int(stmt, 1);
		out.holder_name  = column_string(stmt, 2);
		out.claimed_at   = sqlite3_column_int64(stmt, 3);
		out.last_seen_at = sqlite3_column_int64(stmt, 4);
	}
}

namespace hb::server
{

bool ensure_guild_schema(sqlite3* db, const char* db_label)
{
	if (db == nullptr)
	{
		return false;
	}

	const sqlite_schema_state state =
		VerifySqliteSchemaVersion(db, db_label, guilds_db_schema_version);
	if (state == sqlite_schema_state::mismatch)
	{
		return false;
	}
	if (state == sqlite_schema_state::current)
	{
		return true;
	}

	// Fresh database: the full v1 schema in one transaction. Name columns are
	// COLLATE NOCASE like game.db's, so uniqueness and every by-name lookup
	// mean the case-insensitive thing players assume. The version stamp and
	// the name-length CHECK are spliced from their constants ({0}/{1}) so the
	// DDL can never drift from the gate above or from GuildDefs.h.
	const std::string schema_sql = std::format(
		"BEGIN;"
		"CREATE TABLE IF NOT EXISTS meta ("
		" key TEXT PRIMARY KEY,"
		" value TEXT NOT NULL"
		");"
		"INSERT INTO meta(key, value) VALUES('schema_version','{0}');"

		// guid is AUTOINCREMENT so a disbanded guild's identity is never
		// re-issued — characters' crusade_guid and later audit trails may
		// outlive the guild they name.
		"CREATE TABLE IF NOT EXISTS guilds ("
		" guid INTEGER PRIMARY KEY AUTOINCREMENT,"
		" name TEXT NOT NULL UNIQUE COLLATE NOCASE CHECK(length(name) BETWEEN 1 AND {1}),"
		" side INTEGER NOT NULL CHECK(side IN (1,2)),"
		" master_name TEXT NOT NULL COLLATE NOCASE,"
		" created_at INTEGER NOT NULL,"
		" xp INTEGER NOT NULL DEFAULT 0 CHECK(xp >= 0),"
		" level INTEGER NOT NULL DEFAULT 1 CHECK(level >= 1)"
		");"

		// char_name is the primary key: one guild per character is schema
		// fact. The master's row is not special in this table — the invariant
		// that guilds.master_name names a rank-0 row here is create/transfer
		// code plus the boot sweep.
		"CREATE TABLE IF NOT EXISTS guild_members ("
		" char_name TEXT PRIMARY KEY COLLATE NOCASE,"
		" guild_guid INTEGER NOT NULL REFERENCES guilds(guid) ON DELETE CASCADE,"
		" rank INTEGER NOT NULL,"
		" joined_at INTEGER NOT NULL,"
		" donated_gold INTEGER NOT NULL DEFAULT 0 CHECK(donated_gold >= 0),"
		" donated_ek INTEGER NOT NULL DEFAULT 0 CHECK(donated_ek >= 0),"
		" donated_contribution INTEGER NOT NULL DEFAULT 0 CHECK(donated_contribution >= 0)"
		");"
		"CREATE INDEX IF NOT EXISTS idx_guild_members_guild ON guild_members(guild_guid);"

		"CREATE TABLE IF NOT EXISTS guild_rank_permissions ("
		" guild_guid INTEGER NOT NULL REFERENCES guilds(guid) ON DELETE CASCADE,"
		" rank INTEGER NOT NULL,"
		" mask INTEGER NOT NULL,"
		" PRIMARY KEY (guild_guid, rank)"
		");"

		"CREATE TABLE IF NOT EXISTS guild_requests ("
		" request_id INTEGER PRIMARY KEY AUTOINCREMENT,"
		" guild_guid INTEGER NOT NULL REFERENCES guilds(guid) ON DELETE CASCADE,"
		" char_name TEXT NOT NULL COLLATE NOCASE,"
		" kind INTEGER NOT NULL,"
		" created_at INTEGER NOT NULL,"
		" UNIQUE (guild_guid, char_name, kind)"
		");"

		"CREATE TABLE IF NOT EXISTS guild_treasury ("
		" guild_guid INTEGER PRIMARY KEY REFERENCES guilds(guid) ON DELETE CASCADE,"
		" gold INTEGER NOT NULL DEFAULT 0 CHECK(gold >= 0)"
		");"

		"CREATE TABLE IF NOT EXISTS guild_treasury_withdrawals ("
		" withdrawal_id INTEGER PRIMARY KEY AUTOINCREMENT,"
		" guild_guid INTEGER NOT NULL REFERENCES guilds(guid) ON DELETE CASCADE,"
		" char_name TEXT NOT NULL COLLATE NOCASE,"
		" amount INTEGER NOT NULL CHECK(amount > 0),"
		" withdrawn_at INTEGER NOT NULL"
		");"
		"CREATE INDEX IF NOT EXISTS idx_guild_withdrawals_guild"
		" ON guild_treasury_withdrawals(guild_guid);"

		// holder_name is UNIQUE (one Title per member, §3.1.1) and cascades
		// with the member row, so a kick or leave releases the slot by
		// construction. That the holder's guild matches guild_guid is a
		// cross-table fact foreign keys cannot say — claim_title enforces it
		// and validate() sweeps it.
		"CREATE TABLE IF NOT EXISTS guild_titles ("
		" guild_guid INTEGER NOT NULL REFERENCES guilds(guid) ON DELETE CASCADE,"
		" kind INTEGER NOT NULL,"
		" holder_name TEXT NOT NULL UNIQUE COLLATE NOCASE"
		"  REFERENCES guild_members(char_name) ON DELETE CASCADE,"
		" claimed_at INTEGER NOT NULL,"
		" last_seen_at INTEGER NOT NULL"
		");"
		"CREATE INDEX IF NOT EXISTS idx_guild_titles_guild ON guild_titles(guild_guid);"
		"COMMIT;",
		guilds_db_schema_version, max_guild_name_length);

	if (!exec_sql(db, schema_sql.c_str()))
	{
		hb::logger::error("[GUILDDB] {}: could not create the v{} schema",
			db_label, guilds_db_schema_version);
		return false;
	}
	hb::logger::log("[GUILDDB] {}: fresh database, v{} schema created",
		db_label, guilds_db_schema_version);
	return true;
}

bool guild_sqlite_store::open(const std::string& path)
{
	if (m_db.is_open())
	{
		return true;
	}
	if (!m_db.open(path, "GUILDDB"))
	{
		return false;
	}
	if (!ensure_guild_schema(m_db.handle(), path.c_str()))
	{
		m_db.close();
		return false;
	}
	return true;
}

void guild_sqlite_store::close()
{
	m_db.close();
}

//--------------------------------------------------------------------
// Guild lifecycle
//--------------------------------------------------------------------

bool guild_sqlite_store::create_guild(const char* name, int side,
	const char* master_name, int64_t now, int64_t& out_guid, std::string& out_error)
{
	out_guid = 0;
	out_error.clear();
	if (!is_open() || name == nullptr || master_name == nullptr)
	{
		out_error = "guild store unavailable";
		return false;
	}
	if (!is_valid_guild_name(name))
	{
		out_error = std::format("invalid guild name '{}' (1-{} letters, digits,"
			" spaces, '_' or '-'; no leading/trailing space)",
			name, max_guild_name_length);
		return false;
	}
	if (side != 1 && side != 2)
	{
		out_error = std::format("invalid side {}", side);
		return false;
	}
	{
		guild_record existing;
		if (find_guild_by_name(name, existing))
		{
			out_error = std::format("a guild named '{}' already exists", existing.name);
			return false;
		}
	}
	if (is_member_anywhere(master_name))
	{
		out_error = std::format("'{}' is already in a guild", master_name);
		return false;
	}

	txn_scope txn(m_db.handle());
	if (!txn.active())
	{
		out_error = "transaction failed";
		return false;
	}

	{
		sqlite3_stmt* stmt = m_db.cached(
			"INSERT INTO guilds(name, side, master_name, created_at)"
			" VALUES(?,?,?,?);");
		if (stmt == nullptr
			|| !bind_text(stmt, 1, name)
			|| sqlite3_bind_int(stmt, 2, side) != SQLITE_OK
			|| !bind_text(stmt, 3, master_name)
			|| sqlite3_bind_int64(stmt, 4, now) != SQLITE_OK
			|| sqlite3_step(stmt) != SQLITE_DONE)
		{
			out_error = std::format("guilds insert failed: {}", sqlite3_errmsg(m_db.handle()));
			return false;
		}
	}
	const int64_t guid = sqlite3_last_insert_rowid(m_db.handle());

	{
		sqlite3_stmt* stmt = m_db.cached(
			"INSERT INTO guild_members(char_name, guild_guid, rank, joined_at)"
			" VALUES(?,?,?,?);");
		if (stmt == nullptr
			|| !bind_text(stmt, 1, master_name)
			|| sqlite3_bind_int64(stmt, 2, guid) != SQLITE_OK
			|| sqlite3_bind_int(stmt, 3, guild_rank::guildmaster) != SQLITE_OK
			|| sqlite3_bind_int64(stmt, 4, now) != SQLITE_OK
			|| sqlite3_step(stmt) != SQLITE_DONE)
		{
			out_error = std::format("master member insert failed: {}", sqlite3_errmsg(m_db.handle()));
			return false;
		}
	}

	for (int rank = guild_rank::guildmaster; rank <= guild_rank::guildsman; rank++)
	{
		sqlite3_stmt* stmt = m_db.cached(
			"INSERT INTO guild_rank_permissions(guild_guid, rank, mask) VALUES(?,?,?);");
		if (stmt == nullptr
			|| sqlite3_bind_int64(stmt, 1, guid) != SQLITE_OK
			|| sqlite3_bind_int(stmt, 2, rank) != SQLITE_OK
			|| sqlite3_bind_int64(stmt, 3, default_permission_mask(rank)) != SQLITE_OK
			|| sqlite3_step(stmt) != SQLITE_DONE)
		{
			out_error = std::format("permission seed failed: {}", sqlite3_errmsg(m_db.handle()));
			return false;
		}
	}

	{
		sqlite3_stmt* stmt = m_db.cached(
			"INSERT INTO guild_treasury(guild_guid, gold) VALUES(?, 0);");
		if (stmt == nullptr
			|| sqlite3_bind_int64(stmt, 1, guid) != SQLITE_OK
			|| sqlite3_step(stmt) != SQLITE_DONE)
		{
			out_error = std::format("treasury seed failed: {}", sqlite3_errmsg(m_db.handle()));
			return false;
		}
	}

	if (!txn.commit())
	{
		out_error = "commit failed";
		return false;
	}
	out_guid = guid;
	return true;
}

bool guild_sqlite_store::disband_guild(int64_t guid)
{
	if (!is_open())
	{
		return false;
	}
	sqlite3_stmt* stmt = m_db.cached("DELETE FROM guilds WHERE guid = ?;");
	if (stmt == nullptr
		|| sqlite3_bind_int64(stmt, 1, guid) != SQLITE_OK
		|| sqlite3_step(stmt) != SQLITE_DONE)
	{
		return false;
	}
	return sqlite3_changes(m_db.handle()) > 0;
}

bool guild_sqlite_store::load_guild(int64_t guid, guild_record& out)
{
	if (!is_open())
	{
		return false;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"SELECT guid, name, side, master_name, created_at, xp, level"
		" FROM guilds WHERE guid = ?;");
	if (stmt == nullptr || sqlite3_bind_int64(stmt, 1, guid) != SQLITE_OK)
	{
		return false;
	}
	if (sqlite3_step(stmt) != SQLITE_ROW)
	{
		return false;
	}
	read_guild_row(stmt, out);
	sqlite3_reset(stmt);
	return true;
}

bool guild_sqlite_store::find_guild_by_name(const char* name, guild_record& out)
{
	if (!is_open() || name == nullptr)
	{
		return false;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"SELECT guid, name, side, master_name, created_at, xp, level"
		" FROM guilds WHERE name = ?;");
	if (stmt == nullptr || !bind_text(stmt, 1, name))
	{
		return false;
	}
	if (sqlite3_step(stmt) != SQLITE_ROW)
	{
		return false;
	}
	read_guild_row(stmt, out);
	sqlite3_reset(stmt);
	return true;
}

std::vector<guild_record> guild_sqlite_store::all_guilds()
{
	std::vector<guild_record> rows;
	if (!is_open())
	{
		return rows;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"SELECT guid, name, side, master_name, created_at, xp, level"
		" FROM guilds ORDER BY guid;");
	if (stmt == nullptr)
	{
		return rows;
	}
	while (sqlite3_step(stmt) == SQLITE_ROW)
	{
		guild_record& row = rows.emplace_back();
		read_guild_row(stmt, row);
	}
	return rows;
}

bool guild_sqlite_store::set_progress(int64_t guid, int64_t xp, int level)
{
	if (!is_open() || xp < 0 || level < 1)
	{
		return false;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"UPDATE guilds SET xp = ?, level = ? WHERE guid = ?;");
	if (stmt == nullptr
		|| sqlite3_bind_int64(stmt, 1, xp) != SQLITE_OK
		|| sqlite3_bind_int(stmt, 2, level) != SQLITE_OK
		|| sqlite3_bind_int64(stmt, 3, guid) != SQLITE_OK
		|| sqlite3_step(stmt) != SQLITE_DONE)
	{
		return false;
	}
	return sqlite3_changes(m_db.handle()) > 0;
}

//--------------------------------------------------------------------
// Membership
//--------------------------------------------------------------------

bool guild_sqlite_store::add_member(int64_t guid, const char* char_name, int rank,
	int64_t now)
{
	if (!is_open() || char_name == nullptr)
	{
		return false;
	}
	// Only create/transfer mint a guildmaster; everything else is a join.
	if (rank != guild_rank::officer && rank != guild_rank::guildsman)
	{
		return false;
	}
	if (!guild_exists(guid))
	{
		return false;
	}
	if (is_member_anywhere(char_name))
	{
		return false;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"INSERT INTO guild_members(char_name, guild_guid, rank, joined_at)"
		" VALUES(?,?,?,?);");
	return stmt != nullptr
		&& bind_text(stmt, 1, char_name)
		&& sqlite3_bind_int64(stmt, 2, guid) == SQLITE_OK
		&& sqlite3_bind_int(stmt, 3, rank) == SQLITE_OK
		&& sqlite3_bind_int64(stmt, 4, now) == SQLITE_OK
		&& sqlite3_step(stmt) == SQLITE_DONE;
}

bool guild_sqlite_store::remove_member(const char* char_name)
{
	if (!is_open() || char_name == nullptr)
	{
		return false;
	}
	guild_member_record member;
	if (!member_of(char_name, member))
	{
		return false;
	}
	// The master leaves by transfer or disband only; deleting this row would
	// leave a guild whose master is not a member, which validate() rightly
	// refuses to boot.
	if (is_guild_master(member))
	{
		return false;
	}

	txn_scope txn(m_db.handle());
	if (!txn.active())
	{
		return false;
	}

	// The held Title cascades with the member row (schema). The pending leave
	// request does not — it references no member row — so it is retired here;
	// join requests to OTHER guilds are the character's own business and stay.
	{
		sqlite3_stmt* stmt = m_db.cached(
			"DELETE FROM guild_requests WHERE char_name = ? AND guild_guid = ?;");
		if (stmt == nullptr
			|| !bind_text(stmt, 1, char_name)
			|| sqlite3_bind_int64(stmt, 2, member.guild_guid) != SQLITE_OK
			|| sqlite3_step(stmt) != SQLITE_DONE)
		{
			return false;
		}
	}
	{
		sqlite3_stmt* stmt = m_db.cached("DELETE FROM guild_members WHERE char_name = ?;");
		if (stmt == nullptr
			|| !bind_text(stmt, 1, char_name)
			|| sqlite3_step(stmt) != SQLITE_DONE)
		{
			return false;
		}
	}
	return txn.commit();
}

bool guild_sqlite_store::set_rank(const char* char_name, int rank)
{
	if (!is_open() || char_name == nullptr)
	{
		return false;
	}
	if (rank != guild_rank::officer && rank != guild_rank::guildsman)
	{
		return false;
	}
	guild_member_record member;
	if (!member_of(char_name, member))
	{
		return false;
	}
	if (is_guild_master(member))
	{
		return false;
	}
	return write_rank(char_name, rank);
}

bool guild_sqlite_store::transfer_mastership(int64_t guid, const char* new_master_name)
{
	if (!is_open() || new_master_name == nullptr)
	{
		return false;
	}
	guild_record guild;
	if (!load_guild(guid, guild))
	{
		return false;
	}
	guild_member_record successor;
	if (!member_of(new_master_name, successor) || successor.guild_guid != guid)
	{
		return false;
	}
	if (hb_stricmp(guild.master_name.c_str(), successor.char_name.c_str()) == 0)
	{
		return false;
	}

	txn_scope txn(m_db.handle());
	if (!txn.active())
	{
		return false;
	}

	if (!write_rank(guild.master_name.c_str(), guild_rank::officer)
		|| !write_rank(new_master_name, guild_rank::guildmaster))
	{
		return false;
	}
	sqlite3_stmt* rename = m_db.cached(
		"UPDATE guilds SET master_name = ? WHERE guid = ?;");
	if (rename == nullptr
		|| !bind_text(rename, 1, new_master_name)
		|| sqlite3_bind_int64(rename, 2, guid) != SQLITE_OK
		|| sqlite3_step(rename) != SQLITE_DONE)
	{
		return false;
	}
	return txn.commit();
}

bool guild_sqlite_store::member_of(const char* char_name, guild_member_record& out)
{
	if (!is_open() || char_name == nullptr)
	{
		return false;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"SELECT guild_guid, char_name, rank, joined_at,"
		" donated_gold, donated_ek, donated_contribution"
		" FROM guild_members WHERE char_name = ?;");
	if (stmt == nullptr || !bind_text(stmt, 1, char_name))
	{
		return false;
	}
	if (sqlite3_step(stmt) != SQLITE_ROW)
	{
		return false;
	}
	read_member_row(stmt, out);
	sqlite3_reset(stmt);
	return true;
}

std::vector<guild_member_record> guild_sqlite_store::roster(int64_t guid)
{
	std::vector<guild_member_record> rows;
	if (!is_open())
	{
		return rows;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"SELECT guild_guid, char_name, rank, joined_at,"
		" donated_gold, donated_ek, donated_contribution"
		" FROM guild_members WHERE guild_guid = ? ORDER BY rank, char_name;");
	if (stmt == nullptr || sqlite3_bind_int64(stmt, 1, guid) != SQLITE_OK)
	{
		return rows;
	}
	while (sqlite3_step(stmt) == SQLITE_ROW)
	{
		guild_member_record& row = rows.emplace_back();
		read_member_row(stmt, row);
	}
	return rows;
}

int guild_sqlite_store::member_count(int64_t guid)
{
	if (!is_open())
	{
		return 0;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"SELECT COUNT(*) FROM guild_members WHERE guild_guid = ?;");
	if (stmt == nullptr || sqlite3_bind_int64(stmt, 1, guid) != SQLITE_OK)
	{
		return 0;
	}
	int count = 0;
	if (sqlite3_step(stmt) == SQLITE_ROW)
	{
		count = sqlite3_column_int(stmt, 0);
	}
	sqlite3_reset(stmt);
	return count;
}

bool guild_sqlite_store::add_donation(const char* char_name, int64_t gold,
	int64_t enemy_kills, int64_t contribution)
{
	if (!is_open() || char_name == nullptr)
	{
		return false;
	}
	if (gold < 0 || enemy_kills < 0 || contribution < 0)
	{
		return false;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"UPDATE guild_members SET"
		" donated_gold = donated_gold + ?,"
		" donated_ek = donated_ek + ?,"
		" donated_contribution = donated_contribution + ?"
		" WHERE char_name = ?;");
	if (stmt == nullptr
		|| sqlite3_bind_int64(stmt, 1, gold) != SQLITE_OK
		|| sqlite3_bind_int64(stmt, 2, enemy_kills) != SQLITE_OK
		|| sqlite3_bind_int64(stmt, 3, contribution) != SQLITE_OK
		|| !bind_text(stmt, 4, char_name)
		|| sqlite3_step(stmt) != SQLITE_DONE)
	{
		return false;
	}
	return sqlite3_changes(m_db.handle()) > 0;
}

//--------------------------------------------------------------------
// Permission masks
//--------------------------------------------------------------------

bool guild_sqlite_store::permission_mask(int64_t guid, int rank, uint32_t& out_mask)
{
	out_mask = 0;
	if (!is_open())
	{
		return false;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"SELECT mask FROM guild_rank_permissions WHERE guild_guid = ? AND rank = ?;");
	if (stmt == nullptr
		|| sqlite3_bind_int64(stmt, 1, guid) != SQLITE_OK
		|| sqlite3_bind_int(stmt, 2, rank) != SQLITE_OK)
	{
		return false;
	}
	if (sqlite3_step(stmt) != SQLITE_ROW)
	{
		return false;
	}
	out_mask = static_cast<uint32_t>(sqlite3_column_int64(stmt, 0));
	sqlite3_reset(stmt);
	return true;
}

bool guild_sqlite_store::set_permission_mask(int64_t guid, int rank, uint32_t mask)
{
	if (!is_open() || !is_valid_guild_rank(rank))
	{
		return false;
	}
	if ((mask & ~guild_permission::all_bits) != 0)
	{
		return false;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"UPDATE guild_rank_permissions SET mask = ? WHERE guild_guid = ? AND rank = ?;");
	if (stmt == nullptr
		|| sqlite3_bind_int64(stmt, 1, mask) != SQLITE_OK
		|| sqlite3_bind_int64(stmt, 2, guid) != SQLITE_OK
		|| sqlite3_bind_int(stmt, 3, rank) != SQLITE_OK
		|| sqlite3_step(stmt) != SQLITE_DONE)
	{
		return false;
	}
	return sqlite3_changes(m_db.handle()) > 0;
}

//--------------------------------------------------------------------
// Join/leave queue
//--------------------------------------------------------------------

bool guild_sqlite_store::queue_request(int64_t guid, const char* char_name, int kind,
	int64_t now, int64_t& out_request_id)
{
	out_request_id = 0;
	if (!is_open() || char_name == nullptr || !is_valid_guild_request(kind))
	{
		return false;
	}
	if (!guild_exists(guid))
	{
		return false;
	}
	// The same shape validate() holds the table to at boot: a join from a
	// character already in a guild is stale on arrival, and a leave means
	// anything only from a member of that guild.
	guild_member_record member;
	const bool in_a_guild = member_of(char_name, member);
	if (kind == guild_request::join && in_a_guild)
	{
		return false;
	}
	if (kind == guild_request::leave && (!in_a_guild || member.guild_guid != guid))
	{
		return false;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"INSERT INTO guild_requests(guild_guid, char_name, kind, created_at)"
		" VALUES(?,?,?,?);");
	if (stmt == nullptr
		|| sqlite3_bind_int64(stmt, 1, guid) != SQLITE_OK
		|| !bind_text(stmt, 2, char_name)
		|| sqlite3_bind_int(stmt, 3, kind) != SQLITE_OK
		|| sqlite3_bind_int64(stmt, 4, now) != SQLITE_OK
		|| sqlite3_step(stmt) != SQLITE_DONE)
	{
		return false;   // includes the UNIQUE duplicate-request backstop
	}
	out_request_id = sqlite3_last_insert_rowid(m_db.handle());
	return true;
}

bool guild_sqlite_store::load_request(int64_t request_id, guild_request_record& out)
{
	if (!is_open())
	{
		return false;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"SELECT request_id, guild_guid, char_name, kind, created_at"
		" FROM guild_requests WHERE request_id = ?;");
	if (stmt == nullptr || sqlite3_bind_int64(stmt, 1, request_id) != SQLITE_OK)
	{
		return false;
	}
	if (sqlite3_step(stmt) != SQLITE_ROW)
	{
		return false;
	}
	read_request_row(stmt, out);
	sqlite3_reset(stmt);
	return true;
}

bool guild_sqlite_store::delete_request(int64_t request_id)
{
	if (!is_open())
	{
		return false;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"DELETE FROM guild_requests WHERE request_id = ?;");
	if (stmt == nullptr
		|| sqlite3_bind_int64(stmt, 1, request_id) != SQLITE_OK
		|| sqlite3_step(stmt) != SQLITE_DONE)
	{
		return false;
	}
	return sqlite3_changes(m_db.handle()) > 0;
}

std::vector<guild_request_record> guild_sqlite_store::pending_requests(int64_t guid)
{
	std::vector<guild_request_record> rows;
	if (!is_open())
	{
		return rows;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"SELECT request_id, guild_guid, char_name, kind, created_at"
		" FROM guild_requests WHERE guild_guid = ? ORDER BY request_id;");
	if (stmt == nullptr || sqlite3_bind_int64(stmt, 1, guid) != SQLITE_OK)
	{
		return rows;
	}
	while (sqlite3_step(stmt) == SQLITE_ROW)
	{
		read_request_row(stmt, rows.emplace_back());
	}
	return rows;
}

//--------------------------------------------------------------------
// Treasury
//--------------------------------------------------------------------

int64_t guild_sqlite_store::treasury_gold(int64_t guid)
{
	if (!is_open())
	{
		return -1;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"SELECT gold FROM guild_treasury WHERE guild_guid = ?;");
	if (stmt == nullptr || sqlite3_bind_int64(stmt, 1, guid) != SQLITE_OK)
	{
		return -1;
	}
	int64_t gold = -1;
	if (sqlite3_step(stmt) == SQLITE_ROW)
	{
		gold = sqlite3_column_int64(stmt, 0);
	}
	sqlite3_reset(stmt);
	return gold;
}

bool guild_sqlite_store::treasury_deposit(int64_t guid, int64_t amount)
{
	if (!is_open() || amount <= 0)
	{
		return false;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"UPDATE guild_treasury SET gold = gold + ? WHERE guild_guid = ?;");
	if (stmt == nullptr
		|| sqlite3_bind_int64(stmt, 1, amount) != SQLITE_OK
		|| sqlite3_bind_int64(stmt, 2, guid) != SQLITE_OK
		|| sqlite3_step(stmt) != SQLITE_DONE)
	{
		return false;
	}
	return sqlite3_changes(m_db.handle()) > 0;
}

bool guild_sqlite_store::treasury_withdraw(int64_t guid, const char* char_name,
	int64_t amount, int64_t now)
{
	if (!is_open() || char_name == nullptr || amount <= 0)
	{
		return false;
	}

	txn_scope txn(m_db.handle());
	if (!txn.active())
	{
		return false;
	}

	// The balance check and the debit are one statement, so two concurrent
	// withdrawals can never both read the same balance and both pass.
	{
		sqlite3_stmt* stmt = m_db.cached(
			"UPDATE guild_treasury SET gold = gold - ?"
			" WHERE guild_guid = ? AND gold >= ?;");
		if (stmt == nullptr
			|| sqlite3_bind_int64(stmt, 1, amount) != SQLITE_OK
			|| sqlite3_bind_int64(stmt, 2, guid) != SQLITE_OK
			|| sqlite3_bind_int64(stmt, 3, amount) != SQLITE_OK
			|| sqlite3_step(stmt) != SQLITE_DONE)
		{
			return false;
		}
		if (sqlite3_changes(m_db.handle()) == 0)
		{
			return false;   // unknown guild, or an overdraw
		}
	}
	{
		sqlite3_stmt* stmt = m_db.cached(
			"INSERT INTO guild_treasury_withdrawals(guild_guid, char_name, amount, withdrawn_at)"
			" VALUES(?,?,?,?);");
		if (stmt == nullptr
			|| sqlite3_bind_int64(stmt, 1, guid) != SQLITE_OK
			|| !bind_text(stmt, 2, char_name)
			|| sqlite3_bind_int64(stmt, 3, amount) != SQLITE_OK
			|| sqlite3_bind_int64(stmt, 4, now) != SQLITE_OK
			|| sqlite3_step(stmt) != SQLITE_DONE)
		{
			return false;
		}
	}
	return txn.commit();
}

std::vector<guild_withdrawal_record> guild_sqlite_store::withdraw_log(int64_t guid, int limit)
{
	std::vector<guild_withdrawal_record> rows;
	if (!is_open())
	{
		return rows;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"SELECT guild_guid, char_name, amount, withdrawn_at"
		" FROM guild_treasury_withdrawals WHERE guild_guid = ?"
		" ORDER BY withdrawal_id DESC LIMIT ?;");
	if (stmt == nullptr
		|| sqlite3_bind_int64(stmt, 1, guid) != SQLITE_OK
		|| sqlite3_bind_int64(stmt, 2, limit > 0 ? limit : -1) != SQLITE_OK)
	{
		return rows;
	}
	while (sqlite3_step(stmt) == SQLITE_ROW)
	{
		guild_withdrawal_record& row = rows.emplace_back();
		row.guild_guid   = sqlite3_column_int64(stmt, 0);
		row.char_name    = column_string(stmt, 1);
		row.amount       = sqlite3_column_int64(stmt, 2);
		row.withdrawn_at = sqlite3_column_int64(stmt, 3);
	}
	return rows;
}

//--------------------------------------------------------------------
// Titles
//--------------------------------------------------------------------

bool guild_sqlite_store::claim_title(int64_t guid, int kind, const char* holder_name,
	int64_t now)
{
	if (!is_open() || holder_name == nullptr || !is_valid_guild_title(kind))
	{
		return false;
	}
	guild_member_record member;
	if (!member_of(holder_name, member) || member.guild_guid != guid)
	{
		return false;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"INSERT INTO guild_titles(guild_guid, kind, holder_name, claimed_at, last_seen_at)"
		" VALUES(?,?,?,?,?);");
	// The UNIQUE holder column turns a second claim into a clean false: one
	// Title per member (§3.1.1).
	return stmt != nullptr
		&& sqlite3_bind_int64(stmt, 1, guid) == SQLITE_OK
		&& sqlite3_bind_int(stmt, 2, kind) == SQLITE_OK
		&& bind_text(stmt, 3, holder_name)
		&& sqlite3_bind_int64(stmt, 4, now) == SQLITE_OK
		&& sqlite3_bind_int64(stmt, 5, now) == SQLITE_OK
		&& sqlite3_step(stmt) == SQLITE_DONE;
}

bool guild_sqlite_store::release_title(const char* holder_name)
{
	if (!is_open() || holder_name == nullptr)
	{
		return false;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"DELETE FROM guild_titles WHERE holder_name = ?;");
	if (stmt == nullptr
		|| !bind_text(stmt, 1, holder_name)
		|| sqlite3_step(stmt) != SQLITE_DONE)
	{
		return false;
	}
	return sqlite3_changes(m_db.handle()) > 0;
}

bool guild_sqlite_store::touch_title(const char* holder_name, int64_t now)
{
	if (!is_open() || holder_name == nullptr)
	{
		return false;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"UPDATE guild_titles SET last_seen_at = ? WHERE holder_name = ?;");
	if (stmt == nullptr
		|| sqlite3_bind_int64(stmt, 1, now) != SQLITE_OK
		|| !bind_text(stmt, 2, holder_name)
		|| sqlite3_step(stmt) != SQLITE_DONE)
	{
		return false;
	}
	return sqlite3_changes(m_db.handle()) > 0;
}

std::vector<guild_title_record> guild_sqlite_store::titles(int64_t guid)
{
	std::vector<guild_title_record> rows;
	if (!is_open())
	{
		return rows;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"SELECT guild_guid, kind, holder_name, claimed_at, last_seen_at"
		" FROM guild_titles WHERE guild_guid = ? ORDER BY kind, holder_name;");
	if (stmt == nullptr || sqlite3_bind_int64(stmt, 1, guid) != SQLITE_OK)
	{
		return rows;
	}
	while (sqlite3_step(stmt) == SQLITE_ROW)
	{
		read_title_row(stmt, rows.emplace_back());
	}
	return rows;
}

bool guild_sqlite_store::title_of(const char* holder_name, guild_title_record& out)
{
	if (!is_open() || holder_name == nullptr)
	{
		return false;
	}
	sqlite3_stmt* stmt = m_db.cached(
		"SELECT guild_guid, kind, holder_name, claimed_at, last_seen_at"
		" FROM guild_titles WHERE holder_name = ?;");
	if (stmt == nullptr || !bind_text(stmt, 1, holder_name))
	{
		return false;
	}
	if (sqlite3_step(stmt) != SQLITE_ROW)
	{
		return false;
	}
	read_title_row(stmt, out);
	sqlite3_reset(stmt);
	return true;
}

//--------------------------------------------------------------------
// Boot validation
//--------------------------------------------------------------------

std::vector<std::string> guild_sqlite_store::validate()
{
	std::vector<std::string> errors;
	if (!is_open())
	{
		errors.push_back("guilds.db: store is not open");
		return errors;
	}
	sqlite3* db = m_db.handle();

	// One sweep = one statement + one row describer; a describer returning ""
	// clears its row. Every rank/kind/bit constant below is spliced or checked
	// from GuildDefs.h, never hand-spelled into the SQL -- the enum is the
	// vocabulary (ADR 0006), so the sweep must read it from the same place the
	// write paths do, or a fourth rank would make the validator refuse the
	// world the store just wrote.
	auto sweep = [&](const std::string& sql, auto&& describe)
	{
		sql_query q(db, sql, "guild validate");
		while (q.step())
		{
			std::string error = describe(q);
			if (!error.empty())
			{
				errors.push_back(std::move(error));
			}
		}
	};

	// Referential orphans first, per live table: foreign keys are
	// per-connection, so rows written by a tool that never turned them on are
	// findable only by asking -- and every later sweep may then assume the
	// joins resolve. The append-only withdraw log is deliberately exempt: an
	// orphaned audit row is history, not world state, and the log is the one
	// table whose scan cost grows for the life of the world.
	for (const char* table : { "guild_members", "guild_rank_permissions",
		"guild_requests", "guild_treasury", "guild_titles" })
	{
		sweep(std::format("PRAGMA foreign_key_check({});", table),
			[](sql_query& q)
			{
				return std::format("{}: rowid {}: orphaned row ({} reference)",
					q.as_text(0), q.as_int(1), q.as_text(2));
			});
	}

	// Guild identity: rules the DDL cannot fully spell (charset) plus
	// belt-and-braces for the ones it can.
	for (const guild_record& guild : all_guilds())
	{
		if (!is_valid_guild_name(guild.name))
		{
			errors.push_back(std::format("guilds: guid {}: illegal name '{}'",
				guild.guid, guild.name));
		}
		if (guild.side != 1 && guild.side != 2)
		{
			errors.push_back(std::format("guilds: guid {} '{}': illegal side {}",
				guild.guid, guild.name, guild.side));
		}
		if (guild.xp < 0 || guild.level < 1)
		{
			errors.push_back(std::format("guilds: guid {} '{}': illegal progression xp {} level {}",
				guild.guid, guild.name, guild.xp, guild.level));
		}
	}

	// The master invariant, in three exact shapes so the error names what
	// actually broke: the master must be a member of their own guild, must
	// hold the guildmaster rank, and must be the only member who does.
	sweep("SELECT g.guid, g.name FROM guilds g"
		" LEFT JOIN guild_members m ON m.char_name = g.master_name AND m.guild_guid = g.guid"
		" WHERE m.char_name IS NULL;",
		[](sql_query& q)
		{
			return std::format("guilds: guid {} '{}': master is not a member",
				q.as_int(0), q.as_text(1));
		});
	sweep(std::format("SELECT g.guid, g.name, m.rank FROM guilds g"
		" JOIN guild_members m ON m.char_name = g.master_name AND m.guild_guid = g.guid"
		" WHERE m.rank != {};", static_cast<int>(guild_rank::guildmaster)),
		[](sql_query& q)
		{
			return std::format("guilds: guid {} '{}': master holds rank {}, not guildmaster",
				q.as_int(0), q.as_text(1), q.as_int(2));
		});
	sweep(std::format("SELECT m.guild_guid, COUNT(*) FROM guild_members m"
		" WHERE m.rank = {} GROUP BY m.guild_guid HAVING COUNT(*) != 1;",
		static_cast<int>(guild_rank::guildmaster)),
		[](sql_query& q)
		{
			return std::format("guild_members: guild {}: {} guildmaster-rank members, expected 1",
				q.as_int(0), q.as_int(1));
		});

	// Member rows: the rank vocabulary through the shared predicate, and the
	// counters the CHECKs already guard on write.
	sweep("SELECT char_name, guild_guid, rank,"
		" donated_gold, donated_ek, donated_contribution FROM guild_members;",
		[](sql_query& q)
		{
			if (!is_valid_guild_rank(static_cast<int>(q.as_int(2))))
			{
				return std::format("guild_members: '{}' (guild {}): unknown rank {}",
					q.as_text(0), q.as_int(1), q.as_int(2));
			}
			if (q.as_int(3) < 0 || q.as_int(4) < 0 || q.as_int(5) < 0)
			{
				return std::format("guild_members: '{}': negative lifetime donation counter",
					q.as_text(0));
			}
			return std::string();
		});

	// Permission masks: every guild carries exactly one row per rank the enum
	// defines, and no row stores a rank or bit no gate reads.
	sweep(std::format("SELECT g.guid, g.name, COUNT(p.rank) FROM guilds g"
		" LEFT JOIN guild_rank_permissions p ON p.guild_guid = g.guid"
		" GROUP BY g.guid HAVING COUNT(p.rank) != {};", guild_rank_count),
		[](sql_query& q)
		{
			return std::format("guild_rank_permissions: guid {} '{}': {} mask rows, expected {}",
				q.as_int(0), q.as_text(1), q.as_int(2), guild_rank_count);
		});
	sweep("SELECT guild_guid, rank, mask FROM guild_rank_permissions;",
		[](sql_query& q)
		{
			if (!is_valid_guild_rank(static_cast<int>(q.as_int(1))))
			{
				return std::format("guild_rank_permissions: guild {}: unknown rank {}",
					q.as_int(0), q.as_int(1));
			}
			const int64_t mask = q.as_int(2);
			if (mask < 0 || (mask & ~static_cast<int64_t>(guild_permission::all_bits)) != 0)
			{
				return std::format("guild_rank_permissions: guild {} rank {}: illegal mask {:#x}",
					q.as_int(0), q.as_int(1), static_cast<uint64_t>(mask));
			}
			return std::string();
		});

	// Treasury: exactly one wallet per guild, never negative, and a withdraw
	// log whose rows all mean a real withdrawal.
	sweep("SELECT g.guid, g.name, COUNT(t.guild_guid) FROM guilds g"
		" LEFT JOIN guild_treasury t ON t.guild_guid = g.guid"
		" GROUP BY g.guid HAVING COUNT(t.guild_guid) != 1;",
		[](sql_query& q)
		{
			return std::format("guild_treasury: guid {} '{}': {} wallet rows, expected 1",
				q.as_int(0), q.as_text(1), q.as_int(2));
		});
	sweep("SELECT guild_guid, gold FROM guild_treasury WHERE gold < 0;",
		[](sql_query& q)
		{
			return std::format("guild_treasury: guild {}: negative balance {}",
				q.as_int(0), q.as_int(1));
		});
	sweep("SELECT withdrawal_id, guild_guid, amount FROM guild_treasury_withdrawals"
		" WHERE amount <= 0;",
		[](sql_query& q)
		{
			return std::format("guild_treasury_withdrawals: id {} (guild {}): non-positive amount {}",
				q.as_int(0), q.as_int(1), q.as_int(2));
		});

	// The queue: known kinds, and the two membership rules queue_request
	// enforces on the way in. The engine retires requests inside the same
	// transaction that changes membership, so a stale row here means a code
	// path skipped that -- exactly what fail-fast is for.
	sweep("SELECT request_id, guild_guid, char_name, kind FROM guild_requests;",
		[](sql_query& q)
		{
			if (!is_valid_guild_request(static_cast<int>(q.as_int(3))))
			{
				return std::format("guild_requests: id {} (guild {}, '{}'): unknown kind {}",
					q.as_int(0), q.as_int(1), q.as_text(2), q.as_int(3));
			}
			return std::string();
		});
	sweep(std::format("SELECT r.request_id, r.char_name FROM guild_requests r"
		" JOIN guild_members m ON m.char_name = r.char_name"
		" WHERE r.kind = {};", static_cast<int>(guild_request::join)),
		[](sql_query& q)
		{
			return std::format("guild_requests: id {}: join request from '{}' who is already in a guild",
				q.as_int(0), q.as_text(1));
		});
	sweep(std::format("SELECT r.request_id, r.char_name, r.guild_guid FROM guild_requests r"
		" LEFT JOIN guild_members m ON m.char_name = r.char_name AND m.guild_guid = r.guild_guid"
		" WHERE r.kind = {} AND m.char_name IS NULL;", static_cast<int>(guild_request::leave)),
		[](sql_query& q)
		{
			return std::format("guild_requests: id {}: leave request from '{}' who is not a member of guild {}",
				q.as_int(0), q.as_text(1), q.as_int(2));
		});

	// Titles: known kinds through the shared predicate, and the cross-table
	// fact the foreign keys cannot say -- a Title's guild is its holder's guild.
	sweep("SELECT holder_name, guild_guid, kind FROM guild_titles;",
		[](sql_query& q)
		{
			if (!is_valid_guild_title(static_cast<int>(q.as_int(2))))
			{
				return std::format("guild_titles: '{}' (guild {}): unknown Title kind {}",
					q.as_text(0), q.as_int(1), q.as_int(2));
			}
			return std::string();
		});
	sweep("SELECT t.holder_name, t.guild_guid, m.guild_guid FROM guild_titles t"
		" JOIN guild_members m ON m.char_name = t.holder_name"
		" WHERE m.guild_guid != t.guild_guid;",
		[](sql_query& q)
		{
			return std::format("guild_titles: '{}': Title in guild {} but membership in guild {}",
				q.as_text(0), q.as_int(1), q.as_int(2));
		});

	return errors;
}

//--------------------------------------------------------------------
// Private helpers
//--------------------------------------------------------------------

bool guild_sqlite_store::is_member_anywhere(const char* char_name)
{
	guild_member_record ignored;
	return member_of(char_name, ignored);
}

bool guild_sqlite_store::guild_exists(int64_t guid)
{
	sqlite3_stmt* stmt = m_db.cached("SELECT 1 FROM guilds WHERE guid = ?;");
	if (stmt == nullptr || sqlite3_bind_int64(stmt, 1, guid) != SQLITE_OK)
	{
		return false;
	}
	const bool found = (sqlite3_step(stmt) == SQLITE_ROW);
	sqlite3_reset(stmt);
	return found;
}

bool guild_sqlite_store::is_guild_master(const guild_member_record& member)
{
	guild_record guild;
	return load_guild(member.guild_guid, guild)
		&& hb_stricmp(guild.master_name.c_str(), member.char_name.c_str()) == 0;
}

bool guild_sqlite_store::write_rank(const char* char_name, int rank)
{
	sqlite3_stmt* stmt = m_db.cached(
		"UPDATE guild_members SET rank = ? WHERE char_name = ?;");
	if (stmt == nullptr
		|| sqlite3_bind_int(stmt, 1, rank) != SQLITE_OK
		|| !bind_text(stmt, 2, char_name)
		|| sqlite3_step(stmt) != SQLITE_DONE)
	{
		return false;
	}
	return sqlite3_changes(m_db.handle()) > 0;
}

} // namespace hb::server
