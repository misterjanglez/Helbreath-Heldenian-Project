#pragma once

#include "PacketCommon.h"
#include "NetConstants.h"

#include <cstdint>

namespace hb {
namespace net {
	HB_PACK_BEGIN

	// Individual crusade structure entry (6 bytes)
	struct HB_PACKED CrusadeStructureEntry {
		std::uint8_t type;
		std::int16_t x;
		std::int16_t y;
		std::uint8_t side;
	};

	// Crusade map status header (precedes CrusadeStructureEntry array)
	struct HB_PACKED CrusadeMapStatusHeader {
		char map_name[hb::shared::limits::MapNameLen];
		std::int16_t send_point;
		std::uint8_t count;
	};

	// Meteor strike structure HP data header
	struct HB_PACKED MeteorStrikeHeader {
		std::uint16_t total_points;
	};

	HB_PACK_END
}
}
