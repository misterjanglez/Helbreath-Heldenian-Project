#pragma once
#include "IDialogBox.h"

class CItem;

class DialogBox_Inventory : public IDialogBox
{
public:
	DialogBox_Inventory(CGame* game);
	~DialogBox_Inventory() override = default;

	void on_draw() override;
	bool on_click() override;
	bool on_double_click() override;
	PressResult on_press() override;
	bool on_item_drop() override;

	bool cancels_text_input_on_enable() const override { return false; }
private:
	// Layout constants
	static constexpr int ITEM_OFFSET_X = 32;
	static constexpr int ITEM_OFFSET_Y = 44;
	static constexpr int COUNT_OFFSET_X = 39;  // 29 + 10
	static constexpr int COUNT_OFFSET_Y = 51;  // 41 + 10

	// Button regions
	// The two captions were painted into the chest, so they were only ever drawn
	// on hover — the idle state was the panel. A flat panel has no lettering, so
	// both are real buttons now, sitting a few pixels higher than the plates did
	// to give a TTF caption room the art did not need.
	static constexpr ui_rect btn_upgrade{ 23, 167, 53, 16 };
	static constexpr ui_rect btn_manufacture{ 140, 167, 72, 16 };

	// Helper: draw a single inventory item with proper coloring and state
	void draw_inventory_item(CItem* item, int itemIdx, int baseX, int baseY);
};
