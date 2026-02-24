// Screen_OnGame.h: Main gameplay screen
//
// Handles in-game rendering and input processing when player is in the world
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "IGameScreen.h"
#include "GuildManager.h"
#include "DialogBoxManager.h"
#include "NetworkMessageManager.h"
#include <cstdint>
#include <memory>

class CPlayer;

class Screen_OnGame : public IGameScreen
{
public:
    SCREEN_TYPE(Screen_OnGame)

    explicit Screen_OnGame(CGame* game);
    ~Screen_OnGame() override = default;

    void on_initialize() override;
    void on_uninitialize() override;
    void on_update() override;
    void on_render() override;
    bool on_text_input(uint32_t codepoint) override;
    bool on_key_down(KeyCode key) override;
    bool on_key_up(KeyCode key) override;
    bool on_game_msg(uint32_t msg_id, uint16_t msg_type, char* data, uint32_t msg_size) override;

    void handle_create_new_guild_response(char* data);
    void handle_disband_guild_response(char* data);

    void item_drop_external_screen(char item_id, short mouse_x, short mouse_y);
    guild_manager& get_guild_manager() { return m_guild_manager; }
    DialogBoxManager& get_dialog_box_manager() { return *m_dialog_box_manager; }

    // Player lifecycle — static because there's only ever one player
    static void create_player();
    static void destroy_player();
    static CPlayer* get_player() { return s_player.get(); }

    // Hotkey registration (Screen_OnGame.Hotkeys.cpp)
    void register_hotkeys();

private:
    // Complex hotkey handlers (Screen_OnGame.Hotkeys.cpp)
    void hotkey_use_health_potion();
    void hotkey_use_mana_potion();
    void hotkey_tab_toggle_combat();
    void hotkey_escape();
    void hotkey_special_ability();
    void hotkey_load_backup_chat();
    void hotkey_whisper_cycle_up();
    void hotkey_whisper_cycle_down();
    void hotkey_toggle_force_attack();
    void hotkey_cycle_detail_level();
    void hotkey_toggle_sound_and_music();
    void hotkey_whisper_target();

    void render_item_tooltip();
    void draw_tile_grid();           // Simple dark grid lines
    void draw_patching_grid();       // Debug grid with zone colors
    void draw_spell_target_overlay(); // Spell AoE targeting overlay (debug only)

private:
    // Screen-specific state (previously file-scope static variables)
    short m_sMsX = 0;
    short m_sMsY = 0;
    short m_sMsZ = 0;
    char m_cLB = 0;
    char m_cRB = 0;
    uint32_t m_time = 0;
    short m_sDivX = 0;
    short m_sModX = 0;
    short m_sDivY = 0;
    short m_sModY = 0;
    short m_pivot_x = 0;
    short m_pivot_y = 0;
    uint32_t m_dwPrevChatTime = 0;
    uint32_t m_dwLastBubbleTime = 0;
    guild_manager m_guild_manager;
    std::unique_ptr<DialogBoxManager> m_dialog_box_manager;
    std::unique_ptr<NetworkMessageManager> m_network_message_manager;

    static std::unique_ptr<CPlayer> s_player;
};
