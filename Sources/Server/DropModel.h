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
#include <map>
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

	// How many times min..max is rolled and summed, so a row can express the
	// original's `iDice(throws, range)` rather than only `iDice(1, range)`.
	// The scatter's gold pile is `iDice(10, 15000)` — ten rolls of 1..15000,
	// clustered near 75,005 — where a single uniform roll over the same span
	// has the right mean and three times the spread. 1 is every other row.
	int      count_throws = 1;
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

	// The guaranteed head (#89). The first `guaranteed_rolls` rolls cannot come
	// up empty: when the draw lands in the table's "nothing" remainder they pay
	// out `guarantee_item_id` instead. That substitution IS the floor under a
	// scatter boss's yield — the original wrote it as
	//
	//     if (iItemID == 0 && iProb == 100) iItemID = 90;   // Gold
	//
	// and it is why a Wyvern always dropped at least six items. Modelling it as
	// a guaranteed row instead would be wrong twice over: the item would also
	// become winnable on the tail rolls, where the original could yield nothing.
	//
	// The guarantee item is NOT a competing row. It carries a 0-ppb row for its
	// count spec alone (drop_entry::is_guarantee_payload), so how much gold a
	// pile holds is authored next to the gold, and the validator ties the two
	// together in both directions.
	int      guaranteed_rolls = 0;
	int      guarantee_item_id = 0;

	// Where that row sits in `entries`, found once by the loader. -1 when the
	// table has no guarantee. An index rather than a pointer because `entries`
	// is still growing while the loader fills it.
	int      guarantee_entry_index = -1;

	// The guarantee's payload row, or null. Every caller wants the row, not the
	// index, and none of them should be scanning for it per kill.
	const drop_entry* guarantee_entry() const
	{
		return (guarantee_entry_index >= 0 &&
			guarantee_entry_index < static_cast<int>(entries.size()))
			? &entries[guarantee_entry_index] : nullptr;
	}

	// Rolls past the head divide every row's resolved chance by this. The
	// original multiplied each block's rarity bound by `fProb`, which is 1.0 for
	// the head and 5.0 for the tail — the same factor in all three of its blocks,
	// so one integer divisor reproduces it exactly. Integer division, like the
	// rest of the ppb chain, so the report and the roll agree byte for byte on
	// every platform. 1 means the tail rolls at full rarity.
	int      tail_rarity_divisor = 1;

	// Rolls at index >= guaranteed_rolls, given a drawn roll count.
	int tail_rolls(int rolls) const
	{
		return rolls > guaranteed_rolls ? rolls - guaranteed_rolls : 0;
	}

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

// The same table's chances for a roll past the guaranteed head: every head
// chance divided by `tail_rarity_divisor`. Positionally matching, like
// resolve_drop_chances, and derived from its output rather than recomputed — the
// generosity stack must not be applied twice, and the report and the roll have
// to price the tail identically.
void resolve_tail_chances(const drop_table& table,
	const std::vector<uint32_t>& head_chances,
	std::vector<uint32_t>& out_chances);

// The count range one won row pays out, clamped to a usable range. Every row
// states its own, with a single exception: a gold row that authors none takes
// the monster's own dice, which is where all gold but the boss scatter's comes
// from. Shared so the roller and the scattersmoke harness cannot disagree about
// how much a pile holds.
void resolve_entry_counts(const drop_entry& entry, int gold_dice_min,
	int gold_dice_max, int& min_count, int& max_count);

// Index into `chances` the draw selects, or -1 for "nothing". `draw` must be
// uniform over [0, drop_chance_denominator).
int roll_drop_row(const std::vector<uint32_t>& chances, uint32_t draw);

// Summed chances, for the expected-yield arithmetic the report needs. Returns a
// uint64 because a saturated table sums to exactly the denominator.
uint64_t total_chance(const std::vector<uint32_t>& chances);

// One drop slot, fully resolved: the chances the roller would use and what a
// kill expects of each row.
//
// It is here rather than inside the report because three readers now need those
// numbers — the human `dropodds npc` listing, the machine `dropodds rows` lines
// that #85's analytics consume as their PREDICTED side, and the prover that
// checks the arithmetic. A per-kill expectation computed three times is a
// per-kill expectation that can disagree with itself, and the whole value of the
// observed-vs-authored diff rests on its predicted side being the same number
// the report prints and the roller rolls.
struct resolved_slot
{
	const drop_table* table = nullptr;
	std::vector<uint32_t> chances;        // head chances, positionally on entries
	std::vector<uint32_t> tail_chances;   // the same rows past the guaranteed head
	bool   saturated = false;
	int    head_rolls = 0;                // rolls that cannot come up empty
	double expected_rolls = 0.0;          // rolls per kill, averaged over the range
	double head_total = 0.0;              // chance a head roll yields anything
	double tail_total = 0.0;
	double guarantee_per_kill = 0.0;      // the head's leftover, paid in the guarantee item

	size_t size() const { return chances.size(); }
	int item_id(size_t i) const { return table->entries[i].item_id; }
	bool is_guarantee(size_t i) const { return item_id(i) == table->guarantee_item_id; }

	double per_roll(size_t i) const;
	double per_roll_tail(size_t i) const;

	// The expected number of this row's item one kill yields. A kill is
	// `head_rolls` rolls at the authored rarity plus the rest at the divided one,
	// so a row's per-kill expectation is NOT its per-roll chance times the roll
	// count — which is the arithmetic #89 made non-obvious and this exists to
	// state once.
	double per_kill(size_t i) const;

	// What a kill yields in total, across every row. Every head roll pays out
	// something by construction (the leftover buys the guarantee item), and each
	// roll past the head pays out with probability tail_total — so this is
	// `head_rolls + (expected_rolls - head_rolls) * tail_total`, and the sum of
	// per_kill() over all rows must equal it. That identity is what the prover
	// checks; it is the one statement that catches a mis-derived head/tail split.
	double expected_yield() const;
};

// Resolve one slot through the roller's own arithmetic. False when the table id
// names no table — the monster simply has no drops in that slot.
bool resolve_slot(const std::map<int, drop_table>& tables, int table_id,
	const drop_multipliers& multipliers, int stage_slot, uint8_t loot_grade,
	double rep_factor, const CItem* const* item_configs, int item_config_count,
	resolved_slot& out);

} // namespace hb::server
