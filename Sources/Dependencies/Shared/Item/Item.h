// Item.h: Unified CItem class for Helbreath Client and Server
//
// This class combines the item definitions from both Client/Item.h and Server/Item.h
// with the addition of m_cDisplayName for localized display names.
//
// Note: Display names are now stored directly in the database (name column).
// The server sends display names directly, so no client-side lookup is needed.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "ItemEnums.h"
#include "NetConstants.h"
#include <cstring>
#include <cstdint>

struct dice_range
{
	int min;
	int max;
};

constexpr dice_range parse_dice(int num_rolls, int num_sides, int bonus = 0)
{
	return { num_rolls + bonus, num_rolls * num_sides + bonus };
}

class CItem
{
public:
    inline CItem()
    {
        std::memset(m_name, 0, sizeof(m_name));

        m_id_num = 0;
        m_item_type = 0;
        m_equip_pos = 0;
        m_item_effect_type = 0;

        m_item_effect_value1 = 0;
        m_item_effect_value2 = 0;
        m_item_effect_value3 = 0;
        m_item_effect_value4 = 0;
        m_item_effect_value5 = 0;
        m_item_effect_value6 = 0;

        m_max_life_span = 0;
        m_special_effect = 0;
        m_special_effect_value1 = 0;
        m_special_effect_value2 = 0;

        m_x = 0;
        m_y = 0;

        m_display_id = -1;

        m_appearance_value = 0;
        m_speed = 0;

        m_price = 0;
        m_weight = 0;
        m_level_limit = 0;
        m_gender_limit = 0;

        m_related_skill = 0;

        m_category = 0;
        m_is_for_sale = false;

        m_count = 1;
        m_touch_effect_type = 0;
        m_touch_effect_value1 = 0;
        m_touch_effect_value2 = 0;
        m_touch_effect_value3 = 0;

        m_item_color = 0;
        m_item_special_effect_value1 = 0;
        m_item_special_effect_value2 = 0;
        m_item_special_effect_value3 = 0;

        m_cur_life_span = 0;
        m_attribute = 0;
    }

    inline virtual ~CItem()
    {
    }

    //------------------------------------------------------------------------
    // Core Item Data
    //------------------------------------------------------------------------

    char  m_name[hb::shared::limits::ItemNameLen];    // Internal item name (from database)

    short m_id_num;                 // Item ID number (unique identifier)
    char  m_item_type;              // Item type (see ItemType enum)
    char  m_equip_pos;              // Equipment position (see EquipPos enum)

    //------------------------------------------------------------------------
    // Item Effects
    //------------------------------------------------------------------------

    short m_item_effect_type;        // Primary effect type (see ItemEffectType enum)
    short m_item_effect_value1;      // Effect value 1
    short m_item_effect_value2;      // Effect value 2
    short m_item_effect_value3;      // Effect value 3
    short m_item_effect_value4;      // Effect value 4 (for armor: stat bonus type)
    short m_item_effect_value5;      // Effect value 5 (for armor: stat bonus amount)
    short m_item_effect_value6;      // Effect value 6

    //------------------------------------------------------------------------
    // Special Effects
    //------------------------------------------------------------------------

    short m_special_effect;         // Special effect type
    short m_special_effect_value1;   // Special effect value 1
    short m_special_effect_value2;   // Special effect value 2

    //------------------------------------------------------------------------
    // Item-Specific Effects (attributes applied to item instance)
    //------------------------------------------------------------------------

    short m_item_special_effect_value1;  // Item spec effect value 1
    short m_item_special_effect_value2;  // Item spec effect value 2
    short m_item_special_effect_value3;  // Item spec effect value 3

    //------------------------------------------------------------------------
    // Touch Effects (triggered on first touch/acquisition)
    //------------------------------------------------------------------------

    short m_touch_effect_type;       // Touch effect type (see TouchEffectType enum)
    short m_touch_effect_value1;     // Touch effect value 1
    short m_touch_effect_value2;     // Touch effect value 2
    short m_touch_effect_value3;     // Touch effect value 3

    //------------------------------------------------------------------------
    // Visual Properties
    //------------------------------------------------------------------------

    short m_display_id = -1;       // Atlas display ID (maps to ItemSpriteMetadata, -1 = unmapped)
    char  m_appearance_value;             // Appearance value (for equipped items)
    char  m_item_color;             // Item color variant

    //------------------------------------------------------------------------
    // Position (client-side for inventory/ground display)
    //------------------------------------------------------------------------

    short m_x;                     // X position
    short m_y;                     // Y position

    //------------------------------------------------------------------------
    // Stats and Limits
    //------------------------------------------------------------------------

    char  m_speed;                 // Weapon attack speed
    uint32_t m_price;              // Base price in gold
    uint16_t m_weight;             // Weight (affects encumbrance)
    short m_level_limit;            // Minimum level to use
    char  m_gender_limit;           // Gender restriction (0=none, 1=male, 2=female)
    short m_related_skill;          // Related skill for proficiency

    //------------------------------------------------------------------------
    // Durability
    //------------------------------------------------------------------------

    uint16_t m_max_life_span;        // Maximum durability
    uint16_t m_cur_life_span;        // Current durability

    //------------------------------------------------------------------------
    // Miscellaneous
    //------------------------------------------------------------------------

    char  m_category;              // Item category (for shop filtering)
    bool  m_is_for_sale;             // Can be sold to NPC shops
    uint64_t m_count;             // Stack count (for stackable items)

    //------------------------------------------------------------------------
    // Attribute Flags
    //
    // Bit layout: aaaa bbbb cccc dddd eeee ffff xxxx xxx1
    //   1: Custom-Made Item flag
    //   a: Item attribute type
    //   b: Item attribute value
    //   c: Special item flag
    //   d: Special item flag value
    //   e: Additional special flag
    //   f: Additional special flag value
    //------------------------------------------------------------------------

    uint32_t m_attribute;

    //------------------------------------------------------------------------
    // Display Name Helpers
    //------------------------------------------------------------------------

    // Returns display name if set, otherwise falls back to m_name
    // Note: Since DB migration, m_name now contains display names directly
    const char* get_display_name() const
    {
        return m_name;
    }

    //------------------------------------------------------------------------
    // Type-Safe Enum Accessors
    //------------------------------------------------------------------------

    hb::shared::item::EquipPos get_equip_pos() const
    {
        return hb::shared::item::to_equip_pos(m_equip_pos);
    }

    void set_equip_pos(hb::shared::item::EquipPos pos)
    {
        m_equip_pos = hb::shared::item::to_int(pos);
    }

    hb::shared::item::ItemType get_item_type() const
    {
        return hb::shared::item::to_item_type(m_item_type);
    }

    void set_item_type(hb::shared::item::ItemType type)
    {
        m_item_type = hb::shared::item::to_int(type);
    }

    hb::shared::item::ItemEffectType get_item_effect_type() const
    {
        return hb::shared::item::to_item_effect_type(m_item_effect_type);
    }

    void set_item_effect_type(hb::shared::item::ItemEffectType type)
    {
        m_item_effect_type = hb::shared::item::to_int(type);
    }

    bool sprite_is_female() const
    {
        return m_gender_limit == 2;
    }

    hb::shared::item::TouchEffectType get_touch_effect_type() const
    {
        return hb::shared::item::to_touch_effect_type(m_touch_effect_type);
    }

    void set_touch_effect_type(hb::shared::item::TouchEffectType type)
    {
        m_touch_effect_type = hb::shared::item::to_int(type);
    }

    //------------------------------------------------------------------------
    // Attribute Helpers
    //------------------------------------------------------------------------

    bool is_custom_made() const
    {
        return (m_attribute & 0x00000001) != 0;
    }

    void set_custom_made(bool custom)
    {
        if (custom)
            m_attribute |= 0x00000001;
        else
            m_attribute &= ~0x00000001;
    }

    // get attribute type (bits 28-31, 'a' nibble)
    uint8_t get_attribute_type() const
    {
        return static_cast<uint8_t>((m_attribute >> 28) & 0x0F);
    }

    // get attribute value (bits 24-27, 'b' nibble)
    uint8_t get_attribute_value() const
    {
        return static_cast<uint8_t>((m_attribute >> 24) & 0x0F);
    }

    // Check if item is stackable based on its type
    bool is_stackable() const
    {
        return hb::shared::item::is_stackable_type(get_item_type());
    }

    // Base damage range from dice values (throw D range + bonus)
    dice_range get_damage_range() const
    {
        return parse_dice(m_item_effect_value1, m_item_effect_value2, m_item_effect_value3);
    }

    // Light attribute percentage from special flag (bits 20-23 type, 16-19 value)
    // Type 6 = light, percentage = value * 4
    int get_light_percent() const
    {
        uint8_t special_type = static_cast<uint8_t>((m_attribute & 0x00F00000) >> 20);
        if (special_type == 6)
        {
            uint8_t special_value = static_cast<uint8_t>((m_attribute & 0x000F0000) >> 16);
            return special_value * 4;
        }
        return 0;
    }

    // Weight adjusted for light attribute
    int get_effective_weight() const
    {
        int light = get_light_percent();
        if (light > 0)
            return static_cast<int>(m_weight) * (100 - light) / 100;
        return m_weight;
    }

    // Check if item is a weapon
    bool is_weapon() const
    {
        return hb::shared::item::is_weapon_slot(get_equip_pos());
    }

    // Check if item is armor
    bool is_armor() const
    {
        return hb::shared::item::is_armor_slot(get_equip_pos());
    }

    // Check if item is an accessory
    bool is_accessory() const
    {
        return hb::shared::item::is_accessory_slot(get_equip_pos());
    }
};
