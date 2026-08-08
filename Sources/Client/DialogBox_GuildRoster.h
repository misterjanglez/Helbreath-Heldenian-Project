// DialogBox_GuildRoster.h: the guild Roster (#124, plan §3.1.1 deviation 3)
//
// The surface the original never had: the member list (name, Rank, Title,
// lifetime donated gold, online status) with the §3.1.1 UI-first moderation
// on top — permission-gated action buttons over a selected row (kick,
// promote, demote, strip Title, transfer mastership) and the Title slot
// machine's claim/drop row per ADR 0007. Slash commands remain the fallback;
// every rule here is a pre-check for responsiveness, the server re-enforces
// all of them.
//
// Data is guild_manager's cached roster/info answers: re-requested on open
// and on the Refresh button, and re-requested from on_update when a notify
// handler marks them stale (a kick or promote redraws without reopening).
// Online status changes silently (login/logout has no guild notify), which
// is what the Refresh button is for.
//
//////////////////////////////////////////////////////////////////////

#pragma once
#include "IDialogBox.h"
#include "GuildManager.h"

#include <cstdint>
#include <string>

class DialogBox_GuildRoster : public IDialogBox
{
public:
	DialogBox_GuildRoster(CGame* game);
	~DialogBox_GuildRoster() override = default;

	void on_draw() override;
	bool on_click() override;
	void on_update() override;
	PressResult on_press() override;

	bool on_enable(int type, int64_t v1, int v2, const char* string) override;

private:
	// The moderation actions that go through the selected row. kick and
	// transfer are destructive enough to earn the confirm strip.
	enum class action : uint8_t
	{
		none = 0,
		kick,
		promote,
		demote,
		strip_title,
		transfer,
	};

	// One Title row of the slot strip: counts and holders from the answers,
	// the claim/drop verdicts precomputed so the drawn button state and the
	// hit-tested one come from the same build.
	struct title_line
	{
		uint8_t kind;        // guild_title
		ui_rect rect;        // the Claim/Drop button slot
		int slots;           // from PacketResponseGuildInfo
		int held;            // counted from the roster
		bool held_by_me;
		bool can_claim;
		std::string holders; // comma-joined holder names, pre-clipped
	};

	// One visible action-bar button: what it does, where it sits, whether the
	// selected row qualifies. Built by build_action_buttons for draw and
	// click alike.
	struct action_button
	{
		action act;
		const char* label;
		ui_rect rect;
		bool enabled;
	};

	void build_title_lines(title_line (&lines)[3]) const;
	int build_action_buttons(action_button (&buttons)[5]) const;
	// The selected roster row, resolved by name so a refresh that reorders
	// (or drops) the member cannot redirect a pending action; nullptr when
	// nothing valid is selected.
	const guild_manager::roster_row* selected() const;
	void act_on(action what, const char* target);
	void refresh();
	void play_click_sound() const;

	void draw_titles(short sX, short sY);
	void draw_list(short sX, short sY);
	void draw_actions(short sX, short sY);
	bool click_titles();
	bool click_list();
	bool click_actions();

	std::string m_selected;
	action m_confirm = action::none;
	int m_scroll = 0;

	// --- layout ----------------------------------------------------------
	static constexpr int panel_w = 460;
	static constexpr int panel_h = 420;

	static constexpr int title_top = 50;
	static constexpr int title_pitch = 20;
	static constexpr ui_rect title_btn{ 366, 0, 80, 17 };   // y per row

	static constexpr int head_y = 124;
	static constexpr int list_top = 140;
	static constexpr int row_pitch = 18;
	static constexpr int visible_rows = 12;
	static constexpr int list_bottom = list_top + visible_rows * row_pitch;

	// Column lefts; donated is right-aligned into its span.
	static constexpr int col_member = 14;
	static constexpr int col_rank = 118;
	static constexpr int col_title = 204;
	static constexpr int col_donated = 282;
	static constexpr int col_donated_w = 84;
	static constexpr int col_status = 380;
	static constexpr ui_rect row_band{ 12, 0, 426, 17 };    // y per row

	static constexpr ui_rect scroll_bar{ 444, list_top, 7,
		visible_rows * row_pitch };

	static constexpr int action_y = 362;
	static constexpr int action_w = 82;
	static constexpr int action_gap = 6;

	static constexpr ui_rect btn_refresh{ 14, 388, 74, 20 };
	static constexpr ui_rect btn_close{ 372, 388, 74, 20 };
	static constexpr ui_rect btn_confirm_yes{ 240, action_y, 74, 20 };
	static constexpr ui_rect btn_confirm_no{ 322, action_y, 74, 20 };
};
