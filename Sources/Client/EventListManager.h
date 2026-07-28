#pragma once

#include <cstdint>
#include <string>
#include <cstring>
#include <cstdio>

#include "Render/PrimitiveTypes.h"

class CGame;

namespace hb::shared::render { class IRenderer; }

// A colored span inside an event line — the Tier-colored item name in the pickup
// line (Item Tiers spec §11). len == 0 means the line draws in its base color.
struct event_highlight
{
	size_t start = 0;
	size_t len = 0;
	hb::shared::render::Color color;
};

struct EventEntry
{
	uint32_t time = 0;
	char color = 0;
	std::string txt;
	event_highlight highlight;
};

class event_list_manager
{
public:
	static event_list_manager& get();
	void set_game(CGame* game);

	void add_event(const char* txt, char color = 0, bool dup_allow = true,
		const event_highlight& highlight = {});
	void add_event_top(const char* txt, char color);
	void show_events(uint32_t time);

private:
	event_list_manager() = default;
	~event_list_manager() = default;

	CGame* m_game = nullptr;
	EventEntry m_events[6]{};
	EventEntry m_events2[6]{};
};
