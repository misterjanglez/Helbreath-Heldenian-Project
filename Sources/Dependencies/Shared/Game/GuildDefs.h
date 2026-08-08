// GuildDefs.h: the guild vocabulary — ranks, permission bits, Titles, name rules
//
// The Phase-2 guild rebuild's fixed points, shared because both sides will
// eventually speak them: the server stores and gates on these values from #120
// on, and the client renders rank titles and permission-gated buttons once the
// wire family lands (#123). Stored values (guilds.db) and — later — wire values
// alike, so everything here is append-only once a real world exists.
//
// Ranks are identity, not authority: per ADR 0006 no gate ever asks "is rank
// X?" — it asks "does this member's rank hold this permission bit?". The only
// rank comparisons that exist at all are "kick targets below own rank" and the
// two deliberately hardcoded master-only actions (transfer-mastership,
// disband), which are not bits precisely so no mask edit can ever hand them out.
//
// Design contract: PLANS/CoreSystems_Restoration_Plan.md §3.1.1; ADR 0006/0007.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string_view>

namespace hb::shared::guild
{

// Fixed roster of three (§3.1.1 deviation 1). Lower value = more authority,
// keeping the original's 0-is-guildmaster flavor; `none` matches its -1 for
// the unguilded. A configurable-rank editor later adds rows, not meanings.
namespace guild_rank
{
	enum guild_rank : int8_t
	{
		none        = -1,
		guildmaster = 0,
		officer     = 1,
		guildsman   = 2,
	};
}

constexpr bool is_valid_guild_rank(int rank)
{
	return rank >= guild_rank::guildmaster && rank <= guild_rank::guildsman;
}

// The roster size, derived from the enum span so the per-guild mask seeding
// and the boot validator can never count different ranks than the enum
// defines — the whole point of ADR 0006's "a fourth rank is not a schema
// change" promise.
constexpr int guild_rank_count = guild_rank::guildsman - guild_rank::guildmaster + 1;

// One bit per guild-gated action. The per-guild per-rank masks seeded from the
// defaults below live in guilds.db (guild_rank_permissions); these bit values
// are the schema of that column and must never be renumbered.
namespace guild_permission
{
	enum guild_permission : uint32_t
	{
		approve_join       = 1u << 0,   // decide queued admission requests
		approve_leave      = 1u << 1,   // decide queued secession requests
		kick               = 1u << 2,   // expel a member below own rank
		promote_demote     = 1u << 3,   // move members between ranks
		council_access     = 1u << 4,   // the cross-guild [Council] channel
		commander_eligible = 1u << 5,   // may claim the Commander Title (ADR 0007)
		fightzone_reserve  = 1u << 6,   // book the fightzone (§3.4, Phase 4)
		treasury_withdraw  = 1u << 7,   // withdraw guild gold (always logged)
		title_manage       = 1u << 8,   // strip / re-assign held Titles
	};

	constexpr uint32_t all_bits =
		approve_join | approve_leave | kick | promote_demote | council_access |
		commander_eligible | fightzone_reserve | treasury_withdraw | title_manage;
}

// The §3.1.1 default masks, seeded per guild at creation. Data from day one so
// "configurable ranks" later is an editing dialog, not a schema change.
constexpr uint32_t default_guildsman_mask = 0;
constexpr uint32_t default_officer_mask =
	guild_permission::approve_join | guild_permission::approve_leave |
	guild_permission::kick | guild_permission::council_access |
	guild_permission::commander_eligible | guild_permission::fightzone_reserve |
	guild_permission::treasury_withdraw | guild_permission::title_manage;
constexpr uint32_t default_guildmaster_mask =
	default_officer_mask | guild_permission::promote_demote;

constexpr uint32_t default_permission_mask(int rank)
{
	switch (rank)
	{
	case guild_rank::guildmaster: return default_guildmaster_mask;
	case guild_rank::officer:     return default_officer_mask;
	case guild_rank::guildsman:   return default_guildsman_mask;
	default:                      return 0;
	}
}

// The Title slot machine (ADR 0007): claimable, capped, rotating power.
// Slot counts per Guild Level are balance-session data, not constants here.
namespace guild_title
{
	enum guild_title : uint8_t
	{
		commander  = 1,   // war duty (Phase 3 consumes this)
		huntmaster = 2,   // PvE bonuses
		raidmaster = 3,   // PvP bonuses
	};
}

constexpr bool is_valid_guild_title(int kind)
{
	return kind >= guild_title::commander && kind <= guild_title::raidmaster;
}

// The Title's name, for guild.log lines and the provers. Lowercase because
// log lines are.
constexpr const char* guild_title_name(int kind)
{
	switch (kind)
	{
	case guild_title::commander:  return "commander";
	case guild_title::huntmaster: return "huntmaster";
	case guild_title::raidmaster: return "raidmaster";
	default:                      return "unknown";
	}
}

// The player-facing spelling (event lines, the #124 dialogs) — beside the
// log spelling so a fourth Title cannot land in one and miss the other.
constexpr const char* guild_title_display_name(int kind)
{
	switch (kind)
	{
	case guild_title::commander:  return "Commander";
	case guild_title::huntmaster: return "Huntmaster";
	case guild_title::raidmaster: return "Raidmaster";
	default:                      return "Unknown";
	}
}

// The queued join/leave ceremony (§3.1.1 deviation 2): requests wait until any
// approver decides. Stored in guilds.db (guild_requests.kind).
namespace guild_request
{
	enum guild_request : uint8_t
	{
		join  = 1,
		leave = 2,
	};
}

constexpr bool is_valid_guild_request(int kind)
{
	return kind == guild_request::join || kind == guild_request::leave;
}

// Which permission bit decides a queued request of `kind` — the filing
// notices, the decide gate and the queue-view gate all ask this, so the
// pairing exists exactly once.
constexpr uint32_t deciding_bit_for(int kind)
{
	return kind == guild_request::join
		? guild_permission::approve_join : guild_permission::approve_leave;
}

// The §3.1/§3.1.1 ceremony numbers, shared because the client dialogs gate the
// same buttons the server enforces (the original client pre-checked level and
// charisma before opening the create dialog, and #124 will again).
constexpr int guild_create_min_level = 20;
constexpr int guild_create_min_charisma = 20;

// The floor the Guildmaster's charisma may never redistribute below (§3.1.1:
// the CHR-20 rule applies to the Guildmaster only; everyone else keeps the
// universal stat floor).
constexpr int guildmaster_charisma_floor = 20;

// Kennedy self-dismissal: the ceremony's price in experience, subtracted with
// a clamp at zero exactly as the original did (no level-down exists).
constexpr int kennedy_secession_exp_penalty = 300;

// Guild name rules (#120): 1..20 characters from letters/digits/space/'_'/'-'.
// Uniqueness is case-insensitive and lives in the guilds.db schema; these are
// the character-level rules every entry point (create dialog, Kennedy
// apply-by-name, store, boot validator) must share so no two of them can
// disagree about what a legal name is.
constexpr size_t max_guild_name_length = 20;

constexpr bool is_valid_guild_name_char(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		(c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-';
}

// Leading/trailing spaces are refused on top of the charset: " Knights" and
// "Knights" would otherwise be two distinct guilds no player can tell apart.
constexpr bool is_valid_guild_name(std::string_view name)
{
	if (name.empty() || name.size() > max_guild_name_length)
		return false;
	if (name.front() == ' ' || name.back() == ' ')
		return false;
	for (char c : name)
	{
		if (!is_valid_guild_name_char(c))
			return false;
	}
	return true;
}

// --- Wire vocabulary (#123) -------------------------------------------------
// Everything below crosses the wire (PacketGuild.h) and is append-only like
// the stored values above.

// Rank titles travel as strings, not rank numbers: the overhead line and the
// character dialog print what the server sends, so the ADR 0006 configurable
// rank editor can someday rename ranks without another Compatibility bump.
// 15 characters + null; the defaults below are the only titles v1 mints.
constexpr int guild_rank_title_len = 16;

constexpr const char* default_rank_title(int rank)
{
	switch (rank)
	{
	case guild_rank::guildmaster: return "Guildmaster";
	case guild_rank::officer:     return "Officer";
	case guild_rank::guildsman:   return "Guildsman";
	default:                      return "";
	}
}

// The three Donation lanes as the client names them (§3.1.2: no conversion
// between lanes, so the lane is part of the request, never inferred).
namespace guild_donation_lane
{
	enum guild_donation_lane : uint8_t
	{
		gold         = 1,
		enemy_kills  = 2,
		contribution = 3,
	};
}

namespace guild_treasury_op
{
	enum guild_treasury_op : uint8_t
	{
		deposit  = 1,
		withdraw = 2,
	};
}

// Why a member left, on the GuildLeft notify: the roster line is the same,
// the event text is not.
namespace guild_leave_reason
{
	enum guild_leave_reason : uint8_t
	{
		left      = 1,   // filed secession, approved
		kicked    = 2,   // kick / ban
		self_exit = 3,   // Kennedy self-dismissal
	};
}

// What happened to a Title, on the GuildTitleChanged notify.
namespace guild_title_change
{
	enum guild_title_change : uint8_t
	{
		claimed       = 1,
		dropped       = 2,   // holder gave it up
		stripped      = 3,   // Officer+ took it
		released_idle = 4,   // the 10-minute inactivity sweep
		released_rank = 5,   // rank move cost the eligibility bit (Commander)
	};
}

// Which request an action envelope answers (PacketResponseGuildAction.action).
namespace guild_wire_action
{
	enum guild_wire_action : uint8_t
	{
		create            = 1,
		disband           = 2,
		join              = 3,
		leave             = 4,
		self_exit         = 5,
		decide            = 6,
		kick              = 7,
		promote           = 8,
		demote            = 9,
		transfer          = 10,
		donate            = 11,
		treasury_deposit  = 12,
		treasury_withdraw = 13,
		title_claim       = 14,
		title_drop        = 15,
		title_strip       = 16,
	};
}

// Why an entry point refused (or didn't). Shared vocabulary since #123: the
// action envelope carries these to the client, one name per gate so the
// prover asserts the exact rung of the ladder, guild.log explains itself and
// the client maps codes to player-facing text (#124). Append-only.
namespace guild_result
{
	enum guild_result : int
	{
		ok = 0,
		no_client,              // bad handle / not in the world yet
		crusade_active,         // create, disband, secession: not during crusade
		already_in_guild,
		not_in_guild,
		bad_name,               // charset/length rules (above)
		name_taken,             // case-insensitive collision
		name_mismatch,          // disband: typed confirmation != guild name
		level_too_low,          // create: level < guild_create_min_level
		charisma_too_low,       // create gate, and transfer's incoming master
		not_citizen,            // location "NONE" / no side
		not_in_own_town,        // create: standing outside the home town
		wrong_side,             // join: applicant's side != guild's side
		no_ticket,              // items 88/89: the ceremony's price is missing
		request_pending,        // an identical request is already queued
		unknown_guild,
		unknown_request,        // no such request, or not this approver's guild
		no_permission,          // the required guild_permission bit is absent
		not_master,             // transfer/disband: hardcoded master-only
		master_cannot_leave,    // the master transfers or disbands, never exits
		target_not_found,       // named member not in the actor's guild
		target_is_master,       // kick/promote/demote aimed at the master
		target_not_below_rank,  // kick: target must rank strictly below actor
		target_offline,         // transfer: incoming master must be online
		bad_rank,               // promote/demote outside guildsman<->officer
		store_error,            // the store refused; details in guild.log
		donations_disabled,     // the donations_enabled switch is off (#122)
		amount_too_small,       // below the lane's configured minimum, or not positive
		insufficient_funds,     // the actor's own balance is short of the amount
		guild_full,             // member cap f(Guild Level) reached (join approval)
		officers_at_capacity,   // officer cap f(Guild Level) reached (promote)
		bad_title,              // not a guild_title kind
		already_titled,         // the claimant already holds a Title (one per member)
		no_title_slot,          // every slot of that kind at this Guild Level is held
		no_title,               // drop/strip aimed at someone holding nothing
		treasury_short,         // withdraw larger than the wallet
		bad_request,            // malformed wire field (unknown lane/op) — #123
	};
}

// The result's name, for log lines, the prover and the client's fallback text
// ("ok", "no_ticket", ...).
constexpr const char* guild_result_name(int result)
{
	switch (result)
	{
	case guild_result::ok:                    return "ok";
	case guild_result::no_client:             return "no_client";
	case guild_result::crusade_active:        return "crusade_active";
	case guild_result::already_in_guild:      return "already_in_guild";
	case guild_result::not_in_guild:          return "not_in_guild";
	case guild_result::bad_name:              return "bad_name";
	case guild_result::name_taken:            return "name_taken";
	case guild_result::name_mismatch:         return "name_mismatch";
	case guild_result::level_too_low:         return "level_too_low";
	case guild_result::charisma_too_low:      return "charisma_too_low";
	case guild_result::not_citizen:           return "not_citizen";
	case guild_result::not_in_own_town:       return "not_in_own_town";
	case guild_result::wrong_side:            return "wrong_side";
	case guild_result::no_ticket:             return "no_ticket";
	case guild_result::request_pending:       return "request_pending";
	case guild_result::unknown_guild:         return "unknown_guild";
	case guild_result::unknown_request:       return "unknown_request";
	case guild_result::no_permission:         return "no_permission";
	case guild_result::not_master:            return "not_master";
	case guild_result::master_cannot_leave:   return "master_cannot_leave";
	case guild_result::target_not_found:      return "target_not_found";
	case guild_result::target_is_master:      return "target_is_master";
	case guild_result::target_not_below_rank: return "target_not_below_rank";
	case guild_result::target_offline:        return "target_offline";
	case guild_result::bad_rank:              return "bad_rank";
	case guild_result::store_error:           return "store_error";
	case guild_result::donations_disabled:    return "donations_disabled";
	case guild_result::amount_too_small:      return "amount_too_small";
	case guild_result::insufficient_funds:    return "insufficient_funds";
	case guild_result::guild_full:            return "guild_full";
	case guild_result::officers_at_capacity:  return "officers_at_capacity";
	case guild_result::bad_title:             return "bad_title";
	case guild_result::already_titled:        return "already_titled";
	case guild_result::no_title_slot:         return "no_title_slot";
	case guild_result::no_title:              return "no_title";
	case guild_result::treasury_short:        return "treasury_short";
	case guild_result::bad_request:           return "bad_request";
	default:                                  return "unknown";
	}
}

} // namespace hb::shared::guild
