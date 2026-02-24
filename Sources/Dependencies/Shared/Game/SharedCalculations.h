#pragma once

#include "FormulaEngine.h"
#include <cstdint>
#include <stdexcept>
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
struct angelic_chr      { double value; static constexpr const char* key() { return "angelic_chr"; } };
struct total_stats      { double value; static constexpr const char* key() { return "total_stats"; } };
struct base_stat_value  { double value; static constexpr const char* key() { return "base_stat_value"; } };
struct creation_stat_bonus { double value; static constexpr const char* key() { return "creation_stat_bonus"; } };
struct max_level           { double value; static constexpr const char* key() { return "max_level"; } };
struct weapon_speed        { double value; static constexpr const char* key() { return "weapon_speed"; } };
struct attack_delay_value  { double value; static constexpr const char* key() { return "attack_delay_value"; } };

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
// Max Resource Calculations
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
// Carry Weight
// ============================================================================

template <typename... Args>
int max_load(const formula_engine& fe, Args&&... args)
{
	return fe.evaluate("max_load", build_stat_map(std::forward<Args>(args)...));
}

// ============================================================================
// Experience
// ============================================================================

template <typename... Args>
uint32_t level_exp(const formula_engine& fe, Args&&... args)
{
	return static_cast<uint32_t>(fe.evaluate("level_exp", build_stat_map(std::forward<Args>(args)...)));
}

// ============================================================================
// Level-Up Point Pool
// ============================================================================

template <typename... Args>
int level_up_points(const formula_engine& fe, Args&&... args)
{
	return fe.evaluate("level_up_pool", build_stat_map(std::forward<Args>(args)...));
}

// ============================================================================
// Max Stat Value
// ============================================================================

template <typename... Args>
int max_stat_value(const formula_engine& fe, Args&&... args)
{
	return fe.evaluate("max_stat_value", build_stat_map(std::forward<Args>(args)...));
}

// ============================================================================
// Constant formulas (no args — evaluate cross-references only)
// ============================================================================

inline int levelup_stat_gain(const formula_engine& fe)
{
	return fe.evaluate("levelup_stat_gain", {});
}

inline int base_stat_total(const formula_engine& fe)
{
	return fe.evaluate("base_stat_total", {});
}

// ============================================================================
// Weapon Swing Formulas
// ============================================================================

template <typename... Args>
int attack_delay(const formula_engine& fe, Args&&... args)
{
	return fe.evaluate("attack_delay", build_stat_map(std::forward<Args>(args)...));
}

template <typename... Args>
int swing_time(const formula_engine& fe, Args&&... args)
{
	return fe.evaluate("swing_time", build_stat_map(std::forward<Args>(args)...));
}

inline int swing_str_divisor(const formula_engine& fe) { return fe.evaluate("swing_str_divisor", {}); }
inline int swing_frames(const formula_engine& fe)      { return fe.evaluate("swing_frames", {}); }
inline int base_frame_time(const formula_engine& fe)    { return fe.evaluate("base_frame_time", {}); }
inline int delay_per_frame(const formula_engine& fe)    { return fe.evaluate("delay_per_frame", {}); }
inline int run_frame_time(const formula_engine& fe)     { return fe.evaluate("run_frame_time", {}); }

} // namespace hb::shared::calc
