#pragma once
#include "IDialogBox.h"
#include "UITheme.h"

class DialogBox_NpcTalk : public IDialogBox
{
public:
	DialogBox_NpcTalk(CGame* game);
	~DialogBox_NpcTalk() override = default;

	void on_draw() override;
	bool on_click() override;
	PressResult on_press() override;

	bool on_enable(int type, int64_t v1, int v2, const char* string) override;
	bool on_disable() override;

	enum class mode : uint8_t
	{
		ok_only = 0,
		accept_decline = 1,
		next = 2,
	};

	mode m_mode{mode::ok_only};
	short m_scroll_view{};
	int m_text_line_count{};
	int m_dialog_id{};

private:
	int get_total_lines() const;
	void draw_buttons(short sX, short sY);
	void draw_text_content(short sX, short sY);
	void draw_scroll_bar(short sX, short sY, int total_lines);
	void handle_scroll_bar_drag(short sX, short sY, short mouse_x, short mouse_y, int total_lines, char lb);

	static constexpr ui_rect btn_left = ui_layout::btn_left;
	static constexpr ui_rect btn_right = ui_layout::btn_right;
	static constexpr ui_rect link_next{190, 296, 89, 21};
	static constexpr ui_rect area_scroll = hb::client::ui_theme::grab_area(
		hb::client::ui_theme::list_gutter);
};
