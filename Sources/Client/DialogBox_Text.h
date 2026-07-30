#pragma once

#include "IDialogBox.h"
#include "UITheme.h"

class DialogBox_Text : public IDialogBox
{
public:
	DialogBox_Text(CGame* game);
	~DialogBox_Text() override = default;

	void on_draw() override;
	bool on_click() override;
	PressResult on_press() override;

	bool on_enable(int type, int64_t v1, int v2, const char* string) override;

	char m_mode{};
	short m_scroll_view{};

private:
	int get_total_lines() const;

	static constexpr ui_rect btn_close = ui_layout::btn_right;
	static constexpr ui_rect area_scroll = hb::client::ui_theme::grab_area(
		hb::client::ui_theme::list_gutter);
};
