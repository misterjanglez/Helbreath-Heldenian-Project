#pragma once

namespace hb::shared::balance
{
	constexpr int angelic_bonus          = 16;
	constexpr int swing_str_divisor      = 13;
	constexpr int swing_frames           = 8;
	constexpr int base_frame_time        = 78;
	constexpr int delay_per_frame        = 12;
	constexpr int run_frame_time         = 39;
	constexpr int walk_frame_time        = 70;
	// Frames the client plays to cross one tile (PlayerAnim Move/Run run
	// frames 0..max_frame). The server's move-speed floor multiplies by this,
	// so it cannot live in the client alone.
	constexpr int move_frames            = 8;
	// Per-frame haste bonus, as the client derives it (MapData.cpp,
	// Client/Game.cpp swing timing). Named so the 2.3 is auditable in one
	// place instead of re-derived at every site.
	constexpr int haste_frame_bonus      = static_cast<int>(run_frame_time / 2.3);
	// Tile-duration status factors used by the interpolation path
	// (Client/EntityMotion.cpp), and the progress at which quick-actions
	// releases the next move command (Client/Screen_OnGame.cpp).
	constexpr int haste_move_duration_pct   = 70;
	constexpr int frozen_move_duration_pct  = 125;
	constexpr int quick_action_unlock_pct   = 95;
	constexpr int weight_units_per_stone = 1000;
	constexpr int equip_str_threshold    = 11 * weight_units_per_stone;
	// Ceiling on the worn set's accumulated mana saving. The server clamps
	// m_mana_save_ratio to it in the equip switch and the client clamps its own
	// total to it at cast time; both had the bare 80. The server's copy is a
	// literal still — that switch belongs to a different workstream.
	constexpr int mana_save_max          = 80;
}
