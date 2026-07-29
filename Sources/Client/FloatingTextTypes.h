#pragma once

#include <cstdint>
#include "CommonTypes.h"

// ---------------------------------------------------------------
// Floating text category enums
// ---------------------------------------------------------------

enum class chat_text_type : uint8_t {
	player_chat
};

enum class damage_text_type : uint8_t {
	Small,      // <12 pts  - small sprite font
	Medium,     // 12-39 pts, Immune, Failed - medium sprite font
	Large,      // 40+ pts, Critical! - large sprite font
};

// Damage-over-time ticks. Their own category rather than a damage_text_type,
// because the point is the color: a player has to be able to tell poison from
// bleed at a glance, and the Damage sizes are all one yellow.
enum class dot_text_type : uint8_t {
	poison,     // green
	bleed,      // red
};

enum class notify_text_type : uint8_t {
	skill_change,    // "+2% Mining" - yellow, delayed 650ms
	magic_cast_name,  // "Fire Ball!" - red sprite font
	LevelUp,        // "Level up!" - large sprite font
	enemy_kill,      // "Enemy Kill!" - large sprite font
};

// ---------------------------------------------------------------
// Animation parameters for each floating text type
// ---------------------------------------------------------------

struct AnimParams {
	uint32_t m_lifetime_ms;    // Total display duration
	uint32_t m_show_delay_ms;   // Delay before visible (0 = instant)
	int m_start_offset_y;        // Starting Y offset above entity foot (pixels)
	int m_rise_pixels;          // Total upward rise distance
	int m_rise_duration_ms;      // Time to reach final position
	int m_font_offset;          // Offset from SprFont3_0 (0=large, 1=medium, 2=small)
	hb::shared::render::Color m_color;              // Text color
	bool m_use_sprite_font;      // true = sprite font, false = renderer text
};

// ---------------------------------------------------------------
// Parameter tables (constexpr)
// ---------------------------------------------------------------

namespace FloatingTextParams {

inline constexpr AnimParams Chat[] = {
	// player_chat: white renderer text, 4s, fast rise
	{ 4000, 0, 55, 10, 200, 0, GameColors::UIWhite, false },
};

// NOTE: m_color on sprite-font rows is the color the draw actually uses. It
// used to be ignored there — the sprite path hardcoded Yellow2x — so these
// rows said UIDmgYellow while rendering Yellow2x. They now state what they
// render; the values are unchanged pixels, only honestly declared.
inline constexpr AnimParams Damage[] = {
	// Small:  yellow sprite font (small), 200ms, fast rise
	{ 500, 0, 55, 20, 200, 2, GameColors::Yellow2x, true },
	// Medium: yellow sprite font (medium), 200ms, fast rise
	{ 500, 0, 55, 20, 200, 1, GameColors::Yellow2x, true },
	// Large:  yellow sprite font (large), 200ms, fast rise
	{ 500, 0, 55, 20, 200, 0, GameColors::Yellow2x, true },
};

// DoT ticks: same sprite font and motion as a small damage number, so they read
// as damage, but colored by source. Slightly longer-lived than a hit number
// because a tick is not accompanied by an attack animation to draw the eye.
inline constexpr AnimParams Dot[] = {
	// poison: green sprite font (small). PoisonText is already this codebase's
	// poison color — the HUD draws a poisoned HP number in it.
	{ 700, 0, 55, 20, 200, 2, GameColors::PoisonText, true },
	// bleed:  red sprite font (small)
	{ 700, 0, 55, 20, 200, 2, GameColors::Red4x, true },
};

inline constexpr AnimParams Notify[] = {
	// skill_change: yellow renderer text, 4s, delayed 650ms
	// (renderer-text path, which always honored m_color — unchanged)
	{ 4000, 0, 55, 10, 80, 0, GameColors::UIDmgYellow, false },
	// magic_cast_name: red sprite font (large), 2s
	// (drawn by its own branch, which hardcodes Red4x; row now says so)
	{ 2000, 0, 55, 15, 200, 0, GameColors::Red4x, true },
	// LevelUp: yellow sprite font (large), 2s, fast rise
	{ 2000, 0, 55, 10, 80, 0, GameColors::Yellow2x, true },
	// enemy_kill: yellow sprite font (large), 2s, fast rise
	{ 2000, 0, 55, 10, 80, 0, GameColors::Yellow2x, true },
};

} // namespace FloatingTextParams
