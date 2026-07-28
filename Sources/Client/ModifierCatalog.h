// ModifierCatalog.h: client-side replicated modifier catalog (Tiers 1-D)
//
// Filled from PacketModifierCatalogEntry rows on login (or cache replay);
// drives tooltip labels, item-name prefixes, and display value scaling.
// Indexed by unified modifier ID (Shared Item/ModifierIds.h).
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include "Render/PrimitiveTypes.h"

struct modifier_catalog_entry
{
	bool present = false;
	std::string display_name;   // item-name prefix word ("" = none)
	std::string effect_label;   // "{}" inside = whole-line format
	std::string effect_format;  // value format ("" = label-only line)
	uint8_t multiplier = 1;     // display scale applied to rolled values
	uint8_t bucket_id = 0;
	uint8_t min_tier = 0;
	bool marquee = false;
};

// Replicated tier presentation (spec §11): name + name color per tier.
struct tier_presentation_entry
{
	std::string name;
	hb::shared::render::Color color = hb::shared::render::Color::White();
};
