#include "GmMintSpec.h"

#include <cstdio>
#include <sstream>

using namespace hb::shared::item;

namespace hb::server
{

std::vector<std::string> split_args(const char* args)
{
	std::vector<std::string> tokens;
	std::istringstream stream(args != nullptr ? args : "");
	std::string token;
	while (stream >> token) tokens.push_back(token);
	return tokens;
}

std::string parse_mint_spec(const std::vector<std::string>& tokens, size_t first,
	item_attribute_data& out)
{
	out.clear();
	if (first >= tokens.size())
		return "expected a tier";

	// Range-checked rather than clamped: a typo'd tier or id must not
	// silently become a different, legal one.
	int tier = 0;
	if (std::sscanf(tokens[first].c_str(), "%d", &tier) != 1 || tier < 0 || tier > 255)
		return "tier must be a number 0-255 (0 = untiered)";
	out.tier = static_cast<uint8_t>(tier);

	const size_t slots = tokens.size() - first - 1;
	if (slots > modifier_slot_count)
		return "an item carries at most four modifiers";

	for (size_t i = 0; i < slots; i++)
	{
		const std::string& token = tokens[first + 1 + i];
		int type = 0, value = 0, value2 = 0;
		const int fields = std::sscanf(token.c_str(), "%d:%d:%d", &type, &value, &value2);
		if (fields < 2)
			return "each modifier is mod_id:value or mod_id:value:value2";

		if (type < 0 || type > 255 || value < 0 || value > 255 || value2 < 0 || value2 > 255)
			return "modifier id and values must be 0-255";

		item_modifier& slot = out.modifiers[i];
		slot.type = static_cast<uint8_t>(type);
		slot.value = static_cast<uint8_t>(value);
		slot.value2 = (fields == 3) ? static_cast<uint8_t>(value2) : uint8_t{ 0 };
	}
	return {};
}

} // namespace hb::server
