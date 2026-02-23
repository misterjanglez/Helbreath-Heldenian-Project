#include "Game.h"
#include "TextInputManager.h"
#include "MagicCastingSystem.h"
#include "ChatManager.h"
#include "AudioManager.h"
#include "ConfigManager.h"
#include "HotkeyManager.h"
#include "DialogBox_ChatHistory.h"
#include "DialogBox_GuildMenu.h"
#include "DialogBox_ItemDropAmount.h"
#include "InventoryManager.h"
#include "lan_eng.h"
#include <format>
#include <string>


using namespace hb::shared::net;
using hb::shared::item::ItemType;
namespace MouseButton = hb::shared::input::MouseButton;

void CGame::register_hotkeys()
{
	auto& hotkeys = HotkeyManager::get();
	hotkeys.clear();

	HotkeyManager::KeyCombo ctrlOnly{ KeyCode::Unknown, true, false, false };

	hotkeys.register_hotkey({ KeyCode::A, ctrlOnly.ctrl, ctrlOnly.shift, ctrlOnly.alt }, HotkeyManager::Trigger::KeyUp,
		[this]() { hotkey_toggle_force_attack(); });
	hotkeys.register_hotkey({ KeyCode::D, ctrlOnly.ctrl, ctrlOnly.shift, ctrlOnly.alt }, HotkeyManager::Trigger::KeyUp,
		[this]() { hotkey_cycle_detail_level(); });
	hotkeys.register_hotkey({ KeyCode::H, ctrlOnly.ctrl, ctrlOnly.shift, ctrlOnly.alt }, HotkeyManager::Trigger::KeyUp,
		[this]() { hotkey_toggle_help(); });
	hotkeys.register_hotkey({ KeyCode::W, ctrlOnly.ctrl, ctrlOnly.shift, ctrlOnly.alt }, HotkeyManager::Trigger::KeyUp,
		[this]() { hotkey_toggle_dialog_transparency(); });
	hotkeys.register_hotkey({ KeyCode::X, ctrlOnly.ctrl, ctrlOnly.shift, ctrlOnly.alt }, HotkeyManager::Trigger::KeyUp,
		[this]() { hotkey_toggle_system_menu(); });
	hotkeys.register_hotkey({ KeyCode::M, ctrlOnly.ctrl, ctrlOnly.shift, ctrlOnly.alt }, HotkeyManager::Trigger::KeyUp,
		[this]() { hotkey_toggle_guide_map(); });
	hotkeys.register_hotkey({ KeyCode::R, ctrlOnly.ctrl, ctrlOnly.shift, ctrlOnly.alt }, HotkeyManager::Trigger::KeyUp,
		[this]() { hotkey_toggle_running_mode(); });
	hotkeys.register_hotkey({ KeyCode::S, ctrlOnly.ctrl, ctrlOnly.shift, ctrlOnly.alt }, HotkeyManager::Trigger::KeyUp,
		[this]() { hotkey_toggle_sound_and_music(); });
	hotkeys.register_hotkey({ KeyCode::T, ctrlOnly.ctrl, ctrlOnly.shift, ctrlOnly.alt }, HotkeyManager::Trigger::KeyUp,
		[this]() { hotkey_whisper_target(); });

}

void CGame::hotkey_toggle_force_attack()
{
	if (!hb::shared::input::is_ctrl_down() || GameModeManager::get_mode() != GameMode::MainGame || text_input_manager::get().is_active()) {
		return;
	}
	if (m_player->m_force_attack)
	{
		m_player->m_force_attack = false;
		add_event_list(DEF_MSG_FORCEATTACK_OFF, 10);
	}
	else
	{
		m_player->m_force_attack = true;
		add_event_list(DEF_MSG_FORCEATTACK_ON, 10);
	}
}

void CGame::hotkey_cycle_detail_level()
{
	if (!hb::shared::input::is_ctrl_down() || GameModeManager::get_mode() != GameMode::MainGame || text_input_manager::get().is_active()) {
		return;
	}
	int detailLevel = config_manager::get().get_detail_level();
	detailLevel++;
	if (detailLevel > 2) detailLevel = 0;
	config_manager::get().set_detail_level(detailLevel);
	switch (detailLevel) {
	case 0:
		add_event_list(NOTIFY_MSG_DETAIL_LEVEL_LOW, 10);
		break;
	case 1:
		add_event_list(NOTIFY_MSG_DETAIL_LEVEL_MEDIUM, 10);
		break;
	case 2:
		add_event_list(NOTIFY_MSG_DETAIL_LEVEL_HIGH, 10);
		break;
	}
}

void CGame::hotkey_toggle_help()
{
	if (!hb::shared::input::is_ctrl_down() || GameModeManager::get_mode() != GameMode::MainGame || text_input_manager::get().is_active()) {
		return;
	}
	if (m_dialog_box_manager.is_enabled(DialogBoxId::Help) == false)
		m_dialog_box_manager.enable_dialog_box(DialogBoxId::Help, 0, 0, 0);
	else
	{
		m_dialog_box_manager.disable_dialog_box(DialogBoxId::Help);
		m_dialog_box_manager.disable_dialog_box(DialogBoxId::Text);
	}
}

void CGame::hotkey_toggle_dialog_transparency()
{
	if (!hb::shared::input::is_ctrl_down() || GameModeManager::get_mode() != GameMode::MainGame || text_input_manager::get().is_active()) {
		return;
	}
	bool enabled = config_manager::get().is_dialog_transparency_enabled();
	config_manager::get().set_dialog_transparency_enabled(!enabled);
}

void CGame::hotkey_toggle_system_menu()
{
	if (!hb::shared::input::is_ctrl_down() || GameModeManager::get_mode() != GameMode::MainGame || text_input_manager::get().is_active()) {
		return;
	}
	if (m_dialog_box_manager.is_enabled(DialogBoxId::SystemMenu) == false)
		m_dialog_box_manager.enable_dialog_box(DialogBoxId::SystemMenu, 0, 0, 0);
	else m_dialog_box_manager.disable_dialog_box(DialogBoxId::SystemMenu);
}

void CGame::hotkey_toggle_guide_map()
{
	if (GameModeManager::get_mode() != GameMode::MainGame || !hb::shared::input::is_ctrl_down()) {
		return;
	}
	if (m_dialog_box_manager.is_enabled(DialogBoxId::GuideMap) == true) m_dialog_box_manager.disable_dialog_box(DialogBoxId::GuideMap);
	else m_dialog_box_manager.enable_dialog_box(DialogBoxId::GuideMap, 0, 0, 0, 0);
}

void CGame::hotkey_toggle_running_mode()
{
	if (!hb::shared::input::is_ctrl_down() || GameModeManager::get_mode() != GameMode::MainGame || text_input_manager::get().is_active()) {
		return;
	}
	bool runningMode = config_manager::get().is_running_mode_enabled();
	if (runningMode)
	{
		config_manager::get().set_running_mode_enabled(false);
		add_event_list(NOTIFY_MSG_CONVERT_WALKING_MODE, 10);
	}
	else
	{
		config_manager::get().set_running_mode_enabled(true);
		add_event_list(NOTIFY_MSG_CONVERT_RUNNING_MODE, 10);
	}
}

void CGame::hotkey_toggle_sound_and_music()
{
	if (!hb::shared::input::is_ctrl_down() || GameModeManager::get_mode() != GameMode::MainGame || text_input_manager::get().is_active()) {
		return;
	}
	if (audio_manager::get().is_music_enabled())
	{
		audio_manager::get().set_music_enabled(false);
		config_manager::get().set_music_enabled(false);
		audio_manager::get().stop_music();
		add_event_list(NOTIFY_MSG_MUSIC_OFF, 10);
		return;
	}
	if (audio_manager::get().is_sound_enabled())
	{
		audio_manager::get().stop_sound(sound_type::Effect, 38);
		audio_manager::get().set_sound_enabled(false);
		config_manager::get().set_sound_enabled(false);
		add_event_list(NOTIFY_MSG_SOUND_OFF, 10);
		return;
	}
	if (audio_manager::get().is_sound_available())
	{
		audio_manager::get().set_music_enabled(true);
		config_manager::get().set_music_enabled(true);
		add_event_list(NOTIFY_MSG_MUSIC_ON, 10);
	}
	if (audio_manager::get().is_sound_available())
	{
		audio_manager::get().set_sound_enabled(true);
		config_manager::get().set_sound_enabled(true);
		add_event_list(NOTIFY_MSG_SOUND_ON, 10);
	}
	start_bgm();
}

void CGame::hotkey_whisper_target()
{
	if (!hb::shared::input::is_ctrl_down() || GameModeManager::get_mode() != GameMode::MainGame || text_input_manager::get().is_active()) {
		return;
	}
	std::string tempid;

	char lb, rb;
	short sX, sY, mouse_x, mouse_y, z;
	sX = m_dialog_box_manager.get_dialog_box(DialogBoxId::ChatHistory)->m_x;
	sY = m_dialog_box_manager.get_dialog_box(DialogBoxId::ChatHistory)->m_y;
	mouse_x = static_cast<short>(hb::shared::input::get_mouse_x());

	mouse_y = static_cast<short>(hb::shared::input::get_mouse_y());

	z = static_cast<short>(hb::shared::input::get_mouse_wheel_delta());

	lb = hb::shared::input::is_mouse_button_down(MouseButton::Left) ? 1 : 0;

	rb = hb::shared::input::is_mouse_button_down(MouseButton::Right) ? 1 : 0;
	if (m_dialog_box_manager.is_enabled(DialogBoxId::ChatHistory) == true && (mouse_x >= sX + 20) && (mouse_x <= sX + 360) && (mouse_y >= sY + 35) && (mouse_y <= sY + 139))
	{
		char buff[64];
		int i = (139 - mouse_y + sY) / 13;
		int msg_idx = i + m_dialog_box_manager.get_dialog_as<DialogBox_ChatHistory>(DialogBoxId::ChatHistory)->m_scroll_position;
		CMsg* chat_msg = ChatManager::get().get_message(msg_idx);
		if (chat_msg == nullptr) return;
		if (chat_msg->m_pMsg[0] == ' ') { i++; chat_msg = ChatManager::get().get_message(i + m_dialog_box_manager.get_dialog_as<DialogBox_ChatHistory>(DialogBoxId::ChatHistory)->m_scroll_position); }
		if (chat_msg == nullptr) return;
		std::snprintf(buff, sizeof(buff), "%s", chat_msg->m_pMsg);
		char* sep = std::strchr(buff, ':');
		if (sep) *sep = '\0';
		tempid = std::format("/to {}", buff);
		send_command(MsgId::CommandChatMsg, 0, 0, 0, 0, 0, tempid.c_str());
	}
	else if (m_entity_state.is_player() && (m_entity_state.m_name[0] != '\0') && (m_ilusion_owner_h == 0)
		&& ((m_is_crusade_mode == false) || !IsHostile(m_entity_state.m_status.relationship)))
	{
		tempid = std::format("/to {}", m_entity_state.m_name.data());
		send_command(MsgId::CommandChatMsg, 0, 0, 0, 0, 0, tempid.c_str());
	}
	else
	{
		text_input_manager::get().end_input();
		m_chat_msg = "/to ";
		text_input_manager::get().start_input(CHAT_INPUT_X(), CHAT_INPUT_Y(), ChatMsgMaxLen, m_chat_msg);
		text_input_manager::get().set_chat_background(true);
	}
}

void CGame::hotkey_simple_use_health_potion()
{
	int i = 0;
	if (m_player->m_hp <= 0) return;
	if (m_item_using_status == true)
	{
		add_event_list(USE_RED_POTION1, 10);
		return;
	}
	if (m_dialog_box_manager.is_enabled(DialogBoxId::Exchange) == true)
	{
		add_event_list(USE_RED_POTION2, 10);
		return;
	}
	for (i = 0; i < hb::shared::limits::MaxItems; i++)
	{
		if ((m_item_list[i] != 0) && (!inventory_manager::get().is_locked(i)))
		{
			CItem* cfg = get_item_config(m_item_list[i]->m_id_num);
			if (cfg && cfg->get_item_type() == ItemType::Consume && cfg->m_item_effect_type == hb::shared::item::to_int(hb::shared::item::ItemEffectType::HP))
			{
				send_command(MsgId::CommandCommon, CommonType::ReqUseItem, 0, i, 0, 0, 0);
				inventory_manager::get().lock_item(i);
				m_item_using_status = true;
				return;
			}
		}
	}
}

void CGame::hotkey_simple_use_mana_potion()
{
	int i = 0;
	if (m_player->m_hp <= 0) return;
	if (m_item_using_status == true)
	{
		add_event_list(USE_BLUE_POTION1, 10);
		return;
	}
	if (m_dialog_box_manager.is_enabled(DialogBoxId::Exchange) == true)
	{
		add_event_list(USE_BLUE_POTION2, 10);
		return;
	}

	for (i = 0; i < hb::shared::limits::MaxItems; i++)
	{
		if ((m_item_list[i] != 0) && (!inventory_manager::get().is_locked(i)))
		{
			CItem* cfg = get_item_config(m_item_list[i]->m_id_num);
			if (cfg && cfg->get_item_type() == ItemType::Consume && cfg->m_item_effect_type == hb::shared::item::to_int(hb::shared::item::ItemEffectType::MP))
			{
				send_command(MsgId::CommandCommon, CommonType::ReqUseItem, 0, i, 0, 0, 0);
				inventory_manager::get().lock_item(i);
				m_item_using_status = true;
				return;
			}
		}
	}
}

void CGame::hotkey_simple_load_backup_chat()
{
	if (((m_dialog_box_manager.is_enabled(DialogBoxId::GuildMenu) == true) && (m_dialog_box_manager.get_dialog_as<DialogBox_GuildMenu>(DialogBoxId::GuildMenu)->m_mode == DialogBox_GuildMenu::mode::create_guild) && (m_dialog_box_manager.get_top_id() == DialogBoxId::GuildMenu)) ||
		((m_dialog_box_manager.is_enabled(DialogBoxId::ItemDropExternal) == true) && (m_dialog_box_manager.get_dialog_as<DialogBox_ItemDropAmount>(DialogBoxId::ItemDropExternal)->m_mode == DialogBox_ItemDropAmount::mode::input) && (m_dialog_box_manager.get_top_id() == DialogBoxId::ItemDropExternal)))
	{
		return;
	}
	if ((!text_input_manager::get().is_active()) && (m_backup_chat_msg[0] != '!') && (m_backup_chat_msg[0] != '~') && (m_backup_chat_msg[0] != '^') &&
		(m_backup_chat_msg[0] != '@'))
	{
		m_chat_msg = m_backup_chat_msg;
		text_input_manager::get().start_input(CHAT_INPUT_X(), CHAT_INPUT_Y(), ChatMsgMaxLen, m_chat_msg);
		text_input_manager::get().set_chat_background(true);
	}
}

void CGame::hotkey_simple_use_magic_shortcut()
{
	if (GameModeManager::get_mode() != GameMode::MainGame) return;
	magic_casting_system::get().begin_cast(m_magic_short_cut);
}

void CGame::hotkey_simple_toggle_character_info()
{
	if (m_dialog_box_manager.is_enabled(DialogBoxId::CharacterInfo) == false)
		m_dialog_box_manager.enable_dialog_box(DialogBoxId::CharacterInfo, 0, 0, 0);
	else m_dialog_box_manager.disable_dialog_box(DialogBoxId::CharacterInfo);
}

void CGame::hotkey_simple_toggle_inventory()
{
	if (m_dialog_box_manager.is_enabled(DialogBoxId::Inventory) == false)
		m_dialog_box_manager.enable_dialog_box(DialogBoxId::Inventory, 0, 0, 0);
	else m_dialog_box_manager.disable_dialog_box(DialogBoxId::Inventory);
}

void CGame::hotkey_simple_toggle_magic()
{
	if (m_dialog_box_manager.is_enabled(DialogBoxId::Magic) == false)
		m_dialog_box_manager.enable_dialog_box(DialogBoxId::Magic, 0, 0, 0);
	else m_dialog_box_manager.disable_dialog_box(DialogBoxId::Magic);
}

void CGame::hotkey_simple_toggle_skill()
{
	if (m_dialog_box_manager.is_enabled(DialogBoxId::Skill) == false)
		m_dialog_box_manager.enable_dialog_box(DialogBoxId::Skill, 0, 0, 0);
	else m_dialog_box_manager.disable_dialog_box(DialogBoxId::Skill);
}

void CGame::hotkey_simple_toggle_chat_history()
{
	if (m_dialog_box_manager.is_enabled(DialogBoxId::ChatHistory) == false)
		m_dialog_box_manager.enable_dialog_box(DialogBoxId::ChatHistory, 0, 0, 0);
	else m_dialog_box_manager.disable_dialog_box(DialogBoxId::ChatHistory);
}

void CGame::hotkey_simple_toggle_system_menu()
{
	if (text_input_manager::get().is_active()) return;
	if (m_dialog_box_manager.is_enabled(DialogBoxId::SystemMenu) == false)
		m_dialog_box_manager.enable_dialog_box(DialogBoxId::SystemMenu, 0, 0, 0);
	else m_dialog_box_manager.disable_dialog_box(DialogBoxId::SystemMenu);
}

void CGame::hotkey_simple_use_shortcut1()
{
	use_shortcut(1);
}

void CGame::hotkey_simple_use_shortcut2()
{
	use_shortcut(2);
}

void CGame::hotkey_simple_use_shortcut3()
{
	use_shortcut(3);
}

void CGame::hotkey_simple_whisper_cycle_up()
{
	m_arrow_pressed = 1;
	if (GameModeManager::get_mode() == GameMode::MainGame)
	{
		ChatManager::get().cycle_whisper_up();
		int idx = ChatManager::get().get_whisper_index();
		const char* name = ChatManager::get().get_whisper_target_name(idx);
		if (name != nullptr) {
			text_input_manager::get().end_input();
			m_chat_msg = std::format("/to {}", name);
			text_input_manager::get().start_input(CHAT_INPUT_X(), CHAT_INPUT_Y(), ChatMsgMaxLen, m_chat_msg);
			text_input_manager::get().set_chat_background(true);
		}
	}
}

void CGame::hotkey_simple_whisper_cycle_down()
{
	m_arrow_pressed = 3;
	if (GameModeManager::get_mode() == GameMode::MainGame)
	{
		ChatManager::get().cycle_whisper_down();
		int idx = ChatManager::get().get_whisper_index();
		const char* name = ChatManager::get().get_whisper_target_name(idx);
		if (name != nullptr) {
			text_input_manager::get().end_input();
			m_chat_msg = std::format("/to {}", name);
			text_input_manager::get().start_input(CHAT_INPUT_X(), CHAT_INPUT_Y(), ChatMsgMaxLen, m_chat_msg);
			text_input_manager::get().set_chat_background(true);
		}
	}
}

void CGame::hotkey_simple_arrow_left()
{
	m_arrow_pressed = 4;
}

void CGame::hotkey_simple_arrow_right()
{
	m_arrow_pressed = 2;
}

void CGame::hotkey_simple_screenshot()
{
	create_screen_shot();
}

void CGame::hotkey_simple_tab_toggle_combat()
{
	if (GameModeManager::get_mode() == GameMode::MainGame)
	{
		if (hb::shared::input::is_shift_down())
		{
			m_cur_focus--;
			if (m_cur_focus < 1) m_cur_focus = m_max_focus;
		}
		else
		{
			m_cur_focus++;
			if (m_cur_focus > m_max_focus) m_cur_focus = 1;
		}
		send_command(MsgId::CommandCommon, CommonType::ToggleCombatMode, 0, 0, 0, 0, 0);
	}
}

void CGame::hotkey_simple_toggle_safe_attack()
{
	if (GameModeManager::get_mode() == GameMode::MainGame) {
		send_command(MsgId::CommandCommon, CommonType::ToggleSafeAttackMode, 0, 0, 0, 0, 0);
	}
}

void CGame::hotkey_simple_escape()
{
	// Note: Escape handling is automatic through hb::shared::input::is_key_pressed(KeyCode::Escape)
	if (GameModeManager::get_mode() == GameMode::MainGame)
	{
		// Cancel active chat input
		if (text_input_manager::get().is_active())
		{
			m_chat_msg.clear();
			text_input_manager::get().end_input();
			return;
		}

		if ((m_is_observer_mode == true) && (hb::shared::input::is_shift_down())) {
			if (m_logout_count == -1) { m_logout_count = 1; m_logout_count_time = GameClock::get_time_ms(); }
			m_dialog_box_manager.disable_dialog_box(DialogBoxId::SystemMenu);
			play_game_sound('E', 14, 5);
		}
		else if (m_logout_count != -1) {
			if (m_force_disconn == false) {
				m_logout_count = -1;
				add_event_list(DLGBOX_CLICK_SYSMENU2, 10);
			}
		}
		if (m_is_get_pointing_mode == true) {
			m_is_get_pointing_mode = false;
			add_event_list(COMMAND_PROCESSOR1, 10);
		}
		m_is_f1_help_window_enabled = false;
	}
}

void CGame::hotkey_simple_special_ability()
{
	int i = 0;
	uint32_t time = GameClock::get_time_ms();
	if (GameModeManager::get_mode() != GameMode::MainGame) return;
	if (text_input_manager::get().is_active()) return;
	if (m_player->m_is_special_ability_enabled == true)
	{
		if (m_player->m_special_ability_type != 0) {
			send_command(MsgId::CommandCommon, CommonType::RequestActivateSpecAbility, 0, 0, 0, 0, 0);
			m_player->m_is_special_ability_enabled = false;
		}
		else add_event_list(ON_KEY_UP26, 10);
	}
	else {
		if (m_player->m_special_ability_type == 0) add_event_list(ON_KEY_UP26, 10);
		else {
			std::string G_cTxt;
			if (m_player->m_playerAppearance.effect_type != 0) {
				add_event_list(ON_KEY_UP28, 10);
				return;
			}

			i = (time - m_special_ability_setting_time) / 1000;
			i = m_player->m_special_ability_time_left_sec - i;
			if (i < 0) i = 0;

			if (i < 60) {
				switch (m_player->m_special_ability_type) {
				case 1: G_cTxt = std::format(ON_KEY_UP29, i); break;//"
				case 2: G_cTxt = std::format(ON_KEY_UP30, i); break;//"
				case 3: G_cTxt = std::format(ON_KEY_UP31, i); break;//"
				case 4: G_cTxt = std::format(ON_KEY_UP32, i); break;//"
				case 5: G_cTxt = std::format(ON_KEY_UP33, i); break;//"
				case 50:G_cTxt = std::format(ON_KEY_UP34, i); break;//"
				case 51:G_cTxt = std::format(ON_KEY_UP35, i); break;//"
				case 52:G_cTxt = std::format(ON_KEY_UP36, i); break;//"
				}
			}
			else {
				switch (m_player->m_special_ability_type) {
				case 1: G_cTxt = std::format(ON_KEY_UP37, i / 60); break;//"
				case 2: G_cTxt = std::format(ON_KEY_UP38, i / 60); break;//"
				case 3: G_cTxt = std::format(ON_KEY_UP39, i / 60); break;//"
				case 4: G_cTxt = std::format(ON_KEY_UP40, i / 60); break;//"
				case 5: G_cTxt = std::format(ON_KEY_UP41, i / 60); break;//"
				case 50:G_cTxt = std::format(ON_KEY_UP42, i / 60); break;//"
				case 51:G_cTxt = std::format(ON_KEY_UP43, i / 60); break;//"
				case 52:G_cTxt = std::format(ON_KEY_UP44, i / 60); break;//"
				}
			}
			add_event_list(G_cTxt.c_str(), 10);
		}
	}
}

void CGame::hotkey_simple_zoom_in()
{
	if (text_input_manager::get().is_active() == false)
	{
		config_manager::get().set_zoom_map_enabled(true);
	}
}

void CGame::hotkey_simple_zoom_out()
{
	if (text_input_manager::get().is_active() == false)
	{
		config_manager::get().set_zoom_map_enabled(false);
	}
}
