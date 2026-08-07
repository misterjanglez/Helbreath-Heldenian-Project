// CmdGuildProgCheck.cpp: the guild progression contract prover (#122)
//
// guilddbcheck (#120) proves what a guilds.db may BE and guildcheck (#121)
// what a player may DO; this proves what a guild may EARN — §3.1.2's whole
// machinery. The dataset half exercises the gamedata loader and its fail-fast
// validator against a scratch config database, one corruption per check. The
// engine half stages a synthetic three-level curve on CGame under a restore
// guard — these checks are about the machinery, and a prover that read the
// live seed rows would break the moment an operator tuned a number — and
// drives the real guild_manager entry points with synthetic clients against a
// scratch guilds.db, exactly like guildcheck.
//
// Timestamps never reach the output lines, so Windows and Linux runs compare
// byte for byte even though the engine stamps the real clock into the scratch
// rows.
//
// Design contract: PLANS/CoreSystems_Restoration_Plan.md §3.1.2 + issue #122.
//
//////////////////////////////////////////////////////////////////////

#include "CmdGuildProgCheck.h"

#include <cstring>
#include <string>

#include "CheckTally.h"
#include "Client.h"
#include "Game.h"
#include "GuildManager.h"
#include "GuildProgressionConfig.h"
#include "GuildSqliteStore.h"
#include "Item.h"
#include "ItemManager.h"
#include "ServerConsole.h"
#include "TimeUtils.h"
#include "sqlite3.h"

using namespace hb::shared::guild;
using hb::server::check_tally;
using hb::server::find_free_handle;
using hb::server::give_item;
using hb::server::guild_level_row;
using hb::server::guild_member_record;
using hb::server::guild_progression_config;
using hb::server::guild_record;
using hb::server::guild_result::ok;
using hb::server::guild_title_record;
using hb::server::probe_client;

namespace guild_result = hb::server::guild_result;

namespace
{
	constexpr const char* probe_db = "guildprogcheck_probe.db";
	constexpr const char* probe_ledger_db = "guildprogcheck_ledger_probe.db";
	constexpr const char* probe_config_db = "guildprogcheck_config_probe.db";

	// Fixed fake clock for the store-staged rows whose stamps nothing asserts.
	constexpr int64_t t0 = 1000;

	bool exec(sqlite3* db, const char* sql)
	{
		return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
	}

	// Rebuild the §A dataset from scratch: three coherent levels plus the
	// full knob set. Dropped and reseeded between corruptions so each check
	// mutates a known-good world rather than the previous check's leavings.
	bool reseed_config_probe(sqlite3* db)
	{
		return exec(db,
			"DROP TABLE IF EXISTS guild_level_curve;"
			"DROP TABLE IF EXISTS guild_settings;"
			"CREATE TABLE guild_level_curve ("
			" level INTEGER PRIMARY KEY, gold INTEGER NOT NULL,"
			" enemy_kills INTEGER NOT NULL, contribution INTEGER NOT NULL,"
			" member_cap INTEGER NOT NULL, officer_cap INTEGER NOT NULL,"
			" hunt_slots INTEGER NOT NULL, raid_slots INTEGER NOT NULL,"
			" repair_pct INTEGER NOT NULL);"
			"INSERT INTO guild_level_curve VALUES"
			" (1,0,0,0,15,1,0,0,0),"
			" (2,1000,5,5,20,1,1,1,0),"
			" (3,2000,10,10,25,2,1,1,5);"
			"CREATE TABLE guild_settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);"
			"INSERT INTO guild_settings VALUES"
			" ('donations_enabled','0'),('title_bonuses_enabled','0'),"
			" ('title_inactivity_ms','600000'),('donation_min_gold','1000'),"
			" ('donation_min_ek','1'),('donation_min_contribution','1'),"
			" ('huntmaster_damage_pct','10'),('huntmaster_tier_shift_pct','125'),"
			" ('huntmaster_tier_shift_min_tier','2'),('raidmaster_attack_power','3');");
	}

	// Errors the probe dataset draws after `mutation` runs against it; -1 when
	// the mutation or the load itself failed (every caller expects >= 0).
	int validation_errors(sqlite3* db, const char* mutation)
	{
		if (mutation != nullptr && !exec(db, mutation))
		{
			return -1;
		}
		guild_progression_config config;
		if (!hb::server::load_guild_progression(db, config))
		{
			return -1;
		}
		return static_cast<int>(hb::server::validate_guild_progression(config).size());
	}

	// Stage CGame::m_guild_progression and put the boot-loaded dataset back
	// however the run ends — the engine reads the live member, so this is
	// what makes every engine check independent of the tunable seed rows.
	class progression_guard
	{
	public:
		explicit progression_guard(CGame* game)
			: m_game(game), m_saved(game->m_guild_progression) {}
		~progression_guard() { m_game->m_guild_progression = std::move(m_saved); }

		progression_guard(const progression_guard&) = delete;
		progression_guard& operator=(const progression_guard&) = delete;

	private:
		CGame* m_game;
		guild_progression_config m_saved;
	};

	// The synthetic curve the engine checks run against: thresholds small
	// enough to cross deliberately, caps small enough to hit. Knobs keep the
	// struct's defaults (minimums 1000/1/1, 600000 ms, 10/125/2/3), which are
	// the numbers the checks below assert.
	guild_progression_config synthetic_config()
	{
		guild_progression_config config;
		config.levels = {
			{ 1, 0,    0, 0, 3, 1, 0, 0, 0 },
			{ 2, 1000, 1, 1, 4, 2, 1, 1, 5 },
			{ 3, 3000, 3, 2, 6, 2, 2, 1, 10 },
		};
		config.donations_enabled = true;
		config.title_bonuses_enabled = true;
		return config;
	}

	// The character-sheet shape every probe needs: a side and citizenship for
	// the join ceremony, strength for the gold grants.
	void shape_probe(CGame* game, int handle)
	{
		CClient* client = game->m_client_list[handle];
		client->m_level = 100;
		client->m_str = 100;
		client->m_side = 1;
		std::snprintf(client->m_location, sizeof(client->m_location), "%s", "aresden");
	}

	uint64_t gold_of(CGame* game, int handle)
	{
		return game->m_item_manager->get_item_count_by_id(handle,
			hb::shared::item::ItemId::Gold);
	}
}

void CmdGuildProgCheck::execute(CGame* game, const char* args)
{
	(void)args;

	check_tally tally("GUILDPROGCHECK", "guildprogcheck");

	if (game == nullptr || game->m_guild_manager == nullptr
		|| game->m_guild_store == nullptr || !game->m_guild_store->is_open())
	{
		hb::console::error("guildprogcheck: the guild engine or store is not up.");
		return;
	}

	//----------------------------------------------------------------------
	// A. The dataset contract: the loader + fail-fast validator, one
	//    corruption per check against a scratch config database.
	//----------------------------------------------------------------------

	{
		hb::server::remove_probe_db(probe_config_db);
		sqlite3* config_db = nullptr;
		if (sqlite3_open(probe_config_db, &config_db) != SQLITE_OK)
		{
			hb::console::error("guildprogcheck: could not create the scratch config database '{}'.",
				probe_config_db);
			return;
		}

		tally.record("dataset_clean_validates",
			reseed_config_probe(config_db)
			&& validation_errors(config_db, nullptr) == 0);

		// One corruption per check, each against a freshly reseeded world so
		// no check ever tests the previous one's leavings.
		auto refuses = [&](const char* name, const char* mutation)
		{
			tally.record(name, reseed_config_probe(config_db)
				&& validation_errors(config_db, mutation) > 0);
		};
		refuses("dataset_level_hole_refused",
			"DELETE FROM guild_level_curve WHERE level = 2;");
		refuses("dataset_nonzero_level1_refused",
			"UPDATE guild_level_curve SET gold = 5 WHERE level = 1;");
		refuses("dataset_decreasing_column_refused",
			"UPDATE guild_level_curve SET member_cap = 15 WHERE level = 3;");
		refuses("dataset_unknown_knob_refused",
			"INSERT INTO guild_settings VALUES ('donation_min_glod','1');");
		refuses("dataset_missing_knob_refused",
			"DELETE FROM guild_settings WHERE key = 'donations_enabled';");
		refuses("dataset_nonboolean_switch_refused",
			"UPDATE guild_settings SET value = '7' WHERE key = 'donations_enabled';");
		refuses("dataset_nonnumeric_knob_refused",
			"UPDATE guild_settings SET value = 'soon' WHERE key = 'title_inactivity_ms';");
		refuses("dataset_empty_curve_refused",
			"DELETE FROM guild_level_curve;");

		{
			// The world half: levels already earned are never revoked, so a
			// curve shorter than an existing guild's level is refused — and
			// exactly at the max it is not.
			guild_progression_config good;
			const bool loaded = reseed_config_probe(config_db)
				&& hb::server::load_guild_progression(config_db, good);
			tally.record("dataset_world_above_max_refused",
				loaded
				&& !hb::server::validate_guild_progression_world(good, good.max_level() + 1).empty()
				&& hb::server::validate_guild_progression_world(good, good.max_level()).empty()
				&& hb::server::validate_guild_progression_world(good, 0).empty());
		}

		sqlite3_close(config_db);
		hb::server::remove_probe_db(probe_config_db);
	}

	// The live dataset reloads clean through the candidate path — proven
	// BEFORE the synthetic staging below, because a real reload swaps the
	// running config from disk.
	tally.record("live_reload_accepts_disk", game->reload_guild_progression());

	//----------------------------------------------------------------------
	// B. The engine: a synthetic curve, a scratch world, real entry points.
	//----------------------------------------------------------------------

	hb::server::guild_store_swap swap(game, probe_db);
	if (!swap.ok())
	{
		hb::console::error("guildprogcheck: could not create the scratch database '{}'.", probe_db);
		return;
	}
	hb::server::ledger_sink_swap ledger_swap(game, probe_ledger_db);
	if (!ledger_swap.ok())
	{
		hb::console::error("guildprogcheck: could not create the scratch ledger '{}'.", probe_ledger_db);
		return;
	}
	progression_guard staged(game);
	game->m_guild_progression = synthetic_config();

	hb::server::guild_manager& engine = *game->m_guild_manager;
	hb::server::guild_sqlite_store& store = *game->m_guild_store;

	const int master_h = find_free_handle(game, 1);
	const int officer_h = find_free_handle(game, master_h + 1);
	const int member_h = find_free_handle(game, officer_h + 1);
	const int joiner_h = find_free_handle(game, member_h + 1);
	const int outsider_h = find_free_handle(game, joiner_h + 1);
	if (outsider_h < 0)
	{
		hb::console::error("guildprogcheck: no free client handles for the probes.");
		return;
	}
	probe_client master(game, master_h, "gpacct1", "GpMaster", "aresden", 100, 100);
	probe_client officer(game, officer_h, "gpacct2", "GpOfficer", "aresden", 101, 100);
	probe_client member(game, member_h, "gpacct3", "GpMember", "aresden", 102, 100);
	probe_client joiner(game, joiner_h, "gpacct4", "GpJoiner", "aresden", 103, 100);
	probe_client outsider(game, outsider_h, "gpacct5", "GpOutsider", "aresden", 104, 100);
	for (int handle : { master_h, officer_h, member_h, joiner_h, outsider_h })
	{
		shape_probe(game, handle);
	}

	// Stage the guild through the store (creation gates are #121's, proven in
	// guildcheck) and hydrate the online members through the engine.
	int64_t knights = 0;
	{
		std::string error;
		if (!store.create_guild("GpKnights", 1, "GpMaster", t0, knights, error)
			|| !store.add_member(knights, "GpOfficer", guild_rank::guildsman, t0)
			|| !store.add_member(knights, "GpMember", guild_rank::guildsman, t0))
		{
			hb::console::error("guildprogcheck: could not stage the scratch guild: {}", error);
			return;
		}
	}
	engine.hydrate_login(master_h);
	engine.hydrate_login(officer_h);
	engine.hydrate_login(member_h);
	CClient* master_c = game->m_client_list[master_h];
	CClient* officer_c = game->m_client_list[officer_h];
	CClient* member_c = game->m_client_list[member_h];

	tally.record("hydration_caches_level",
		master_c->m_guild_level == 1 && master_c->m_guild_title == 0);

	//----------------------------------------------------------------------
	// Donations: the gate ladder, the burn, the AND-gate climb.
	//----------------------------------------------------------------------

	game->m_guild_progression.donations_enabled = false;
	tally.record("donate_switch_off_refused",
		engine.donate_gold(master_h, 1000) == guild_result::donations_disabled);
	game->m_guild_progression.donations_enabled = true;

	tally.record("donate_below_minimum_refused",
		engine.donate_gold(master_h, 500) == guild_result::amount_too_small
		&& engine.donate_gold(master_h, -5) == guild_result::amount_too_small);
	tally.record("donate_short_gold_refused",
		engine.donate_gold(master_h, 1000) == guild_result::insufficient_funds);
	tally.record("donate_outsider_refused",
		engine.donate_gold(outsider_h, 1000) == guild_result::not_in_guild);

	{
		guild_record after;
		guild_member_record lifetime;
		game->m_item_manager->grant_gold(master_h, 5000);
		tally.record("donate_gold_burns_and_records",
			engine.donate_gold(master_h, 1000) == ok
			&& gold_of(game, master_h) == 4000
			&& store.load_guild(knights, after)
			&& after.burned_gold == 1000 && after.burned_ek == 0
			&& store.member_of("GpMaster", lifetime)
			&& lifetime.donated_gold == 1000);
		// L2 wants 1000 gold AND 1 EK AND 1 contribution: one lane crossing
		// alone must not climb (§3.1.2's whole point).
		tally.record("one_lane_does_not_level", after.level == 1);
	}

	tally.record("donate_short_ek_refused",
		engine.donate_enemy_kills(master_h, 1) == guild_result::insufficient_funds);
	master_c->m_enemy_kill_count = 5;
	{
		guild_record after;
		tally.record("donate_ek_burns_counter",
			engine.donate_enemy_kills(master_h, 1) == ok
			&& master_c->m_enemy_kill_count == 4
			&& store.load_guild(knights, after)
			&& after.burned_ek == 1 && after.level == 1);
	}

	// #129: m_contribution can sit negative — the balance gate refuses that
	// donor like any other shortfall instead of minting from a debt.
	master_c->m_contribution = -5;
	tally.record("donate_negative_contribution_refused",
		engine.donate_contribution(master_h, 1) == guild_result::insufficient_funds);

	master_c->m_contribution = 4;
	{
		guild_record after;
		tally.record("all_lanes_level_up",
			engine.donate_contribution(master_h, 1) == ok
			&& master_c->m_contribution == 3
			&& store.load_guild(knights, after)
			&& after.level == 2);
		tally.record("level_up_walks_online_cache",
			master_c->m_guild_level == 2 && officer_c->m_guild_level == 2);
	}

	// The climb to L3 (3000/3/2): each lane alone stalls until the last one
	// crosses — cumulative totals carry, nothing converts.
	{
		guild_record after;
		tally.record("gold_lane_alone_stalls",
			engine.donate_gold(master_h, 2000) == ok
			&& store.load_guild(knights, after)
			&& after.burned_gold == 3000 && after.level == 2);
		tally.record("ek_lane_alone_stalls",
			engine.donate_enemy_kills(master_h, 2) == ok
			&& store.load_guild(knights, after)
			&& after.burned_ek == 3 && after.level == 2);
		tally.record("last_lane_levels_again",
			engine.donate_contribution(master_h, 1) == ok
			&& store.load_guild(knights, after)
			&& after.burned_contribution == 2 && after.level == 3
			&& master_c->m_guild_level == 3);
	}
	{
		guild_member_record lifetime;
		tally.record("lifetime_totals_accrue",
			store.member_of("GpMaster", lifetime)
			&& lifetime.donated_gold == 3000
			&& lifetime.donated_ek == 3
			&& lifetime.donated_contribution == 2);
	}

	//----------------------------------------------------------------------
	// The curve caps: member cap on join approval, Officer capacity on
	// promote — the #121 carry-over rung.
	//----------------------------------------------------------------------

	{
		// L3's member cap is 6; three online members + three staged rows fill
		// it. The refusal leaves the request queued, so freeing a seat lets
		// the SAME request decide clean.
		int64_t request = 0;
		const bool staged_full =
			store.add_member(knights, "GpF1", guild_rank::guildsman, t0)
			&& store.add_member(knights, "GpF2", guild_rank::guildsman, t0)
			&& store.add_member(knights, "GpF3", guild_rank::guildsman, t0)
			&& give_item(game, joiner_h, hb::shared::item::ItemId::GuildAdmissionTicket)
			&& engine.file_join_request(joiner_h, "GpKnights", request) == ok;
		tally.record("join_at_member_cap_refused",
			staged_full
			&& engine.decide_request(master_h, request, true) == guild_result::guild_full);
		tally.record("join_after_seat_frees_ok",
			store.remove_member("GpF3")
			&& engine.decide_request(master_h, request, true) == ok
			&& game->m_client_list[joiner_h]->m_guild_guid == knights);
	}

	{
		// L3's Officer capacity is 2, the master never counted.
		tally.record("promote_first_officer_ok",
			engine.promote_member(master_h, "GpOfficer") == ok
			&& store.officer_count(knights) == 1);
		tally.record("promote_second_officer_ok",
			engine.promote_member(master_h, "GpMember") == ok
			&& store.officer_count(knights) == 2);
		tally.record("promote_at_capacity_refused",
			engine.promote_member(master_h, "GpF1") == guild_result::officers_at_capacity
			&& store.officer_count(knights) == 2);
		tally.record("demote_frees_capacity",
			engine.demote_member(master_h, "GpMember") == ok
			&& store.officer_count(knights) == 1);
	}

	//----------------------------------------------------------------------
	// Titles: the duty-slot machine.
	//----------------------------------------------------------------------

	tally.record("claim_bad_kind_refused",
		engine.claim_title(master_h, 99) == guild_result::bad_title);
	tally.record("claim_commander_without_bit_refused",
		engine.claim_title(member_h, guild_title::commander) == guild_result::no_permission);
	tally.record("claim_commander_by_officer_ok",
		engine.claim_title(officer_h, guild_title::commander) == ok
		&& officer_c->m_guild_title == guild_title::commander);
	tally.record("commander_slot_pinned_at_one",
		engine.claim_title(master_h, guild_title::commander) == guild_result::no_title_slot);

	tally.record("strip_without_bit_refused",
		engine.strip_title(member_h, "GpOfficer") == guild_result::no_permission);
	tally.record("strip_by_title_manage_ok",
		engine.strip_title(master_h, "GpOfficer") == ok
		&& officer_c->m_guild_title == 0);
	tally.record("strip_untitled_refused",
		engine.strip_title(master_h, "GpOfficer") == guild_result::no_title);

	tally.record("drop_title_roundtrip",
		engine.claim_title(master_h, guild_title::huntmaster) == ok
		&& engine.drop_title(master_h) == ok
		&& engine.drop_title(master_h) == guild_result::no_title);

	// L3 slots: two Huntmasters, one Raidmaster — first come, first served.
	tally.record("hunt_slots_fill_fcfs",
		engine.claim_title(master_h, guild_title::huntmaster) == ok
		&& engine.claim_title(officer_h, guild_title::huntmaster) == ok
		&& engine.claim_title(member_h, guild_title::huntmaster) == guild_result::no_title_slot);
	tally.record("raid_slot_fills_fcfs",
		engine.claim_title(member_h, guild_title::raidmaster) == ok
		&& engine.claim_title(joiner_h, guild_title::raidmaster) == guild_result::no_title_slot);
	tally.record("one_title_per_member",
		engine.claim_title(master_h, guild_title::raidmaster) == guild_result::already_titled);

	{
		// Losing the rank does not lose a hunting Title — only Commander is
		// eligibility-bound, through the bit, never the rank name.
		guild_title_record held;
		tally.record("demote_keeps_noncommander_title",
			engine.promote_member(master_h, "GpMember") == ok
			&& engine.demote_member(master_h, "GpMember") == ok
			&& store.title_of("GpMember", held)
			&& held.kind == guild_title::raidmaster);
	}
	{
		// A Commander demoted below the eligibility bit loses the duty in the
		// same stroke.
		guild_title_record held;
		tally.record("demotion_releases_commander",
			engine.drop_title(officer_h) == ok
			&& engine.claim_title(officer_h, guild_title::commander) == ok
			&& engine.demote_member(master_h, "GpOfficer") == ok
			&& !store.title_of("GpOfficer", held)
			&& officer_c->m_guild_title == 0);
		engine.promote_member(master_h, "GpOfficer");
	}
	{
		// Membership takes the Title with it — the store cascade, seen from
		// the engine: a kick frees the slot and clears the cache.
		guild_title_record held;
		tally.record("kick_releases_title",
			engine.kick_member(master_h, "GpMember") == ok
			&& !store.title_of("GpMember", held)
			&& member_c->m_guild_guid == 0
			&& member_c->m_guild_title == 0);
	}

	//----------------------------------------------------------------------
	// The inactivity sweep: online holders ride the AFK clock, offline
	// holders ride last_seen_at, ten minutes either way.
	//----------------------------------------------------------------------

	{
		const uint32_t now_ms = GameClock::GetTimeMS();
		// Every online holder's clock reads fresh unless a check backdates it.
		master_c->m_afk_activity_time = now_ms;
		officer_c->m_afk_activity_time = now_ms;

		// Active online holder: retained, and the persisted clock touched
		// forward from a deliberately ancient stamp.
		guild_title_record held;
		const bool backdated = exec(store.handle(),
			"UPDATE guild_titles SET last_seen_at = 1000 WHERE holder_name = 'GpMaster';");
		engine.tick_titles(now_ms);
		tally.record("tick_touches_active_holder",
			backdated
			&& store.title_of("GpMaster", held)
			&& held.kind == guild_title::huntmaster
			&& held.last_seen_at > 1000);

		// Idle past the knob: released, cache cleared.
		master_c->m_afk_activity_time = now_ms - 700000;
		engine.tick_titles(now_ms);
		tally.record("tick_releases_idle_online_holder",
			!store.title_of("GpMaster", held)
			&& master_c->m_guild_title == 0);

		// Offline holder past the knob: released. GpF1 is a store-only member
		// with no client; the raid slot freed by the kick above.
		tally.record("tick_releases_idle_offline_holder",
			store.claim_title(knights, guild_title::raidmaster, "GpF1",
				hb::time::unix_now() - 700)
			&& (engine.tick_titles(now_ms),
				!store.title_of("GpF1", held)));

		// Offline holder inside the knob: retained.
		tally.record("tick_keeps_fresh_offline_holder",
			store.claim_title(knights, guild_title::raidmaster, "GpF1",
				hb::time::unix_now())
			&& (engine.tick_titles(now_ms),
				store.title_of("GpF1", held))
			&& held.kind == guild_title::raidmaster);
		store.release_title("GpF1");
	}

	//----------------------------------------------------------------------
	// Treasury: the wallet and its log — and the firewall to the altar.
	//----------------------------------------------------------------------

	tally.record("deposit_nonpositive_refused",
		engine.treasury_deposit(master_h, 0) == guild_result::amount_too_small);
	tally.record("deposit_short_gold_refused",
		engine.treasury_deposit(master_h, 999999) == guild_result::insufficient_funds);
	{
		guild_record after;
		tally.record("deposit_moves_gold",
			engine.treasury_deposit(master_h, 500) == ok
			&& gold_of(game, master_h) == 1500
			&& store.treasury_gold(knights) == 500);
		// The altar/wallet firewall (ADR 0007): the deposit minted no burn.
		tally.record("deposit_feeds_no_progression",
			store.load_guild(knights, after)
			&& after.burned_gold == 3000 && after.level == 3);
	}
	tally.record("withdraw_without_bit_refused",
		engine.treasury_withdraw(joiner_h, 100) == guild_result::no_permission);
	{
		const bool withdrawn = engine.treasury_withdraw(officer_h, 200) == ok;
		const auto log = store.withdraw_log(knights, 0);
		tally.record("withdraw_pays_and_logs",
			withdrawn
			&& gold_of(game, officer_h) == 200
			&& store.treasury_gold(knights) == 300
			&& log.size() == 1
			&& log[0].amount == 200);
	}
	tally.record("withdraw_overdraw_refused",
		engine.treasury_withdraw(officer_h, 10000) == guild_result::treasury_short
		&& store.treasury_gold(knights) == 300);

	//----------------------------------------------------------------------
	// The bonus seams the combat/loot/repair hooks read.
	//----------------------------------------------------------------------

	{
		// GpOfficer takes a Title back for the truth table.
		const bool staged_title =
			engine.claim_title(officer_h, guild_title::huntmaster) == ok;
		tally.record("holds_title_reads_cache_and_switch",
			staged_title
			&& game->client_holds_title(officer_h, guild_title::huntmaster)
			&& !game->client_holds_title(officer_h, guild_title::raidmaster)
			&& !game->client_holds_title(outsider_h, guild_title::huntmaster));
		game->m_guild_progression.title_bonuses_enabled = false;
		tally.record("holds_title_dark_when_disabled",
			!game->client_holds_title(officer_h, guild_title::huntmaster));
		// The repair discount is a curve grant, not a Title bonus: the switch
		// must not touch it (§3.1.2 item 5), and the unguilded read 0.
		tally.record("repair_discount_reads_curve_not_switch",
			game->guild_repair_discount_pct(officer_h) == 10
			&& game->guild_repair_discount_pct(outsider_h) == 0);
		game->m_guild_progression.title_bonuses_enabled = true;
	}

	tally.record("scratch_validates_clean", store.validate().empty());

	tally.report();
}
