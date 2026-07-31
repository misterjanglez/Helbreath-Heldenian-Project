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
	uint8_t bucket_id = 0;      // bucket identity, not an ordering key
	uint8_t min_tier = 0;
	bool marquee = false;
	uint8_t effect_placement = 0;   // effect_placement enum: standalone vs inline
	uint8_t bucket_sort_order = 0;  // THE tooltip ordering key (tier_buckets.sort_order)
	std::string bucket_name;        // group heading for the Gear pane ("" = none)
	uint8_t band_min = 0;           // display-unit band, for the GM creator's picker
	uint8_t band_max = 0;

	// An attribute-pair row rolls two independent values and says so with a
	// second placeholder in its format — the only replicated signal for what
	// the server calls effect_id::add_attribute_pair, which never travels.
	// The tooltip can stay pair-blind (it hands both rolls to the formatter
	// and a one-placeholder row ignores the second), but anything that has to
	// decide whether a second value EXISTS has to ask.
	bool is_pair() const
	{
		const size_t first = effect_format.find("{}");
		return first != std::string::npos
			&& effect_format.find("{}", first + 2) != std::string::npos;
	}
};

// Replicated tier presentation (spec §11): name + name color per tier.
struct tier_presentation_entry
{
	std::string name;
	hb::shared::render::Color color = hb::shared::render::Color::White();
};
