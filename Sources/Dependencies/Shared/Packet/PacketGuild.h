// PacketGuild.h: the guild wire family (#123, plan §3.1/§3.1.1 + Appendix A)
//
// The rebuild of the family the 506f7f1 gut removed, re-allocated instead of
// transcribed: every request is a typed struct on its own top-level msg id
// (no CommonType v1..v4+text arms), server events ride the Notify channel,
// and the whole family dispatches through the Phase-1 seams — the server's
// packet_handler_registry inbound, CGame::send_packet outbound. The
// original's two id collisions (0x0A25, 0x0EA03206) cannot recur because
// none of these ids live in those ranges; the one deliberately faithful
// number is the name-cache answer on the original's 0x0BA6.
//
// Mutation requests are answered twice: a PacketResponseGuildAction envelope
// to the actor (which request, which guild_result), and — when state actually
// changed — the matching event notify to everyone whose view of the guild
// moved (the engine sends those from the mutation itself, so chat commands
// and the inactivity sweep announce exactly like wire requests).
//
// Strings on the wire are fixed arrays, null-padded, matching the buffer the
// receiving side holds (CharNameLen names, max_guild_name_length + 1 guild
// names, guild_rank_title_len titles). Rank titles travel as text so the
// ADR 0006 rank editor can rename ranks without a protocol change.
//
// RequestGuildInvite is RESERVED: the id is allocated (Appendix A's ask) so a
// later master-initiated invite flow costs no Compatibility bump, but no
// struct exists and no handler answers it in v1.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "PacketHeaders.h"
#include "NetConstants.h"
#include "Game/GuildDefs.h"

#include <cstdint>

namespace hb {
namespace net {
	HB_PACK_BEGIN

	constexpr std::size_t kGuildNameWireLen =
		hb::shared::guild::max_guild_name_length + 1;
	constexpr std::size_t kGuildRankTitleWireLen =
		hb::shared::guild::guild_rank_title_len;

	// The own-guild snapshot, embedded wherever a client learns its OWN
	// standing — the char-init contents (PacketResponseInitDataHeader) and
	// the mid-session re-sync (PacketNotifyGuildSelf). One struct so the two
	// cannot drift and both sides fill/read it through one function each.
	// Empty guild_name = unguilded; every other field is then zero.
	struct HB_PACKED PacketGuildSelfState {
		char guild_name[kGuildNameWireLen];
		int8_t rank;
		char rank_title[kGuildRankTitleWireLen];
		uint16_t level;
		uint32_t permission_mask;
		uint8_t title;          // guild_title kind, 0 = none
	};

	// --- Requests (client -> server) ------------------------------------

	struct HB_PACKED PacketRequestGuildCreate : packet_base {
		PacketHeader header;
		char name[kGuildNameWireLen];
	};

	// Disband wants the guild name retyped (§3.1: typed-name confirmation).
	struct HB_PACKED PacketRequestGuildDisband : packet_base {
		PacketHeader header;
		char typed_name[kGuildNameWireLen];
	};

	// File an admission request — the ticket-to-approver hand AND the Kennedy
	// apply-by-name path are the same filing (§3.1.1 deviation 2).
	struct HB_PACKED PacketRequestGuildJoin : packet_base {
		PacketHeader header;
		char guild_name[kGuildNameWireLen];
	};

	// File a secession request against one's own guild.
	struct HB_PACKED PacketRequestGuildLeave : packet_base {
		PacketHeader header;
	};

	// Kennedy self-dismissal (−300 exp, officers auto-demote on the way out).
	struct HB_PACKED PacketRequestGuildSelfExit : packet_base {
		PacketHeader header;
	};

	// Approve or reject one queued join/leave request.
	struct HB_PACKED PacketRequestGuildDecide : packet_base {
		PacketHeader header;
		int64_t request_id;
		uint8_t approve;
	};

	// The by-name moderation shape, shared by kick / promote / demote /
	// transfer-mastership / title-strip — the msg id says which.
	struct HB_PACKED PacketRequestGuildMember : packet_base {
		PacketHeader header;
		char target[hb::shared::limits::CharNameLen];
	};

	// One Donation burn (lane = guild_donation_lane).
	struct HB_PACKED PacketRequestGuildDonate : packet_base {
		PacketHeader header;
		uint8_t lane;
		int64_t amount;
	};

	// Treasury movement (op = guild_treasury_op).
	struct HB_PACKED PacketRequestGuildTreasury : packet_base {
		PacketHeader header;
		uint8_t op;
		int64_t amount;
	};

	// Claim a Title slot (kind = guild_title).
	struct HB_PACKED PacketRequestGuildTitleClaim : packet_base {
		PacketHeader header;
		uint8_t kind;
	};

	// Give up one's own Title.
	struct HB_PACKED PacketRequestGuildTitleDrop : packet_base {
		PacketHeader header;
	};

	struct HB_PACKED PacketRequestGuildRoster : packet_base {
		PacketHeader header;
	};

	struct HB_PACKED PacketRequestGuildQueue : packet_base {
		PacketHeader header;
	};

	struct HB_PACKED PacketRequestGuildInfo : packet_base {
		PacketHeader header;
	};

	// The lazy name-cache miss (the original 0x0A59 flow, typed): whose guild
	// line is wanted (a live object id) and which client cache slot the
	// answer should land in — echoed back verbatim, never interpreted
	// server-side beyond the send.
	struct HB_PACKED PacketRequestGuildName : packet_base {
		PacketHeader header;
		int16_t object_id;
		int16_t cache_index;
	};

	// --- Responses (server -> client, answering one request) ------------

	// The envelope every mutation request gets back: which request kind
	// (guild_wire_action), the gate it stopped at (guild_result, ok when it
	// went through), and the queued row id when the action minted one
	// (join/leave filings), else 0.
	struct HB_PACKED PacketResponseGuildAction {
		PacketHeader header;
		uint8_t action;
		int32_t result;
		int64_t request_id;
	};

	// Roster: header then `count` entries, PacketWriter-composed. The whole
	// membership travels (the cap tops out at 128, §3.1.2); `online` and
	// `title` are the live decorations the store alone cannot answer.
	struct HB_PACKED PacketResponseGuildRosterHeader {
		PacketHeader header;
		uint16_t count;
	};

	struct HB_PACKED PacketResponseGuildRosterEntry {
		char member[hb::shared::limits::CharNameLen];
		int8_t rank;
		uint8_t title;          // guild_title kind, 0 = none
		uint8_t online;
		int64_t donated_gold;
		int64_t donated_ek;
		int64_t donated_contribution;
	};

	// The pending join/leave queue, newest capped: `count` entries follow,
	// `total` is the queue's true size so a clipped view says so.
	constexpr std::size_t kGuildQueueMaxEntries = 100;

	struct HB_PACKED PacketResponseGuildQueueHeader {
		PacketHeader header;
		uint16_t count;
		uint16_t total;
	};

	struct HB_PACKED PacketResponseGuildQueueEntry {
		int64_t request_id;
		uint8_t kind;           // guild_request::join / leave
		char applicant[hb::shared::limits::CharNameLen];
		int64_t created_at;     // unix seconds
	};

	// The guild summary the Donate/Treasury/Info panels read. next_* are the
	// NEXT level's cumulative thresholds, all zero at the curve's top.
	struct HB_PACKED PacketResponseGuildInfo {
		PacketHeader header;
		char guild_name[kGuildNameWireLen];
		char master_name[hb::shared::limits::CharNameLen];
		uint16_t level;
		uint16_t member_count;
		uint16_t member_cap;
		uint16_t officer_count;
		uint16_t officer_cap;
		uint8_t commander_slots;
		uint8_t hunt_slots;
		uint8_t raid_slots;
		uint8_t repair_pct;
		uint8_t donations_enabled;
		int64_t treasury_gold;
		int64_t burned_gold;
		int64_t burned_ek;
		int64_t burned_contribution;
		int64_t next_gold;
		int64_t next_ek;
		int64_t next_contribution;
		int64_t created_at;     // unix seconds
	};

	// --- Notifies (server -> client events, MsgId::Notify channel) -------

	// The name-cache answer (the original's 30-byte 0x0BA6, grown a title and
	// a max-level flag): fills the echoed slot. Empty guild_name = unguilded.
	struct HB_PACKED PacketNotifyGuildName {
		PacketHeader header;
		int16_t cache_index;
		int8_t rank;
		uint8_t at_max_level;   // the gold-name render hint (§3.1.2, #124)
		char guild_name[kGuildNameWireLen];
		char rank_title[kGuildRankTitleWireLen];
	};

	// To every online member: someone joined.
	struct HB_PACKED PacketNotifyGuildJoined {
		PacketHeader header;
		char member[hb::shared::limits::CharNameLen];
	};

	// To every online member (the departed one included): someone left, was
	// kicked, or Kennedy-exited (reason = guild_leave_reason).
	struct HB_PACKED PacketNotifyGuildLeft {
		PacketHeader header;
		char member[hb::shared::limits::CharNameLen];
		uint8_t reason;
	};

	// To every online member: the guild is gone.
	struct HB_PACKED PacketNotifyGuildDisbanded {
		PacketHeader header;
		char guild_name[kGuildNameWireLen];
	};

	// To every online member: a member's rank moved (promote / demote /
	// either end of a transfer).
	struct HB_PACKED PacketNotifyGuildRankChanged {
		PacketHeader header;
		char member[hb::shared::limits::CharNameLen];
		int8_t new_rank;
		char rank_title[kGuildRankTitleWireLen];
	};

	// To every online member: a Title changed hands or state
	// (reason = guild_title_change; held says whether `member` now holds it).
	struct HB_PACKED PacketNotifyGuildTitleChanged {
		PacketHeader header;
		char member[hb::shared::limits::CharNameLen];
		uint8_t kind;           // guild_title
		uint8_t held;
		uint8_t reason;
	};

	// To every online member: the burn counters crossed a gate (§3.1.2).
	struct HB_PACKED PacketNotifyGuildLevelUp {
		PacketHeader header;
		uint16_t new_level;
	};

	// To every online holder of the deciding permission bit: a join/leave
	// request entered the queue.
	struct HB_PACKED PacketNotifyGuildRequestQueued {
		PacketHeader header;
		uint8_t kind;           // guild_request::join / leave
		char applicant[hb::shared::limits::CharNameLen];
	};

	// To the applicant, if online when an approver decided. Approval also
	// re-syncs their GuildSelf; a rejection moves no state, so this line is
	// the only way an online applicant hears it (an offline one simply finds
	// the request gone — the queue is the offline-proof path, §3.1.1).
	struct HB_PACKED PacketNotifyGuildRequestDecided {
		PacketHeader header;
		uint8_t kind;           // guild_request::join / leave
		uint8_t approved;
		char guild_name[kGuildNameWireLen];
	};

	// To the member whose own standing changed (joined, left, kicked,
	// promoted, demoted, transferred, titled): the same own-guild snapshot
	// the char-init contents carry, re-synced mid-session.
	struct HB_PACKED PacketNotifyGuildSelf {
		PacketHeader header;
		PacketGuildSelfState state;
	};

	HB_PACK_END
}
}
