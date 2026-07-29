// DropModel.h: absolute per-(item, monster) drop rarity (Item Tiers #73)
//
// A drop-table row is an ABSOLUTE per-kill chance for that item from that
// monster — `drop_chance_ppb`, parts per billion — not a weight relative to
// whatever else happens to share its table. Adding or removing a row therefore
// cannot change any other row's odds, which is the property the whole model
// exists to protect: identical authored weights used to produce a 216x rarity
// gap for the same unique on two different monsters.
//
// The column is ppb rather than the "1 in N" the data is authored and reported
// in, because both rules that matter are exact integer comparisons — a
// guaranteed table's rows sum to exactly 1_000_000_000, and saturation is a
// `>` against it. Summed floats cannot carry either, and the Windows/Linux
// byte-identical `dropodds` gate (#66, #72) cannot survive float accumulation
// order. Readability is delivered as presentation instead: the
// `drop_entries_readable` view and the dropodds report both print "1 in N".
//
// On top sit five GENEROSITY multipliers. Larger ppb is more likely, so they
// MULTIPLY — the superseded "1 in N" form divided, and inverting that is the
// easiest mistake in this file:
//
//     effective_ppb = drop_chance_ppb x (global x stage x category x grade x rep)
//
// The first four are stored tuning knobs. `rep` is the odd one out: it is
// per-player, computed from the killer's reputation, and applies only to the
// gear and unique categories (#88). It is 1.0 for a neutral player, which is
// everyone until they are voted on.
//
// They scale row rarity only, which shrinks a table's "nothing" remainder.
// They never touch roll_count, so turning stage 2 up cannot make a scatter
// boss spread more items, and on a table whose rows already sum to 1.0 they
// are a deliberate no-op — something drops on every roll already.
//
// There is ONE table type and ONE roll routine. "Stage" is only which slot a
// monster references: nothing in the roll, the rate model, the multipliers or
// the validator branches on it, so a stage-1 table set to scatter/corpse-decay
// behaves identically to a stage-2 one.
//
// Design contract: docs/adr/0005-absolute-drop-chances.md,
// PLANS/ItemTiers_Plan.md §8 Amendment A1.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>

class CItem;

namespace hb::server
{

// One part per billion is the unit; a row at this value drops every kill.
inline constexpr uint32_t drop_chance_denominator = 1'000'000'000u;

// Which generosity lever a row answers to, so consumables can be tuned
// without touching gear — the #67 failure, made structurally impossible.
namespace drop_category
{
enum drop_category : uint8_t
{
	gear = 0,          // anything equippable, including rings and necklaces
	consumable,        // potions, food, body parts, components, everything else
	gold,
	unique,            // the is_special_item named roster
	count
};
}

// Where the roll's winnings land. `spiral` is the original's fixed
// ITEMSPREAD_FIEXD_COORD 5x5 centre-out square.
namespace drop_placement
{
enum drop_placement : uint8_t { single = 0, spiral };
}

// When they land. `decay` is the original's second drop, which appears as the
// corpse decays rather than at the moment of death.
namespace drop_delay
{
enum drop_delay : uint8_t { death = 0, decay };
}

// NPC types the drop pipeline refuses outright: guards, dummies and crops
// never roll a drop. It lives beside the rate model rather than in
// EntityManager.h so the report and the boot validator can exclude exactly
// what the pipeline excludes, no more and no less, without pulling in the
// whole entity manager.
inline bool npc_type_never_drops(short npc_type)
{
	return npc_type == 21 || npc_type == 34 || npc_type == 64;
}

const char* drop_category_name(uint8_t category);
const char* drop_placement_name(uint8_t placement);
const char* drop_delay_name(uint8_t delay);

// The vocabularies, owned here so the loader and the validator cannot disagree
// about what a legal value is. Empty for anything else — the validator is what
// reports the typo, so a bad row never silently changes where loot lands.
bool parse_drop_placement(const std::string& value, uint8_t& out);
bool parse_drop_delay(const std::string& value, uint8_t& out);

struct drop_entry
{
	int      item_id = 0;
	uint32_t chance_ppb = 0;     // as authored, before any multiplier
	int      min_count = 0;
	int      max_count = 0;
};

struct drop_table
{
	int         id = 0;
	std::string name;
	std::string description;

	// Which slot this table is authored for. DECLARATIVE ONLY — the validator
	// uses it to apply the §8 rule that stage-2 tables hold no tier-scope gear.
	// The roll never reads it; what a table does is decided by the properties
	// below, and which multiplier applies is decided by the slot it was
	// reached through.
	int stage = 1;

	int      roll_count_min = 1;
	int      roll_count_max = 1;
	uint8_t  placement = drop_placement::single;
	uint8_t  delay = drop_delay::death;

	std::vector<drop_entry> entries;

	// Summed authored ppb. > drop_chance_denominator is malformed (a boot
	// validator error); == it means "nothing" can never come up.
	uint64_t total_ppb = 0;
};

// The generosity stack. Every layer defaults to 1.0, lives in gamedata.db and
// reloads live with `reload tiers` — there is no restart-only term left in the
// chain.
struct drop_multipliers
{
	static constexpr int max_stage = 2;
	static constexpr int max_grade = 5;

	double global = 1.0;
	double stage[max_stage + 1] = { 1.0, 1.0, 1.0 };          // indexed 1..2
	double category[drop_category::count] = { 1.0, 1.0, 1.0, 1.0 };
	double grade[max_grade + 1] = { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 };  // indexed 1..5

	// The fifth layer is PER-PLAYER, so unlike the four above it is not a
	// stored number but one computed per kill from the killer's reputation
	// (#88). These three describe its shape and live in data like the rest.
	//
	// The original scaled its consumable/equipment split by
	// `rating x m_cRepDropModifier`, clamped to +/-1000 against a 1000-wide
	// window — which on the equipment side is exactly a 0.0..2.0 factor sitting
	// at 1.0 for a neutral player. That curve is reproduced here and then
	// floored: a pariah farms gear slowly rather than never, because a penalty
	// players inflict on each other by vote must not be able to lock someone
	// out of the reward loop outright (owner decision, #88).
	//
	// `reputation_modifier = 0` disables the layer entirely — the same escape
	// hatch the original had, which 0-floored the config token.
	double reputation_modifier = 5.0;    // the original's m_cRepDropModifier
	double reputation_floor    = 0.25;
	double reputation_cap      = 2.0;

	// Exactly 1.0 at rating 0, so a world whose players have never voted rolls
	// precisely as authored and this layer costs nothing until it is used.
	double reputation_factor(int rating) const;

	// Reputation moves what a player farms FOR and nothing else: gear and the
	// named uniques. Gold and consumables drop at the ordinary rate no matter
	// who lands the kill, so an outcast is slowed, never starved.
	static bool reputation_applies(uint8_t category_id);

	// The product for one row. Out-of-range stage/grade contribute 1.0 rather
	// than reading off the end — a bad grade is the validator's error to
	// report, not a crash here.
	double product(int stage_slot, uint8_t category_id, uint8_t loot_grade,
		double rep_factor = 1.0) const;
};

// The generosity category of one drop row. `item_config` may be null (an
// unknown item id), which reads as consumable — the validator is what reports
// the dangling row.
uint8_t drop_category_of(const CItem* item_config, int item_id);

// One table's rows after the generosity stack and proportional saturation, in
// ppb, positionally matching `table.entries`. The sum is always <=
// drop_chance_denominator; whatever is left over is "nothing".
//
// This is the single piece of arithmetic the roll and the dropodds report
// share. A report that disagreed with the roller would be worse than no
// report, so neither restates it.
//
// `rep_factor` is the killer's reputation layer, from
// `drop_multipliers::reputation_factor`. It is deliberately NOT defaulted: it
// silently changes rates, so every call site has to say whose reputation it
// means, and `1.0` — the neutral player, and what the report shows unless
// asked otherwise — has to be written down rather than inherited.
//
// `saturated` reports that the stack pushed the table past 1.0 and it was
// scaled proportionally back — ratios intact, "nothing" at zero. Past that
// point a multiplier increase is a no-op rather than a silent distortion.
void resolve_drop_chances(const drop_table& table,
	const drop_multipliers& multipliers,
	int stage_slot, uint8_t loot_grade, double rep_factor,
	const CItem* const* item_configs, int item_config_count,
	std::vector<uint32_t>& out_chances, bool* saturated = nullptr);

// Index into `chances` the draw selects, or -1 for "nothing". `draw` must be
// uniform over [0, drop_chance_denominator).
int roll_drop_row(const std::vector<uint32_t>& chances, uint32_t draw);

} // namespace hb::server
