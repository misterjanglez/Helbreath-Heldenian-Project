// DialogBox_GuildOperation.h: the requests queue (#124, §3.1.1 deviation 2)
//
// The original's one-request-at-a-time modal (query 0x0B02 → wait → answer)
// became a queue: join/leave filings wait in guilds.db until any holder of
// the deciding permission bit rules on them, so this dialog is a list —
// every pending request with Approve/Reject beside it, newest capped by the
// wire's kGuildQueueMaxEntries with the true total shown when clipped.
//
// Openable from the guild menu (the link only shows for approvers); each
// decision sends PacketRequestGuildDecide and the ok envelope re-requests
// the queue, so the list thins as the approver works through it.
//
//////////////////////////////////////////////////////////////////////

#pragma once
#include "IDialogBox.h"

#include <cstdint>

class DialogBox_GuildOperation : public IDialogBox
{
public:
	DialogBox_GuildOperation(CGame* game);
	~DialogBox_GuildOperation() override = default;

	void on_draw() override;
	bool on_click() override;
	void on_update() override;
	PressResult on_press() override;

	bool on_enable(int type, int64_t v1, int v2, const char* string) override;

private:
	// --- layout ----------------------------------------------------------
	static constexpr int panel_w = 380;
	static constexpr int panel_h = 360;

	static constexpr int list_top = 40;
	static constexpr int row_pitch = 22;
	static constexpr int visible_rows = 11;
	static constexpr int list_bottom = list_top + visible_rows * row_pitch;

	static constexpr int col_kind = 14;
	static constexpr int col_applicant = 58;
	static constexpr int col_age = 168;
	static constexpr ui_rect btn_approve{ 232, 0, 64, 18 };   // y per row
	static constexpr ui_rect btn_reject{ 302, 0, 64, 18 };

	static constexpr ui_rect scroll_bar{ 368, list_top, 7,
		visible_rows * row_pitch };

	static constexpr ui_rect btn_refresh{ 14, 330, 74, 20 };
	static constexpr ui_rect btn_close{ 292, 330, 74, 20 };

	// One decide button of one visible row, built once per event so the
	// drawn pair and the hit-tested pair cannot disagree.
	struct row_button
	{
		int64_t request_id;
		bool approve;
		ui_rect rect;
	};

	int build_row_buttons(row_button (&buttons)[visible_rows * 2]) const;
	void play_click_sound() const;

	int m_scroll = 0;
};
