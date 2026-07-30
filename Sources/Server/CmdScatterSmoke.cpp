// CmdScatterSmoke.cpp: observed yield and composition for a monster's drop slot (#89)
//
// `dropodds` prints what the data SAYS. This prints what the loop DOES, which
// after #89 is no longer the same question: a table with a guaranteed head runs
// its rolls in two phases at two different rarities, and the head's "nothing"
// outcome pays out an item instead. Multiplying a row's chance by the roll count
// no longer gives its per-kill rate, so the arithmetic needs a witness.
//
// It rolls through the same three primitives the real roller uses —
// resolve_drop_chances, resolve_tail_chances, roll_drop_row — and reproduces
// only roll_drop_slot's loop. What it deliberately does NOT do is place items on
// a map, which is the part that needs a live NPC handle.
//
// Determinism is a requirement, not a convenience: #66 and #72 were both gated
// by diffing machine-readable lines between Windows and Linux, and this harness
// is the gate for #89. std::mt19937 is specified bit-for-bit by the standard;
// std::uniform_int_distribution is NOT, so the draw below reduces the generator's
// output by hand.
//
//////////////////////////////////////////////////////////////////////

#include "CmdScatterSmoke.h"

#include "DropModel.h"
#include "Game.h"                   // CGame
#include "Item.h"
#include "Npc.h"
#include "ServerConsole.h"
#include "TierConfigStore.h"
#include "TierConfigValidator.h"

#include <algorithm>                // std::sort
#include <cstdint>
#include <cstdio>
#include <format>
#include <map>
#include <random>
#include <string>
#include <utility>                  // std::pair
#include <vector>

using hb::server::drop_chance_denominator;
using hb::server::drop_table;
using hb::server::tier_validation_context;

namespace
{

// The seed is fixed and arbitrary; what matters is that it never changes, so a
// composition shift between two runs is a data or code change rather than luck.
constexpr uint32_t scatter_seed = 20260730u;
constexpr int default_kills = 100000;

// Unbiased draw over [0, drop_chance_denominator) straight off mt19937.
//
// A bare `rng() % denominator` would not do: 2^32 is 4 whole denominators plus a
// remainder of 294,967,296, so the bottom 29% of the range would come up 25%
// more often than the rest and every rate in the report would be wrong in the
// same direction. Rejecting the ragged tail costs ~6.9% of draws and removes the
// bias exactly.
uint32_t draw(std::mt19937& rng)
{
	constexpr uint64_t whole = (0x100000000ull / drop_chance_denominator)
		* drop_chance_denominator;
	uint64_t value;
	do { value = rng(); } while (value >= whole);
	return static_cast<uint32_t>(value % drop_chance_denominator);
}

// A row's stack size, mirroring CEntityManager::roll_entry_count but on this
// harness's own generator so the totals are reproducible.
long long roll_count(const hb::server::drop_entry& entry, int min_count,
	int max_count, std::mt19937& rng)
{
	if (max_count < min_count) max_count = min_count;
	const int throws = entry.count_throws > 1 ? entry.count_throws : 1;
	const uint32_t span = static_cast<uint32_t>(max_count - min_count);

	long long total = 0;
	for (int i = 0; i < throws; i++)
		total += (span > 0)
			? min_count + static_cast<int>(draw(rng) % (span + 1))
			: min_count;
	return total;
}

} // namespace

void CmdScatterSmoke::execute(CGame* game, const char* args)
{
	if (game == nullptr) return;

	int npc_id = 0, kills = default_kills, stage = 2;
	if (args == nullptr ||
		std::sscanf(args, "%d %d %d", &npc_id, &kills, &stage) < 1)
	{
		hb::console::error("Usage: scattersmoke <npc_id> [kills] [stage]");
		return;
	}
	if (kills < 1) kills = default_kills;
	if (stage != 1 && stage != 2)
	{
		hb::console::error("scattersmoke: stage must be 1 or 2");
		return;
	}

	const tier_validation_context context = game->running_validation_context();
	if (context.npc_configs == nullptr || context.drop_tables == nullptr)
	{
		hb::console::error("scattersmoke: npc configs or drop tables are not loaded.");
		return;
	}
	if (npc_id < 0 || npc_id >= context.npc_config_count ||
		context.npc_configs[npc_id] == nullptr)
	{
		hb::console::error("scattersmoke: no npc config {}", npc_id);
		return;
	}
	const CNpc& npc = *context.npc_configs[npc_id];

	const int table_id = (stage == 1) ? npc.m_stage1_table_id : npc.m_stage2_table_id;
	const auto found = context.drop_tables->find(table_id);
	if (found == context.drop_tables->end())
	{
		hb::console::error("scattersmoke: npc {} '{}' has no stage-{} table (id {})",
			npc_id, npc.m_npc_name, stage, table_id);
		return;
	}
	const drop_table& table = found->second;

	// Neutral reputation: the per-player layer is 1.0 for anyone who has never
	// been voted on, and a harness must not depend on who did the killing.
	std::vector<uint32_t> chances, tail_chances;
	bool saturated = false;
	hb::server::resolve_drop_chances(table, game->get_tier_config().generosity,
		stage, static_cast<uint8_t>(npc.m_loot_grade), 1.0,
		context.item_configs, context.item_config_count, chances, &saturated);
	hb::server::resolve_tail_chances(table, chances, tail_chances);

	const hb::server::drop_entry* guarantee = table.guarantee_entry();

	std::mt19937 rng(scatter_seed);
	std::map<int, long long> composition;
	std::map<int, long long> yield_histogram;
	long long items = 0, gold_piles = 0, gold_amount = 0;
	int observed_min = -1, observed_max = 0;

	for (int kill = 0; kill < kills; kill++)
	{
		int rolls = table.roll_count_min;
		if (table.roll_count_max > rolls)
			rolls += static_cast<int>(draw(rng) %
				static_cast<uint32_t>(table.roll_count_max - rolls + 1));

		int won_this_kill = 0;
		for (int k = 0; k < rolls; k++)
		{
			const bool guaranteed = (k < table.guaranteed_rolls);
			const std::vector<uint32_t>& roll_chances =
				guaranteed ? chances : tail_chances;

			const int index = hb::server::roll_drop_row(roll_chances, draw(rng));
			const hb::server::drop_entry* won = (index >= 0)
				? &table.entries[index]
				: (guaranteed ? guarantee : nullptr);
			if (won == nullptr) continue;

			int min_count = 0, max_count = 0;
			hb::server::resolve_entry_counts(*won,
				static_cast<int>(npc.m_gold_dice_min),
				static_cast<int>(npc.m_gold_dice_max), min_count, max_count);
			const long long count = roll_count(*won, min_count, max_count, rng);
			if (won->item_id == hb::shared::item::ItemId::Gold)
			{
				if (count <= 0) continue;
				gold_piles++;
				gold_amount += count;
			}

			composition[won->item_id]++;
			won_this_kill++;
			items++;
		}

		yield_histogram[won_this_kill]++;
		if (observed_min < 0 || won_this_kill < observed_min) observed_min = won_this_kill;
		if (won_this_kill > observed_max) observed_max = won_this_kill;
	}

	const double per_kill = static_cast<double>(items) / kills;
	hb::console::write(std::format(
		"scattersmoke: npc {} '{}' grade {}, stage-{} table {} '{}', {} kills, seed {}",
		npc_id, npc.m_npc_name, npc.m_loot_grade, stage, table.id, table.name,
		kills, scatter_seed), console_color::bright);
	hb::console::write(std::format(
		"  {} rolls a kill: {} guaranteed head -> item {}, {} tail at {}x rarity",
		table.roll_count_min, table.guaranteed_rolls, table.guarantee_item_id,
		table.tail_rolls(table.roll_count_min), table.tail_rarity_divisor));
	if (saturated)
		hb::console::write("  SATURATED - rows were scaled back to 1.0",
			console_color::warning);
	hb::console::write(std::format(
		"  items per kill: min {} max {} mean {:.3f}",
		observed_min < 0 ? 0 : observed_min, observed_max, per_kill));
	hb::console::write(std::format(
		"  gold: {:.3f} piles a kill, {:.0f} a pile, {:.0f} a kill",
		static_cast<double>(gold_piles) / kills,
		gold_piles > 0 ? static_cast<double>(gold_amount) / gold_piles : 0.0,
		static_cast<double>(gold_amount) / kills));

	hb::console::write("  composition, most common first:", console_color::muted);
	std::vector<std::pair<int, long long>> ranked(composition.begin(), composition.end());
	std::sort(ranked.begin(), ranked.end(),
		[](const auto& a, const auto& b) {
			return a.second != b.second ? a.second > b.second : a.first < b.first;
		});
	for (const auto& [item_id, count] : ranked)
	{
		const CItem* config = (item_id >= 0 && item_id < context.item_config_count)
			? context.item_configs[item_id] : nullptr;
		const double rate = static_cast<double>(count) / kills;
		hb::console::write(std::format("    {:>5}  {:<32}{:>10.4f} a kill{}",
			item_id, config != nullptr ? config->m_name : "?", rate,
			item_id == table.guarantee_item_id ? "  (guarantee)" : ""));
	}

	// Machine-readable, for the Windows/Linux byte-identity diff.
	for (const auto& [n, count] : yield_histogram)
		hb::console::write(std::format("SCATTERSMOKE YIELD {} {}", n, count));
	for (const auto& [item_id, count] : ranked)
		hb::console::write(std::format("SCATTERSMOKE ITEM {} {}", item_id, count));
	hb::console::write(std::format("SCATTERSMOKE TOTALS {} {} {}",
		items, gold_piles, gold_amount));
}
