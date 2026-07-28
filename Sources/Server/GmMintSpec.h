// GmMintSpec.h: operator-typed mint specs (Tiers 3-G)
//
// The one parser turning "<tier> [mod_id:value[:value2] ...]" into the
// attribute POD the Roll strategy gates. Two operator venues share it:
// the /createitem chat command and the mintcheck console command. The
// GM creator dialog does not — it sends the POD on the wire already.
//
// Parsing never judges legality: out-of-range ids, impossible tiers and
// duplicate Buckets all parse fine and are the mint gate's business.
// One place decides what is legal (PLANS/ItemTiers_Plan.md §10).
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <vector>

#include "Item/ItemAttributeData.h"

namespace hb::server
{

// Whitespace-separated argument tokens, in order. Both venues take their
// arguments as one operator-typed string and index into the result.
std::vector<std::string> split_args(const char* args);

// Reads tokens[first] as the tier and every token after it as a modifier
// slot. Empty return = parsed into `out`; otherwise the complaint to show
// the operator verbatim.
std::string parse_mint_spec(const std::vector<std::string>& tokens, size_t first,
	hb::shared::item::item_attribute_data& out);

} // namespace hb::server
