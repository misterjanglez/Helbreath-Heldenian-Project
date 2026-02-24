// IGameScreen.cpp: Implementation of IGameScreen helper methods
//
//////////////////////////////////////////////////////////////////////

#include "IGameScreen.h"
#include "EventListManager.h"
#include "Game.h"
#include "GameModeManager.h"
#include "GameFonts.h"
#include "TextLibExt.h"
#include "GlobalDef.h"
#include "SpriteID.h"

IGameScreen::IGameScreen(CGame* game)
    : m_game(game)
{
}

// ============== Drawing Helpers ==============

void IGameScreen::draw_new_dialog_box(char type, int sX, int sY, int frame,
                                    bool is_no_color_key, bool is_trans)
{
    m_game->draw_new_dialog_box(type, sX, sY, frame, is_no_color_key, is_trans);
}

void IGameScreen::get_centered_dialog_pos(char type, int frame, int& outX, int& outY)
{
    auto rect = m_game->m_sprite[type]->GetFrameRect(frame);
    outX = (LOGICAL_WIDTH() - rect.width) / 2;
    outY = (LOGICAL_HEIGHT() - rect.height) / 2;
}

void IGameScreen::put_string(int iX, int iY, const char* string, const hb::shared::render::Color& color)
{
    hb::shared::text::draw_text(GameFont::Default, iX, iY, string, hb::shared::text::TextStyle::from_color(color));
}

void IGameScreen::put_aligned_string(int x1, int x2, int iY, const char* string,
                                    const hb::shared::render::Color& color)
{
    hb::shared::text::draw_text_aligned(GameFont::Default, x1, iY, x2 - x1, 15, string,
                             hb::shared::text::TextStyle::from_color(color), hb::shared::text::Align::TopCenter);
}

void IGameScreen::put_string_spr_font(int iX, int iY, const char* str, uint8_t r, uint8_t g, uint8_t b)
{
    hb::shared::text::draw_text(GameFont::Bitmap1, iX, iY, str, hb::shared::text::TextStyle::with_highlight(hb::shared::render::Color(r, g, b)));
}

void IGameScreen::draw_version()
{
    m_game->draw_version();
}


// ============== Event/Message Helpers ==============

void IGameScreen::add_event_list(const char* txt, char color, bool dup_allow)
{
    event_list_manager::get().add_event(txt, color, dup_allow);
}

// ============== Timing Helper ==============

uint32_t IGameScreen::get_elapsed_ms() const
{
    return GameClock::get_time_ms() - GameModeManager::get_mode_start_time();
}
