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

// The Title's name, for guild.log lines, the provers and — later — the
// client dialogs (#124). Lowercase because log lines are.
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

} // namespace hb::shared::guild
