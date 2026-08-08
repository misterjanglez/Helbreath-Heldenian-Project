// NetworkMessages_Guild.cpp: the guild wire family's server half (#123)
//
// The first Handlers/ domain file — the server-side mirror of the client's
// NetworkMessages_*.cpp pattern, registered on the Phase-1 seam instead of
// grown into msg_process's switch. Every handler is the same three moves:
// cast the typed request (PacketCast bounds it against msg_size), run the
// guild engine's entry point (GuildManager.h — the gate ladders live there,
// never here), and answer with the typed result envelope. State-change
// EVENTS are not sent from here: the engine broadcasts those from the
// mutation itself, so chat commands and the inactivity sweep announce
// exactly like wire requests (GuildManager.h, "#123 notify layer").
//
// Wire strings arrive as fixed arrays a hostile client controls end to end,
// so every one is re-terminated into a local buffer before the engine sees
// it — no handler passes req->name onward raw.
//
// The read-only queries (roster / queue / info / name) answer directly from
// the store and the caches. An unguilded asker gets the empty shape, not an
// error envelope: the panels that send these open for members, and an empty
// answer is what "no guild" looks like to them. The queue additionally
// requires a deciding permission bit — its contents are applications, not
// roster fact, and non-approvers have no business paging them.
//
//////////////////////////////////////////////////////////////////////

#include "PacketHandlerRegistry.h"
#include "NetworkMessages.h"

#include <vector>

#include "../Game.h"
#include "../GuildManager.h"
#include "../GuildProgressionConfig.h"
#include "../GuildSqliteStore.h"
#include "../StringCompat.h"
#include "Packet/PacketGuild.h"
#include "Packet/PacketHelpers.h"

using namespace hb::shared::net;
using namespace hb::shared::guild;
using hb::net::fill_wire_name;
using hb::net::make_notify;
using hb::net::terminate_wire_string;

namespace hb::server
{
namespace
{
	void send_action_result(CGame& game, int client_h, uint8_t action,
		int result, int64_t request_id = 0)
	{
		hb::net::PacketResponseGuildAction pkt{};
		pkt.header.msg_id = MsgId::ResponseGuildAction;
		pkt.action = action;
		pkt.result = static_cast<int32_t>(result);
		pkt.request_id = request_id;
		game.send_packet(client_h, pkt);
	}

	// --- Lifecycle -------------------------------------------------------

	void handle_create(CGame& game, int client_h, const char* data,
		std::size_t msg_size)
	{
		const auto* req = hb::net::PacketCast<hb::net::PacketRequestGuildCreate>(
			data, msg_size);
		if (req == nullptr) return;
		char name[hb::net::kGuildNameWireLen];
		terminate_wire_string(name, req->name);
		send_action_result(game, client_h, guild_wire_action::create,
			game.m_guild_manager->create_guild(client_h, name));
	}

	void handle_disband(CGame& game, int client_h, const char* data,
		std::size_t msg_size)
	{
		const auto* req = hb::net::PacketCast<hb::net::PacketRequestGuildDisband>(
			data, msg_size);
		if (req == nullptr) return;
		char typed_name[hb::net::kGuildNameWireLen];
		terminate_wire_string(typed_name, req->typed_name);
		send_action_result(game, client_h, guild_wire_action::disband,
			game.m_guild_manager->disband_guild(client_h, typed_name));
	}

	void handle_join(CGame& game, int client_h, const char* data,
		std::size_t msg_size)
	{
		const auto* req = hb::net::PacketCast<hb::net::PacketRequestGuildJoin>(
			data, msg_size);
		if (req == nullptr) return;
		char guild_name[hb::net::kGuildNameWireLen];
		terminate_wire_string(guild_name, req->guild_name);
		int64_t request_id = 0;
		const int result = game.m_guild_manager->file_join_request(client_h,
			guild_name, request_id);
		send_action_result(game, client_h, guild_wire_action::join, result,
			request_id);
	}

	void handle_leave(CGame& game, int client_h, const char*, std::size_t)
	{
		int64_t request_id = 0;
		const int result = game.m_guild_manager->file_leave_request(client_h,
			request_id);
		send_action_result(game, client_h, guild_wire_action::leave, result,
			request_id);
	}

	void handle_self_exit(CGame& game, int client_h, const char*, std::size_t)
	{
		send_action_result(game, client_h, guild_wire_action::self_exit,
			game.m_guild_manager->kennedy_self_exit(client_h));
	}

	void handle_decide(CGame& game, int client_h, const char* data,
		std::size_t msg_size)
	{
		const auto* req = hb::net::PacketCast<hb::net::PacketRequestGuildDecide>(
			data, msg_size);
		if (req == nullptr) return;
		send_action_result(game, client_h, guild_wire_action::decide,
			game.m_guild_manager->decide_request(client_h, req->request_id,
				req->approve != 0),
			req->request_id);
	}

	// --- Moderation (the shared by-name request shape) -------------------

	// One body for the five by-name actions: cast, terminate, run, answer.
	template <typename EntryPoint>
	void handle_member_action(CGame& game, int client_h, const char* data,
		std::size_t msg_size, uint8_t action, EntryPoint entry_point)
	{
		const auto* req = hb::net::PacketCast<hb::net::PacketRequestGuildMember>(
			data, msg_size);
		if (req == nullptr) return;
		char target[hb::shared::limits::CharNameLen];
		terminate_wire_string(target, req->target);
		send_action_result(game, client_h, action,
			(game.m_guild_manager.get()->*entry_point)(client_h, target));
	}

	void handle_kick(CGame& game, int client_h, const char* data,
		std::size_t msg_size)
	{
		handle_member_action(game, client_h, data, msg_size,
			guild_wire_action::kick, &guild_manager::kick_member);
	}

	void handle_promote(CGame& game, int client_h, const char* data,
		std::size_t msg_size)
	{
		handle_member_action(game, client_h, data, msg_size,
			guild_wire_action::promote, &guild_manager::promote_member);
	}

	void handle_demote(CGame& game, int client_h, const char* data,
		std::size_t msg_size)
	{
		handle_member_action(game, client_h, data, msg_size,
			guild_wire_action::demote, &guild_manager::demote_member);
	}

	void handle_transfer(CGame& game, int client_h, const char* data,
		std::size_t msg_size)
	{
		handle_member_action(game, client_h, data, msg_size,
			guild_wire_action::transfer, &guild_manager::transfer_mastership);
	}

	void handle_title_strip(CGame& game, int client_h, const char* data,
		std::size_t msg_size)
	{
		handle_member_action(game, client_h, data, msg_size,
			guild_wire_action::title_strip, &guild_manager::strip_title);
	}

	// --- Progression, Titles, Treasury -----------------------------------

	void handle_donate(CGame& game, int client_h, const char* data,
		std::size_t msg_size)
	{
		const auto* req = hb::net::PacketCast<hb::net::PacketRequestGuildDonate>(
			data, msg_size);
		if (req == nullptr) return;
		int result = guild_result::bad_request;
		switch (req->lane)
		{
		case guild_donation_lane::gold:
			result = game.m_guild_manager->donate_gold(client_h, req->amount);
			break;
		case guild_donation_lane::enemy_kills:
			result = game.m_guild_manager->donate_enemy_kills(client_h, req->amount);
			break;
		case guild_donation_lane::contribution:
			result = game.m_guild_manager->donate_contribution(client_h, req->amount);
			break;
		}
		send_action_result(game, client_h, guild_wire_action::donate, result);
	}

	void handle_treasury(CGame& game, int client_h, const char* data,
		std::size_t msg_size)
	{
		const auto* req = hb::net::PacketCast<hb::net::PacketRequestGuildTreasury>(
			data, msg_size);
		if (req == nullptr) return;
		switch (req->op)
		{
		case guild_treasury_op::deposit:
			send_action_result(game, client_h, guild_wire_action::treasury_deposit,
				game.m_guild_manager->treasury_deposit(client_h, req->amount));
			break;
		case guild_treasury_op::withdraw:
			send_action_result(game, client_h, guild_wire_action::treasury_withdraw,
				game.m_guild_manager->treasury_withdraw(client_h, req->amount));
			break;
		default:
			send_action_result(game, client_h, guild_wire_action::treasury_deposit,
				guild_result::bad_request);
			break;
		}
	}

	void handle_title_claim(CGame& game, int client_h, const char* data,
		std::size_t msg_size)
	{
		const auto* req = hb::net::PacketCast<hb::net::PacketRequestGuildTitleClaim>(
			data, msg_size);
		if (req == nullptr) return;
		send_action_result(game, client_h, guild_wire_action::title_claim,
			game.m_guild_manager->claim_title(client_h, req->kind));
	}

	void handle_title_drop(CGame& game, int client_h, const char*, std::size_t)
	{
		send_action_result(game, client_h, guild_wire_action::title_drop,
			game.m_guild_manager->drop_title(client_h));
	}

	// --- Read-only queries -----------------------------------------------

	void handle_roster(CGame& game, int client_h, const char*, std::size_t)
	{
		hb::net::PacketWriter writer;
		auto* head = writer.Append<hb::net::PacketResponseGuildRosterHeader>();
		head->header.msg_id = MsgId::ResponseGuildRoster;

		const CClient* client = game.m_client_list[client_h];
		guild_sqlite_store* store = game.m_guild_manager->store();
		const int64_t guid = client->m_guild_guid;
		if (guid != 0 && store != nullptr)
		{
			const std::vector<guild_member_record> members = store->roster(guid);
			const std::vector<guild_title_record> titles = store->titles(guid);

			// The online names, once — entries then scan this handful instead
			// of the index per row.
			std::vector<const char*> online_names;
			for (int handle : game.m_guild_manager->online_members(guid))
			{
				const CClient* member = game.m_client_list[handle];
				if (member != nullptr)
				{
					online_names.push_back(member->m_char_name);
				}
			}

			// head-> is written before the first entry Append — the writer
			// reallocates underneath pointers it handed out earlier.
			head->count = static_cast<uint16_t>(members.size());
			writer.Reserve(writer.size()
				+ members.size() * sizeof(hb::net::PacketResponseGuildRosterEntry));

			for (const guild_member_record& member : members)
			{
				auto* entry = writer.Append<hb::net::PacketResponseGuildRosterEntry>();
				fill_wire_name(entry->member, member.char_name.c_str());
				entry->rank = static_cast<int8_t>(member.rank);
				entry->title = 0;
				for (const guild_title_record& title : titles)
				{
					if (hb_stricmp(title.holder_name.c_str(),
						member.char_name.c_str()) == 0)
					{
						entry->title = static_cast<uint8_t>(title.kind);
						break;
					}
				}
				entry->online = 0;
				for (const char* online_name : online_names)
				{
					if (hb_stricmp(online_name, member.char_name.c_str()) == 0)
					{
						entry->online = 1;
						break;
					}
				}
				entry->donated_gold = member.donated_gold;
				entry->donated_ek = member.donated_ek;
				entry->donated_contribution = member.donated_contribution;
			}
		}
		game.send_packet_bytes(client_h, writer.Data(), writer.size());
	}

	void handle_queue(CGame& game, int client_h, const char*, std::size_t)
	{
		hb::net::PacketWriter writer;
		auto* head = writer.Append<hb::net::PacketResponseGuildQueueHeader>();
		head->header.msg_id = MsgId::ResponseGuildQueue;

		const CClient* client = game.m_client_list[client_h];
		guild_sqlite_store* store = game.m_guild_manager->store();
		const int64_t guid = client->m_guild_guid;
		const bool may_view = game.m_guild_manager->has_permission(client_h,
				deciding_bit_for(guild_request::join))
			|| game.m_guild_manager->has_permission(client_h,
				deciding_bit_for(guild_request::leave));
		if (guid != 0 && store != nullptr && may_view)
		{
			const std::vector<guild_request_record> pending =
				store->pending_requests(guid);
			const std::size_t count = pending.size() < hb::net::kGuildQueueMaxEntries
				? pending.size() : hb::net::kGuildQueueMaxEntries;
			head->count = static_cast<uint16_t>(count);
			head->total = static_cast<uint16_t>(pending.size());
			writer.Reserve(writer.size()
				+ count * sizeof(hb::net::PacketResponseGuildQueueEntry));
			for (std::size_t i = 0; i < count; i++)
			{
				auto* entry = writer.Append<hb::net::PacketResponseGuildQueueEntry>();
				entry->request_id = pending[i].request_id;
				entry->kind = static_cast<uint8_t>(pending[i].kind);
				fill_wire_name(entry->applicant, pending[i].char_name.c_str());
				entry->created_at = pending[i].created_at;
			}
		}
		game.send_packet_bytes(client_h, writer.Data(), writer.size());
	}

	void handle_info(CGame& game, int client_h, const char*, std::size_t)
	{
		hb::net::PacketResponseGuildInfo pkt{};
		pkt.header.msg_id = MsgId::ResponseGuildInfo;

		const CClient* client = game.m_client_list[client_h];
		guild_sqlite_store* store = game.m_guild_manager->store();
		guild_record guild;
		if (client->m_guild_guid != 0 && store != nullptr
			&& store->load_guild(client->m_guild_guid, guild))
		{
			const hb::server::guild_progression_config& config =
				game.get_guild_progression();
			fill_wire_name(pkt.guild_name, guild.name.c_str());
			fill_wire_name(pkt.master_name, guild.master_name.c_str());
			pkt.level = static_cast<uint16_t>(guild.level);
			pkt.member_count = static_cast<uint16_t>(store->member_count(guild.guid));
			pkt.officer_count = static_cast<uint16_t>(store->officer_count(guild.guid));
			pkt.commander_slots = static_cast<uint8_t>(guild_commander_slots);
			if (const guild_level_row* row = config.find_level(guild.level))
			{
				pkt.member_cap = static_cast<uint16_t>(row->member_cap);
				pkt.officer_cap = static_cast<uint16_t>(row->officer_cap);
				pkt.hunt_slots = static_cast<uint8_t>(row->hunt_slots);
				pkt.raid_slots = static_cast<uint8_t>(row->raid_slots);
				pkt.repair_pct = static_cast<uint8_t>(row->repair_pct);
			}
			pkt.donations_enabled = config.donations_enabled ? 1 : 0;
			pkt.treasury_gold = store->treasury_gold(guild.guid);
			pkt.burned_gold = guild.burned_gold;
			pkt.burned_ek = guild.burned_ek;
			pkt.burned_contribution = guild.burned_contribution;
			if (const guild_level_row* next = config.find_level(guild.level + 1))
			{
				pkt.next_gold = next->gold;
				pkt.next_ek = next->enemy_kills;
				pkt.next_contribution = next->contribution;
			}
			pkt.created_at = guild.created_at;
		}
		game.send_packet(client_h, pkt);
	}

	// The lazy name-cache answer (the restored 0x0BA6). Always answered —
	// an empty name for a dead handle or the unguilded settles the asking
	// slot instead of leaving it to re-ask every render.
	void handle_name(CGame& game, int client_h, const char* data,
		std::size_t msg_size)
	{
		const auto* req = hb::net::PacketCast<hb::net::PacketRequestGuildName>(
			data, msg_size);
		if (req == nullptr) return;

		auto pkt = make_notify<hb::net::PacketNotifyGuildName>(
			Notify::GuildNameAnswer);
		pkt.cache_index = req->cache_index;
		pkt.rank = guild_rank::none;

		const int object_id = req->object_id;
		if (object_id > 0 && object_id < config::MaxClients)
		{
			const CClient* target = game.m_client_list[object_id];
			if (target != nullptr && target->m_guild_guid != 0)
			{
				fill_wire_name(pkt.guild_name, target->m_guild_name);
				pkt.rank = static_cast<int8_t>(target->m_guild_rank);
				fill_wire_name(pkt.rank_title, game.m_guild_manager->rank_title_for(
					target->m_guild_guid, target->m_guild_rank));
				const int max_level = game.get_guild_progression().max_level();
				pkt.at_max_level =
					(max_level > 0 && target->m_guild_level >= max_level) ? 1 : 0;
			}
		}
		game.send_packet(client_h, pkt);
	}
}

void register_guild_packet_handlers(packet_handler_registry& registry)
{
	registry.register_handler(MsgId::RequestGuildCreate, handle_create);
	registry.register_handler(MsgId::RequestGuildDisband, handle_disband);
	registry.register_handler(MsgId::RequestGuildJoin, handle_join);
	registry.register_handler(MsgId::RequestGuildLeave, handle_leave);
	registry.register_handler(MsgId::RequestGuildSelfExit, handle_self_exit);
	registry.register_handler(MsgId::RequestGuildDecide, handle_decide);
	registry.register_handler(MsgId::RequestGuildKick, handle_kick);
	registry.register_handler(MsgId::RequestGuildPromote, handle_promote);
	registry.register_handler(MsgId::RequestGuildDemote, handle_demote);
	registry.register_handler(MsgId::RequestGuildTransfer, handle_transfer);
	registry.register_handler(MsgId::RequestGuildDonate, handle_donate);
	registry.register_handler(MsgId::RequestGuildTreasury, handle_treasury);
	registry.register_handler(MsgId::RequestGuildTitleClaim, handle_title_claim);
	registry.register_handler(MsgId::RequestGuildTitleDrop, handle_title_drop);
	registry.register_handler(MsgId::RequestGuildTitleStrip, handle_title_strip);
	registry.register_handler(MsgId::RequestGuildRoster, handle_roster);
	registry.register_handler(MsgId::RequestGuildQueue, handle_queue);
	registry.register_handler(MsgId::RequestGuildInfo, handle_info);
	registry.register_handler(MsgId::RequestGuildName, handle_name);
	// MsgId::RequestGuildInvite: reserved, deliberately unregistered (v1).
}
}
