#pragma once
#include "IDialogBox.h"
#include "CombatSummary.h"
#include "lan_eng.h"
#include "Item/ItemEnums.h"

#include <string>

struct EquipSlotLayout
{
	hb::shared::item::EquipPos equipPos;
	int offsetX;
	int offsetY;
};

class DialogBox_Character : public IDialogBox
{
public:
	DialogBox_Character(CGame* game);
	~DialogBox_Character() override = default;

	void on_draw() override;
	bool on_click() override;
	bool on_double_click() override;
	PressResult on_press() override;
	bool on_item_drop() override;

	bool cancels_text_input_on_enable() const override { return false; }

	// Which side pane is showing, or none when it is folded away.
	enum class pane : uint8_t
	{
		none = 0,
		combat,
		gear,
	};

private:
	// --- side pane -------------------------------------------------------
	// The panel's own column is 270 wide and every offset in it was measured
	// against that, so the pane is added beside it rather than by re-laying the
	// column out. A flat panel has no fixed background, which is the whole
	// reason the dialog can change width at all.
	static constexpr int column_w = 270;
	static constexpr int pane_w = 200;
	static constexpr int panel_h = 376;

	// Both tabs sit inside the 22px bar, so the active tab's underline lands on
	// the bar's own bottom edge rather than below it.
	static constexpr ui_rect tab_combat{ column_w + 4, 1, 92, 20 };
	static constexpr ui_rect tab_gear  { column_w + 100, 1, 92, 20 };
	// The handle that unfolds a closed pane, on the column's right edge.
	static constexpr ui_rect tab_open  { column_w - 14, 1, 14, 20 };

	// The column's value rows, named once so a caption and its value share one y.
	// The pitch is irregular because it is transcribed from the art.
	enum row_index
	{
		row_level, row_exp, row_next_exp, row_health,
		row_mana, row_stamina, row_max_load, row_ek_count,
		row_count
	};
	static constexpr int row_y[row_count] = { 106, 125, 142, 173, 191, 208, 240, 257 };
	static constexpr const char* row_caption[row_count] = {
		UI_CHARACTER_LEVEL, UI_CHARACTER_EXP, UI_CHARACTER_NEXT_EXP, UI_CHARACTER_HEALTH,
		UI_CHARACTER_MANA, UI_CHARACTER_STAMINA, UI_CHARACTER_MAX_LOAD, UI_CHARACTER_EK_COUNT,
	};
	// The six-stat block below the rule: two rows, three columns.
	static constexpr int stat_row_y[2] = { 285, 304 };

	void draw_pane(int sX, int sY);
	void draw_combat_pane(int x, int y, int w, const hb::client::combat_summary& summary);
	void draw_gear_pane(int x, int y, int w, const hb::client::combat_summary& summary);
	// One caption/value row of the pane. Returns the y of the next row.
	int pane_row(int x, int y, int w, const char* caption, const std::string& value,
		bool notable = false) const;
	void set_pane(pane which);
	bool toggle_pane(pane which);

	pane m_pane = pane::combat;

	// Helper methods
	void draw_stat(int x1, int x2, int y, int baseStat, int angelicBonus, int gearBonus);
	void draw_equipped_item(hb::shared::item::EquipPos equipPos, int drawX, int drawY,
		const char* equip_poi_status, bool highlight, int spriteOffset = 0);
	void draw_male_character(short sX, short sY, short mouse_x, short mouse_y,
		const char* equip_poi_status, char& collison);
	void draw_female_character(short sX, short sY, short mouse_x, short mouse_y,
		const char* equip_poi_status, char& collison);

	// Shared helpers
	void build_equip_status_array(char (&equip_poi_status)[hb::shared::item::DEF_MAXITEMEQUIPPOS]) const;
	char find_equip_item_at_point(short mouse_x, short mouse_y, short sX, short sY,
		const char* equip_poi_status) const;

	static constexpr ui_rect btn_quest{15, 340, 75, 21};
	static constexpr ui_rect btn_party{98, 340, 75, 21};
	static constexpr ui_rect btn_levelup{180, 340, 75, 21};
};
