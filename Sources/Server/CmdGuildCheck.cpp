// CmdGuildCheck.cpp: the guild engine contract prover (#121)
//
// The store prover (guilddbcheck, #120) proves what a guilds.db may BE; this
// one proves what a player may DO — the gate ladders, the ticket ceremonies,
// the membership index and the CClient hydration that #123 will put on the
// wire. It drives the REAL entry points on guild_manager with synthetic
// clients parked in real slots, because the contract lives in the composition:
// a create that passes its gates but forgets to hydrate, or a kick that
// updates the store but strands the index, compiles fine and fails only here.
//
// The engine writes through CGame::m_guild_store, so the live store is swapped
// for a scratch database for the length of the run (the ledger sink likewise —
// ticket consumption and the disband payout book real flows) and restored
// however the command returns. The live guilds.db is never written.
//
// Timestamps never reach the output lines, so Windows and Linux runs compare
// byte for byte even though the engine stamps the real clock into the scratch
// rows.
//
// Design contract: PLANS/CoreSystems_Restoration_Plan.md 3.1/3.1.1 + issue #121.
//
//////////////////////////////////////////////////////////////////////

#include "CmdGuildCheck.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "CheckTally.h"
#include "Client.h"
#include "Game.h"
#include "GameChatCommand.h"
#include "GuildManager.h"
#include "GuildSqliteStore.h"
#include "Item.h"
#include "ItemManager.h"
#include "ServerConsole.h"
#include "StringCompat.h"

using namespace hb::shared::guild;
using hb::server::check_tally;
using hb::server::find_free_handle;
using hb::server::give_item;
using hb::server::guild_record;
using hb::server::guild_member_record;
using hb::server::guild_request_record;
using hb::server::guild_result::ok;
using hb::server::guild_sqlite_store;
using hb::server::probe_client;

namespace guild_result = hb::server::guild_result;

namespace
{
	constexpr const char* probe_db = "guildcheck_probe.db";
	constexpr const char* probe_ledger_db = "guildcheck_ledger_probe.db";

	// Engine gates read the crusade flag; the run pins it false (and flips it
	// deliberately for the crusade checks), then puts the live value back.
	struct crusade_flag_guard
	{
		CGame* game;
		bool saved;
		explicit crusade_flag_guard(CGame* g) : game(g), saved(g->m_is_crusade_mode)
		{
			game->m_is_crusade_mode = false;
		}
		~crusade_flag_guard() { game->m_is_crusade_mode = saved; }
	};

	// Stamp the character-sheet fields the gate ladders read. probe_client
	// set the identity; this sets the worthiness.
	void shape_probe(CGame* game, int handle, int level, int charisma, int side,
		const char* location, int map_index, uint32_t exp)
	{
		CClient* client = game->m_client_list[handle];
		client->m_level = level;
		client->m_charisma = charisma;
		client->m_side = static_cast<char>(side);
		std::snprintf(client->m_location, sizeof(client->m_location), "%s", location);
		client->m_map_index = static_cast<char>(map_index);
		client->m_exp = exp;
		// Tickets and the payout ride add_client_item_list, whose gate is
		// calc_max_load(str, level) — a probe must be strong enough to carry
		// its own ceremony.
		client->m_str = 100;
	}

	// Slot presence rather than count: tickets are non-stackable, and a
	// stackable count would read 0 on a factory-fresh instance anyway.
	bool has_item(CGame* game, int handle, short item_id)
	{
		return game->m_item_manager->find_inventory_item(handle, item_id) >= 0;
	}

	bool index_holds(CGame* game, int64_t guid, int handle)
	{
		for (int h : game->m_guild_manager->online_members(guid))
		{
			if (h == handle)
			{
				return true;
			}
		}
		return false;
	}
}

void CmdGuildCheck::execute(CGame* game, const char* args)
{
	(void)args;

	check_tally tally("GUILDCHECK", "guildcheck");

	if (game == nullptr || game->m_guild_manager == nullptr
		|| game->m_guild_store == nullptr || !game->m_guild_store->is_open())
	{
		hb::console::error("guildcheck: the guild engine or store is not up.");
		return;
	}
	const int aresden_map = game->get_map_index("aresden");
	const int elvine_map = game->get_map_index("elvine");
	if (aresden_map < 0 || elvine_map < 0)
	{
		hb::console::error("guildcheck: the aresden/elvine maps are not loaded.");
		return;
	}

	hb::server::guild_store_swap swap(game, probe_db);
	if (!swap.ok())
	{
		hb::console::error("guildcheck: could not create the scratch database '{}'.", probe_db);
		return;
	}
	hb::server::ledger_sink_swap ledger_swap(game, probe_ledger_db);
	if (!ledger_swap.ok())
	{
		hb::console::error("guildcheck: could not create the scratch ledger '{}'.", probe_ledger_db);
		return;
	}
	crusade_flag_guard crusade(game);
	hb::server::guild_manager& engine = *game->m_guild_manager;
	guild_sqlite_store& store = *game->m_guild_store;

	// Five actors: a founder, two recruits (one becomes an Officer), a
	// low-stat traveler shaped per-check, and an enemy-side citizen.
	const int master_h = find_free_handle(game, 1);
	const int joiner_h = find_free_handle(game, master_h + 1);
	const int second_h = find_free_handle(game, joiner_h + 1);
	const int drifter_h = find_free_handle(game, second_h + 1);
	const int enemy_h = find_free_handle(game, drifter_h + 1);
	if (enemy_h < 0)
	{
		hb::console::error("guildcheck: no free client handles for the probes.");
		return;
	}
	// probe_client's destructor detaches each handle from the per-client
	// indexes (detach_client_from_indexes), so no hand-written sweep here.
	probe_client master(game, master_h, "gcacct1", "GcMaster", "aresden", 100, 100);
	probe_client joiner(game, joiner_h, "gcacct2", "GcJoiner", "aresden", 101, 100);
	probe_client second(game, second_h, "gcacct3", "GcSecond", "aresden", 102, 100);
	probe_client drifter(game, drifter_h, "gcacct4", "GcDrifter", "aresden", 103, 100);
	probe_client enemy(game, enemy_h, "gcacct5", "GcEnemy", "elvine", 104, 100);

	shape_probe(game, master_h, 30, 30, 1, "aresden", aresden_map, 5000);
	shape_probe(game, joiner_h, 12, 10, 1, "aresden", aresden_map, 5000);
	shape_probe(game, second_h, 25, 25, 1, "aresden", aresden_map, 5000);
	shape_probe(game, drifter_h, 19, 30, 1, "aresden", aresden_map, 5000);
	shape_probe(game, enemy_h, 30, 30, 2, "elvine", elvine_map, 5000);

	//----------------------------------------------------------------------
	// Create: each gate refuses on its own rung, then a clean founding.
	//----------------------------------------------------------------------

	tally.record("create_low_level_refused",
		engine.create_guild(drifter_h, "GcKnights") == guild_result::level_too_low);
	shape_probe(game, drifter_h, 30, 19, 1, "aresden", aresden_map, 5000);
	tally.record("create_low_charisma_refused",
		engine.create_guild(drifter_h, "GcKnights") == guild_result::charisma_too_low);
	shape_probe(game, drifter_h, 30, 30, 0, "NONE", aresden_map, 5000);
	tally.record("create_traveler_refused",
		engine.create_guild(drifter_h, "GcKnights") == guild_result::not_citizen);
	shape_probe(game, drifter_h, 30, 30, 1, "aresden", elvine_map, 5000);
	tally.record("create_wrong_town_refused",
		engine.create_guild(drifter_h, "GcKnights") == guild_result::not_in_own_town);
	shape_probe(game, drifter_h, 30, 30, 1, "aresden", aresden_map, 5000);

	game->m_is_crusade_mode = true;
	tally.record("create_during_crusade_refused",
		engine.create_guild(master_h, "GcKnights") == guild_result::crusade_active);
	game->m_is_crusade_mode = false;

	tally.record("create_bad_name_refused",
		engine.create_guild(master_h, "Gc!Knights") == guild_result::bad_name);

	tally.record("create_ok",
		engine.create_guild(master_h, "GcKnights") == ok);
	CClient* master_c = game->m_client_list[master_h];
	const int64_t knights = master_c->m_guild_guid;
	tally.record("create_hydrates_master",
		knights != 0
		&& master_c->m_guild_rank == guild_rank::guildmaster
		&& std::strcmp(master_c->m_guild_name, "GcKnights") == 0
		&& master_c->m_guild_permission_mask == default_guildmaster_mask);
	tally.record("create_indexes_master", index_holds(game, knights, master_h));

	tally.record("create_taken_name_refused",
		engine.create_guild(drifter_h, "gcknights") == guild_result::name_taken);
	tally.record("create_member_refused",
		engine.create_guild(master_h, "GcSeconds") == guild_result::already_in_guild);

	//----------------------------------------------------------------------
	// The join ceremony: ticket on filing, any approver by bit.
	//----------------------------------------------------------------------

	int64_t request = 0;
	tally.record("join_file_no_ticket_refused",
		engine.file_join_request(joiner_h, "GcKnights", request) == guild_result::no_ticket);
	tally.record("join_file_unknown_guild_refused",
		engine.file_join_request(joiner_h, "GcNobody", request) == guild_result::unknown_guild);
	tally.record("join_file_wrong_side_refused",
		give_item(game, enemy_h, hb::shared::item::ItemId::GuildAdmissionTicket)
		&& engine.file_join_request(enemy_h, "GcKnights", request) == guild_result::wrong_side
		&& has_item(game, enemy_h, hb::shared::item::ItemId::GuildAdmissionTicket));

	tally.record("join_file_ok",
		give_item(game, joiner_h, hb::shared::item::ItemId::GuildAdmissionTicket)
		&& engine.file_join_request(joiner_h, "GcKnights", request) == ok
		&& request > 0);
	tally.record("join_ticket_consumed_on_filing",
		!has_item(game, joiner_h, hb::shared::item::ItemId::GuildAdmissionTicket));
	{
		// A separate out-var: every file_* clears its out param first, and the
		// approve below still needs the real request id.
		int64_t duplicate = 0;
		tally.record("join_duplicate_refused",
			engine.file_join_request(joiner_h, "GcKnights", duplicate)
				== guild_result::request_pending);
	}

	tally.record("decide_outsider_refused",
		engine.decide_request(drifter_h, request, true) == guild_result::not_in_guild);
	tally.record("decide_approve_join_ok",
		engine.decide_request(master_h, request, true) == ok);
	CClient* joiner_c = game->m_client_list[joiner_h];
	tally.record("join_hydrates_member",
		joiner_c->m_guild_guid == knights
		&& joiner_c->m_guild_rank == guild_rank::guildsman
		&& joiner_c->m_guild_permission_mask == default_guildsman_mask
		&& index_holds(game, knights, joiner_h));

	{
		// Two applications out; the approved one retires the other (the
		// store's add_member sweep) and the scratch world stays validate-clean.
		int64_t to_knights = 0, to_rivals = 0;
		int64_t rivals = 0;
		std::string error;
		const bool staged =
			store.create_guild("GcRivals", 1, "GcRivalM", 1000, rivals, error)
			&& give_item(game, second_h, hb::shared::item::ItemId::GuildAdmissionTicket)
			&& give_item(game, second_h, hb::shared::item::ItemId::GuildAdmissionTicket)
			&& engine.file_join_request(second_h, "GcKnights", to_knights) == ok
			&& engine.file_join_request(second_h, "GcRivals", to_rivals) == ok;
		tally.record("join_second_application_staged", staged, error);
		tally.record("decide_retires_other_applications",
			engine.decide_request(master_h, to_knights, true) == ok
			&& store.pending_requests(rivals).empty()
			&& store.validate().empty());
	}

	//----------------------------------------------------------------------
	// Permission bits are the gate, not ranks (ADR 0006).
	//----------------------------------------------------------------------

	{
		int64_t plain_request = 0;
		tally.record("decide_without_bit_refused",
			give_item(game, drifter_h, hb::shared::item::ItemId::GuildAdmissionTicket)
			&& engine.file_join_request(drifter_h, "GcKnights", plain_request) == ok
			&& engine.decide_request(joiner_h, plain_request, false) == guild_result::no_permission);

		// Hand the guildsman rank the approve-join bit in data — the same
		// decide now passes with no rank change anywhere. This is the ADR's
		// whole point: the gate reads bits, never rank identity.
		tally.record("decide_by_granted_bit_ok",
			store.set_permission_mask(knights, guild_rank::guildsman,
				guild_permission::approve_join)
			&& (engine.hydrate_login(joiner_h),
				engine.decide_request(joiner_h, plain_request, false) == ok));
		store.set_permission_mask(knights, guild_rank::guildsman, default_guildsman_mask);
		engine.hydrate_login(joiner_h);
	}

	//----------------------------------------------------------------------
	// Promote / demote: the bit, the two ranks, the refreshed mask.
	//----------------------------------------------------------------------

	tally.record("promote_without_bit_refused",
		engine.promote_member(joiner_h, "GcSecond") == guild_result::no_permission);
	tally.record("promote_ok",
		engine.promote_member(master_h, "GcSecond") == ok);
	CClient* second_c = game->m_client_list[second_h];
	tally.record("promote_refreshes_mask",
		second_c->m_guild_rank == guild_rank::officer
		&& second_c->m_guild_permission_mask == default_officer_mask);
	tally.record("promote_officer_refused",
		engine.promote_member(master_h, "GcSecond") == guild_result::bad_rank);
	tally.record("demote_guildsman_refused",
		engine.demote_member(master_h, "GcJoiner") == guild_result::bad_rank);
	tally.record("promote_master_refused",
		engine.promote_member(master_h, "GcMaster") == guild_result::target_is_master);

	//----------------------------------------------------------------------
	// Kick: strictly below the actor's rank, online or offline.
	//----------------------------------------------------------------------

	tally.record("kick_without_bit_refused",
		engine.kick_member(joiner_h, "GcSecond") == guild_result::no_permission);
	tally.record("kick_master_refused",
		engine.kick_member(second_h, "GcMaster") == guild_result::target_is_master);
	tally.record("kick_self_refused",
		engine.kick_member(second_h, "GcSecond") == guild_result::target_not_below_rank);
	tally.record("kick_online_ok",
		engine.kick_member(second_h, "GcJoiner") == ok
		&& joiner_c->m_guild_guid == 0
		&& joiner_c->m_guild_rank == guild_rank::none
		&& !index_holds(game, knights, joiner_h));
	{
		guild_member_record ghost;
		tally.record("kick_offline_ok",
			store.add_member(knights, "GcGhost", guild_rank::guildsman, 1000)
			&& engine.kick_member(second_h, "GcGhost") == ok
			&& !store.member_of("GcGhost", ghost));
	}

	//----------------------------------------------------------------------
	// The /banguild fallback rides the same engine gate.
	//----------------------------------------------------------------------

	tally.record("banguild_command_routes_to_kick",
		store.add_member(knights, "GcGhost", guild_rank::guildsman, 1000)
		&& GameChatCommandManager::get().process_command(second_h,
			"/banguild GcGhost", sizeof("/banguild GcGhost") - 1)
		&& [&] { guild_member_record ghost; return !store.member_of("GcGhost", ghost); }());

	//----------------------------------------------------------------------
	// The leave ceremony and Kennedy's door.
	//----------------------------------------------------------------------

	{
		int64_t back = 0;
		if (give_item(game, joiner_h, hb::shared::item::ItemId::GuildAdmissionTicket)
			&& engine.file_join_request(joiner_h, "GcKnights", back) == ok)
		{
			engine.decide_request(master_h, back, true);
		}
	}
	tally.record("leave_file_master_refused",
		engine.file_leave_request(master_h, request) == guild_result::master_cannot_leave);
	tally.record("leave_file_no_ticket_refused",
		engine.file_leave_request(joiner_h, request) == guild_result::no_ticket);
	tally.record("leave_file_ok",
		give_item(game, joiner_h, hb::shared::item::ItemId::GuildSecessionTicket)
		&& engine.file_leave_request(joiner_h, request) == ok
		&& !has_item(game, joiner_h, hb::shared::item::ItemId::GuildSecessionTicket));
	tally.record("leave_reject_keeps_member",
		engine.decide_request(master_h, request, false) == ok
		&& joiner_c->m_guild_guid == knights);
	tally.record("leave_approve_removes_member",
		give_item(game, joiner_h, hb::shared::item::ItemId::GuildSecessionTicket)
		&& engine.file_leave_request(joiner_h, request) == ok
		&& engine.decide_request(master_h, request, true) == ok
		&& joiner_c->m_guild_guid == 0
		&& !index_holds(game, knights, joiner_h));

	{
		int64_t back = 0;
		if (give_item(game, joiner_h, hb::shared::item::ItemId::GuildAdmissionTicket)
			&& engine.file_join_request(joiner_h, "GcKnights", back) == ok)
		{
			engine.decide_request(master_h, back, true);
		}
		give_item(game, joiner_h, hb::shared::item::ItemId::GuildSecessionTicket);

		game->m_is_crusade_mode = true;
		tally.record("kennedy_during_crusade_refused",
			engine.kennedy_self_exit(joiner_h) == guild_result::crusade_active);
		game->m_is_crusade_mode = false;

		tally.record("kennedy_master_refused",
			engine.kennedy_self_exit(master_h) == guild_result::master_cannot_leave);

		// exp 5000 -> 4700: the ceremony's price lands.
		tally.record("kennedy_exit_ok",
			engine.kennedy_self_exit(joiner_h) == ok
			&& joiner_c->m_exp == 5000 - kennedy_secession_exp_penalty
			&& joiner_c->m_guild_guid == 0
			&& !index_holds(game, knights, joiner_h));
		tally.record("kennedy_ticket_consumed",
			!has_item(game, joiner_h, hb::shared::item::ItemId::GuildSecessionTicket));
		tally.record("kennedy_no_ticket_refused",
			[&] {
				int64_t back = 0;
				return give_item(game, joiner_h, hb::shared::item::ItemId::GuildAdmissionTicket)
					&& engine.file_join_request(joiner_h, "GcKnights", back) == ok
					&& engine.decide_request(master_h, back, true) == ok
					&& engine.kennedy_self_exit(joiner_h) == guild_result::no_ticket;
			}());

		// The unsigned clamp: 100 exp - 300 is zero, not four billion. An
		// Officer takes the same door through the auto-demote. GcSecond steps
		// down first: a Level-1 guild holds one Officer (#122's capacity gate,
		// proven in guildprogcheck) and this check is about Kennedy's door,
		// not the cap.
		joiner_c->m_exp = 100;
		tally.record("kennedy_officer_exits_and_exp_clamps",
			give_item(game, joiner_h, hb::shared::item::ItemId::GuildSecessionTicket)
			&& engine.demote_member(master_h, "GcSecond") == ok
			&& engine.promote_member(master_h, "GcJoiner") == ok
			&& engine.kennedy_self_exit(joiner_h) == ok
			&& joiner_c->m_exp == 0
			&& joiner_c->m_guild_guid == 0
			&& engine.promote_member(master_h, "GcSecond") == ok);
	}

	//----------------------------------------------------------------------
	// Transfer-mastership and the master's CHR floor.
	//----------------------------------------------------------------------

	tally.record("transfer_not_master_refused",
		engine.transfer_mastership(second_h, "GcMaster") == guild_result::not_master);
	{
		guild_member_record ghost;
		tally.record("transfer_offline_refused",
			store.add_member(knights, "GcGhost", guild_rank::guildsman, 1000)
			&& engine.transfer_mastership(master_h, "GcGhost") == guild_result::target_offline
			&& store.member_of("GcGhost", ghost));
		store.remove_member("GcGhost");
	}
	second_c->m_charisma = 10;
	tally.record("transfer_low_charisma_refused",
		engine.transfer_mastership(master_h, "GcSecond") == guild_result::charisma_too_low);
	second_c->m_charisma = 25;

	tally.record("master_charisma_floor_active",
		engine.min_charisma(master_h) == guildmaster_charisma_floor
		&& engine.min_charisma(second_h) == 0);
	tally.record("transfer_ok",
		engine.transfer_mastership(master_h, "GcSecond") == ok
		&& master_c->m_guild_rank == guild_rank::officer
		&& second_c->m_guild_rank == guild_rank::guildmaster
		&& second_c->m_guild_permission_mask == default_guildmaster_mask);
	tally.record("charisma_floor_follows_master",
		engine.min_charisma(master_h) == 0
		&& engine.min_charisma(second_h) == guildmaster_charisma_floor);
	{
		guild_record after;
		tally.record("transfer_updates_store",
			store.load_guild(knights, after)
			&& hb_stricmp(after.master_name.c_str(), "GcSecond") == 0);
	}

	//----------------------------------------------------------------------
	// Hydration and the index across logout/login.
	//----------------------------------------------------------------------

	engine.on_logout(master_h);
	tally.record("logout_prunes_index", !index_holds(game, knights, master_h));
	engine.hydrate_login(master_h);
	tally.record("login_rehydrates",
		index_holds(game, knights, master_h)
		&& master_c->m_guild_guid == knights
		&& master_c->m_guild_rank == guild_rank::officer);
	tally.record("unknown_guild_has_no_members",
		game->m_guild_manager->online_members(987654).empty());

	//----------------------------------------------------------------------
	// Disband: master-only, the typed name, the Treasury payout, the sweep.
	//----------------------------------------------------------------------

	tally.record("disband_not_master_refused",
		engine.disband_guild(master_h, "GcKnights") == guild_result::not_master);
	tally.record("disband_name_mismatch_refused",
		engine.disband_guild(second_h, "GcWrong") == guild_result::name_mismatch);
	game->m_is_crusade_mode = true;
	tally.record("disband_during_crusade_refused",
		engine.disband_guild(second_h, "GcKnights") == guild_result::crusade_active);
	game->m_is_crusade_mode = false;

	tally.record("disband_pays_and_clears",
		store.treasury_deposit(knights, 500)
		&& engine.disband_guild(second_h, "gcknights") == ok
		&& game->m_item_manager->get_item_count_by_id(second_h,
			hb::shared::item::ItemId::Gold) == 500
		&& second_c->m_guild_guid == 0
		&& master_c->m_guild_guid == 0
		&& game->m_guild_manager->online_members(knights).empty());

	{
		guild_record gone;
		tally.record("disband_removes_guild", !store.load_guild(knights, gone));
	}
	tally.record("scratch_validates_clean", store.validate().empty());

	tally.report();
}
