#pragma once
#include "IDialogBox.h"

// The stat grid shared by the level-up dialog and its majestic sibling: two
// value wells and a +/- pair per row, six rows at a 19px pitch. It used to be
// one pak frame drawn at (16, 100), so these numbers are transcribed from it �
// the wells sit exactly where the two value columns are already drawn, and the
// buttons where the old highlight sprites were.
namespace stat_grid
{
	// The art's box was 24 wide, which was enough for the two digits it ever
	// showed; the value is drawn at x 73 and can reach three, so this one is
	// sized to the text rather than to the frame it replaces.
	constexpr int points_x = 69, points_y = 100, points_w = 30, points_h = 17;

	constexpr int current_x = 104, current_w = 33;
	constexpr int pending_x = 156, pending_w = 33;
	constexpr int well_h = 15;

	// A row's arrows sit two pixels below its wells; the pak highlight frames
	// were 12x12 while the hit box tested half that, so the click target and the
	// button a player could see disagreed.
	constexpr int arrow_size = 12;
	constexpr int arrow_drop = 2;
	constexpr int inc_x = 195, dec_x = 210;

	constexpr ui_rect increase(int arrow_y) { return { inc_x, arrow_y, arrow_size, arrow_size }; }
	constexpr ui_rect decrease(int arrow_y) { return { dec_x, arrow_y, arrow_size, arrow_size }; }
}

class DialogBox_LevelUpSetting : public IDialogBox
{
public:
	DialogBox_LevelUpSetting(CGame* game);
	~DialogBox_LevelUpSetting() override = default;

	void on_draw() override;
	bool on_click() override;

	bool on_enable(int type, int64_t v1, int v2, const char* string) override;

	int m_initial_lu_points{};

private:
	void draw_stat_row(short sX, short sY, int y_offset, const char* label,
	                 int current_stat, int pending_change, short mouse_x, short mouse_y,
	                 int arrow_y_offset, bool can_increase, bool can_decrease);

	bool handle_stat_click(short mouse_x, short mouse_y, short sX, short sY,
	                     int y_offset, int& current_stat, int16_t& pending_change);
};
