#pragma once

#include "PacketCommon.h"
#include "PacketHeaders.h"
#include <cstdint>

namespace hb {
namespace net {

//------------------------------------------------------------------------
// Color Palette Configuration Packet
// Sent from server to client to define the color palette tables
// used for item tints, weapon tints, and hair colors.
// tableId selects the target table: 0 = regular, 1 = weapon.
// The weapon table (indexes 1-9) mirrors the original client's separate
// weapon color table; regular carries items 0-15, prefixes 16-21, hair 32-47.
//------------------------------------------------------------------------

namespace color_palette_table
{
	constexpr uint8_t regular = 0;
	constexpr uint8_t weapon = 1;
}

// Palette index bands shared by client and server: prefix tints occupy 16-21.
// Their sprite-tint colors live in the weapon table; the regular table's 16-21
// entries hold the absolute colors used for name-text dyes.
constexpr uint8_t first_prefix_color = 16;
constexpr uint8_t last_prefix_color = 21;

HB_PACK_BEGIN
struct HB_PACKED PacketColorPaletteConfigEntry
{
	uint8_t tableId;
	uint8_t colorId;
	uint8_t r;
	uint8_t g;
	uint8_t b;
};
HB_PACK_END

//------------------------------------------------------------------------
// Color Palette Configuration Packet Header
// Sent before a batch of color palette entries
//------------------------------------------------------------------------

HB_PACK_BEGIN
struct HB_PACKED PacketColorPaletteConfigHeader
{
	PacketHeader header;            // msg_id = MsgId::ColorPaletteConfigContents
	uint16_t     colorCount;        // Number of entries in this packet
	uint16_t     totalColors;       // Total entries across all packets
	uint16_t     packetIndex;       // Index of this packet (0-based)
};
HB_PACK_END

} // namespace net
} // namespace hb
