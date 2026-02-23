// Screen_Login.cpp: Login Screen Implementation
//
//////////////////////////////////////////////////////////////////////

#include "Screen_Login.h"
#include "Game.h"
#include "GameModeManager.h"
#include "IInput.h"
#include "GlobalDef.h"
#include "ASIOSocket.h"
#include "Misc.h"
#include "GameFonts.h"
#include "TextLibExt.h"
#include "TextFieldRenderer.h"
#include "InputStateHelper.h"

using namespace hb::shared::net;
using namespace hb::client::sprite_id;

Screen_Login::Screen_Login(CGame* game)
    : IGameScreen(game)
{
}

void Screen_Login::on_initialize()
{
    GameModeManager::set_current_mode(GameMode::Login);
    m_game->m_arrow_pressed = 0;

    m_controls.set_screen_size(LOGICAL_WIDTH(), LOGICAL_HEIGHT());
    m_controls.set_hover_focus(false);
    m_controls.set_enter_advances_focus(true);
    m_controls.set_default_button(BTN_LOGIN);

    // Clipboard provider (via engine abstraction)
    m_controls.set_clipboard_provider(
        [] { return hb::shared::input::get_clipboard_text(); },
        [](const std::string& s) { hb::shared::input::set_clipboard_text(s); }
    );

    // Textbox render helper — draws text, cursor, and selection for all states
    auto textbox_renderer = [](const cc::control& c) {
        auto& field = static_cast<const cc::text_input&>(c);
        hb::client::draw_text_field(field, GameClock::get_time_ms(),
            hb::shared::text::TextStyle::with_shadow(GameColors::InputValid),
            hb::shared::text::TextStyle::with_shadow(GameColors::InputInvalid));
    };

    // === Textboxes ===
    auto* tb_name = m_controls.add<cc::textbox>(TXT_NAME, cc::rect{234, 222, 147, 17}, 11);
    tb_name->set_character_filter(hb::client::username_allowed_chars);
    tb_name->set_validator([](const std::string& s) {
        return !s.empty() && CMisc::check_valid_name(s.c_str());
    });
    tb_name->set_render_handler(textbox_renderer);

    auto* tb_password = m_controls.add<cc::textbox>(TXT_PASSWORD, cc::rect{234, 245, 147, 17}, 11);
    tb_password->set_hidden(true);
    tb_password->set_character_filter(hb::client::password_allowed_chars);
    tb_password->set_validator([](const std::string& s) {
        return !s.empty() && CMisc::check_valid_string(s.c_str());
    });
    tb_password->set_render_handler(textbox_renderer);

    // === Buttons ===
    auto* btn_login = m_controls.add<cc::button>(BTN_LOGIN, cc::rect{140, 343, 84, 20});
    btn_login->set_on_click([this](int) { submit_login(); });
    btn_login->set_click_sound([this] { play_game_sound('E', 14, 5); });
    btn_login->set_render_handler([this](const cc::control& c) {
        // Only highlight when both fields are valid
        auto* tb_n = m_controls.find_as<cc::textbox>(TXT_NAME);
        auto* tb_p = m_controls.find_as<cc::textbox>(TXT_PASSWORD);
        bool all_valid = tb_n && tb_n->is_valid() && tb_p && tb_p->is_valid();
        if (all_valid && c.is_highlighted())
        {
            auto sb = c.screen_bounds();
            m_game->draw_new_dialog_box(InterfaceNdLogin, sb.x, sb.y, 3, true);
        }
    });

    auto* btn_cancel = m_controls.add<cc::button>(BTN_CANCEL, cc::rect{316, 343, 76, 20});
    btn_cancel->set_on_click([this](int) { m_game->change_game_mode(GameMode::MainMenu); });
    btn_cancel->set_click_sound([this] { play_game_sound('E', 14, 5); });
    btn_cancel->set_render_handler([this](const cc::control& c) {
        if (c.is_highlighted())
        {
            auto sb = c.screen_bounds();
            m_game->draw_new_dialog_box(InterfaceNdLogin, sb.x, sb.y, 4, true);
        }
    });

    m_controls.set_focus_order({TXT_NAME, TXT_PASSWORD, BTN_LOGIN, BTN_CANCEL});
    m_controls.set_focus(TXT_NAME);
}

void Screen_Login::on_uninitialize()
{
}

void Screen_Login::on_update()
{
    cc::input_state input;
    hb::client::fill_input_state(input);
    m_controls.update(input, GameClock::get_time_ms());

    if (m_controls.escape_pressed())
        m_game->change_game_mode(GameMode::MainMenu);
}

void Screen_Login::submit_login()
{
    auto* tb_name = m_controls.find_as<cc::textbox>(TXT_NAME);
    auto* tb_pass = m_controls.find_as<cc::textbox>(TXT_PASSWORD);

    if (!tb_name->is_valid() || !tb_pass->is_valid()) return;

    m_game->m_account_name = tb_name->text().c_str();
    m_game->m_account_password = tb_pass->text().c_str();

    m_game->m_l_sock = std::make_unique<hb::shared::net::ASIOSocket>(
        m_game->m_io_pool->get_context(), game_limits::socket_block_limit);
    m_game->m_l_sock->connect(m_game->m_log_server_addr.c_str(), m_game->m_log_server_port);
    m_game->m_l_sock->init_buffer_size(hb::shared::limits::MsgBufferSize);

    m_game->change_game_mode(GameMode::Connecting);
    m_game->m_connect_mode = MsgId::request_login;
    std::snprintf(m_game->m_msg, sizeof(m_game->m_msg), "%s", "11");
}

void Screen_Login::on_render()
{
    // Background
    draw_new_dialog_box(InterfaceNdLogin, 0, 0, 0, true);

    // Login box fade-in: 500ms delay, then 200ms fade
    static constexpr uint32_t FADE_DELAY_MS = 500;
    static constexpr uint32_t FADE_DURATION_MS = 200;

    uint32_t elapsed = get_elapsed_ms();
    if (elapsed > FADE_DELAY_MS)
    {
        float progress = static_cast<float>(elapsed - FADE_DELAY_MS) / FADE_DURATION_MS;
        float alpha = progress > 1.0f ? 1.0f : progress;
        m_game->m_sprite[InterfaceNdLogin]->draw(99, 182, 2,
            hb::shared::sprite::DrawParams::alpha_blend(alpha));
    }

    // Controls (textbox values + buttons)
    m_controls.render();

    draw_version();
}
