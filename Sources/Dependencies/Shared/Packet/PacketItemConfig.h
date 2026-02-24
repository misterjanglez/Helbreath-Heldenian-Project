#pragma once

#include "PacketCommon.h"
#include "PacketHeaders.h"
#include "NetConstants.h"
#include <cstdint>

namespace hb {
namespace net {

//------------------------------------------------------------------------
// Item Configuration Packet
// Sent from server to client to define item properties.
// Binary format replaces the old text-based "Item = ..." parsing.
//------------------------------------------------------------------------

HB_PACK_BEGIN
struct HB_PACKED PacketItemConfigEntry
{
	int16_t  itemId;                // Item ID number
	char     name[hb::shared::limits::ItemNameLen];    // Item display name (fixed size, null-padded)
	int8_t   itemType;              // Item type (see ItemType enum)
	int8_t   equipPos;              // Equipment position (see EquipPos enum)
	int16_t  effectType;            // Primary effect type
	int16_t  effectValue1;          // Effect value 1
	int16_t  effectValue2;          // Effect value 2
	int16_t  effectValue3;          // Effect value 3
	int16_t  effectValue4;          // Effect value 4
	int16_t  effectValue5;          // Effect value 5
	int16_t  effectValue6;          // Effect value 6
	uint16_t maxLifeSpan;           // Maximum durability
	int16_t  specialEffect;         // Special effect type
	int32_t  price;                 // Price (negative if not for sale)
	uint16_t weight;                // Item weight
	int8_t   apprValue;             // Appearance value
	int8_t   speed;                 // Weapon attack speed
	int16_t  levelLimit;            // Minimum level requirement
	int8_t   genderLimit;           // Gender restriction
	int16_t  specialEffectValue1;   // Special effect value 1
	int16_t  specialEffectValue2;   // Special effect value 2
	int16_t  relatedSkill;          // Related skill ID
	int8_t   category;              // Item category
	int8_t   itemColor;             // Item color variant
	int16_t  displayId;             // Atlas display ID (maps to ItemSpriteMetadata)
};
HB_PACK_END

// Note: Actual size depends on packing. Both client and server use this same header.

//------------------------------------------------------------------------
// Item Configuration Packet Header
// Sent before a batch of item entries
//------------------------------------------------------------------------

HB_PACK_BEGIN
struct HB_PACKED PacketItemConfigHeader
{
	PacketHeader header;            // Standard packet header (msg_id = hb::shared::net::MsgId::ItemConfigContents)
	uint16_t     itemCount;         // Number of items in this packet
	uint16_t     totalItems;        // Total items across all packets (0 if unknown/single packet)
	uint16_t     packetIndex;       // Index of this packet (0-based, for multi-packet sends)
};
HB_PACK_END

} // namespace net
} // namespace hb
