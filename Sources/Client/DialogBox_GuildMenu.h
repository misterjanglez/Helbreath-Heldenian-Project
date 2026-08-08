// DialogBox_GuildMenu.h: the guild hub dialog (#124, plan §3.1/§3.1.1)
//
// The rebuilt pre-gut guild menu on the new wire: the Kennedy ceremonies the
// original carried (create, disband, the two ticket purchases) plus the
// §3.1.1 surfaces — apply-by-name filing, leave-request filing, Kennedy
// self-dismissal, the Donation altar and the Treasury — and links into the
// roster and requests-queue dialogs. Fightzone reservation stays out until
// Phase 4.
//
// Every mode sends through Screen_OnGame's guild_manager and returns to the
// menu; outcomes arrive as the action envelope (player-facing line) and the
// event notifies, and GuildSelf moves the CPlayer state the menu gates on —
// the dialog holds no guild state of its own beyond the donate panel's
// formatted-totals cache.
//
//////////////////////////////////////////////////////////////////////

#pragma once
#include "IDialogBox.h"
#include "Game/GuildDefs.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

class guild_manager;

class DialogBox_GuildMenu : public IDialogBox
{
public:
	DialogBox_GuildMenu(CGame* game);
	~DialogBox_GuildMenu() override = default;

	void on_draw() override;
	bool on_click() override;
	void on_update() override;

	bool on_enable(int type, int64_t v1, int v2, const char* string) override;
	bool on_disable() override;

private:
	enum class mode : uint8_t
	{
		main_menu = 0,
		create_input,       // typed new-guild name
		apply_input,        // typed guild name to file a join request at
		disband_confirm,    // typed-name confirmation (§3.1)
		admission_ticket,   // item 88 purchase prose + confirm
		secession_ticket,   // item 89 purchase prose + confirm
		leave_confirm,      // file the queued secession request
		self_exit_confirm,  // Kennedy dismissal, −300 exp
		donate,             // the three burn lanes (§3.1.2)
		treasury,           // deposit / withdraw
	};

	// One state-gated row of the main menu: what it opens (a sibling dialog,
	// or a mode of this one) and whether the player currently qualifies. The
	// list is built once per event and walked by draw and click alike, so
	// what a row is and where it hit-tests cannot disagree — the salvage
	// duplicated every rect between DrawModeN and on_click_modeN, which is
	// the coordinate-drift bug generator the CControls plan documents.
	struct link_row
	{
		mode target;                // entered when dialog == None
		DialogBoxId::Type dialog;   // opened (and this menu closed) when set
		const char* label;
		bool enabled;
	};

	// The active mode's text field, when it has one: where the chrome sits
	// and what the input accepts. One table serves the field drawing, the
	// input binding and the teardown test.
	struct field_desc
	{
		int y;                      // field top, dialog-relative
		unsigned char max_len;
		std::string_view filter;
	};

	int build_links(link_row (&rows)[8]) const;
	std::optional<field_desc> input_field() const;
	void enter_mode(mode next);
	guild_manager& guild_mgr() const;
	int64_t parsed_amount() const;
	// Both amount panels stay in their mode after a send; the typed amount
	// must not survive into the next send.
	void clear_amount();

	// The footer contract every ceremony mode shares: btn_left submits when
	// allowed, btn_right cancels; both land back on the menu.
	bool click_footer(bool submit_enabled, auto&& submit)
	{
		if (mouse_in(btn_left) && submit_enabled)
		{
			submit();
			play_click_sound();
			enter_mode(mode::main_menu);
			return true;
		}
		if (mouse_in(btn_right))
		{
			play_click_sound();
			enter_mode(mode::main_menu);
			return true;
		}
		return false;
	}

	void draw_main_menu(short sX, short sY);
	// Prompt + two dim hint lines + Yes/No footer (leave, self-exit).
	void draw_prompt(short sX, short sY, const char* prompt,
		const char* hint1, const char* hint2);
	// Prompt + text field + up to two dim hint lines + submit/Cancel footer
	// (create, apply).
	void draw_field_prompt(short sX, short sY, const char* prompt,
		const char* hint1, const char* hint2, const char* submit_label,
		bool submit_enabled);
	void draw_disband_confirm(short sX, short sY);
	void draw_ticket(short sX, short sY, bool admission);
	void draw_donate(short sX, short sY);
	void draw_treasury(short sX, short sY);
	void draw_text_field(short sX, short sY, int field_y);

	bool click_main_menu();
	bool click_donate();
	bool click_treasury();

	void purchase_ticket(bool admission);
	void play_click_sound() const;

	mode m_mode = mode::main_menu;
	std::string m_input;
	uint8_t m_donate_lane = hb::shared::guild::guild_donation_lane::gold;

	// The donate panel's lane totals, formatted once per info answer rather
	// than per frame (comma-formatting three int64 pairs every frame is pure
	// churn between packets). The sentinel forces the first build.
	std::string m_donate_totals[3];
	bool m_donate_at_top = false;
	uint32_t m_donate_info_version = UINT32_MAX;

	// Layout. The 258x339 footprint and the shared footer slots are the
	// dialog-standard ones; rows are the theme pitch.
	static constexpr int link_top = 88;
	static constexpr int link_pitch = 20;
	static constexpr ui_rect link_band{ 29, 0, 200, 19 };   // y filled per row
	static constexpr int text_field_x = 30;
	static constexpr int text_field_w = 198;
	static constexpr int text_field_h = 20;

	static constexpr ui_rect btn_left = ui_layout::btn_left;
	static constexpr ui_rect btn_right = ui_layout::btn_right;

	// Donate-panel rows (the amount row is treasury's too): the three lane
	// rows double as the lane selector.
	static constexpr int lane_top = 112;
	static constexpr int lane_pitch = 20;
	static constexpr ui_rect lane_band{ 24, 0, 210, 19 };
	static constexpr int amount_field_y = 228;

	// Treasury's two-action footer, plus its own centred Back beneath.
	static constexpr ui_rect btn_deposit{ 30, 262, 74, 20 };
	static constexpr ui_rect btn_withdraw{ 154, 262, 74, 20 };
	static constexpr ui_rect btn_treasury_back{ 92, 292, 74, 20 };
};
