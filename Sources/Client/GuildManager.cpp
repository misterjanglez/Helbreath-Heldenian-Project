// GuildManager.cpp (client): name cache mechanics + the request senders.
// See the header for the split; NetworkMessages_Guild.cpp owns turning
// answers into calls on this class.
//
//////////////////////////////////////////////////////////////////////

#include "GuildManager.h"

#include <cstring>

#include "Game.h"
#include "Packet/PacketHelpers.h"

using namespace hb::shared::net;
using hb::net::fill_wire_name;
using hb::net::terminate_wire_string;

void guild_manager::reset()
{
	for (cache_entry& entry : m_entries)
	{
		entry = {};
	}
	drop_guild_answers();
}

//----------------------------------------------------------------------
// The name cache
//----------------------------------------------------------------------

guild_manager::cache_entry* guild_manager::find_entry(const char* char_name)
{
	for (cache_entry& entry : m_entries)
	{
		if (entry.char_name[0] != '\0'
			&& std::strcmp(entry.char_name, char_name) == 0)
		{
			return &entry;
		}
	}
	return nullptr;
}

guild_manager::cache_entry* guild_manager::claim_slot(const char* char_name)
{
	cache_entry* stalest = &m_entries[0];
	for (cache_entry& entry : m_entries)
	{
		if (entry.ref_time_ms < stalest->ref_time_ms)
		{
			stalest = &entry;
		}
	}
	*stalest = {};
	fill_wire_name(stalest->char_name, char_name);
	return stalest;
}

const guild_manager::cache_entry* guild_manager::lookup(const char* char_name,
	int object_id, uint32_t now_ms)
{
	if (char_name == nullptr || char_name[0] == '\0')
	{
		return nullptr;
	}
	cache_entry* entry = find_entry(char_name);
	if (entry == nullptr)
	{
		entry = claim_slot(char_name);
	}
	entry->ref_time_ms = now_ms;
	if (entry->answered)
	{
		return entry;
	}
	if (entry->request_time_ms == 0
		|| now_ms - entry->request_time_ms >= retry_interval_ms)
	{
		entry->request_time_ms = now_ms;
		send_name_request(object_id, static_cast<int>(entry - m_entries));
	}
	return nullptr;
}

void guild_manager::invalidate(const char* char_name)
{
	if (char_name == nullptr)
	{
		return;
	}
	cache_entry* entry = find_entry(char_name);
	if (entry != nullptr)
	{
		*entry = {};
	}
}

void guild_manager::on_name_answer(const hb::net::PacketNotifyGuildName& pkt)
{
	if (pkt.cache_index < 0 || pkt.cache_index >= max_entries)
	{
		return;
	}
	cache_entry& entry = m_entries[pkt.cache_index];
	// A slot the LRU re-keyed while this answer was in flight keeps its new
	// key; the answer still lands (the original's behavior) and the newer
	// request's own answer overwrites it moments later.
	terminate_wire_string(entry.guild_name, pkt.guild_name);
	terminate_wire_string(entry.rank_title, pkt.rank_title);
	entry.rank = pkt.rank;
	entry.at_max_level = pkt.at_max_level;
	entry.answered = true;
}

//----------------------------------------------------------------------
// Cached answers
//----------------------------------------------------------------------

void guild_manager::on_roster_response(const char* data, uint32_t msg_size)
{
	m_roster.clear();
	const auto* head = hb::net::PacketCast<hb::net::PacketResponseGuildRosterHeader>(
		data, msg_size);
	if (head == nullptr)
	{
		return;
	}
	m_roster.reserve(head->count);
	const char* cursor = data + sizeof(*head);
	std::size_t remaining = msg_size - sizeof(*head);
	for (uint16_t i = 0; i < head->count; i++)
	{
		const auto* entry = hb::net::PacketCast<hb::net::PacketResponseGuildRosterEntry>(
			cursor, remaining);
		if (entry == nullptr)
		{
			break;
		}
		roster_row& row = m_roster.emplace_back(*entry);
		row.member[sizeof(row.member) - 1] = '\0';
		cursor += sizeof(*entry);
		remaining -= sizeof(*entry);
	}
}

void guild_manager::on_queue_response(const char* data, uint32_t msg_size)
{
	m_queue.clear();
	m_queue_total = 0;
	const auto* head = hb::net::PacketCast<hb::net::PacketResponseGuildQueueHeader>(
		data, msg_size);
	if (head == nullptr)
	{
		return;
	}
	m_queue_total = head->total;
	m_queue.reserve(head->count);
	const char* cursor = data + sizeof(*head);
	std::size_t remaining = msg_size - sizeof(*head);
	for (uint16_t i = 0; i < head->count; i++)
	{
		const auto* entry = hb::net::PacketCast<hb::net::PacketResponseGuildQueueEntry>(
			cursor, remaining);
		if (entry == nullptr)
		{
			break;
		}
		queue_row& row = m_queue.emplace_back(*entry);
		row.applicant[sizeof(row.applicant) - 1] = '\0';
		cursor += sizeof(*entry);
		remaining -= sizeof(*entry);
	}
}

void guild_manager::on_info_response(const hb::net::PacketResponseGuildInfo& pkt)
{
	m_info = pkt;
	m_info.guild_name[sizeof(m_info.guild_name) - 1] = '\0';
	m_info.master_name[sizeof(m_info.master_name) - 1] = '\0';
	m_info_valid = true;
}

//----------------------------------------------------------------------
// Request senders
//----------------------------------------------------------------------

void guild_manager::send_create(const char* name)
{
	hb::net::PacketRequestGuildCreate pkt{};
	pkt.header.msg_id = MsgId::RequestGuildCreate;
	fill_wire_name(pkt.name, name);
	m_game->send_game_packet(pkt);
}

void guild_manager::send_disband(const char* typed_name)
{
	hb::net::PacketRequestGuildDisband pkt{};
	pkt.header.msg_id = MsgId::RequestGuildDisband;
	fill_wire_name(pkt.typed_name, typed_name);
	m_game->send_game_packet(pkt);
}

void guild_manager::send_join(const char* guild_name)
{
	hb::net::PacketRequestGuildJoin pkt{};
	pkt.header.msg_id = MsgId::RequestGuildJoin;
	fill_wire_name(pkt.guild_name, guild_name);
	m_game->send_game_packet(pkt);
}

void guild_manager::send_leave()
{
	hb::net::PacketRequestGuildLeave pkt{};
	pkt.header.msg_id = MsgId::RequestGuildLeave;
	m_game->send_game_packet(pkt);
}

void guild_manager::send_self_exit()
{
	hb::net::PacketRequestGuildSelfExit pkt{};
	pkt.header.msg_id = MsgId::RequestGuildSelfExit;
	m_game->send_game_packet(pkt);
}

void guild_manager::send_decide(int64_t request_id, bool approve)
{
	hb::net::PacketRequestGuildDecide pkt{};
	pkt.header.msg_id = MsgId::RequestGuildDecide;
	pkt.request_id = request_id;
	pkt.approve = approve ? 1 : 0;
	m_game->send_game_packet(pkt);
}

void guild_manager::send_member_request(uint32_t msg_id, const char* target)
{
	hb::net::PacketRequestGuildMember pkt{};
	pkt.header.msg_id = msg_id;
	fill_wire_name(pkt.target, target);
	m_game->send_game_packet(pkt);
}

void guild_manager::send_kick(const char* target)
{
	send_member_request(MsgId::RequestGuildKick, target);
}

void guild_manager::send_promote(const char* target)
{
	send_member_request(MsgId::RequestGuildPromote, target);
}

void guild_manager::send_demote(const char* target)
{
	send_member_request(MsgId::RequestGuildDemote, target);
}

void guild_manager::send_transfer(const char* target)
{
	send_member_request(MsgId::RequestGuildTransfer, target);
}

void guild_manager::send_title_strip(const char* target)
{
	send_member_request(MsgId::RequestGuildTitleStrip, target);
}

void guild_manager::send_donate(uint8_t lane, int64_t amount)
{
	hb::net::PacketRequestGuildDonate pkt{};
	pkt.header.msg_id = MsgId::RequestGuildDonate;
	pkt.lane = lane;
	pkt.amount = amount;
	m_game->send_game_packet(pkt);
}

void guild_manager::send_treasury(uint8_t op, int64_t amount)
{
	hb::net::PacketRequestGuildTreasury pkt{};
	pkt.header.msg_id = MsgId::RequestGuildTreasury;
	pkt.op = op;
	pkt.amount = amount;
	m_game->send_game_packet(pkt);
}

void guild_manager::send_title_claim(uint8_t kind)
{
	hb::net::PacketRequestGuildTitleClaim pkt{};
	pkt.header.msg_id = MsgId::RequestGuildTitleClaim;
	pkt.kind = kind;
	m_game->send_game_packet(pkt);
}

void guild_manager::send_title_drop()
{
	hb::net::PacketRequestGuildTitleDrop pkt{};
	pkt.header.msg_id = MsgId::RequestGuildTitleDrop;
	m_game->send_game_packet(pkt);
}

void guild_manager::send_roster_request()
{
	hb::net::PacketRequestGuildRoster pkt{};
	pkt.header.msg_id = MsgId::RequestGuildRoster;
	m_game->send_game_packet(pkt);
}

void guild_manager::send_queue_request()
{
	hb::net::PacketRequestGuildQueue pkt{};
	pkt.header.msg_id = MsgId::RequestGuildQueue;
	m_game->send_game_packet(pkt);
}

void guild_manager::send_info_request()
{
	hb::net::PacketRequestGuildInfo pkt{};
	pkt.header.msg_id = MsgId::RequestGuildInfo;
	m_game->send_game_packet(pkt);
}

void guild_manager::send_name_request(int object_id, int cache_index)
{
	hb::net::PacketRequestGuildName pkt{};
	pkt.header.msg_id = MsgId::RequestGuildName;
	pkt.object_id = static_cast<int16_t>(object_id);
	pkt.cache_index = static_cast<int16_t>(cache_index);
	m_game->send_game_packet(pkt);
}
