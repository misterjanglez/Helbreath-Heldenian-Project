#pragma once

#include "BalanceConstants.h"
#include "FormulaEngine.h"
#include <algorithm>
#include <cstdint>
#include <utility>

namespace hb::shared::calc
{

// ============================================================================
// Typed stat args — each stat is a distinct type with its stat_map key
// ============================================================================

struct vit              { double value; static constexpr const char* key() { return "vit"; } };
struct str              { double value; static constexpr const char* key() { return "str"; } };
struct dex              { double value; static constexpr const char* key() { return "dex"; } };
struct intel            { double value; static constexpr const char* key() { return "int"; } };
struct mag              { double value; static constexpr const char* key() { return "mag"; } };
struct chr              { double value; static constexpr const char* key() { return "chr"; } };
struct level            { double value; static constexpr const char* key() { return "level"; } };
struct angelic_str      { double value; static constexpr const char* key() { return "angelic_str"; } };
struct angelic_mag      { double value; static constexpr const char* key() { return "angelic_mag"; } };
struct angelic_int      { double value; static constexpr const char* key() { return "angelic_int"; } };
struct angelic_dex      { double value; static constexpr const char* key() { return "angelic_dex"; } };

// Fold variadic args into stat_map
template <typename Arg>
void collect_arg(stat_map& m, const Arg& arg) { m[Arg::key()] = arg.value; }

template <typename... Args>
stat_map build_stat_map(Args&&... args)
{
	stat_map m;
	(collect_arg(m, std::forward<Args>(args)), ...);
	return m;
}

// ============================================================================
// Max Resource Calculations (DB formulas)
// ============================================================================

template <typename... Args>
int max_hp(const formula_engine& fe, Args&&... args)
{
	return fe.evaluate("max_hp", build_stat_map(std::forward<Args>(args)...));
}

template <typename... Args>
int max_mp(const formula_engine& fe, Args&&... args)
{
	return fe.evaluate("max_mp", build_stat_map(std::forward<Args>(args)...));
}

template <typename... Args>
int max_sp(const formula_engine& fe, Args&&... args)
{
	return fe.evaluate("max_sp", build_stat_map(std::forward<Args>(args)...));
}

// ============================================================================
// Carry Weight (DB formula)
// ============================================================================

template <typename... Args>
int max_load(const formula_engine& fe, Args&&... args)
{
	return fe.evaluate("max_load", build_stat_map(std::forward<Args>(args)...));
}

// ============================================================================
// Experience (DB formula)
// ============================================================================

template <typename... Args>
uint32_t level_exp(const formula_engine& fe, Args&&... args)
{
	return static_cast<uint32_t>(fe.evaluate("level_exp", build_stat_map(std::forward<Args>(args)...)));
}

// ============================================================================
// HP Regen (DB formulas)
// ============================================================================

template <typename... Args>
int hp_regen_max_roll(const formula_engine& fe, Args&&... args)
{
	return fe.evaluate("hp_regen_max_roll", build_stat_map(std::forward<Args>(args)...));
}

template <typename... Args>
int hp_regen_min_roll(const formula_engine& fe, Args&&... args)
{
	return fe.evaluate("hp_regen_min_roll", build_stat_map(std::forward<Args>(args)...));
}

template <typename... Args>
int hp_regen_roll_variance(const formula_engine& fe, Args&&... args)
{
	return fe.evaluate("hp_regen_roll_variance", build_stat_map(std::forward<Args>(args)...));
}

// ============================================================================
// MP Regen (DB formulas)
// ============================================================================

template <typename... Args>
int mp_regen_max_roll(const formula_engine& fe, Args&&... args)
{
	return fe.evaluate("mp_regen_max_roll", build_stat_map(std::forward<Args>(args)...));
}

template <typename... Args>
int mp_regen_min_roll(const formula_engine& fe, Args&&... args)
{
	return fe.evaluate("mp_regen_min_roll", build_stat_map(std::forward<Args>(args)...));
}

template <typename... Args>
int mp_regen_roll_variance(const formula_engine& fe, Args&&... args)
{
	return fe.evaluate("mp_regen_roll_variance", build_stat_map(std::forward<Args>(args)...));
}

// ============================================================================
// SP Regen (DB formulas)
// ============================================================================

template <typename... Args>
int sp_regen_max_roll(const formula_engine& fe, Args&&... args)
{
	return fe.evaluate("sp_regen_max_roll", build_stat_map(std::forward<Args>(args)...));
}

template <typename... Args>
int sp_regen_min_roll(const formula_engine& fe, Args&&... args)
{
	return fe.evaluate("sp_regen_min_roll", build_stat_map(std::forward<Args>(args)...));
}

template <typename... Args>
int sp_regen_roll_variance(const formula_engine& fe, Args&&... args)
{
	return fe.evaluate("sp_regen_roll_variance", build_stat_map(std::forward<Args>(args)...));
}

// ============================================================================
// Physical Absorption
//
// A blow picks one hit zone and only that zone's armour absorbs it. The server
// rolls dice(1,10000) and splits it four ways (CombatManager, the hit_point
// switch); the thresholds below are those splits in per-ten-thousand, stated
// here so the character panel's average and the resolution that produces it
// cannot drift. The combat switch should adopt these in place of its literals.
//
// There is deliberately no "total physical absorption": the zones are
// alternatives, not addends, and summing them would overstate by about 4x.
// ============================================================================

namespace hit_zone
{
	constexpr int body_ppm  = 4999;   // 1..4999
	constexpr int legs_ppm  = 2500;   // 5000..7499  (leggings + boots, summed)
	constexpr int arms_ppm  = 1500;   // 7500..8999
	constexpr int head_ppm  = 1002;   // 9000..10000
	constexpr int total_ppm = body_ppm + legs_ppm + arms_ppm + head_ppm;

	// Each zone is clamped before it is applied, so the cap belongs to the zone
	// rather than to the sum.
	constexpr int max_absorb_pct = 80;
}

// The share of an average blow that armour absorbs: each zone's absorption
// clamped, then weighted by how often that zone is the one struck. This is what
// a single "physical absorption" number can honestly mean.
inline int physical_absorption_average(int body, int legs, int arms, int head)
{
	const auto zone = [](int value) { return std::clamp(value, 0, hit_zone::max_absorb_pct); };

	return (zone(body) * hit_zone::body_ppm
	      + zone(legs) * hit_zone::legs_ppm
	      + zone(arms) * hit_zone::arms_ppm
	      + zone(head) * hit_zone::head_ppm) / hit_zone::total_ppm;
}

// ============================================================================
// Percentage Bonus
//
// How a recovery percentage is applied to a rolled amount. Not a DB formula —
// arithmetic the server's RegenManager has inline, reproduced here because the
// character panel prints the resulting range and must round it the same way the
// server does. Stated once so the two cannot drift; RegenManager should adopt
// this in place of its own copy, exactly as the defence-ratio pair below.
// ============================================================================

inline int apply_percent_bonus(int base, int percent)
{
	if (percent == 0) return base;
	return base + static_cast<int>(static_cast<double>(percent) / 100.0 * static_cast<double>(base));
}

// ============================================================================
// Weapon Swing Formulas (computed directly from balance constants)
// ============================================================================

inline int attack_delay(int weapon_spd, int player_str, int player_angelic_str)
{
	return std::max(weapon_spd - (player_str + player_angelic_str) / balance::swing_str_divisor, 0);
}

inline int swing_time(int atk_delay_value)
{
	return balance::swing_frames * (balance::base_frame_time + atk_delay_value * balance::delay_per_frame);
}

// ============================================================================
// Defence Ratio
//
// Not a DB formula — arithmetic hardcoded in the item-equip pass (the server's
// ItemManager) that the client also has to reproduce to put a defence number on
// the character panel. Stated here so the two sides cannot drift; the server's
// equip pass should adopt these in place of its inline copies.
// ============================================================================

// Unarmoured base, before anything worn is added.
inline int defense_ratio_base(int player_dex)
{
	return player_dex * 2;
}

// What one equipped defence piece adds: its own defence value, plus half of the
// refinement percentage a custom-made piece carries (0 for an ordinary one).
inline int defense_ratio_from_item(int item_defense, int custom_refine_percent)
{
	const double refine = (static_cast<double>(custom_refine_percent) / 100.0) * item_defense;
	return item_defense + static_cast<int>(refine / 2.0);
}

// ============================================================================
// Marquee speed scaling (Item Tiers spec §5)
// ============================================================================

// The (1 - r) every Marquee speed line applies: cast-time reduction against the
// Magic animation, move speed against locomotion frames and tile duration. The
// percentage is clamped so no data row can stop time, and the result never
// reaches zero.
inline int apply_speed_reduction(int base_ms, int reduction_pct)
{
	return std::max(base_ms * (100 - std::clamp(reduction_pct, 0, 99)) / 100, 1);
}

// ============================================================================
// Movement Pacing (Item Tiers spec §5)
// ============================================================================
//
// `move_speed_pct` throughout is the Marquee move-speed total the server
// publishes on PlayerStatusData.

// How long one walk/run frame takes for a player in a given state — the
// client's animation math (MapData.cpp status-modifier block), where haste and
// frozen adjust the *frame* additively.
inline int move_frame_time(bool running, bool haste, bool frozen, int move_speed_pct)
{
	const int base = running ? balance::run_frame_time : balance::walk_frame_time;

	int frame = base;
	if (frozen) frame += base >> 2;
	if (haste)  frame -= balance::haste_frame_bonus;

	return apply_speed_reduction(frame, move_speed_pct);
}

// How long the client interpolates the slide across one tile
// (EntityMotion::get_duration_for_action), where haste and frozen scale the
// whole *duration* multiplicatively.
inline int move_interpolation_time(bool running, bool haste, bool frozen, int move_speed_pct)
{
	int duration = balance::move_frames * (running ? balance::run_frame_time : balance::walk_frame_time);
	if (haste)  duration = duration * balance::haste_move_duration_pct / 100;
	if (frozen) duration = duration * balance::frozen_move_duration_pct / 100;

	return apply_speed_reduction(duration, move_speed_pct);
}

// The fastest a legal client can cross one tile — the expected gap between two
// consecutive move packets, and so the basis of the anti-cheat move floor.
//
// Two client paths release the next move command and a legal client uses
// whichever fires first, so this is the faster of the two (Screen_OnGame.cpp):
//   - animation completion — move_frames x the per-frame time;
//   - quick-actions chaining — 95% of the interpolated tile duration.
// The two models agree with no status effects and diverge under haste/frozen
// (a hasted walk is 432 ms by frames but 372 ms by interpolation), so taking
// the minimum is what keeps the floor below every legal cadence.
//
// Cycle 4-C folded Marquee move speed into BOTH terms, so the speed axis alone
// never splits them; the haste/frozen divergence is older than Item Tiers and
// outlives it. Collapsing the two is a balance decision, not a refactor —
// additive would slow hasted movement 372 -> 410 ms/tile, multiplicative would
// speed the hasted walk animation past the original client's 54 ms/frame — so
// it stays an open question for the design owner rather than a silent pick.
inline int move_tile_time(bool running, bool haste, bool frozen, int move_speed_pct)
{
	const int animation_ms = balance::move_frames * move_frame_time(running, haste, frozen, move_speed_pct);
	const int chained_ms = move_interpolation_time(running, haste, frozen, move_speed_pct)
		* balance::quick_action_unlock_pct / 100;

	return std::max(std::min(animation_ms, chained_ms), 1);
}

} // namespace hb::shared::calc
