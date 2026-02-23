// Screen_OnGame.h: Main gameplay screen
//
// Handles in-game rendering and input processing when player is in the world
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "IGameScreen.h"
#include <cstdint>

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

    void item_drop_external_screen(char item_id, short mouse_x, short mouse_y);

private:
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
};
