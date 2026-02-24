#pragma once

#include <cstdint>
#include <memory>
#include <array>
#include <string>
#include <vector>

class CItem;

struct tooltip_effect
{
	std::string label;   // e.g., "Magic Casting Probability "
	std::string value;   // e.g., "+9%"
};

struct ItemNameInfo
{
	std::string name;
	std::vector<tooltip_effect> effects;
	bool is_special = false;

	// Backward compat: reconstruct combined effect/extra text for non-tooltip callers
	std::string effect_text() const
	{
		if (effects.empty()) return "";
		if (effects.size() == 1) return effects[0].label + effects[0].value;
		std::string result;
		for (size_t i = 0; i + 1 < effects.size(); ++i)
			result += effects[i].label + effects[i].value;
		return result;
	}

	std::string extra_text() const
	{
		if (effects.size() < 2) return "";
		return effects.back().label + effects.back().value;
	}
};

class item_name_formatter
{
public:
	static item_name_formatter& get();

	// Inject item config list (address must remain stable)
	void set_item_configs(const std::array<std::unique_ptr<CItem>, 5000>& configs);

	// format item name, returning name + attribute strings + special flag
	ItemNameInfo format(CItem* item);
	ItemNameInfo format(short item_id, uint32_t attribute);

private:
	item_name_formatter() = default;

	CItem* get_config(int item_id) const;

	const std::array<std::unique_ptr<CItem>, 5000>* m_item_configs = nullptr;
};
