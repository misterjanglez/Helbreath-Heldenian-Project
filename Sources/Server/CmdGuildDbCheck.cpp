// CmdGuildDbCheck.cpp: the guild-store contract prover (#120)
//
// The guild store's promises are the kind that compile perfectly and are wrong
// in production, because what they alter is what a guilds.db row set MEANS:
//
//   - A guild that exists without its master membership, its three seeded
//     masks or its Treasury is a guild half of Phase 2 will crash into.
//   - "One guild per character" and "one Title per member" are schema facts;
//     if the collation or UNIQUE constraints are wrong, "Bob" and "bob" can
//     each hold one of everything.
//   - The Treasury debit and its log row must be one commit, or the audit
//     trail the withdraw permission depends on has silent holes.
//   - The boot validator must actually catch the states the API refuses to
//     create, or a defect in the later guild engine persists a world that
//     next boot cannot explain.
//
// So this proves those properties, not the code paths that implement them.
// The live guilds.db is only read (its connection posture and a validate()
// sweep); everything that writes runs against scratch files carrying the same
// v1 schema. Fixed fake timestamps keep the output free of real clocks: the
// Linux gate compares these lines against the Windows run byte for byte.
//
// Design contract: PLANS/CoreSystems_Restoration_Plan.md 3.1/3.1.1 + issue #120.
//
//////////////////////////////////////////////////////////////////////

#include "CmdGuildDbCheck.h"

#include <format>
#include <string>
#include <vector>

#include "CheckTally.h"
#include "Game.h"
#include "GuildSqliteStore.h"
#include "ServerConsole.h"
#include "sqlite3.h"

using namespace hb::shared::guild;
using hb::server::check_tally;
using hb::server::guild_record;
using hb::server::guild_member_record;
using hb::server::guild_request_record;
using hb::server::guild_sqlite_store;
using hb::server::guild_title_record;
using hb::server::probe_scalar;
using hb::server::probe_text;

namespace
{
	constexpr const char* probe_db = "guilddbcheck_probe.db";
	constexpr const char* probe_stale_db = "guilddbcheck_stale_probe.db";

	// Fixed fake clock: deterministic rows, and any real timestamp that shows
	// up in the scratch file is recognisable on sight.
	constexpr int64_t t0 = 1000;
	constexpr int64_t t1 = 1001;
	constexpr int64_t t2 = 1002;

	bool exec(sqlite3* db, const std::string& sql)
	{
		return sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
	}

	void remove_probe_files()
	{
		hb::server::remove_probe_db(probe_db);
		hb::server::remove_probe_db(probe_stale_db);
	}

	// Cleans up however this command returns; a forgotten scratch file would
	// sit next to the real guilds.db looking like a world.
	struct probe_cleanup
	{
		~probe_cleanup() { remove_probe_files(); }
	};

	// COUNT(*) of `table` rows belonging to one guild, for the cascade proofs.
	int64_t guild_rows(sqlite3* db, const char* table, int64_t guid)
	{
		return probe_scalar(db,
			std::format("SELECT COUNT(*) FROM {} WHERE guild_guid = {};", table, guid).c_str());
	}
}

void CmdGuildDbCheck::execute(CGame* game, const char* args)
{
	(void)args;

	check_tally tally("GUILDDBCHECK", "guilddbcheck");

	//----------------------------------------------------------------------
	// The live connection: posture only, never writes.
	//----------------------------------------------------------------------

	if (game == nullptr || game->m_guild_store == nullptr || !game->m_guild_store->is_open()) {
		hb::console::error("guilddbcheck: the live guilds.db is not open.");
		return;
	}
	sqlite3* live = game->m_guild_store->handle();

	// WAL + foreign keys are the same posture every store here runs in: WAL so
	// an external reader can query the live file, foreign keys because the
	// disband cascade is decoration without them.
	tally.record("live_wal", probe_text(live, "PRAGMA journal_mode;") == "wal");
	tally.record("live_foreign_keys", probe_scalar(live, "PRAGMA foreign_keys;") == 1);
	tally.record("live_schema_v1",
		probe_text(live, "SELECT value FROM meta WHERE key='schema_version';") == "1");

	// The boot gate already required this to start, so this proves the sweep
	// still holds mid-run rather than only at boot. On failure the detail
	// names the first broken invariant — a bare FAIL from a validator would
	// say the world is broken while withholding what the sweep exists to say.
	{
		auto live_errors = game->m_guild_store->validate();
		tally.record("live_validates_clean", live_errors.empty(),
			live_errors.empty() ? std::string() : live_errors.front());
	}

	//----------------------------------------------------------------------
	// Scratch world: schema lifecycle.
	//----------------------------------------------------------------------

	probe_cleanup cleanup;
	remove_probe_files();

	guild_sqlite_store scratch;
	if (!scratch.open(probe_db)) {
		hb::console::error("guilddbcheck: could not create the scratch database '{}'.", probe_db);
		return;
	}
	sqlite3* db = scratch.handle();

	tally.record("fresh_stamps_v1",
		probe_text(db, "SELECT value FROM meta WHERE key='schema_version';") == "1");

	// Idempotent: a second ensure on a current-version file is a no-op pass.
	tally.record("ensure_idempotent", hb::server::ensure_guild_schema(db, probe_db));

	{
		// Any other stamp is refused — fresh start has no migration behind it.
		// Stamped "2" rather than something absurd: a future version is the
		// stamp a gate written as "less than" instead of "not equal" would let
		// through.
		sqlite3* stale = nullptr;
		bool refused = false;
		if (sqlite3_open(probe_stale_db, &stale) == SQLITE_OK) {
			if (exec(stale, "CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);")
				&& exec(stale, "INSERT INTO meta(key,value) VALUES('schema_version','2');")) {
				refused = !hb::server::ensure_guild_schema(stale, "guilddbcheck(stale)");
			}
			sqlite3_close(stale);
		}
		tally.record("stale_schema_refused", refused);
	}

	//----------------------------------------------------------------------
	// Creation: one transaction, everything a valid guild must have.
	//----------------------------------------------------------------------

	int64_t knights = 0;
	std::string error;
	tally.record("create_guild_ok",
		scratch.create_guild("Knights", 1, "Alice", t0, knights, error) && knights > 0,
		error);

	{
		guild_member_record master;
		tally.record("create_seeds_master",
			scratch.member_of("Alice", master)
			&& master.guild_guid == knights
			&& master.rank == guild_rank::guildmaster);
	}
	{
		uint32_t gm = 0, off = 0, gs = 0;
		tally.record("create_seeds_masks",
			scratch.permission_mask(knights, guild_rank::guildmaster, gm)
			&& scratch.permission_mask(knights, guild_rank::officer, off)
			&& scratch.permission_mask(knights, guild_rank::guildsman, gs)
			&& gm == default_guildmaster_mask
			&& off == default_officer_mask
			&& gs == default_guildsman_mask);
	}
	tally.record("create_seeds_treasury", scratch.treasury_gold(knights) == 0);

	{
		// The other half of idempotent: re-running the ensure over a database
		// with data in it must leave the data alone.
		guild_record still_there;
		tally.record("reensure_preserves_data",
			hb::server::ensure_guild_schema(db, probe_db)
			&& scratch.load_guild(knights, still_there)
			&& still_there.name == "Knights");
	}

	{
		int64_t ignored = 0;
		tally.record("name_too_long_refused",
			!scratch.create_guild("ABCDEFGHIJKLMNOPQRSTU", 1, "Zed", t0, ignored, error));
		tally.record("name_bad_charset_refused",
			!scratch.create_guild("Bad!Name", 1, "Zed", t0, ignored, error));
		tally.record("name_edge_space_refused",
			!scratch.create_guild(" Knights", 1, "Zed", t0, ignored, error));
		tally.record("name_duplicate_ci_refused",
			!scratch.create_guild("kNiGhTs", 2, "Zed", t0, ignored, error));
		tally.record("side_refused",
			!scratch.create_guild("Wanderers", 3, "Zed", t0, ignored, error));
		tally.record("master_already_guilded_refused",
			!scratch.create_guild("Seconds", 1, "Alice", t0, ignored, error));
	}

	//----------------------------------------------------------------------
	// Membership: one guild per character, ranks, succession.
	//----------------------------------------------------------------------

	int64_t rangers = 0;
	if (!scratch.create_guild("Rangers", 2, "Cara", t0, rangers, error)) {
		hb::console::error("guilddbcheck: could not create the second scratch guild: {}", error);
		return;
	}

	tally.record("add_member_ok", scratch.add_member(knights, "Bob", guild_rank::guildsman, t1));
	{
		guild_member_record bob;
		tally.record("member_lookup_ci",
			scratch.member_of("bob", bob)
			&& bob.char_name == "Bob"
			&& bob.guild_guid == knights);
	}
	tally.record("second_guild_refused",
		!scratch.add_member(rangers, "Bob", guild_rank::guildsman, t1));
	tally.record("add_at_master_rank_refused",
		!scratch.add_member(knights, "Zed", guild_rank::guildmaster, t1));

	tally.record("set_rank_ok", scratch.set_rank("Bob", guild_rank::officer));
	tally.record("set_rank_to_master_refused", !scratch.set_rank("Bob", guild_rank::guildmaster));
	tally.record("set_rank_of_master_refused", !scratch.set_rank("Alice", guild_rank::officer));
	tally.record("remove_master_refused", !scratch.remove_member("Alice"));

	{
		guild_record after;
		guild_member_record alice, bob;
		tally.record("transfer_swaps_ranks",
			scratch.transfer_mastership(knights, "Bob")
			&& scratch.load_guild(knights, after)
			&& after.master_name == "Bob"
			&& scratch.member_of("Alice", alice) && alice.rank == guild_rank::officer
			&& scratch.member_of("Bob", bob) && bob.rank == guild_rank::guildmaster);
	}
	tally.record("transfer_to_outsider_refused", !scratch.transfer_mastership(knights, "Cara"));

	{
		guild_member_record bob;
		tally.record("donation_counters_accrue",
			scratch.add_donation("Bob", 100, 2, 3)
			&& !scratch.add_donation("Bob", -1, 0, 0)
			&& scratch.member_of("Bob", bob)
			&& bob.donated_gold == 100 && bob.donated_ek == 2 && bob.donated_contribution == 3);
	}

	//----------------------------------------------------------------------
	// Permission masks as data.
	//----------------------------------------------------------------------

	{
		uint32_t mask = 0;
		tally.record("mask_update_roundtrip",
			scratch.set_permission_mask(knights, guild_rank::guildsman,
				guild_permission::council_access)
			&& scratch.permission_mask(knights, guild_rank::guildsman, mask)
			&& mask == guild_permission::council_access);
		tally.record("mask_unknown_bit_refused",
			!scratch.set_permission_mask(knights, guild_rank::guildsman, 1u << 30));
	}

	//----------------------------------------------------------------------
	// The join/leave queue.
	//----------------------------------------------------------------------

	{
		int64_t request = 0;
		guild_request_record loaded;
		tally.record("request_queue_roundtrip",
			scratch.queue_request(knights, "Dave", guild_request::join, t1, request)
			&& scratch.load_request(request, loaded)
			&& loaded.char_name == "Dave" && loaded.kind == guild_request::join
			&& scratch.pending_requests(knights).size() == 1);
		int64_t duplicate = 0;
		tally.record("request_duplicate_refused",
			!scratch.queue_request(knights, "Dave", guild_request::join, t2, duplicate));
		tally.record("request_join_from_member_refused",
			!scratch.queue_request(rangers, "Bob", guild_request::join, t1, duplicate));
		tally.record("request_leave_from_nonmember_refused",
			!scratch.queue_request(knights, "Dave", guild_request::leave, t1, duplicate));
		tally.record("request_delete_ok",
			scratch.delete_request(request) && scratch.pending_requests(knights).empty());
	}

	//----------------------------------------------------------------------
	// Treasury: wallet in, wallet out, always logged, never negative.
	//----------------------------------------------------------------------

	tally.record("deposit_withdraw_balance",
		scratch.treasury_deposit(knights, 100)
		&& scratch.treasury_withdraw(knights, "Bob", 40, t1)
		&& scratch.treasury_gold(knights) == 60);
	{
		auto log = scratch.withdraw_log(knights, 0);
		tally.record("withdraw_logged",
			log.size() == 1 && log[0].char_name == "Bob" && log[0].amount == 40);
	}
	tally.record("overdraw_refused",
		!scratch.treasury_withdraw(knights, "Bob", 1000, t1)
		&& scratch.treasury_gold(knights) == 60
		&& scratch.withdraw_log(knights, 0).size() == 1);
	tally.record("deposit_nonpositive_refused",
		!scratch.treasury_deposit(knights, 0) && !scratch.treasury_deposit(knights, -5));

	//----------------------------------------------------------------------
	// Titles: one per member, guild-bound, released with the member.
	//----------------------------------------------------------------------

	tally.record("claim_title_ok",
		scratch.claim_title(knights, guild_title::huntmaster, "Bob", t1));
	tally.record("one_title_per_member",
		!scratch.claim_title(knights, guild_title::raidmaster, "Bob", t1));
	tally.record("claim_across_guilds_refused",
		!scratch.claim_title(rangers, guild_title::huntmaster, "Bob", t1));
	{
		guild_title_record title;
		tally.record("touch_title_updates_clock",
			scratch.touch_title("Bob", t2)
			&& scratch.title_of("Bob", title)
			&& title.claimed_at == t1 && title.last_seen_at == t2);
	}
	tally.record("release_then_reclaim_ok",
		scratch.release_title("Bob")
		&& scratch.claim_title(knights, guild_title::huntmaster, "Alice", t2));
	{
		// A kick releases the slot by construction: the Title row cascades
		// with the member row.
		guild_title_record orphan;
		tally.record("member_removal_releases_title",
			scratch.add_member(knights, "Eve", guild_rank::guildsman, t2)
			&& scratch.claim_title(knights, guild_title::raidmaster, "Eve", t2)
			&& scratch.remove_member("Eve")
			&& !scratch.title_of("Eve", orphan));
	}

	//----------------------------------------------------------------------
	// Disband: everything cascades, identity is never re-issued.
	//----------------------------------------------------------------------

	{
		int64_t queued = 0;
		scratch.treasury_deposit(rangers, 10);
		scratch.queue_request(rangers, "Dave", guild_request::join, t1, queued);
		tally.record("disband_cascades",
			scratch.disband_guild(rangers)
			&& guild_rows(db, "guild_members", rangers) == 0
			&& guild_rows(db, "guild_rank_permissions", rangers) == 0
			&& guild_rows(db, "guild_requests", rangers) == 0
			&& guild_rows(db, "guild_treasury", rangers) == 0
			&& guild_rows(db, "guild_titles", rangers) == 0);
	}
	int64_t wolves = 0;
	tally.record("guid_never_reused",
		scratch.create_guild("Wolves", 1, "Cara", t0, wolves, error) && wolves > rangers,
		error);

	//----------------------------------------------------------------------
	// The boot validator catches what the API refuses to create.
	//----------------------------------------------------------------------

	{
		auto scratch_errors = scratch.validate();
		tally.record("validate_clean_passes", scratch_errors.empty(),
			scratch_errors.empty() ? std::string() : scratch_errors.front());
	}

	{
		// Demote the master by hand — set_rank refused this above.
		const bool broke = exec(db, "UPDATE guild_members SET rank = 1 WHERE char_name = 'Bob';");
		tally.record("validate_catches_demoted_master",
			broke && !scratch.validate().empty());
		exec(db, "UPDATE guild_members SET rank = 0 WHERE char_name = 'Bob';");
	}
	{
		const bool broke = exec(db, std::format(
			"DELETE FROM guild_rank_permissions WHERE guild_guid = {} AND rank = 2;", knights));
		tally.record("validate_catches_missing_mask",
			broke && !scratch.validate().empty());
		exec(db, std::format(
			"INSERT INTO guild_rank_permissions(guild_guid, rank, mask) VALUES({}, 2, 0);", knights));
	}
	{
		// A Title held across guild lines: both foreign keys hold (the guild
		// exists, the holder is a member somewhere), so only the sweep can say
		// it is wrong. Bob is guild-less of Titles but a Knight; the row names
		// the Wolves.
		const bool broke = exec(db, std::format(
			"INSERT INTO guild_titles(guild_guid, kind, holder_name, claimed_at, last_seen_at)"
			" VALUES({}, 1, 'Bob', {}, {});", wolves, t2, t2));
		tally.record("validate_catches_cross_guild_title",
			broke && !scratch.validate().empty());
		exec(db, "DELETE FROM guild_titles WHERE holder_name = 'Bob';");
	}
	{
		// A stale join: the requester joined (any) guild and the request row
		// survived, meaning some future code path skipped its transaction.
		const bool broke = exec(db, std::format(
			"INSERT INTO guild_requests(guild_guid, char_name, kind, created_at)"
			" VALUES({}, 'Alice', 1, {});", wolves, t2));
		tally.record("validate_catches_stale_join",
			broke && !scratch.validate().empty());
		exec(db, "DELETE FROM guild_requests WHERE char_name = 'Alice';");
	}
	{
		auto final_errors = scratch.validate();
		tally.record("validate_clean_after_repairs", final_errors.empty(),
			final_errors.empty() ? std::string() : final_errors.front());
	}

	scratch.close();
	tally.report();
}
