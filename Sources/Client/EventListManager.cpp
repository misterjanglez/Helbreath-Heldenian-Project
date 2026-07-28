#include "EventListManager.h"
#include "Game.h"
#include "GameFonts.h"
#include "TextLibExt.h"
#include "GameTimer.h"
#include "lan_eng.h"
#include "Screen_OnGame.h"

event_list_manager& event_list_manager::get()
{
	static event_list_manager instance;
	return instance;
}

void event_list_manager::set_game(CGame* game)
{
	m_game = game;
}

namespace {

// Scroll a six-slot list up one and write the newest entry into the last slot.
void push_event(EventEntry (&list)[6], const char* txt, char color, const event_highlight& highlight)
{
	for (int i = 1; i < 6; i++)
		list[i - 1] = list[i];

	list[5].txt = txt;
	list[5].color = color;
	list[5].time = GameClock::get_time_ms();
	list[5].highlight = highlight;
}

// The event-list palette. Both lists share it; an unlisted color index draws
// nothing at all, which is what the original per-list switches did by omission.
const hb::shared::render::Color* event_color(char color)
{
	switch (color) {
	case 0:                       return &GameColors::UINearWhite;
	case 1:                       return &GameColors::ChatEventGreen;
	case 2:                       return &GameColors::UIWorldChat;
	case 3:                       return &GameColors::UIFactionChat;
	case 4:                       return &GameColors::UIPartyChat;
	case hb::shared::owner::Slime:  return &GameColors::UIGameMasterChat;
	case hb::shared::owner::Howard: return &GameColors::UINormalChat;
	default:                      return nullptr;
	}
}

// One event line. A highlight span (the Tier-colored item name of a pickup line)
// splits the draw into prefix / name / suffix, each advancing by its own measured
// width; without one this is the single draw_text it has always been.
void draw_event_line(const EventEntry& entry, int x, int y)
{
	const hb::shared::render::Color* base = event_color(entry.color);
	if (base == nullptr) return;

	const auto& span = entry.highlight;
	if (span.len == 0 || span.start + span.len > entry.txt.size())
	{
		hb::shared::text::draw_text(GameFont::Default, x, y, entry.txt.c_str(),
			hb::shared::text::TextStyle::with_shadow(*base));
		return;
	}

	const std::string prefix = entry.txt.substr(0, span.start);
	const std::string name = entry.txt.substr(span.start, span.len);
	const std::string suffix = entry.txt.substr(span.start + span.len);

	if (!prefix.empty())
	{
		hb::shared::text::draw_text(GameFont::Default, x, y, prefix.c_str(),
			hb::shared::text::TextStyle::with_shadow(*base));
		x += hb::shared::text::measure_text(GameFont::Default, prefix.c_str()).width;
	}

	hb::shared::text::draw_text(GameFont::Default, x, y, name.c_str(),
		hb::shared::text::TextStyle::with_shadow(span.color));

	if (!suffix.empty())
	{
		x += hb::shared::text::measure_text(GameFont::Default, name.c_str()).width;
		hb::shared::text::draw_text(GameFont::Default, x, y, suffix.c_str(),
			hb::shared::text::TextStyle::with_shadow(*base));
	}
}

}

void event_list_manager::add_event(const char* txt, char color, bool dup_allow, const event_highlight& highlight)
{
	if ((dup_allow == false) && (m_events[5].txt == txt)) return;
	push_event(color == 10 ? m_events2 : m_events, txt, color, highlight);
}

void event_list_manager::add_event_top(const char* txt, char color)
{
	push_event(m_events, txt, color, {});
}

void event_list_manager::show_events(uint32_t time)
{
	int i;
	int baseY = EVENTLIST2_BASE_Y();
	m_game->m_Renderer->begin_text_batch();
	// uint32_t subtraction wraps safely — expired events (time far in the past) produce
	// large differences that exceed 5000, so the comparison remains correct across wrap
	for (i = 0; i < 6; i++)
		if ((time - m_events[i].time) < 5000)
			draw_event_line(m_events[i], 10, 10 + i * 15);

	for (i = 0; i < 6; i++)
		if ((time - m_events2[i].time) < 5000)
			draw_event_line(m_events2[i], 10, baseY + i * 15);
	if (m_game->on_game()->m_skill_using_status == true)
	{
		int text_w = hb::shared::text::GetTextRenderer()->measure_text(SHOW_EVENT_LIST1).width;
		int text_x = (LOGICAL_WIDTH() - text_w) / 2;
		int text_y = LOGICAL_HEIGHT() / 2 + 45;
		hb::shared::text::draw_text(GameFont::Default, text_x, text_y, SHOW_EVENT_LIST1, hb::shared::text::TextStyle::with_shadow(GameColors::UINearWhite));
	}
	m_game->m_Renderer->end_text_batch();
}
