#pragma once
#include "IDialogBox.h"

class DialogBox_CityHallMenu : public IDialogBox
{
public:
	DialogBox_CityHallMenu(CGame* game);
	~DialogBox_CityHallMenu() override = default;

	void on_draw() override;
	bool on_click() override;

	enum class mode : uint8_t
	{
		main_menu = 0,
		citizenship_warning = 1,
		offering_citizenship = 2,
		citizenship_success = 3,
		citizenship_failed = 4,
		reward_gold = 5,
		hero_items = 7,
		cancel_quest = 8,
		change_play_mode = 9,
		teleport_menu = 10,
		hero_item_confirm = 11,
		dark_item_class = 12,
		dark_items = 13,
		dark_item_confirm = 14,
	};
	mode m_mode{mode::main_menu};
	int m_hero_item_id{};
	int m_dark_item_id{};

private:
	// Mode 0 menu link rects (dialog-relative)
	static constexpr ui_rect link_citizenship{36, 71, 184, 24};
	static constexpr ui_rect link_reward_gold{36, 96, 184, 24};
	static constexpr ui_rect link_hero_items{36, 121, 184, 24};
	static constexpr ui_rect link_cancel_quest{36, 146, 184, 24};
	static constexpr ui_rect link_change_playmode{36, 171, 184, 24};
	static constexpr ui_rect link_teleport{36, 196, 184, 24};
	static constexpr ui_rect link_crusade_role{36, 221, 184, 24};
	static constexpr ui_rect link_dark_items{36, 246, 184, 24};
	// Dark-item pages: shared by draw (hover) and click (hit) so the two never drift
	static constexpr ui_rect dark_class_knight_row{35, 125, 185, 15};
	static constexpr ui_rect dark_class_mage_row{35, 155, 185, 15};
	static constexpr ui_rect dark_item_row(int i) { return ui_rect{35, 95 + i * 20, 185, 15}; }
	void DrawMode0_MainMenu(short sX, short sY, short size_x);
	void DrawMode1_CitizenshipWarning(short sX, short sY, short size_x);
	void DrawMode2_OfferingCitizenship(short sX, short sY, short size_x);
	void DrawMode3_CitizenshipSuccess(short sX, short sY, short size_x);
	void DrawMode4_CitizenshipFailed(short sX, short sY, short size_x);
	void DrawMode5_RewardGold(short sX, short sY, short size_x);
	void DrawMode7_HeroItems(short sX, short sY, short size_x);
	void DrawMode8_CancelQuest(short sX, short sY, short size_x);
	void DrawMode9_ChangePlayMode(short sX, short sY, short size_x);
	void DrawMode10_TeleportMenu(short sX, short sY, short size_x);
	void DrawMode11_HeroItemConfirm(short sX, short sY, short size_x);
	void DrawMode12_DarkItemClass(short sX, short sY, short size_x);
	void DrawMode13_DarkItems(short sX, short sY, short size_x);
	void DrawMode14_DarkItemConfirm(short sX, short sY, short size_x);

	bool on_click_mode0(short sX, short sY);
	bool on_click_mode1();
	bool OnClickMode3_4();
	bool on_click_mode5();
	bool on_click_mode7(short sX, short sY);
	bool on_click_mode8();
	bool on_click_mode9();
	bool on_click_mode10(short sX, short sY);
	bool on_click_mode11(short sX, short sY);
	bool on_click_mode12(short sX, short sY);
	bool on_click_mode13(short sX, short sY);
	bool on_click_mode14(short sX, short sY);

	// Shared confirm page used by both the hero (mode 11) and dark (mode 14) flows
	void draw_item_confirm(short sX, short sY, short size_x, const char* item_name);
	bool click_item_confirm(short sX, short sY, uint16_t common_type, int item_id, mode cancel_mode);

	std::string m_cTakeHeroItemName;
	const char* m_take_dark_item_name{""};
	bool m_dark_mage_page{};
};
