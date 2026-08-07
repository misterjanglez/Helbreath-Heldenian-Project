#pragma once

// The event_schedule table's in-memory shape, shared by its consumers (CGame
// owns the loaded rows, WarManager drives wars from them, the SQLite store
// parses them, the GM commands validate against it) so the event set and its
// spellings cannot drift apart between files (#97).

namespace hb::server::config
{
// Which war event an event_schedule row drives.
namespace scheduled_event
{
	enum type : int
	{
		crusade    = 0,
		heldenian  = 1,
		apocalypse = 2,
		count      = 3,
	};

	// Canonical event_type column spellings, indexed by the enum.
	constexpr const char* names[count] = { "crusade", "heldenian", "apocalypse" };
}

// Heldenian battle types, as stored in event_schedule.battle_type and chosen
// by /beginheldenian. The values are wire/DB data — do not renumber.
namespace heldenian_battle
{
	enum type : int
	{
		battlefield  = 1,
		castle_siege = 2,
	};

	constexpr bool is_valid(int value)
	{
		return value == battlefield || value == castle_siege;
	}
}

// One row of the event_schedule table: when a war event auto-starts and, for
// heldenian/apocalypse, auto-ends. Times are server-local; day is tm_wday
// (0 = Sunday). Inactive rows are loaded but never fire.
struct event_schedule_row
{
	scheduled_event::type event;
	int day;                        // 0 = Sunday .. 6 = Saturday
	int start_hour, start_minute;
	int end_hour, end_minute;       // -1/-1 = no scheduled end
	bool is_active;
	int battle_type;                // heldenian only: heldenian_battle::type
};
} // namespace hb::server::config
