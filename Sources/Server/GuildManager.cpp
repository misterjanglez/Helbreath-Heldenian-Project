// GuildManager.cpp: the guild policy engine (#121) — see GuildManager.h for
// the contract. Gate ladders run in the original's order where an original
// flow exists; every refusal and every state change writes one guild.log
// line, which is the whole observable surface until #123 puts these results
// on the wire.
//
//////////////////////////////////////////////////////////////////////

#include "GuildManager.h"

#include <cstring>

#include "Client.h"
#include "Game.h"
#include "GuildSqliteStore.h"
#include "ItemManager.h"
#include "Log.h"
#include "Map.h"
#include "ServerLogChannels.h"
#include "StringCompat.h"
#include "TimeUtils.h"

using namespace hb::shared::guild;
using namespace hb::shared::net;
using hb::log_channel;
namespace ItemLogAction = hb::server::net::ItemLogAction;

namespace hb::server
{

const char* guild_result_name(int result)
{
	switch (result)
	{
	case guild_result::ok:                     return "ok";
	case guild_result::no_client:              return "no_client";
	case guild_result::crusade_active:         return "crusade_active";
	case guild_result::already_in_guild:       return "already_in_guild";
	case guild_result::not_in_guild:           return "not_in_guild";
	case guild_result::bad_name:               return "bad_name";
	case guild_result::name_taken:             return "name_taken";
	case guild_result::name_mismatch:          return "name_mismatch";
	case guild_result::level_too_low:          return "level_too_low";
	case guild_result::charisma_too_low:       return "charisma_too_low";
	case guild_result::not_citizen:            return "not_citizen";
	case guild_result::not_in_own_town:        return "not_in_own_town";
	case guild_result::wrong_side:             return "wrong_side";
	case guild_result::no_ticket:              return "no_ticket";
	case guild_result::request_pending:        return "request_pending";
	case guild_result::unknown_guild:          return "unknown_guild";
	case guild_result::unknown_request:        return "unknown_request";
	case guild_result::no_permission:          return "no_permission";
	case guild_result::not_master:             return "not_master";
	case guild_result::master_cannot_leave:    return "master_cannot_leave";
	case guild_result::target_not_found:       return "target_not_found";
	case guild_result::target_is_master:       return "target_is_master";
	case guild_result::target_not_below_rank:  return "target_not_below_rank";
	case guild_result::target_offline:         return "target_offline";
	case guild_result::bad_rank:               return "bad_rank";
	case guild_result::store_error:            return "store_error";
	case guild_result::donations_disabled:     return "donations_disabled";
	case guild_result::amount_too_small:       return "amount_too_small";
	case guild_result::insufficient_funds:     return "insufficient_funds";
	case guild_result::guild_full:             return "guild_full";
	case guild_result::officers_at_capacity:   return "officers_at_capacity";
	case guild_result::bad_title:              return "bad_title";
	case guild_result::already_titled:         return "already_titled";
	case guild_result::no_title_slot:          return "no_title_slot";
	case guild_result::no_title:               return "no_title";
	case guild_result::treasury_short:         return "treasury_short";
	default:                                   return "unknown";
	}
}

namespace
{
	// The empty answer online_members gives for a guild with nobody on —
	// file-scope so the hit path pays no function-local-static init guard.
	const std::vector<int> no_members;

	// The §3.1.2 climb: from the guild's current level, take each next curve
	// row whose three cumulative thresholds the post-donation totals ALL
	// meet (the AND-gate; over-donation carries because the totals do).
	// Upward only — this can never return less than guild.level.
	int level_after(const hb::server::guild_progression_config& config,
		const hb::server::guild_record& guild,
		int64_t gold_delta, int64_t ek_delta, int64_t contribution_delta)
	{
		const int64_t gold = guild.burned_gold + gold_delta;
		const int64_t kills = guild.burned_ek + ek_delta;
		const int64_t contribution = guild.burned_contribution + contribution_delta;
		int level = guild.level;
		while (const hb::server::guild_level_row* next = config.find_level(level + 1))
		{
			if (gold < next->gold || kills < next->enemy_kills
				|| contribution < next->contribution)
			{
				break;
			}
			level++;
		}
		return level;
	}

	// One shape for every refusal line, so guild.log reads uniformly and a
	// grep for a result name finds every ceremony it stopped. `detail` is for
	// the store_error cases, whose result name alone names no cause.
	int refuse(const char* verb, const CClient& client, int result,
		const char* detail = nullptr)
	{
		if (detail != nullptr)
		{
			hb::logger::log<log_channel::guild>("{} refused ({}): '{}' - {}",
				verb, guild_result_name(result), client.m_char_name, detail);
		}
		else
		{
			hb::logger::log<log_channel::guild>("{} refused ({}): '{}'",
				verb, guild_result_name(result), client.m_char_name);
		}
		return result;
	}
}

guild_sqlite_store* guild_manager::store() const
{
	if (m_game == nullptr || m_game->m_guild_store == nullptr
		|| !m_game->m_guild_store->is_open())
	{
		return nullptr;
	}
	return m_game->m_guild_store.get();
}

CClient* guild_manager::client_at(int client_h) const
{
	CClient* client = client_raw(client_h);
	if (client == nullptr || !client->m_is_init_complete)
	{
		return nullptr;
	}
	return client;
}

CClient* guild_manager::client_raw(int client_h) const
{
	if (m_game == nullptr || client_h < 0 || client_h >= hb::server::config::MaxClients)
	{
		return nullptr;
	}
	return m_game->m_client_list[client_h];
}

const hb::server::guild_level_row* guild_manager::curve_row(int64_t guid,
	guild_record& out) const
{
	guild_sqlite_store* db = store();
	if (db == nullptr || !db->load_guild(guid, out))
	{
		return nullptr;
	}
	return m_game->get_guild_progression().find_level(out.level);
}

bool guild_manager::is_in_own_town(const CClient& client) const
{
	const CMap* map = (client.m_map_index >= 0 && client.m_map_index < hb::server::config::MaxMaps)
		? m_game->m_map_list[client.m_map_index] : nullptr;
	if (map == nullptr)
	{
		return false;
	}
	// The original's exact test: the citizenship string against the map's
	// location name — NOT a map-name whitelist, and NOT the misnamed
	// m_is_inside_own_town flag (which flags the enemy town).
	return std::strncmp(client.m_location, map->m_location_name, 10) == 0;
}

//----------------------------------------------------------------------
// Lifecycle
//----------------------------------------------------------------------

int guild_manager::create_guild(int client_h, const char* name)
{
	CClient* client = client_at(client_h);
	if (client == nullptr || name == nullptr)
	{
		return guild_result::no_client;
	}
	guild_sqlite_store* db = store();
	if (db == nullptr)
	{
		return refuse("create", *client, guild_result::store_error);
	}
	// The original's gate order: crusade first, then standing, then the
	// combined worthiness block (level, charisma, citizenship, home town) —
	// split here so the refusal names its rung.
	if (m_game->m_is_crusade_mode)
	{
		return refuse("create", *client, guild_result::crusade_active);
	}
	if (client->m_guild_guid != 0)
	{
		return refuse("create", *client, guild_result::already_in_guild);
	}
	if (!is_valid_guild_name(name))
	{
		return refuse("create", *client, guild_result::bad_name);
	}
	if (client->m_level < guild_create_min_level)
	{
		return refuse("create", *client, guild_result::level_too_low);
	}
	if (client->m_charisma < guild_create_min_charisma)
	{
		return refuse("create", *client, guild_result::charisma_too_low);
	}
	const int side = client->m_side;
	if (std::memcmp(client->m_location, "NONE", 4) == 0 || (side != 1 && side != 2))
	{
		return refuse("create", *client, guild_result::not_citizen);
	}
	if (!is_in_own_town(*client))
	{
		return refuse("create", *client, guild_result::not_in_own_town);
	}
	guild_record taken;
	if (db->find_guild_by_name(name, taken))
	{
		return refuse("create", *client, guild_result::name_taken);
	}

	int64_t guid = 0;
	std::string error;
	if (!db->create_guild(name, side, client->m_char_name, hb::time::unix_now(),
		guid, error))
	{
		return refuse("create", *client, guild_result::store_error, error.c_str());
	}

	refresh_client(client_h);
	hb::logger::log<log_channel::guild>("create: '{}' founded '{}' (guid {}, side {})",
		client->m_char_name, name, guid, side);
	return guild_result::ok;
}

int guild_manager::disband_guild(int client_h, const char* typed_name)
{
	CClient* client = client_at(client_h);
	if (client == nullptr || typed_name == nullptr)
	{
		return guild_result::no_client;
	}
	guild_sqlite_store* db = store();
	if (db == nullptr)
	{
		return refuse("disband", *client, guild_result::store_error);
	}
	if (m_game->m_is_crusade_mode)
	{
		return refuse("disband", *client, guild_result::crusade_active);
	}
	if (client->m_guild_guid == 0)
	{
		return refuse("disband", *client, guild_result::not_in_guild);
	}
	// Hardcoded master-only (ADR 0006): deliberately a rank identity check,
	// never a permission bit, so no mask edit can ever hand disband out.
	if (client->m_guild_rank != guild_rank::guildmaster)
	{
		return refuse("disband", *client, guild_result::not_master);
	}
	const int64_t guid = client->m_guild_guid;
	// The typed-name ceremony: confirmation, case-insensitive like the name's
	// own identity. (The original only echo-checked the client's cached
	// name; §3.1.1 makes it a deliberate retyping.)
	if (hb_stricmp(client->m_guild_name, typed_name) != 0)
	{
		return refuse("disband", *client, guild_result::name_mismatch);
	}

	const int64_t remainder = db->treasury_gold(guid);

	if (!db->disband_guild(guid))
	{
		return refuse("disband", *client, guild_result::store_error);
	}

	// The Treasury remainder goes to the master (§3.1.1 deviation 6) — after
	// the cascade so a failed disband cannot pay twice. grant_gold delivers
	// to the inventory or the ground at their feet, so it arrives either way.
	if (remainder > 0)
	{
		m_game->m_item_manager->grant_gold(client_h,
			static_cast<uint64_t>(remainder));
		hb::logger::log<log_channel::guild>(
			"disband: treasury remainder {} paid to master '{}'",
			remainder, client->m_char_name);
	}

	// The "broadcast": every online member is cleared inline, off the index
	// instead of the original's 2000-slot name scan. The bucket is copied
	// because clear_client edits it underfoot. #123 gives this loop its wire
	// shape; until then the log line is the announcement.
	const std::vector<int> online = online_members(guid);
	for (int member_h : online)
	{
		clear_client(member_h);
	}
	hb::logger::log<log_channel::guild>(
		"disband: '{}' disbanded guild {} ('{}'), {} member(s) were online",
		client->m_char_name, guid, typed_name, online.size());
	return guild_result::ok;
}

//----------------------------------------------------------------------
// The queued join/leave ceremony
//----------------------------------------------------------------------

int guild_manager::file_join_request(int client_h, const char* guild_name,
	int64_t& out_request_id)
{
	out_request_id = 0;
	CClient* client = client_at(client_h);
	if (client == nullptr || guild_name == nullptr)
	{
		return guild_result::no_client;
	}
	guild_sqlite_store* db = store();
	if (db == nullptr)
	{
		return refuse("join-file", *client, guild_result::store_error);
	}
	// No crusade gate: the original's admission hand-over had none (only the
	// secession ceremonies did), and the queue exists precisely so filing
	// never depends on the moment.
	if (client->m_guild_guid != 0)
	{
		return refuse("join-file", *client, guild_result::already_in_guild);
	}
	guild_record guild;
	if (!db->find_guild_by_name(guild_name, guild))
	{
		return refuse("join-file", *client, guild_result::unknown_guild);
	}
	if (std::memcmp(client->m_location, "NONE", 4) == 0)
	{
		return refuse("join-file", *client, guild_result::not_citizen);
	}
	// Town-bound organizations of one side (§3.1): the original enforced this
	// through the in-person citizenship match; the queue enforces it by side.
	if (client->m_side != guild.side)
	{
		return refuse("join-file", *client, guild_result::wrong_side);
	}
	if (db->has_pending_request(guild.guid, client->m_char_name, guild_request::join))
	{
		return refuse("join-file", *client, guild_result::request_pending);
	}
	const int slot = m_game->m_item_manager->find_inventory_item(client_h,
		hb::shared::item::ItemId::GuildAdmissionTicket);
	if (slot < 0)
	{
		return refuse("join-file", *client, guild_result::no_ticket);
	}
	if (!db->queue_request(guild.guid, client->m_char_name, guild_request::join,
		hb::time::unix_now(), out_request_id))
	{
		return refuse("join-file", *client, guild_result::store_error);
	}
	// Consumed on filing (§3.1.1) — the queue row exists, so the ticket burns
	// now. The original burned it on hand-over before any decision too; what
	// it never gets to do here is burn on a refused ceremony.
	consume_ticket(client_h, slot);
	hb::logger::log<log_channel::guild>(
		"join-file: '{}' applied to '{}' (guid {}, request {})",
		client->m_char_name, guild.name, guild.guid, out_request_id);
	return guild_result::ok;
}

int guild_manager::file_leave_request(int client_h, int64_t& out_request_id)
{
	out_request_id = 0;
	CClient* client = client_at(client_h);
	if (client == nullptr)
	{
		return guild_result::no_client;
	}
	guild_sqlite_store* db = store();
	if (db == nullptr)
	{
		return refuse("leave-file", *client, guild_result::store_error);
	}
	// The original's secession ceremony is crusade-gated (both the approve
	// path and Kennedy's), unlike admission.
	if (m_game->m_is_crusade_mode)
	{
		return refuse("leave-file", *client, guild_result::crusade_active);
	}
	if (client->m_guild_guid == 0)
	{
		return refuse("leave-file", *client, guild_result::not_in_guild);
	}
	if (client->m_guild_rank == guild_rank::guildmaster)
	{
		return refuse("leave-file", *client, guild_result::master_cannot_leave);
	}
	const int64_t guid = client->m_guild_guid;
	if (db->has_pending_request(guid, client->m_char_name, guild_request::leave))
	{
		return refuse("leave-file", *client, guild_result::request_pending);
	}
	const int slot = m_game->m_item_manager->find_inventory_item(client_h,
		hb::shared::item::ItemId::GuildSecessionTicket);
	if (slot < 0)
	{
		return refuse("leave-file", *client, guild_result::no_ticket);
	}
	if (!db->queue_request(guid, client->m_char_name, guild_request::leave,
		hb::time::unix_now(), out_request_id))
	{
		return refuse("leave-file", *client, guild_result::store_error);
	}
	consume_ticket(client_h, slot);
	hb::logger::log<log_channel::guild>(
		"leave-file: '{}' filed secession from guild {} (request {})",
		client->m_char_name, guid, out_request_id);
	return guild_result::ok;
}

int guild_manager::decide_request(int approver_h, int64_t request_id, bool approve)
{
	CClient* approver = client_at(approver_h);
	if (approver == nullptr)
	{
		return guild_result::no_client;
	}
	guild_sqlite_store* db = store();
	if (db == nullptr)
	{
		return refuse("decide", *approver, guild_result::store_error);
	}
	if (approver->m_guild_guid == 0)
	{
		return refuse("decide", *approver, guild_result::not_in_guild);
	}
	guild_request_record request;
	if (!db->load_request(request_id, request)
		|| request.guild_guid != approver->m_guild_guid)
	{
		// Another guild's request id gets the same answer as a stale one —
		// the queue's contents are not probeable across guild lines.
		return refuse("decide", *approver, guild_result::unknown_request);
	}
	const uint32_t needed_bit = (request.kind == guild_request::join)
		? guild_permission::approve_join
		: guild_permission::approve_leave;
	if (!has_permission(approver_h, needed_bit))
	{
		return refuse("decide", *approver, guild_result::no_permission);
	}

	const char* verb = (request.kind == guild_request::join) ? "join" : "leave";
	if (!approve)
	{
		if (!db->delete_request(request_id))
		{
			return refuse("decide", *approver, guild_result::store_error);
		}
		hb::logger::log<log_channel::guild>(
			"decide: '{}' rejected {} request {} from '{}' (guild {})",
			approver->m_char_name, verb, request_id, request.char_name,
			request.guild_guid);
		return guild_result::ok;
	}

	if (request.kind == guild_request::join)
	{
		// The member cap is the curve's grant at the guild's level (§3.1.2;
		// the original's 128 constant existed and was never checked — this is
		// the first join gate that reads a cap at all).
		guild_record guild;
		const hb::server::guild_level_row* row = curve_row(request.guild_guid, guild);
		if (row == nullptr)
		{
			return refuse("decide", *approver, guild_result::store_error);
		}
		if (db->member_count(guild.guid) >= row->member_cap)
		{
			return refuse("decide", *approver, guild_result::guild_full);
		}
		if (!db->add_member(request.guild_guid, request.char_name.c_str(),
			guild_rank::guildsman, hb::time::unix_now()))
		{
			return refuse("decide", *approver, guild_result::store_error);
		}
		// add_member retired this request (and every other application the
		// character had out) inside its own transaction. The joiner is in no
		// index bucket yet, so this is the engine's one name scan; a miss
		// returns 0, which refresh_client ignores like any dead handle.
		refresh_client(m_game->find_client_by_name(request.char_name.c_str()));
	}
	else
	{
		if (!db->remove_member(request.char_name.c_str()))
		{
			return refuse("decide", *approver, guild_result::store_error);
		}
		// remove_member retired the leave request with the member row; a -1
		// (offline) falls out of clear_client the same way.
		clear_client(find_online_member(request.guild_guid,
			request.char_name.c_str()));
	}
	hb::logger::log<log_channel::guild>(
		"decide: '{}' approved {} request {} from '{}' (guild {})",
		approver->m_char_name, verb, request_id, request.char_name,
		request.guild_guid);
	return guild_result::ok;
}

int guild_manager::kennedy_self_exit(int client_h)
{
	CClient* client = client_at(client_h);
	if (client == nullptr)
	{
		return guild_result::no_client;
	}
	guild_sqlite_store* db = store();
	if (db == nullptr)
	{
		return refuse("kennedy-exit", *client, guild_result::store_error);
	}
	if (m_game->m_is_crusade_mode)
	{
		return refuse("kennedy-exit", *client, guild_result::crusade_active);
	}
	if (client->m_guild_guid == 0)
	{
		return refuse("kennedy-exit", *client, guild_result::not_in_guild);
	}
	if (client->m_guild_rank == guild_rank::guildmaster)
	{
		return refuse("kennedy-exit", *client, guild_result::master_cannot_leave);
	}
	const int slot = m_game->m_item_manager->find_inventory_item(client_h,
		hb::shared::item::ItemId::GuildSecessionTicket);
	if (slot < 0)
	{
		// The original burned the ticket even when the ceremony was refused;
		// gating first is the mechanical fix — nothing is consumed unless
		// the exit happens.
		return refuse("kennedy-exit", *client, guild_result::no_ticket);
	}
	const int64_t guid = client->m_guild_guid;
	// §3.1.1: an Officer may self-exit, demoted to Guildsman on the way out —
	// the original's "rank 12 only" door, widened by one auto-demote step.
	if (client->m_guild_rank == guild_rank::officer)
	{
		if (!db->set_rank(client->m_char_name, guild_rank::guildsman))
		{
			return refuse("kennedy-exit", *client, guild_result::store_error);
		}
	}
	if (!db->remove_member(client->m_char_name))
	{
		refresh_client(client_h);   // the demotion may have landed; resync
		return refuse("kennedy-exit", *client, guild_result::store_error);
	}
	consume_ticket(client_h, slot);

	// The −300, clamped before subtracting: m_exp is unsigned, and the
	// original's post-subtraction "< 0" clamp is the wrap this avoids. No
	// level-down exists, exactly as in the original.
	if (client->m_exp < static_cast<uint32_t>(kennedy_secession_exp_penalty))
	{
		client->m_exp = 0;
	}
	else
	{
		client->m_exp -= kennedy_secession_exp_penalty;
	}
	m_game->send_notify_msg(0, client_h, Notify::Exp, 0, 0, 0, 0);

	clear_client(client_h);
	hb::logger::log<log_channel::guild>(
		"kennedy-exit: '{}' left guild {} (-{} exp)",
		client->m_char_name, guid, kennedy_secession_exp_penalty);
	return guild_result::ok;
}

//----------------------------------------------------------------------
// Moderation
//----------------------------------------------------------------------

int guild_manager::kick_member(int actor_h, const char* target_name)
{
	CClient* actor = client_at(actor_h);
	if (actor == nullptr || target_name == nullptr)
	{
		return guild_result::no_client;
	}
	guild_sqlite_store* db = store();
	if (db == nullptr)
	{
		return refuse("kick", *actor, guild_result::store_error);
	}
	if (actor->m_guild_guid == 0)
	{
		return refuse("kick", *actor, guild_result::not_in_guild);
	}
	if (!has_permission(actor_h, guild_permission::kick))
	{
		return refuse("kick", *actor, guild_result::no_permission);
	}
	guild_member_record target;
	if (!db->member_of(target_name, target)
		|| target.guild_guid != actor->m_guild_guid)
	{
		return refuse("kick", *actor, guild_result::target_not_found);
	}
	if (target.rank == guild_rank::guildmaster)
	{
		return refuse("kick", *actor, guild_result::target_is_master);
	}
	// Strictly below the actor's rank (ADR 0006's one sanctioned rank
	// comparison) — which is also what makes self-kick arithmetic nonsense,
	// where the original /ban let a master orphan their own guild.
	if (target.rank <= actor->m_guild_rank)
	{
		return refuse("kick", *actor, guild_result::target_not_below_rank);
	}
	if (!db->remove_member(target.char_name.c_str()))
	{
		return refuse("kick", *actor, guild_result::store_error);
	}
	// Cross-map and offline both work — the store row was the target. An
	// online target is cleared through the index, no client scan.
	const int target_h = find_online_member(actor->m_guild_guid,
		target.char_name.c_str());
	if (target_h >= 0)
	{
		clear_client(target_h);
	}
	hb::logger::log<log_channel::guild>(
		"kick: '{}' removed '{}' from guild {} ({})",
		actor->m_char_name, target.char_name, actor->m_guild_guid,
		target_h >= 0 ? "online" : "offline");
	return guild_result::ok;
}

int guild_manager::promote_member(int actor_h, const char* target_name)
{
	return move_rank(actor_h, target_name, guild_rank::guildsman,
		guild_rank::officer, "promote");
}

int guild_manager::demote_member(int actor_h, const char* target_name)
{
	return move_rank(actor_h, target_name, guild_rank::officer,
		guild_rank::guildsman, "demote");
}

int guild_manager::move_rank(int actor_h, const char* target_name,
	int from_rank, int to_rank, const char* verb)
{
	CClient* actor = client_at(actor_h);
	if (actor == nullptr || target_name == nullptr)
	{
		return guild_result::no_client;
	}
	guild_sqlite_store* db = store();
	if (db == nullptr)
	{
		return refuse(verb, *actor, guild_result::store_error);
	}
	if (actor->m_guild_guid == 0)
	{
		return refuse(verb, *actor, guild_result::not_in_guild);
	}
	if (!has_permission(actor_h, guild_permission::promote_demote))
	{
		return refuse(verb, *actor, guild_result::no_permission);
	}
	guild_member_record target;
	if (!db->member_of(target_name, target)
		|| target.guild_guid != actor->m_guild_guid)
	{
		return refuse(verb, *actor, guild_result::target_not_found);
	}
	if (target.rank == guild_rank::guildmaster)
	{
		return refuse(verb, *actor, guild_result::target_is_master);
	}
	if (target.rank != from_rank)
	{
		return refuse(verb, *actor, guild_result::bad_rank);
	}
	// The promote-side capacity rung (#122, §3.1.2 item 2): at most
	// officers(Guild Level) Officers, the master naturally outside the count
	// because guildmaster is not the officer rank.
	if (to_rank == guild_rank::officer)
	{
		guild_record guild;
		const hb::server::guild_level_row* row = curve_row(actor->m_guild_guid, guild);
		if (row == nullptr)
		{
			return refuse(verb, *actor, guild_result::store_error);
		}
		if (db->officer_count(guild.guid) >= row->officer_cap)
		{
			return refuse(verb, *actor, guild_result::officers_at_capacity);
		}
	}
	if (!db->set_rank(target.char_name.c_str(), to_rank))
	{
		return refuse(verb, *actor, guild_result::store_error);
	}
	// A rank move can strip Commander eligibility — the bit, never the rank
	// (ADR 0006): if the moved member holds Commander and the new rank's
	// mask lacks the bit, the duty releases with the rank (#122).
	{
		guild_title_record held;
		if (db->title_of(target.char_name.c_str(), held)
			&& held.kind == guild_title::commander)
		{
			uint32_t new_mask = 0;
			db->permission_mask(actor->m_guild_guid, to_rank, new_mask);
			if ((new_mask & guild_permission::commander_eligible) == 0)
			{
				db->release_title(target.char_name.c_str());
				hb::logger::log<log_channel::guild>(
					"title-release: '{}' lost commander (guild {}, rank moved"
					" below eligibility)", target.char_name, actor->m_guild_guid);
			}
		}
	}
	// An offline target's cache and mask catch up at their next hydration.
	refresh_client(find_online_member(actor->m_guild_guid,
		target.char_name.c_str()));
	hb::logger::log<log_channel::guild>(
		"{}: '{}' moved '{}' to rank {} (guild {})",
		verb, actor->m_char_name, target.char_name, to_rank,
		actor->m_guild_guid);
	return guild_result::ok;
}

int guild_manager::transfer_mastership(int master_h, const char* new_master_name)
{
	CClient* master = client_at(master_h);
	if (master == nullptr || new_master_name == nullptr)
	{
		return guild_result::no_client;
	}
	guild_sqlite_store* db = store();
	if (db == nullptr)
	{
		return refuse("transfer", *master, guild_result::store_error);
	}
	if (master->m_guild_guid == 0)
	{
		return refuse("transfer", *master, guild_result::not_in_guild);
	}
	// Hardcoded master-only, like disband (ADR 0006).
	if (master->m_guild_rank != guild_rank::guildmaster)
	{
		return refuse("transfer", *master, guild_result::not_master);
	}
	guild_member_record incoming;
	if (!db->member_of(new_master_name, incoming)
		|| incoming.guild_guid != master->m_guild_guid)
	{
		return refuse("transfer", *master, guild_result::target_not_found);
	}
	if (incoming.rank == guild_rank::guildmaster)
	{
		// Transferring to the sitting master — themselves — is a no-op ask.
		return refuse("transfer", *master, guild_result::target_is_master);
	}
	// The incoming master must be online: the CHR floor gate below reads live
	// stats, and a succession nobody is present for has no one to hold it.
	const int incoming_h = find_online_member(master->m_guild_guid,
		incoming.char_name.c_str());
	if (incoming_h < 0)
	{
		return refuse("transfer", *master, guild_result::target_offline);
	}
	CClient* incoming_client = client_at(incoming_h);
	if (incoming_client == nullptr)
	{
		return refuse("transfer", *master, guild_result::target_offline);
	}
	// The floor the master lives under (min_charisma) would be meaningless if
	// succession could install a master already beneath it.
	if (incoming_client->m_charisma < guildmaster_charisma_floor)
	{
		return refuse("transfer", *master, guild_result::charisma_too_low);
	}
	if (!db->transfer_mastership(master->m_guild_guid, incoming.char_name.c_str()))
	{
		return refuse("transfer", *master, guild_result::store_error);
	}
	// One transaction in the store; two cache refreshes here — the old master
	// is an Officer now, the incoming one holds the master mask.
	refresh_client(master_h);
	refresh_client(incoming_h);
	hb::logger::log<log_channel::guild>(
		"transfer: '{}' handed guild {} to '{}' (old master now officer)",
		master->m_char_name, master->m_guild_guid, incoming.char_name);
	return guild_result::ok;
}

//----------------------------------------------------------------------
// Progression: the Donation lanes and the level climb (#122)
//----------------------------------------------------------------------

int guild_manager::donate_gold(int client_h, int64_t amount)
{
	return donate(client_h, donation_lane::gold, amount);
}

int guild_manager::donate_enemy_kills(int client_h, int64_t amount)
{
	return donate(client_h, donation_lane::enemy_kills, amount);
}

int guild_manager::donate_contribution(int client_h, int64_t amount)
{
	return donate(client_h, donation_lane::contribution, amount);
}

int guild_manager::donate(int client_h, donation_lane lane, int64_t amount)
{
	static constexpr const char* verbs[] = {
		"donate-gold", "donate-ek", "donate-contribution" };
	const char* verb = verbs[static_cast<int>(lane)];

	CClient* client = client_at(client_h);
	if (client == nullptr)
	{
		return guild_result::no_client;
	}
	guild_sqlite_store* db = store();
	if (db == nullptr)
	{
		return refuse(verb, *client, guild_result::store_error);
	}
	const hb::server::guild_progression_config& config =
		m_game->get_guild_progression();
	if (!config.donations_enabled)
	{
		return refuse(verb, *client, guild_result::donations_disabled);
	}
	if (client->m_guild_guid == 0)
	{
		return refuse(verb, *client, guild_result::not_in_guild);
	}

	// The lane's minimum and the donor's live balance. m_contribution can
	// sit negative today (#129) — the balance gate refuses that donor the
	// same as any other shortfall.
	int64_t minimum = 0;
	int64_t balance = 0;
	switch (lane)
	{
	case donation_lane::gold:
		minimum = config.donation_min_gold;
		balance = static_cast<int64_t>(m_game->m_item_manager
			->get_item_count_by_id(client_h, hb::shared::item::ItemId::Gold));
		break;
	case donation_lane::enemy_kills:
		minimum = config.donation_min_ek;
		balance = client->m_enemy_kill_count;
		break;
	case donation_lane::contribution:
		minimum = config.donation_min_contribution;
		balance = client->m_contribution;
		break;
	}
	if (amount <= 0 || amount < minimum)
	{
		return refuse(verb, *client, guild_result::amount_too_small);
	}
	if (balance < amount)
	{
		return refuse(verb, *client, guild_result::insufficient_funds);
	}

	guild_record guild;
	if (!db->load_guild(client->m_guild_guid, guild))
	{
		return refuse(verb, *client, guild_result::store_error);
	}

	int64_t gold_delta = 0;
	int64_t ek_delta = 0;
	int64_t contribution_delta = 0;
	switch (lane)
	{
	case donation_lane::gold:         gold_delta = amount; break;
	case donation_lane::enemy_kills:  ek_delta = amount; break;
	case donation_lane::contribution: contribution_delta = amount; break;
	}
	const int new_level = level_after(config, guild, gold_delta, ek_delta,
		contribution_delta);

	// One writer for the lane's balance, so the burn and its rollback are the
	// same body and cannot drift apart. Gold moves as a Counted GuildDonation
	// outflow — the same booked door every other payment takes (and restoring
	// the count books nothing: set_item_count only records decreases, so a
	// refund mints no phantom inflow); the stat lanes are plain counters.
	auto set_balance = [&](int64_t value)
	{
		switch (lane)
		{
		case donation_lane::gold:
			m_game->m_item_manager->set_item_count_by_id(client_h,
				hb::shared::item::ItemId::Gold, static_cast<uint64_t>(value),
				ItemLogAction::GuildDonation);
			break;
		case donation_lane::enemy_kills:
			client->m_enemy_kill_count = static_cast<int>(value);
			break;
		case donation_lane::contribution:
			client->m_contribution = static_cast<int>(value);
			break;
		}
	};

	// Burn first, record second: a crash between the two loses the donor a
	// few points and never mints any (the store header's rule).
	set_balance(balance - amount);

	if (!db->record_donation(guild.guid, client->m_char_name, gold_delta,
		ek_delta, contribution_delta, new_level))
	{
		// In-process store failure: put the burn back.
		set_balance(balance);
		return refuse(verb, *client, guild_result::store_error);
	}

	hb::logger::log<log_channel::guild>(
		"{}: '{}' burned {} (guild {} '{}' now {}/{}/{}, level {})",
		verb, client->m_char_name, amount, guild.guid, guild.name,
		guild.burned_gold + gold_delta, guild.burned_ek + ek_delta,
		guild.burned_contribution + contribution_delta, new_level);

	if (new_level > guild.level)
	{
		// Every online member's cached level follows in one bucket walk; the
		// grants (caps, slots, repair discount) read the cache or the store,
		// so nothing else must be told. #123 gives this its wire shape.
		for (int member_h : online_members(guild.guid))
		{
			CClient* member = client_raw(member_h);
			if (member != nullptr)
			{
				member->m_guild_level = new_level;
			}
		}
		hb::logger::log<log_channel::guild>(
			"level-up: guild {} '{}' reached level {}",
			guild.guid, guild.name, new_level);
	}
	return guild_result::ok;
}

//----------------------------------------------------------------------
// Titles: the duty-slot machine (#122)
//----------------------------------------------------------------------

int guild_manager::claim_title(int client_h, int kind)
{
	CClient* client = client_at(client_h);
	if (client == nullptr)
	{
		return guild_result::no_client;
	}
	guild_sqlite_store* db = store();
	if (db == nullptr)
	{
		return refuse("title-claim", *client, guild_result::store_error);
	}
	if (!is_valid_guild_title(kind))
	{
		return refuse("title-claim", *client, guild_result::bad_title);
	}
	if (client->m_guild_guid == 0)
	{
		return refuse("title-claim", *client, guild_result::not_in_guild);
	}
	// Commander is the one gated claim (ADR 0006's commander_eligible bit,
	// Officer+ by default); Huntmaster/Raidmaster are any member's to take.
	if (kind == guild_title::commander
		&& !has_permission(client_h, guild_permission::commander_eligible))
	{
		return refuse("title-claim", *client, guild_result::no_permission);
	}
	{
		guild_title_record held;
		if (db->title_of(client->m_char_name, held))
		{
			return refuse("title-claim", *client, guild_result::already_titled);
		}
	}

	guild_record guild;
	const hb::server::guild_level_row* row = curve_row(client->m_guild_guid, guild);
	if (row == nullptr)
	{
		return refuse("title-claim", *client, guild_result::store_error);
	}
	int slot_limit = 0;
	switch (kind)
	{
	case guild_title::commander:  slot_limit = hb::server::guild_commander_slots; break;
	case guild_title::huntmaster: slot_limit = row->hunt_slots; break;
	case guild_title::raidmaster: slot_limit = row->raid_slots; break;
	}
	// First come, first served (§3.1.1): the only claim gate on a free slot
	// is being here first.
	const int held_of_kind = db->title_count(guild.guid, kind);
	if (held_of_kind >= slot_limit)
	{
		return refuse("title-claim", *client, guild_result::no_title_slot);
	}
	if (!db->claim_title(guild.guid, kind, client->m_char_name,
		hb::time::unix_now()))
	{
		return refuse("title-claim", *client, guild_result::store_error);
	}
	refresh_client(client_h);
	hb::logger::log<log_channel::guild>(
		"title-claim: '{}' took {} ({}/{} held, guild {} '{}')",
		client->m_char_name, guild_title_name(kind), held_of_kind + 1,
		slot_limit, guild.guid, guild.name);
	return guild_result::ok;
}

int guild_manager::drop_title(int client_h)
{
	CClient* client = client_at(client_h);
	if (client == nullptr)
	{
		return guild_result::no_client;
	}
	guild_sqlite_store* db = store();
	if (db == nullptr)
	{
		return refuse("title-drop", *client, guild_result::store_error);
	}
	if (client->m_guild_guid == 0)
	{
		return refuse("title-drop", *client, guild_result::not_in_guild);
	}
	guild_title_record held;
	if (!db->title_of(client->m_char_name, held))
	{
		return refuse("title-drop", *client, guild_result::no_title);
	}
	if (!db->release_title(client->m_char_name))
	{
		return refuse("title-drop", *client, guild_result::store_error);
	}
	refresh_client(client_h);
	hb::logger::log<log_channel::guild>(
		"title-drop: '{}' released {} (guild {})",
		client->m_char_name, guild_title_name(held.kind), held.guild_guid);
	return guild_result::ok;
}

int guild_manager::strip_title(int actor_h, const char* target_name)
{
	CClient* actor = client_at(actor_h);
	if (actor == nullptr || target_name == nullptr)
	{
		return guild_result::no_client;
	}
	guild_sqlite_store* db = store();
	if (db == nullptr)
	{
		return refuse("title-strip", *actor, guild_result::store_error);
	}
	if (actor->m_guild_guid == 0)
	{
		return refuse("title-strip", *actor, guild_result::not_in_guild);
	}
	if (!has_permission(actor_h, guild_permission::title_manage))
	{
		return refuse("title-strip", *actor, guild_result::no_permission);
	}
	guild_member_record target;
	if (!db->member_of(target_name, target)
		|| target.guild_guid != actor->m_guild_guid)
	{
		return refuse("title-strip", *actor, guild_result::target_not_found);
	}
	guild_title_record held;
	if (!db->title_of(target.char_name.c_str(), held))
	{
		return refuse("title-strip", *actor, guild_result::no_title);
	}
	if (!db->release_title(target.char_name.c_str()))
	{
		return refuse("title-strip", *actor, guild_result::store_error);
	}
	// The freed slot is claimable the same tick; an online target's cache
	// drops the Title now, an offline one's at next hydration.
	refresh_client(find_online_member(actor->m_guild_guid,
		target.char_name.c_str()));
	hb::logger::log<log_channel::guild>(
		"title-strip: '{}' took {} from '{}' (guild {})",
		actor->m_char_name, guild_title_name(held.kind), target.char_name,
		actor->m_guild_guid);
	return guild_result::ok;
}

void guild_manager::tick_titles(uint32_t now_ms)
{
	guild_sqlite_store* db = store();
	if (db == nullptr || m_game == nullptr)
	{
		return;
	}
	const int64_t inactivity_ms =
		m_game->get_guild_progression().title_inactivity_ms;
	const int64_t now_unix = hb::time::unix_now();

	for (const guild_title_record& title : db->all_titles())
	{
		const int holder_h = find_online_member(title.guild_guid,
			title.holder_name.c_str());
		bool release = false;
		if (holder_h >= 0)
		{
			// The holder plays: ride the same activity clock the AFK flag
			// reads. The signed cast makes a clock refreshed just after the
			// sweep snapshot read as slightly negative idle — which fails the
			// threshold like any other freshness — instead of wrapping huge.
			const CClient* holder = m_game->m_client_list[holder_h];
			const int64_t idle_ms = static_cast<int32_t>(
				now_ms - holder->m_afk_activity_time);
			if (idle_ms >= inactivity_ms)
			{
				release = true;
			}
			else
			{
				// Active: keep the persisted clock fresh so a logout (or a
				// crash) starts the offline countdown from the truth.
				db->touch_title(title.holder_name.c_str(), now_unix);
			}
		}
		else
		{
			// Offline: last_seen_at is the last active touch. Logout and
			// disconnect ARE inactivity (§3.1.2) — they just do not shortcut
			// the timer.
			if ((now_unix - title.last_seen_at) * 1000 >= inactivity_ms)
			{
				release = true;
			}
		}
		if (release)
		{
			db->release_title(title.holder_name.c_str());
			if (holder_h >= 0)
			{
				refresh_client(holder_h);
			}
			hb::logger::log<log_channel::guild>(
				"title-release: '{}' lost {} (guild {}, inactive)",
				title.holder_name, guild_title_name(title.kind),
				title.guild_guid);
		}
	}
}

//----------------------------------------------------------------------
// Treasury (#122): the wallet, never the altar
//----------------------------------------------------------------------

int guild_manager::treasury_deposit(int client_h, int64_t amount)
{
	CClient* client = client_at(client_h);
	if (client == nullptr)
	{
		return guild_result::no_client;
	}
	guild_sqlite_store* db = store();
	if (db == nullptr)
	{
		return refuse("treasury-deposit", *client, guild_result::store_error);
	}
	if (client->m_guild_guid == 0)
	{
		return refuse("treasury-deposit", *client, guild_result::not_in_guild);
	}
	if (amount <= 0)
	{
		return refuse("treasury-deposit", *client, guild_result::amount_too_small);
	}
	const int64_t balance = static_cast<int64_t>(m_game->m_item_manager
		->get_item_count_by_id(client_h, hb::shared::item::ItemId::Gold));
	if (balance < amount)
	{
		return refuse("treasury-deposit", *client, guild_result::insufficient_funds);
	}
	// Same crash rule as the altar: the player's gold leaves first (a booked
	// GuildTreasury outflow), the wallet credits second.
	m_game->m_item_manager->set_item_count_by_id(client_h,
		hb::shared::item::ItemId::Gold, static_cast<uint64_t>(balance - amount),
		ItemLogAction::GuildTreasury);
	if (!db->treasury_deposit(client->m_guild_guid, amount))
	{
		m_game->m_item_manager->set_item_count_by_id(client_h,
			hb::shared::item::ItemId::Gold, static_cast<uint64_t>(balance),
			ItemLogAction::GuildTreasury);
		return refuse("treasury-deposit", *client, guild_result::store_error);
	}
	hb::logger::log<log_channel::guild>(
		"treasury-deposit: '{}' stored {} gold (guild {}, wallet {})",
		client->m_char_name, amount, client->m_guild_guid,
		db->treasury_gold(client->m_guild_guid));
	return guild_result::ok;
}

int guild_manager::treasury_withdraw(int client_h, int64_t amount)
{
	CClient* client = client_at(client_h);
	if (client == nullptr)
	{
		return guild_result::no_client;
	}
	guild_sqlite_store* db = store();
	if (db == nullptr)
	{
		return refuse("treasury-withdraw", *client, guild_result::store_error);
	}
	if (client->m_guild_guid == 0)
	{
		return refuse("treasury-withdraw", *client, guild_result::not_in_guild);
	}
	if (!has_permission(client_h, guild_permission::treasury_withdraw))
	{
		return refuse("treasury-withdraw", *client, guild_result::no_permission);
	}
	if (amount <= 0)
	{
		return refuse("treasury-withdraw", *client, guild_result::amount_too_small);
	}
	const int64_t wallet = db->treasury_gold(client->m_guild_guid);
	if (wallet < amount)
	{
		return refuse("treasury-withdraw", *client, guild_result::treasury_short);
	}
	// The store's withdraw is the debit AND the log row in one transaction;
	// the grant delivers to the inventory or the ground at their feet.
	if (!db->treasury_withdraw(client->m_guild_guid, client->m_char_name,
		amount, hb::time::unix_now()))
	{
		return refuse("treasury-withdraw", *client, guild_result::store_error);
	}
	m_game->m_item_manager->grant_gold(client_h, static_cast<uint64_t>(amount));
	hb::logger::log<log_channel::guild>(
		"treasury-withdraw: '{}' took {} gold (guild {}, wallet {})",
		client->m_char_name, amount, client->m_guild_guid, wallet - amount);
	return guild_result::ok;
}

//----------------------------------------------------------------------
// Login / logout hooks and the index
//----------------------------------------------------------------------

void guild_manager::hydrate_login(int client_h)
{
	refresh_client(client_h);
	CClient* client = client_raw(client_h);
	if (client != nullptr && client->m_guild_guid != 0)
	{
		hb::logger::debug("guild: hydrated '{}' into guild {} (rank {})",
			client->m_char_name, client->m_guild_guid, client->m_guild_rank);
	}
}

void guild_manager::on_logout(int client_h)
{
	// delete_client calls this while the CClient is still alive; a client
	// that never hydrated (login-stage teardown) carries guid 0 and this is
	// the no-op it should be.
	CClient* client = client_raw(client_h);
	if (client == nullptr || client->m_guild_guid == 0)
	{
		return;
	}
	index_remove(client->m_guild_guid, client_h);
}

const std::vector<int>& guild_manager::online_members(int64_t guild_guid) const
{
	const auto it = m_online_by_guild.find(guild_guid);
	return it != m_online_by_guild.end() ? it->second : no_members;
}

bool guild_manager::has_permission(int client_h, uint32_t permission_bit) const
{
	const CClient* client = client_at(client_h);
	return client != nullptr && client->m_guild_guid != 0
		&& (client->m_guild_permission_mask & permission_bit) != 0;
}

int guild_manager::min_charisma(int client_h) const
{
	const CClient* client = client_at(client_h);
	if (client != nullptr && client->m_guild_guid != 0
		&& client->m_guild_rank == guild_rank::guildmaster)
	{
		return guildmaster_charisma_floor;
	}
	return 0;
}

//----------------------------------------------------------------------
// Internals
//----------------------------------------------------------------------

void guild_manager::refresh_client(int client_h)
{
	CClient* client = client_raw(client_h);
	if (client == nullptr)
	{
		return;
	}
	const int64_t old_guid = client->m_guild_guid;

	guild_sqlite_store* db = store();
	guild_member_record member;
	guild_record guild;
	if (db == nullptr
		|| !db->member_of(client->m_char_name, member)
		|| !db->load_guild(member.guild_guid, guild))
	{
		clear_client(client_h);
		return;
	}

	uint32_t mask = 0;
	db->permission_mask(guild.guid, member.rank, mask);

	client->m_guild_guid = guild.guid;
	std::snprintf(client->m_guild_name, sizeof(client->m_guild_name), "%s",
		guild.name.c_str());
	client->m_guild_rank = member.rank;
	client->m_guild_permission_mask = mask;
	client->m_guild_level = guild.level;
	guild_title_record held_title;
	client->m_guild_title = db->title_of(client->m_char_name, held_title)
		? held_title.kind : 0;

	if (old_guid != 0 && old_guid != guild.guid)
	{
		index_remove(old_guid, client_h);
	}
	// Unconditionally, not only on a guid change: a re-hydration after
	// on_logout carries the same cached guid but is absent from the index,
	// and index_add already refuses duplicates.
	index_add(guild.guid, client_h);
}

void guild_manager::clear_client(int client_h)
{
	CClient* client = client_raw(client_h);
	if (client == nullptr)
	{
		return;
	}
	if (client->m_guild_guid != 0)
	{
		index_remove(client->m_guild_guid, client_h);
	}
	client->m_guild_guid = 0;
	std::memset(client->m_guild_name, 0, sizeof(client->m_guild_name));
	client->m_guild_rank = guild_rank::none;
	client->m_guild_permission_mask = 0;
	client->m_guild_level = 0;
	client->m_guild_title = 0;
}

void guild_manager::index_add(int64_t guild_guid, int client_h)
{
	std::vector<int>& bucket = m_online_by_guild[guild_guid];
	for (int handle : bucket)
	{
		if (handle == client_h)
		{
			return;
		}
	}
	bucket.push_back(client_h);
}

void guild_manager::index_remove(int64_t guild_guid, int client_h)
{
	const auto it = m_online_by_guild.find(guild_guid);
	if (it == m_online_by_guild.end())
	{
		return;
	}
	std::erase(it->second, client_h);
	if (it->second.empty())
	{
		m_online_by_guild.erase(it);
	}
}

int guild_manager::find_online_member(int64_t guild_guid, const char* char_name) const
{
	for (int handle : online_members(guild_guid))
	{
		const CClient* client = m_game->m_client_list[handle];
		if (client != nullptr && hb_stricmp(client->m_char_name, char_name) == 0)
		{
			return handle;
		}
	}
	return -1;
}

void guild_manager::consume_ticket(int client_h, int slot)
{
	m_game->m_item_manager->item_deplete_handler(client_h,
		static_cast<short>(slot), false);
}

} // namespace hb::server
