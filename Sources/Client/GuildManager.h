// GuildManager.h (client): the guild wire family's client half (#123)
//
// Two jobs, both protocol plumbing rather than UI (the dialogs are #124):
//
//   - The lazy name cache the overhead line reads: the original 100-slot
//     LRU keyed by character name, rebuilt typed. A render-path lookup that
//     misses claims the stalest slot and asks the server once; the answer
//     lands by the slot index the request carried. Slow to re-ask (a few
//     seconds) rather than per-frame, and invalidated by the guild event
//     notifies so a kick or promote redraws without waiting out the LRU.
//
//   - The request senders and the last received roster/queue/info answers —
//     the complete client API #124's dialogs sit on. Answers are cached
//     here so a dialog opens on data it already has and refreshes in place
//     when the next response lands.
//
// Own-guild identity (name, rank, title, level, permissions) is NOT here —
// that lives on CPlayer, filled by the char-init contents and re-synced by
// Notify::GuildSelf.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <vector>

#include "GameConstants.h"
#include "Packet/PacketGuild.h"

class CGame;

class guild_manager
{
public:
	// The original DEF_MAXGUILDNAMES.
	static constexpr int max_entries = game_limits::max_guild_names;

	// How long an unanswered request holds before the lookup may re-ask.
	static constexpr uint32_t retry_interval_ms = 3000;

	// Default member initializers make `entry = {}` the whole story of an
	// empty slot — rank none is not zero, and a clearing site that forgot
	// that would quietly mint guildmasters.
	struct cache_entry
	{
		char char_name[hb::shared::limits::CharNameLen]{};
		char guild_name[hb::net::kGuildNameWireLen]{};
		char rank_title[hb::net::kGuildRankTitleWireLen]{};
		int8_t rank = hb::shared::guild::guild_rank::none;
		uint8_t at_max_level = 0;
		bool answered = false;   // false while the request is in flight
		uint32_t ref_time_ms = 0;    // LRU stamp
		uint32_t request_time_ms = 0;

		bool guilded() const { return answered && guild_name[0] != '\0'; }
	};

	guild_manager() = default;

	void set_game(CGame* game) { m_game = game; }

	// Back to the freshly-connected shape: cache emptied, cached answers
	// dropped. Called when the gameplay screen (re)initializes.
	void reset();

	// The render-path lookup (the original FindGuildName + request flow):
	// the answered entry for `char_name`, or nullptr while unknown — in
	// which case the request is fired (or re-fired after the retry
	// interval) using `object_id`, the asked-about character's live object
	// id. Never sends more than one request per entry per interval.
	const cache_entry* lookup(const char* char_name, int object_id,
		uint32_t now_ms);

	// Forget one character's entry (guild event notifies call this — a
	// kicked or promoted member re-resolves on next sight).
	void invalidate(const char* char_name);

	void on_name_answer(const hb::net::PacketNotifyGuildName& pkt);

	// --- Last received answers (the #124 dialogs' data) -----------------
	// Stored as the wire structs themselves (name fields re-terminated on
	// receipt, the m_info pattern) — no shadow types to keep in lockstep.

	using roster_row = hb::net::PacketResponseGuildRosterEntry;
	using queue_row = hb::net::PacketResponseGuildQueueEntry;

	const std::vector<roster_row>& roster() const { return m_roster; }
	const std::vector<queue_row>& queue() const { return m_queue; }
	uint16_t queue_total() const { return m_queue_total; }
	const hb::net::PacketResponseGuildInfo& info() const { return m_info; }
	bool info_valid() const { return m_info_valid; }

	void on_roster_response(const char* data, uint32_t msg_size);
	void on_queue_response(const char* data, uint32_t msg_size);
	void on_info_response(const hb::net::PacketResponseGuildInfo& pkt);

	// Drop the cached answers without touching the name cache — the
	// GuildSelf handler calls this when the player turns up unguilded
	// (kicked, left, disbanded): the roster of a guild you are no longer in
	// is not data, and the next membership re-requests fresh.
	void drop_guild_answers()
	{
		m_roster.clear();
		m_queue.clear();
		m_queue_total = 0;
		m_info = {};
		m_info_valid = false;
	}

	// --- Request senders (one per wire pair; #124 calls these) ----------

	void send_create(const char* name);
	void send_disband(const char* typed_name);
	void send_join(const char* guild_name);
	void send_leave();
	void send_self_exit();
	void send_decide(int64_t request_id, bool approve);
	void send_kick(const char* target);
	void send_promote(const char* target);
	void send_demote(const char* target);
	void send_transfer(const char* target);
	void send_donate(uint8_t lane, int64_t amount);
	void send_treasury(uint8_t op, int64_t amount);
	void send_title_claim(uint8_t kind);
	void send_title_drop();
	void send_title_strip(const char* target);
	void send_roster_request();
	void send_queue_request();
	void send_info_request();
	void send_name_request(int object_id, int cache_index);

private:
	cache_entry* find_entry(const char* char_name);

	// The stalest slot, cleared and re-keyed — the original's eviction.
	// (lookup stamps the LRU time on every path, this included.)
	cache_entry* claim_slot(const char* char_name);

	void send_member_request(uint32_t msg_id, const char* target);

	CGame* m_game = nullptr;
	cache_entry m_entries[max_entries];

	std::vector<roster_row> m_roster;
	std::vector<queue_row> m_queue;
	uint16_t m_queue_total = 0;
	hb::net::PacketResponseGuildInfo m_info{};
	bool m_info_valid = false;
};
