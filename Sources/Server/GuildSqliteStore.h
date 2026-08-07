// GuildSqliteStore.h: the guild world-state store on guilds.db (#120, D1)
//
// The Phase-2 guild rebuild replaces the original's flat file per guild — which
// had an empty [GUILDSMAN] section and no reader — with this store. Decision D1
// put it in its own database rather than game.db: guilds are world-state, not
// account-state, and unlike Trading Post escrow (#82) nothing a guild row
// changes must commit atomically WITH a character save — a Donation burns the
// donor's live counters and then records itself here, and a crash between
// those two is a few lost points, not a duplicated item.
//
// Fresh start is absolute: the pre-gut guild columns survive only in
// `.guild_migration_bak` files, and those are archaeology — this store never
// reads them, and no migration path from any earlier layout exists. A
// guilds.db stamped at any other schema version is refused at boot exactly
// like a stale game.db (VerifySqliteSchemaVersion; delete it to recreate).
//
// The store owns integrity, the guild engine (#121/#122) owns policy. Rules
// like "who may approve a join" or "what a donation is worth" never appear
// here; rules like "a guild's master is always a member holding the
// guildmaster rank", "one guild per character", "one Title per member" and
// "the Treasury cannot go negative" are enforced here, because they are what
// make a guilds.db row set mean anything at all. The same invariants are
// swept by validate() at every boot — TierConfig-style, errors block startup —
// so a defect in a later engine cannot quietly persist a world this store
// would refuse to build.
//
// Time is a parameter, never read from the clock here: the engine stamps
// `now` (unix seconds) into joins, claims and the withdraw log, which keeps
// every store operation replayable and the guilddbcheck prover deterministic.
//
// Design contract: PLANS/CoreSystems_Restoration_Plan.md §3.1/§3.1.1;
// ADR 0006 (permission-bit ranks), ADR 0007 (burn economy + Title slots).
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "GameDatabase.h"
#include "Game/GuildDefs.h"

struct sqlite3;

namespace hb::server
{
	// Named once, beside the class that opens it (the ledger's pattern).
	// Lowercase because the Linux deployment copies the name verbatim.
	constexpr const char* default_guilds_db_path = "guilds.db";

	// One guilds row. guid is the server-minted permanent identity (§3.1) —
	// AUTOINCREMENT in the schema, so a disbanded guild's guid is never
	// re-issued to a later one. The three burned_* counters are the §3.1.2
	// requirement-gate lanes (cumulative lifetime burns, no conversion
	// between them); level is earned-and-kept, only ever raised.
	struct guild_record
	{
		int64_t     guid = 0;
		std::string name;
		int         side = 0;           // 1 aresden / 2 elvine
		std::string master_name;
		int64_t     created_at = 0;     // unix seconds
		int64_t     burned_gold = 0;
		int64_t     burned_ek = 0;
		int64_t     burned_contribution = 0;
		int         level = 1;
	};

	// One guild_members row. char_name is the table's primary key — one guild
	// per character is schema fact, not a lookup discipline. The lifetime
	// donation counters accrue from day one (§3.1.1 deviation 4) even while
	// the perks they will someday feed ship neutral.
	struct guild_member_record
	{
		int64_t     guild_guid = 0;
		std::string char_name;
		int         rank = hb::shared::guild::guild_rank::none;
		int64_t     joined_at = 0;
		int64_t     donated_gold = 0;
		int64_t     donated_ek = 0;
		int64_t     donated_contribution = 0;
	};

	// One queued join/leave request (§3.1.1 deviation 2: tickets queue until
	// any approver decides; Kennedy files these for offline masters too).
	struct guild_request_record
	{
		int64_t     request_id = 0;
		int64_t     guild_guid = 0;
		std::string char_name;
		int         kind = 0;           // guild_request::join / leave
		int64_t     created_at = 0;
	};

	// One held Title (ADR 0007). last_seen_at is what the AFK/offline
	// auto-release timer reads; the engine touches it while the holder plays.
	struct guild_title_record
	{
		int64_t     guild_guid = 0;
		int         kind = 0;           // guild_title::commander / huntmaster / raidmaster
		std::string holder_name;
		int64_t     claimed_at = 0;
		int64_t     last_seen_at = 0;
	};

	// One withdraw-log row (§3.1.1 deviation 6: withdrawals are always
	// logged). Rows outlive the withdrawer's membership on purpose — a log
	// that forgot everyone who left would be no log at all.
	struct guild_withdrawal_record
	{
		int64_t     guild_guid = 0;
		std::string char_name;
		int64_t     amount = 0;
		int64_t     withdrawn_at = 0;
	};

	// Run the version gate on an already-open connection and lay down the v1
	// DDL when the database is fresh. Idempotent: a current-version database
	// passes through untouched, a fresh one is built, any other stamp is
	// refused (no migrations, ever). Separately callable so guilddbcheck can
	// prove all three outcomes against a scratch file; db_label only names the
	// file in log lines.
	bool ensure_guild_schema(sqlite3* db, const char* db_label);

	// Server-owned store for everything guild: identity, membership, per-rank
	// permission masks, the join/leave queue, the Treasury, and Title slots.
	class guild_sqlite_store
	{
	public:
		guild_sqlite_store() = default;
		~guild_sqlite_store() = default;

		guild_sqlite_store(const guild_sqlite_store&) = delete;
		guild_sqlite_store& operator=(const guild_sqlite_store&) = delete;

		// Open (creating if absent) and apply/verify the schema. False on a
		// stale stamp or SQL failure — the caller treats that as a boot failure.
		bool open(const std::string& path);
		void close();
		bool is_open() const { return m_db.is_open(); }
		sqlite3* handle() const { return m_db.handle(); }

		// --- Guild lifecycle -------------------------------------------------
		// Create a guild and everything a valid one must have, in one
		// transaction: the guilds row, the master's member row (guildmaster
		// rank), the three §3.1.1 default permission masks, and an empty
		// Treasury. The master's own pending join requests elsewhere retire in
		// the same transaction (the add_member rule — founding is joining).
		// False with out_error set on an invalid name, an unknown
		// side, a taken name (case-insensitive), or a master who is already in
		// a guild. Creation *gates* (level, CHR, citizenship, town, crusade)
		// are the engine's checks, not repeated here.
		bool create_guild(const char* name, int side, const char* master_name,
			int64_t now, int64_t& out_guid, std::string& out_error);

		// Remove a guild wholesale; members, masks, requests, Treasury,
		// withdraw log and Titles all cascade away. Handing the Treasury
		// remainder to the master first is the engine's disband flow.
		bool disband_guild(int64_t guid);

		bool load_guild(int64_t guid, guild_record& out);
		bool find_guild_by_name(const char* name, guild_record& out);
		std::vector<guild_record> all_guilds();

		// The highest Guild Level any guild holds (0 when none exist) — what
		// the boot and reload world-vs-curve checks divide the curve against.
		int max_guild_level();

		// Record one Donation's full effect in a single transaction (§3.1.2):
		// the donor's lifetime counters, the guild's three cumulative burn
		// counters, and the level the engine computed against the curve —
		// MAX'd in, never lowered, because levels are earned-and-kept
		// (requirements are checked only on the way up). Deltas must be
		// non-negative with at least one positive; new_level at least 1; the
		// donor must be a member of `guid`. The burn itself — decrementing
		// the donor's live gold/EK/contribution — happens on the character,
		// in the engine, before this records it.
		bool record_donation(int64_t guid, const char* char_name, int64_t gold,
			int64_t enemy_kills, int64_t contribution, int new_level);

		// --- Membership ------------------------------------------------------
		// Add a member at officer or guildsman rank. Refuses the guildmaster
		// rank (only create/transfer mint that), an unknown guild, and a
		// character already in any guild (schema-enforced; this reports it as
		// a clean false). Every pending join request of the new member — at
		// this guild and any other — retires in the same transaction, because
		// a member with a join still queued is a state validate() refuses.
		bool add_member(int64_t guid, const char* char_name, int rank, int64_t now);

		// Remove a member; their held Title and their pending leave request go
		// with them in the same transaction. Refuses the guild's master —
		// transfer or disband first, which keeps "the master is always a
		// member" true at every commit boundary.
		bool remove_member(const char* char_name);

		// Change a member's rank between officer and guildsman. Refuses the
		// guildmaster rank in either direction and refuses the master —
		// transfer_mastership is the only door to and from guildmaster.
		bool set_rank(const char* char_name, int rank);

		// The succession primitive (§3.1.1 deviation 1), one transaction: the
		// old master drops to officer, the new master — who must already be a
		// member of the same guild — rises to guildmaster, and guilds.master_name
		// follows. The CHR-20 floor and who-may-invoke are engine checks.
		bool transfer_mastership(int64_t guid, const char* new_master_name);

		// The character's membership, if any (case-insensitive, like every
		// name lookup here). This is the login-hydration read (#121).
		bool member_of(const char* char_name, guild_member_record& out);

		std::vector<guild_member_record> roster(int64_t guid);
		int member_count(int64_t guid);

		// How many members of `guid` hold the officer rank — the promote-side
		// capacity gate (§3.1.2 item 2) reads this. The guildmaster is not an
		// officer and so is naturally outside the count.
		int officer_count(int64_t guid);

		// --- Permission masks (ADR 0006) ------------------------------------
		bool permission_mask(int64_t guid, int rank, uint32_t& out_mask);

		// Replace one rank's mask. Unknown bits are refused so a later editor
		// cannot store permissions no gate will ever read.
		bool set_permission_mask(int64_t guid, int rank, uint32_t mask);

		// --- Join/leave queue ------------------------------------------------
		// Queue a request. Refuses an unknown guild, an unknown kind, a
		// duplicate (same guild, character and kind), a join from a character
		// already in any guild, and a leave from anyone but a member of THAT
		// guild — the same rules validate() holds the table to at boot.
		bool queue_request(int64_t guid, const char* char_name, int kind,
			int64_t now, int64_t& out_request_id);

		bool load_request(int64_t request_id, guild_request_record& out);
		bool delete_request(int64_t request_id);
		std::vector<guild_request_record> pending_requests(int64_t guid);

		// True when an identical request (guild, character, kind) is already
		// queued — one seek on the UNIQUE index, so the engine's duplicate
		// gate can name request_pending without materializing the queue (the
		// constraint itself stays the backstop).
		bool has_pending_request(int64_t guid, const char* char_name, int kind);

		// --- Treasury (§3.1.1 deviation 6) ----------------------------------
		// The wallet is gold in, gold out, and nothing else: it never mints
		// Guild XP in either direction (ADR 0007's altar/wallet firewall —
		// enforced by there simply being no path from here to xp).
		int64_t treasury_gold(int64_t guid);   // -1 when the guild is unknown

		bool treasury_deposit(int64_t guid, int64_t amount);

		// Withdraw and log, one transaction. False (and nothing changes) on
		// an overdraw or a non-positive amount. Whether char_name held the
		// treasury_withdraw bit was the caller's gate; the log row is why the
		// answer is auditable afterwards.
		bool treasury_withdraw(int64_t guid, const char* char_name, int64_t amount,
			int64_t now);

		// Newest first. limit <= 0 means all rows.
		std::vector<guild_withdrawal_record> withdraw_log(int64_t guid, int limit);

		// --- Titles (ADR 0007) ----------------------------------------------
		// Claim a Title slot for a member of `guid`. Refuses a non-member, an
		// unknown kind, and a second Title for the same character (one per
		// member, schema-enforced). Slot COUNTS against Guild Level — and who
		// may claim Commander — are the engine's gates.
		bool claim_title(int64_t guid, int kind, const char* holder_name, int64_t now);

		bool release_title(const char* holder_name);

		// Refresh the AFK/offline release clock on the holder's Title, if any.
		bool touch_title(const char* holder_name, int64_t now);

		std::vector<guild_title_record> titles(int64_t guid);
		bool title_of(const char* holder_name, guild_title_record& out);

		// How many Titles of one kind `guid`'s members hold — the claim
		// gate's slot arithmetic, in the officer_count shape.
		int title_count(int64_t guid, int kind);

		// Every held Title across every guild, for the engine's inactivity
		// sweep — one statement instead of an all_guilds() walk.
		std::vector<guild_title_record> all_titles();

		// --- Boot validation -------------------------------------------------
		// The TierConfig-style fail-fast sweep, run at every boot: foreign-key
		// orphans, illegal names/sides/ranks/kinds/bits, a master who is not
		// their guild's sole guildmaster member, missing or extra mask and
		// Treasury rows, negative counters, stale queue rows, and Titles held
		// across guild lines. Each string names its table and row; an empty
		// result is the only result a server may start on.
		std::vector<std::string> validate();

	private:
		// True when the character (case-insensitive) has a member row.
		bool is_member_anywhere(const char* char_name);
		bool guild_exists(int64_t guid);

		// Whether this member is their own guild's master — the one row
		// set_rank and remove_member must refuse to touch.
		bool is_guild_master(const guild_member_record& member);

		// The request-retirement policy, in one place: minting a membership
		// (create_guild / add_member) retires the character's JOIN
		// applications at every guild; removing a membership (remove_member)
		// retires whatever they had queued at THAT guild — only leave
		// requests can exist for a member. Both run inside their caller's
		// transaction.
		bool retire_join_requests(const char* char_name);
		bool retire_guild_requests(const char* char_name, int64_t guid);

		// The one UPDATE that moves a member between ranks. False when the
		// row does not exist, so no two callers can disagree about what a
		// zero-row update means.
		bool write_rank(const char* char_name, int rank);

		game_database m_db;
	};
}
