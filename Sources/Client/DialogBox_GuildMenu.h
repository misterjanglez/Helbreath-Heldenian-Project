#pragma once
#include "IDialogBox.h"

class DialogBox_GuildMenu : public IDialogBox
{
public:
	DialogBox_GuildMenu(CGame* game);
	~DialogBox_GuildMenu() override = default;

	void on_draw() override;
	bool on_click() override;

	bool on_enable(int type, int64_t v1, int v2, const char* string) override;
	bool on_disable() override;

	enum class mode : uint8_t
	{
		main_menu = 0,
		create_guild = 1,
		creating = 2,
		guild_created = 3,
		create_failed = 4,
		disband_confirm = 5,
		disbanding = 6,
		disband_success = 7,
		disband_failed = 8,
		admission_ticket = 9,
		admission_result = 10,
		secession_ticket = 11,
		secession_result = 12,
		fightzone_select = 13,
		fightzone_reserved = 14,
		fightzone_won = 15,
		fightzone_lost = 16,
		fightzone_draw = 17,
		fightzone_waiting = 18,
		get_ticket_redirect = 19,
		confirm_cancel = 20,
		fightzone_denied = 21,
		fightzone_canceled = 22,
	};
	mode m_mode{mode::main_menu};

private:
	void DrawMode0_MainMenu(short sX, short sY, short size_x);
	void DrawMode1_CreateGuild(short sX, short sY, short size_x);
	void DrawMode5_DisbandConfirm(short sX, short sY, short size_x);
	void DrawMode9_AdmissionTicket(short sX, short sY, short size_x);
	void DrawMode11_SecessionTicket(short sX, short sY, short size_x);
	void DrawMode13_FightzoneSelect(short sX, short sY, short size_x);
	void DrawMode20_ConfirmCancel(short sX, short sY, short size_x);
	void draw_simple_message(short sX, short sY, short size_x, short mouse_x, short mouse_y, int mode);

	bool on_click_mode0(short sX, short sY);
	bool on_click_mode1(short sX, short sY);
	bool on_click_mode5(short sX, short sY);
	bool on_click_mode9(short sX, short sY);
	bool on_click_mode11(short sX, short sY);
	bool on_click_mode13(short sX, short sY);
	bool on_click_mode_ok_only(short sX, short sY);

	static constexpr int ADJX = -13;
	static constexpr int ADJY = 30;
};
