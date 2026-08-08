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

	// Handlers say WHAT changed by marking the cached answer stale; whether
	// anyone re-requests is the dialogs' business (on_update when open and
	// stale, on_enable unconditionally) — panel refresh policy stays out of
	// the network layer, the same inversion the name cache's invalidate()
	// already practices. Membership and Title notifies reach every online
	// member; the queue has no broadcast, so only the filing notify and the
	// acting approver's decide envelope mark it — co-approvers keep the
	// Refresh button.
	void mark_membership_stale(CGame* game, bool with_info)
	{
		if (guild_manager* manager = screen_guild_manager(game))
		{
			manager->mark_roster_stale();
			if (with_info)
				manager->mark_info_stale();
		}
	}

	// The player-facing line for a refused action (#124); unmapped codes
	// fall back to the raw name so a new server gate is never invisible.
	std::string guild_result_text(int result)
	{
		switch (result)
		{
		case guild_result::crusade_active:        return UI_GUILD_R_CRUSADE;
		case guild_result::already_in_guild:      return UI_GUILD_R_ALREADY_IN;
		case guild_result::not_in_guild:          return UI_GUILD_R_NOT_IN;
		case guild_result::bad_name:              return UI_GUILD_R_BAD_NAME;
		case guild_result::name_taken:            return DRAW_DIALOGBOX_GUILDMENU23;
		case guild_result::name_mismatch:         return UI_GUILD_R_NAME_MISMATCH;
		case guild_result::level_too_low:
			return std::format(UI_GUILD_R_LEVEL, guild_create_min_level);
		case guild_result::charisma_too_low:
			return std::format(UI_GUILD_R_CHARISMA, guild_create_min_charisma);
		case guild_result::not_citizen:           return UI_GUILD_R_NOT_CITIZEN;
		case guild_result::not_in_own_town:       return UI_GUILD_R_NOT_OWN_TOWN;
		case guild_result::wrong_side:            return UI_GUILD_R_WRONG_SIDE;
		case guild_result::no_ticket:             return UI_GUILD_R_NO_TICKET;
		case guild_result::request_pending:       return UI_GUILD_R_PENDING;
		case guild_result::unknown_guild:         return UI_GUILD_R_UNKNOWN_GUILD;
		case guild_result::unknown_request:       return UI_GUILD_R_UNKNOWN_REQUEST;
		case guild_result::no_permission:         return UI_GUILD_R_NO_PERMISSION;
		case guild_result::not_master:            return UI_GUILD_R_NOT_MASTER;
		case guild_result::master_cannot_leave:   return UI_GUILD_R_MASTER_LEAVE;
		case guild_result::target_not_found:      return UI_GUILD_R_TARGET_MISSING;
		case guild_result::target_is_master:      return UI_GUILD_R_TARGET_MASTER;
		case guild_result::target_not_below_rank: return UI_GUILD_R_TARGET_RANK;
		case guild_result::target_offline:        return UI_GUILD_R_TARGET_OFFLINE;
		case guild_result::bad_rank:              return UI_GUILD_R_BAD_RANK;
		case guild_result::donations_disabled:    return UI_GUILD_R_DONATE_OFF;
		case guild_result::amount_too_small:      return UI_GUILD_R_AMOUNT_SMALL;
		case guild_result::insufficient_funds:    return UI_GUILD_R_NO_FUNDS;
		case guild_result::guild_full:            return UI_GUILD_R_GUILD_FULL;
		case guild_result::officers_at_capacity:  return UI_GUILD_R_OFFICERS_FULL;
		case guild_result::already_titled:        return UI_GUILD_R_ALREADY_TITLED;
		case guild_result::no_title_slot:         return UI_GUILD_R_NO_TITLE_SLOT;
		case guild_result::no_title:              return UI_GUILD_R_NO_TITLE;
		case guild_result::treasury_short:        return UI_GUILD_R_TREASURY_SHORT;
		default:
			return std::format(NOTIFYMSG_GUILD_ACTION_FAILED,
				guild_result_name(result));
		}
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
	mark_membership_stale(game, true);
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
	mark_membership_stale(game, true);
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
	mark_membership_stale(game, false);
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
	mark_membership_stale(game, false);
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
	// Caps, slot counts and next-level thresholds all just moved.
	mark_membership_stale(game, true);
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
	// This notify reaches exactly the approvers; one with the queue open
	// sees the new row arrive without touching Refresh.
	if (guild_manager* manager = screen_guild_manager(game))
		manager->mark_queue_stale();
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
		game->add_event_list(guild_result_text(pkt->result).c_str(), 10);
		return;
	}
	// Filing is the one ok with no matching event notify to speak for it.
	if (pkt->action == guild_wire_action::join
		|| pkt->action == guild_wire_action::leave)
	{
		game->add_event_list(NOTIFYMSG_GUILD_REQUEST_FILED, 10);
	}
	// The two answer families without a broadcast: the queue (only the
	// acting approver knows it shrank — an approved join's roster/info churn
	// arrives as the GuildJoined/GuildLeft broadcast, so decide marks only
	// the queue) and the info counters the Donate / Treasury panels show
	// (burns and deposits move them for the actor right now).
	guild_manager* manager = screen_guild_manager(game);
	if (manager == nullptr)
		return;
	if (pkt->action == guild_wire_action::decide)
	{
		manager->mark_queue_stale();
	}
	else if (pkt->action == guild_wire_action::donate
		|| pkt->action == guild_wire_action::treasury_deposit
		|| pkt->action == guild_wire_action::treasury_withdraw)
	{
		manager->mark_info_stale();
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
