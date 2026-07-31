// PacketModifierCatalog.h: unified modifier catalog replication (Tiers 1-D)
//
// One catalog stream replaces the two legacy attribute-type config packets.
// Rides config-cache slot 7 with SHA-256 hash negotiation; the tier
// presentation table (names + colors + name template) and the item-system
// mode flag travel in every chunk header so the client caches all three
// together. Design contract: PLANS/ItemTiers_Plan.md §11/§12.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "PacketCommon.h"
#include "PacketHeaders.h"
#include "Item/ModifierIds.h"

#include <cstdint>

namespace hb {
namespace net {
	HB_PACK_BEGIN
	// One replicated tier row: player-facing name + name color (spec §11).
	struct HB_PACKED PacketTierPresentationEntry {
		char name[16];
		uint8_t r;
		uint8_t g;
		uint8_t b;
	};

	// One replicated catalog row. effect_label/effect_format drive tooltip
	// rendering: a label containing "{}" formats the whole line from the
	// scaled value; otherwise effect_format renders the value text beside
	// the label. display_name is the item-name prefix word (empty = none).
	struct HB_PACKED PacketModifierCatalogEntry {
		uint8_t modifier_id;     // unified modifier ID (ModifierIds.h)
		char display_name[32];
		char effect_label[48];
		char effect_format[16];
		uint8_t multiplier;      // display scale applied to rolled values
		uint8_t bucket_id;       // tier_bucket identity (FK target, not an order)
		uint8_t min_tier;        // lowest tier the modifier rolls at (0 = any)
		uint8_t marquee;         // 1 = Legendary-only marquee bucket entry
		uint8_t effect_placement; // effect_placement; standalone vs inline line
		// tier_buckets.sort_order for this row's bucket, denormalized onto the
		// row because buckets have no packet of their own. THE tooltip ordering
		// key: bucket_id is an identity and a foreign-key target, so reordering
		// the tooltip must not mean renumbering it everywhere it is referenced.
		uint8_t bucket_sort_order;
		// The bucket's player-facing name, denormalized onto the row for the
		// same reason sort_order is: buckets have no packet of their own. The
		// character panel's Gear tab titles its groups with it — before this it
		// could order and rule them apart but had nothing to call them.
		char bucket_name[16];
		// Display-unit band, so the GM creator's value picker can offer only
		// legal values instead of learning the window from a rejection string.
		// uint8 covers the seeded data (widest band tops out at 91).
		uint8_t band_min;
		uint8_t band_max;
	};

	// The presentation fields (mode, template, tiers) are meaningful in
	// chunk 0 only; later chunks repeat them but clients ignore the copies.
	struct HB_PACKED PacketModifierCatalogHeader {
		PacketHeader header;
		uint16_t entryCount;
		uint16_t totalEntries;
		uint16_t packetIndex;
		uint8_t item_system_mode;                 // hb::shared::item::item_system_mode
		char tier_name_template[32];              // e.g. "{tier} {name}"
		PacketTierPresentationEntry tiers[hb::shared::item::tier_count]; // Common..Legendary
	};
	HB_PACK_END
}
}
