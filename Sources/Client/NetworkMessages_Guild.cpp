// NetworkMessages_Guild.cpp: the guild wire family's client handlers (#123)
//
// The notify family lands here (event lines + cache upkeep), and the four
// responses land in the screen's guild_manager, which keeps the last answer
// for the #124 dialogs. Own-guild identity lives on CPlayer: seeded by the
// char-init contents, re-synced whenever Notify::GuildSelf arrives.
//
// Presentation is deliberately thin — add_event_list lines on the lan_eng
// strings. The guild UI suite (#124) replaces the surface, not the plumbing.
//
//////////////////////////////////////////////////////////////////////

#include <cstring>
#include <format>
#include <string>

#include "Game.h"
#include "GuildManager.h"
#include "Screen_OnGame.h"
#include "Packet/PacketGuild.h"
#include "Packet/PacketHelpers.h"
#include "lan_eng.h"

using namespace hb::shared::net;
using namespace hb::shared::guild;
using hb::net::wire_string;

namespace
{
	guild_manager* screen_guild_manager(CGame* game)
	{
		auto* screen = game->get_active_screen_as<Screen_OnGame>();
		return screen != nullptr ? &screen->get_guild_manager() : nullptr;
	}
}

namespace NetworkMessageHandlers {

void HandleGuildNameAnswer(CGame* game, char* data)
{
	const auto* pkt = hb::net::PacketCast<hb::net::PacketNotifyGuildName>(
		data, sizeof(hb::net::PacketNotifyGuildName));
	if (pkt == nullptr) return;
	if (guild_manager* manager = screen_guild_manager(game))
		manager->on_name_answer(*pkt);
}

void HandleGuildSelf(CGame* game, char* data)
{
	const auto* pkt = hb::net::PacketCast<hb::net::PacketNotifyGuildSelf>(
		data, sizeof(hb::net::PacketNotifyGuildSelf));
	if (pkt == nullptr) return;

	CPlayer* player = game->m_player;
	player->set_guild_snapshot(pkt->state);

	if (guild_manager* manager = screen_guild_manager(game))
	{
		// The own overhead line re-resolves against the new state; and if we
		// just turned up unguilded, the cached roster/queue/info describe a
		// guild we are no longer in.
		manager->invalidate(player->m_player_name.c_str());
		if (!player->in_guild())
			manager->drop_guild_answers();
	}
}

void HandleGuildJoined(CGame* game, char* data)
{
	const auto* pkt = hb::net::PacketCast<hb::net::PacketNotifyGuildJoined>(
		data, sizeof(hb::net::PacketNotifyGuildJoined));
	if (pkt == nullptr) return;
	const std::string member = wire_string(pkt->member);
	if (guild_manager* manager = screen_guild_manager(game))
		manager->invalidate(member.c_str());
	game->add_event_list(
		std::format(NOTIFYMSG_GUILD_JOINED, member).c_str(), 10);
}

void HandleGuildLeft(CGame* game, char* data)
{
	const auto* pkt = hb::net::PacketCast<hb::net::PacketNotifyGuildLeft>(
		data, sizeof(hb::net::PacketNotifyGuildLeft));
	if (pkt == nullptr) return;
	const std::string member = wire_string(pkt->member);
	if (guild_manager* manager = screen_guild_manager(game))
		manager->invalidate(member.c_str());
	const char* line = (pkt->reason == guild_leave_reason::kicked)
		? NOTIFYMSG_GUILD_KICKED : NOTIFYMSG_GUILD_LEFT;
	game->add_event_list(std::vformat(line,
		std::make_format_args(member)).c_str(), 10);
}

void HandleGuildDisbanded(CGame* game, char* data)
{
	const auto* pkt = hb::net::PacketCast<hb::net::PacketNotifyGuildDisbanded>(
		data, sizeof(hb::net::PacketNotifyGuildDisbanded));
	if (pkt == nullptr) return;
	const std::string name = wire_string(pkt->guild_name);
	game->add_event_list(
		std::format(NOTIFYMSG_GUILD_DISBANDED, name).c_str(), 10);
	// The own-state wipe arrives as GuildSelf right behind this event.
}

void HandleGuildRankChanged(CGame* game, char* data)
{
	const auto* pkt = hb::net::PacketCast<hb::net::PacketNotifyGuildRankChanged>(
		data, sizeof(hb::net::PacketNotifyGuildRankChanged));
	if (pkt == nullptr) return;
	const std::string member = wire_string(pkt->member);
	const std::string title = wire_string(pkt->rank_title);
	if (guild_manager* manager = screen_guild_manager(game))
		manager->invalidate(member.c_str());
	game->add_event_list(
		std::format(NOTIFYMSG_GUILD_RANK_CHANGED, member, title).c_str(), 10);
}

void HandleGuildTitleChanged(CGame* game, char* data)
{
	const auto* pkt = hb::net::PacketCast<hb::net::PacketNotifyGuildTitleChanged>(
		data, sizeof(hb::net::PacketNotifyGuildTitleChanged));
	if (pkt == nullptr) return;
	const std::string member = wire_string(pkt->member);
	const char* title = guild_title_display_name(pkt->kind);
	if (pkt->held != 0)
		game->add_event_list(
			std::format(NOTIFYMSG_GUILD_TITLE_CLAIMED, member, title).c_str(), 10);
	else
		game->add_event_list(
			std::format(NOTIFYMSG_GUILD_TITLE_RELEASED, member, title).c_str(), 10);
}

void HandleGuildLevelUp(CGame* game, char* data)
{
	const auto* pkt = hb::net::PacketCast<hb::net::PacketNotifyGuildLevelUp>(
		data, sizeof(hb::net::PacketNotifyGuildLevelUp));
	if (pkt == nullptr) return;
	game->m_player->m_guild_level = pkt->new_level;
	const std::string line =
		std::format(NOTIFYMSG_GUILD_LEVEL_UP, pkt->new_level);
	game->add_event_list(line.c_str(), 10);
	game->set_top_msg(line.c_str(), 5);
}

void HandleGuildRequestQueued(CGame* game, char* data)
{
	const auto* pkt = hb::net::PacketCast<hb::net::PacketNotifyGuildRequestQueued>(
		data, sizeof(hb::net::PacketNotifyGuildRequestQueued));
	if (pkt == nullptr) return;
	const std::string applicant =
		wire_string(pkt->applicant);
	const char* line = (pkt->kind == guild_request::join)
		? NOTIFYMSG_GUILD_APPLY_JOIN : NOTIFYMSG_GUILD_APPLY_LEAVE;
	game->add_event_list(std::vformat(line,
		std::make_format_args(applicant)).c_str(), 10);
}

void HandleGuildRequestDecided(CGame* game, char* data)
{
	const auto* pkt = hb::net::PacketCast<hb::net::PacketNotifyGuildRequestDecided>(
		data, sizeof(hb::net::PacketNotifyGuildRequestDecided));
	if (pkt == nullptr) return;
	const std::string guild = wire_string(pkt->guild_name);
	const char* line = (pkt->approved != 0)
		? NOTIFYMSG_GUILD_REQ_APPROVED : NOTIFYMSG_GUILD_REQ_REJECTED;
	game->add_event_list(std::vformat(line,
		std::make_format_args(guild)).c_str(), 10);
}

void HandleGuildActionResult(CGame* game, char* data)
{
	const auto* pkt = hb::net::PacketCast<hb::net::PacketResponseGuildAction>(
		data, sizeof(hb::net::PacketResponseGuildAction));
	if (pkt == nullptr) return;

	if (pkt->result != guild_result::ok)
	{
		game->add_event_list(std::format(NOTIFYMSG_GUILD_ACTION_FAILED,
			guild_result_name(pkt->result)).c_str(), 10);
		return;
	}
	// Filing is the one ok with no matching event notify to speak for it.
	// Panel refresh policy is the #124 dialogs' business (re-request on open
	// / on this envelope), not a which-actions-dirty-what list here.
	if (pkt->action == guild_wire_action::join
		|| pkt->action == guild_wire_action::leave)
	{
		game->add_event_list(NOTIFYMSG_GUILD_REQUEST_FILED, 10);
	}
}

void HandleGuildRoster(CGame* game, char* data, uint32_t msg_size)
{
	if (guild_manager* manager = screen_guild_manager(game))
		manager->on_roster_response(data, msg_size);
}

void HandleGuildQueue(CGame* game, char* data, uint32_t msg_size)
{
	if (guild_manager* manager = screen_guild_manager(game))
		manager->on_queue_response(data, msg_size);
}

void HandleGuildInfo(CGame* game, char* data)
{
	const auto* pkt = hb::net::PacketCast<hb::net::PacketResponseGuildInfo>(
		data, sizeof(hb::net::PacketResponseGuildInfo));
	if (pkt == nullptr) return;
	if (guild_manager* manager = screen_guild_manager(game))
		manager->on_info_response(*pkt);
}

} // namespace NetworkMessageHandlers
