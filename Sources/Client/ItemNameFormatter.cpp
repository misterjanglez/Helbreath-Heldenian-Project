#include "ItemNameFormatter.h"
#include "Item/Item.h"
#include "lan_eng.h"
#include "GameConstants.h"
#include "OwnerType.h"

#include <format>
#include <string>

using hb::shared::item::ItemType;
using hb::shared::item::EquipPos;

item_name_formatter& item_name_formatter::get()
{
	static item_name_formatter instance;
	return instance;
}

void item_name_formatter::set_item_configs(const std::array<std::unique_ptr<CItem>, 5000>& configs)
{
	m_item_configs = &configs;
}

CItem* item_name_formatter::get_config(int item_id) const
{
	if (!m_item_configs || item_id <= 0 || item_id >= 5000) return nullptr;
	return (*m_item_configs)[item_id].get();
}

ItemNameInfo item_name_formatter::format(CItem* item)
{
	ItemNameInfo result;
	std::string txt;
	uint32_t type1, type2, value1, value2, value3;

	CItem* cfg = get_config(item->m_id_num);
	if (!cfg) {
		result.name = "Unknown Item";
		return result;
	}

	const char* name = cfg->get_display_name();

	if (hb::shared::item::is_special_item(item->m_id_num)) result.is_special = true;

	if ((item->m_attribute & 0x00000001) != 0)
	{
		result.is_special = true;
		result.name = name;
		if (cfg->get_item_type() == ItemType::Material)
			result.effects.push_back({"Purity: ", std::format("{}%", item->m_item_special_effect_value2)});
		else
		{
			if (cfg->get_equip_pos() == EquipPos::LeftFinger)
				result.effects.push_back({"Completion: ", std::format("{}%", item->m_item_special_effect_value2)});
			else
				result.effects.push_back({"Completion: ", std::format("{}%", item->m_item_special_effect_value2 + 100)});
		}
	}
	else
	{
		if (item->m_count == 1)
			result.name = name;
		else result.name = std::format(DRAW_DIALOGBOX_SELLOR_REPAIR_ITEM1, item->m_count, name);
	}

	if ((item->m_attribute & 0x00F0F000) != 0)
	{
		result.is_special = true;
		type1 = (item->m_attribute & 0x00F00000) >> 20;
		value1 = (item->m_attribute & 0x000F0000) >> 16;
		type2 = (item->m_attribute & 0x0000F000) >> 12;
		value2 = (item->m_attribute & 0x00000F00) >> 8;
		if (type1 != 0)
		{
			switch (type1) {
			case 1: txt = GET_ITEM_NAME3;   break;
			case 2: txt = GET_ITEM_NAME4;   break;
			case 3: txt = GET_ITEM_NAME5;   break;
			case 4: break;
			case 5: txt = GET_ITEM_NAME6;   break;
			case 6: txt = GET_ITEM_NAME7;   break;
			case 7: txt = GET_ITEM_NAME8;   break;
			case 8: txt = GET_ITEM_NAME9;   break;
			case 9: txt = GET_ITEM_NAME10;  break;
			case hb::shared::owner::Slime: txt = GET_ITEM_NAME11; break;
			case hb::shared::owner::Skeleton: txt = GET_ITEM_NAME12; break;
			case hb::shared::owner::StoneGolem: txt = GET_ITEM_NAME13; break;
			}
			txt += result.name;
			result.name = txt;

			switch (type1) {
			case 1: result.effects.push_back({"Critical Hit Damage", std::format("+{}", value1)}); break;
			case 2: result.effects.push_back({"Poison Damage", std::format("+{}", value1 * 5)}); break;
			case 3: break;
			case 4: break;
			case 5: result.effects.push_back({"Attack Speed -1", ""}); break;
			case 6: result.effects.push_back({std::format("{}% light", value1 * 4), ""}); break;
			case 7: result.effects.push_back({"Damage added", ""}); break;
			case 8: result.effects.push_back({"Endurance ", std::format("+{}%", value1 * 7)}); break;
			case 9: result.effects.push_back({"Extra Damage added", ""}); break;
			case hb::shared::owner::Slime: result.effects.push_back({"Magic Casting Probability ", std::format("+{}%", value1 * 3)}); break;
			case hb::shared::owner::Skeleton: result.effects.push_back({std::format("Replace {}% damage to mana", value1), ""}); break;
			case hb::shared::owner::StoneGolem: result.effects.push_back({"Crit Increase Chance ", std::format("{}%", value1)}); break;
			}

			if (type2 != 0) {
				switch (type2) {
				case 1:  result.effects.push_back({"Poison Resistance", std::format("+{}%", value2 * 7)}); break;
				case 2:  result.effects.push_back({"Hitting Probability", std::format("+{}", value2 * 7)}); break;
				case 3:  result.effects.push_back({"Defense Ratio", std::format("+{}", value2 * 7)}); break;
				case 4:  result.effects.push_back({"HP recovery ", std::format("{}%", value2 * 7)}); break;
				case 5:  result.effects.push_back({"SP recovery ", std::format("{}%", value2 * 7)}); break;
				case 6:  result.effects.push_back({"MP recovery ", std::format("{}%", value2 * 7)}); break;
				case 7:  result.effects.push_back({"Magic Resistance", std::format("+{}%", value2 * 7)}); break;
				case 8:  result.effects.push_back({"Physical Absorption", std::format("+{}%", value2 * 3)}); break;
				case 9:  result.effects.push_back({"Magic Absorption", std::format("+{}%", value2 * 3)}); break;
				case hb::shared::owner::Slime: result.effects.push_back({"Consecutive Attack Damage", std::format("+{}", value2)}); break;
				case hb::shared::owner::Skeleton: result.effects.push_back({"Experience", std::format("+{}%", value2 * 10)}); break;
				case hb::shared::owner::StoneGolem: result.effects.push_back({"Gold", std::format("+{}%", value2 * 10)}); break;
				}
			}
		}
	}

	value3 = (item->m_attribute & 0xF0000000) >> 28;
	if (value3 > 0)
	{
		auto plusPos = result.name.rfind('+');
		if (plusPos != std::string::npos && plusPos + 1 < result.name.size())
		{
			try {
				int existingPlus = std::stoi(result.name.substr(plusPos + 1));
				value3 += existingPlus;
				result.name = std::format("{}+{}", result.name.substr(0, plusPos), value3);
			} catch (...) {
				result.name += std::format("+{}", value3);
			}
		}
		else
		{
			result.name += std::format("+{}", value3);
		}
	}

	// Display mana save effect if present
	auto effectType = cfg->get_item_effect_type();
	int mana_save_value = 0;
	if (effectType == hb::shared::item::ItemEffectType::AttackManaSave)
	{
		mana_save_value = cfg->m_item_effect_value4;
	}
	else if (effectType == hb::shared::item::ItemEffectType::add_effect &&
	         cfg->m_item_effect_value1 == hb::shared::item::to_int(hb::shared::item::AddEffectType::ManaSave))
	{
		mana_save_value = cfg->m_item_effect_value2;
	}

	if (mana_save_value > 0)
	{
		result.is_special = true;
		result.effects.push_back({"Mana save ", std::format("+{}%", mana_save_value)});
	}

	return result;
}

ItemNameInfo item_name_formatter::format(short item_id, uint32_t attribute)
{
	ItemNameInfo result;
	std::string txt;
	uint32_t type1, type2, value1, value2, value3;

	// Look up item config by ID to get display name
	const char* name = nullptr;
	CItem* cfg = get_config(item_id);
	if (cfg != nullptr) {
		name = cfg->m_name;
	}
	if (name == nullptr || name[0] == '\0') {
		result.name = "Unknown Item";
		return result;
	}
	result.name = name;

	if ((attribute & 0x00F0F000) != 0)
	{
		result.is_special = true;
		type1 = (attribute & 0x00F00000) >> 20;
		value1 = (attribute & 0x000F0000) >> 16;
		type2 = (attribute & 0x0000F000) >> 12;
		value2 = (attribute & 0x00000F00) >> 8;
		if (type1 != 0)
		{
			switch (type1) {
			case 1: txt = GET_ITEM_NAME3; break;
			case 2: txt = GET_ITEM_NAME4; break;
			case 3: txt = GET_ITEM_NAME5; break;
			case 4: break;
			case 5: txt = GET_ITEM_NAME6; break;
			case 6: txt = GET_ITEM_NAME7; break;
			case 7: txt = GET_ITEM_NAME8; break;
			case 8: txt = GET_ITEM_NAME9; break;
			case 9: txt = GET_ITEM_NAME10; break;
			case hb::shared::owner::Slime: txt = GET_ITEM_NAME11; break;
			case hb::shared::owner::Skeleton: txt = GET_ITEM_NAME12; break;
			case hb::shared::owner::StoneGolem: txt = GET_ITEM_NAME13; break;
			}
			txt += result.name;
			result.name = txt;

			switch (type1) {
			case 1: result.effects.push_back({"Critical Hit Damage", std::format("+{}", value1)}); break;
			case 2: result.effects.push_back({"Poison Damage", std::format("+{}", value1 * 5)}); break;
			case 3: break;
			case 4: break;
			case 5: result.effects.push_back({"Attack Speed -1", ""}); break;
			case 6: result.effects.push_back({std::format("{}% light", value1 * 4), ""}); break;
			case 7: result.effects.push_back({"Damage added", ""}); break;
			case 8: result.effects.push_back({"Endurance ", std::format("+{}%", value1 * 7)}); break;
			case 9: result.effects.push_back({"Extra Damage added", ""}); break;
			case hb::shared::owner::Slime: result.effects.push_back({"Magic Casting Probability ", std::format("+{}%", value1 * 3)}); break;
			case hb::shared::owner::Skeleton: result.effects.push_back({std::format("Replace {}% damage to mana", value1), ""}); break;
			case hb::shared::owner::StoneGolem: result.effects.push_back({"Crit Increase Chance ", std::format("{}%", value1)}); break;
			}

			if (type2 != 0)
			{
				switch (type2) {
				case 1:  result.effects.push_back({"Poison Resistance", std::format("+{}%", value2 * 7)}); break;
				case 2:  result.effects.push_back({"Hitting Probability", std::format("+{}", value2 * 7)}); break;
				case 3:  result.effects.push_back({"Defense Ratio", std::format("+{}", value2 * 7)}); break;
				case 4:  result.effects.push_back({"HP recovery ", std::format("{}%", value2 * 7)}); break;
				case 5:  result.effects.push_back({"SP recovery ", std::format("{}%", value2 * 7)}); break;
				case 6:  result.effects.push_back({"MP recovery ", std::format("{}%", value2 * 7)}); break;
				case 7:  result.effects.push_back({"Magic Resistance", std::format("+{}%", value2 * 7)}); break;
				case 8:  result.effects.push_back({"Physical Absorption", std::format("+{}%", value2 * 3)}); break;
				case 9:  result.effects.push_back({"Magic Absorption", std::format("+{}%", value2 * 3)}); break;
				case hb::shared::owner::Slime: result.effects.push_back({"Consecutive Attack Damage", std::format("+{}", value2)}); break;
				case hb::shared::owner::Skeleton: result.effects.push_back({"Experience", std::format("+{}%", value2 * 10)}); break;
				case hb::shared::owner::StoneGolem: result.effects.push_back({"Gold", std::format("+{}%", value2 * 10)}); break;
				}
			}
		}
	}

	value3 = (attribute & 0xF0000000) >> 28;
	if (value3 > 0)
	{
		auto plusPos = result.name.rfind('+');
		if (plusPos != std::string::npos && plusPos + 1 < result.name.size())
		{
			try {
				int existingPlus = std::stoi(result.name.substr(plusPos + 1));
				value3 += existingPlus;
				result.name = std::format("{}+{}", result.name.substr(0, plusPos), value3);
			} catch (...) {
				result.name += std::format("+{}", value3);
			}
		}
		else
		{
			result.name += std::format("+{}", value3);
		}
	}

	return result;
}
