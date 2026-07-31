#pragma once
#include "IDialogBox.h"
#include "CombatSummary.h"
#include "UITheme.h"
#include "lan_eng.h"
#include "Item/ItemEnums.h"

#include <string>
#include <vector>

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
	// --- geometry --------------------------------------------------------
	// A flat panel has no fixed background, so the column can be as wide as its
	// content needs. It needed to be: values were CENTRED in a 70px box (that is
	// what put_aligned_string does), so a long one grew out of the box in both
	// directions and walked into its own caption — "33.00/265.00" is already
	// 66px of the 70, and a ten-digit EXP is well past it.
	//
	// The paperdoll is not re-laid out and its offsets are untouched. The male
	// body sprite lands at x 44..91 (pivot (-127,-167) against a draw origin of
	// (171,290)), so every pixel added here goes to the right of it.
	static constexpr int column_w = 320;
	// Widened by the scrollbar gutter rather than having it taken out of the
	// content: the bar used to sit one pixel past the value column, so a
	// right-aligned figure ran straight into it. The pane grew by exactly the
	// gutter it now reserves, which leaves the readable width slightly better
	// than it was (183) instead of 17px worse.
	static constexpr int pane_w = 220;
	static constexpr int panel_h = 460;

	// --- the side pane's scrollable content area -------------------------
	// A fully modified set runs both panes past the bottom of the panel: the
	// Combat tab reaches 27 rows against the 20 that fit, and the Gear tab can
	// carry a line per catalog row plus a heading per bucket. The Combat tab had
	// no overflow handling at all and simply drew off the end; the Gear tab
	// counted what it dropped and said "+N more", which is a truncation wearing
	// a receipt. Both scroll now.
	static constexpr int pane_margin    = hb::client::ui_theme::metrics::margin;
	static constexpr int pane_pitch     = hb::client::ui_theme::metrics::row_pitch;
	static constexpr int pane_top       = hb::client::ui_theme::metrics::tab_height + pane_margin;
	static constexpr int pane_content_h = panel_h - pane_top - pane_margin;

	// The bar gets a reserved gutter, inset by a full margin on BOTH sides:
	//
	//   |<-margin->|<---- content ---->|<-margin->|bar|<-margin->|
	//
	// The first attempt gave it a 6px gap on the left and left the panel's own
	// 10px margin on the right, which put the wider gap on the side away from
	// the text — so the value column still read as touching the bar while the
	// space sat where nothing needed it. One spacing unit throughout is both
	// easier to see and the rule the rest of the theme already follows.
	//
	// The gutter is reserved whether or not the pane is currently long enough to
	// scroll, so rows never shift sideways the moment a bar appears, and
	// grab_area still inflates the thin bar into a comfortable target.
	static constexpr int pane_bar_w = 7;
	static constexpr int pane_content_w = pane_w - pane_margin * 3 - pane_bar_w;
	static constexpr ui_rect pane_scroll_bar{
		column_w + pane_margin * 2 + pane_content_w, pane_top, pane_bar_w, pane_content_h };

	// Caption column and value column. Caption left, value right-aligned to the
	// panel's inner edge — the shape ui_theme::label_value already uses in the
	// pane, so the two halves of this dialog stop being laid out by different
	// rules. x 130 clears the paperdoll.
	static constexpr int caption_x = 130;
	static constexpr int value_right = 306;

	// Both tabs sit inside the 22px bar, so the active tab's underline lands on
	// the bar's own bottom edge rather than below it.
	// Split the bar between them, with the same inset at each end as between the
	// pair, so they stay balanced whenever the pane's width changes. Hand-measured
	// 92px tabs would have left 24px of empty bar at the right once it widened.
	static constexpr int tab_inset = 4;
	static constexpr int tab_w = (pane_w - tab_inset * 3) / 2;
	static constexpr ui_rect tab_combat{ column_w + tab_inset, 1, tab_w, 20 };
	static constexpr ui_rect tab_gear  { column_w + tab_inset * 2 + tab_w, 1, tab_w, 20 };
	// The handle that unfolds a closed pane, on the column's right edge.
	static constexpr ui_rect tab_open  { column_w - 14, 1, 14, 20 };

	// The column's value rows, named once so a caption and its value share one y.
	//
	// The pitch is uniform now — ui_theme::metrics::row_pitch, in three groups
	// with a half-pitch between them. It used to be the art's own irregular
	// 106/125/142/173/191/208/240/257, kept long after the art it was traced
	// from stopped being drawn; the groups are what that spacing was expressing.
	enum row_index
	{
		row_level, row_exp, row_next_exp,
		row_health, row_mana, row_stamina, row_max_load,
		row_ek_count, row_pk_count, row_reputation, row_contribution, row_hunger,
		row_count
	};
	static constexpr int row_y[row_count] = {
		106, 126, 146,
		176, 196, 216, 236,
		266, 286, 306, 326, 346,
	};
	static constexpr const char* row_caption[row_count] = {
		UI_CHARACTER_LEVEL, UI_CHARACTER_EXP, UI_CHARACTER_NEXT_EXP,
		UI_CHARACTER_HEALTH, UI_CHARACTER_MANA, UI_CHARACTER_STAMINA, UI_CHARACTER_MAX_LOAD,
		UI_CHARACTER_EK_COUNT, UI_CHARACTER_PK_COUNT, UI_CHARACTER_REPUTATION,
		UI_CHARACTER_CONTRIBUTION, UI_CHARACTER_HUNGER,
	};

	// The six-stat block below the rule: two rows, three columns. Each column is
	// caption-left / value-right like the rows above, and wide enough for a
	// boosted stat — "34 (+3)" was already overflowing a 40px box.
	static constexpr int stat_row_y[2] = { 376, 396 };
	static constexpr int stat_caption_x[3] = { 16, 118, 220 };
	static constexpr int stat_value_right[3] = { 110, 212, 314 };

	// The two rules, and the empty-slot marks for the three accessory positions.
	//
	// Necklaces and rings have no paperdoll sprite at all — no male/female block
	// in ItemSpriteMetadata.json — so get_item_draw redirects them to the pack
	// atlas and draws the inventory icon at the slot offset. Worn, they show;
	// empty, there was nothing to say the slot existed. Anchored on the measured
	// centre of every item that can land there (neck x 16..62 y 89..145 across
	// 37 items, right ring x 25..40 y 186..202, left ring x 90..112 y 170..194).
	static constexpr int rule_top_y = 96;
	static constexpr int rule_stat_y = 366;
	static constexpr ui_rect slot_neck  { 27, 105, 24, 24 };
	static constexpr ui_rect slot_ring_r{ 20, 182, 24, 24 };
	static constexpr ui_rect slot_ring_l{ 89, 170, 24, 24 };

	// One built line of a side pane.
	//
	// Both panes are BUILT into a list and drawn afterwards, rather than drawn as
	// they are computed. A pane that scrolls has to know how tall it is before it
	// puts anything on screen, and imperative drawing can only discover that by
	// having already overrun. It is also what lets the two panes share one
	// scrolling implementation instead of growing one each.
	struct pane_line
	{
		enum class kind : uint8_t
		{
			heading,   // group title, in the active-tab colour
			row,       // caption left, value right
			rule,      // horizontal separator, centred in its own slot
			note       // dim sentence, wrapped — may be more than one slot tall
		};

		kind type = kind::row;
		std::string caption;
		std::string value;      // row only
		bool notable = false;   // row only — picks the brighter value colour
		int slots = 1;          // height in pane_pitch units
	};

	void draw_pane(int sX, int sY);
	std::vector<pane_line> build_combat_lines(const hb::client::combat_summary& summary) const;
	std::vector<pane_line> build_gear_lines(const hb::client::combat_summary& summary) const;
	// Draws the visible window of `lines`, plus the scrollbar and its input.
	void draw_pane_lines(int x, int y, int w, const std::vector<pane_line>& lines);
	// One caption/value row of the pane. Returns the y of the next row.
	int pane_row(int x, int y, int w, const char* caption, const std::string& value,
		bool notable = false) const;
	// Wrapped text sized in whole slots, so a two-line heading or note cannot be
	// left half-drawn at the bottom of the pane.
	static pane_line make_text(pane_line::kind type, const char* text);
	void set_pane(pane which);
	bool toggle_pane(pane which);

	pane m_pane = pane::combat;

	// First visible line. One position for both tabs, reset when the tab changes:
	// carrying a Gear-tab offset into a shorter Combat list would open it blank.
	int m_pane_scroll = 0;

	// Helper methods
	void draw_stat(int x_right, int y, int base_stat, int angelic_bonus, int gear_bonus);
	// The three accessory wells, drawn under whatever is worn in them.
	void draw_accessory_slots(int sX, int sY) const;
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

	static constexpr ui_rect btn_quest{16, 424, 90, 21};
	static constexpr ui_rect btn_party{115, 424, 90, 21};
	static constexpr ui_rect btn_levelup{214, 424, 90, 21};
};
