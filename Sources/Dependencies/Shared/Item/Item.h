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
#include "ItemInstanceData.h"
#include "NetConstants.h"
#include "Game/BalanceConstants.h"
#include <cstring>
#include <cstdint>
#include <algorithm>

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
        m_item_sub_type = 0;
        m_equip_pos = 0;
        m_item_effect_type = 0;

        m_item_effect_value1 = 0;
        m_item_effect_value2 = 0;
        m_item_effect_value3 = 0;
        m_item_effect_value4 = 0;
        m_item_effect_value5 = 0;
        m_item_effect_value6 = 0;

        m_durability = 0;
        m_special_effect = 0;
        m_special_effect_value1 = 0;
        m_special_effect_value2 = 0;

        m_x = 0;
        m_y = 0;

        m_display_id = -1;

        m_weapon_class = 0;
        m_swing_speed = 0;

        m_sell_price = 0;
        m_weight = 0;
        m_level_requirement = 0;
        m_gender_requirement = 0;

        m_related_skill = 0;

        m_hide_armor = 0;
        m_is_skirt = 0;
        m_stackable = 0;
        m_is_dyeable = 0;
        m_armor_class = 0;
        m_set_id = 0;

        // m_instance is zero-initialized by its default member initializers
    }

    inline virtual ~CItem()
    {
    }

    //------------------------------------------------------------------------
    // Core Item Data
    //------------------------------------------------------------------------

    char  m_name[hb::shared::limits::ItemNameLen];    // Internal item name (from database)

    short m_id_num;                 // Item ID number (unique identifier / lookup key — NOT instance data)
    char  m_item_type;              // Item type (see item_type enum)
    char  m_item_sub_type;          // Item sub-type (see item_sub_type enum)
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
    // Visual Properties
    //------------------------------------------------------------------------

    short m_display_id = -1;       // Atlas display ID (maps to ItemSpriteMetadata, -1 = unmapped)

    //------------------------------------------------------------------------
    // Position (client-side for inventory/ground display)
    //------------------------------------------------------------------------

    short m_x;                     // X position
    short m_y;                     // Y position

    //------------------------------------------------------------------------
    // Weapon Properties
    //------------------------------------------------------------------------

    char  m_weapon_class;          // Weapon class (see weapon_class enum)
    char  m_swing_speed;           // Weapon attack speed

    //------------------------------------------------------------------------
    // Stats and Limits
    //------------------------------------------------------------------------

    uint32_t m_sell_price;         // Sell price in gold (0 = cannot sell)
    uint16_t m_weight;             // Weight (affects encumbrance)
    short m_level_requirement;     // Minimum level to use
    char  m_gender_requirement;    // Gender restriction (0=none, 1=male, 2=female)
    short m_related_skill;         // Related skill for proficiency

    //------------------------------------------------------------------------
    // Durability (config — maximum)
    //------------------------------------------------------------------------

    uint16_t m_durability;         // Maximum durability

    //------------------------------------------------------------------------
    // Behavioral Flags
    //------------------------------------------------------------------------

    char  m_hide_armor;            // Body armor hides sprite when equipped
    char  m_is_skirt;              // Pants render as skirt for female characters
    char  m_stackable;             // Item merges into a single slot with count
    char  m_is_dyeable;            // Item can be a dye target
    char  m_armor_class;           // Armor class (0=none, 1=clothing, 2=armor)
    int16_t m_set_id;              // Equipment set ID (0 = no set)
    int   m_attribute_pool_id = 0; // Legacy attribute pool (items.attribute_pool_id; 0 = never rolls)

    //------------------------------------------------------------------------
    // Client-side lock (prevents interaction while item is in a pending operation)
    //------------------------------------------------------------------------

    bool m_locked = false;

    bool is_locked() const { return m_locked; }
    bool try_lock() { if (m_locked) return false; m_locked = true; return true; }
    void unlock() { m_locked = false; }

    //------------------------------------------------------------------------
    // Instance Data (per-instance mutable fields)
    //------------------------------------------------------------------------

    hb::shared::item::item_instance_data m_instance;

    //------------------------------------------------------------------------
    // Provenance identity (server-only)
    //------------------------------------------------------------------------

    // Permanent server-global identity of this item instance (ADR 0003).
    // 0 = unserialed: every Counted (stackable) item, and the item-config
    // templates that describe a type rather than an instance.
    //
    // Deliberately NOT a member of m_instance. That POD is embedded verbatim in
    // every item-carrying packet, so a Serial inside it would be handed to the
    // client — and never-on-the-wire is the whole reason a tampered client
    // cannot observe the tracking. Keeping it out here also means Serials cost
    // no wire bytes and no Compatibility bump.
    //
    // Only the server ever populates this; the field exists on the shared class
    // because CItem is shared, and stays 0 client-side.
    int64_t m_serial = 0;

    // How this item entered the world — a hb::server::item_origin value, held
    // as a plain int because that enum is a server concept and Shared must not
    // depend on Server headers. 0 = none.
    //
    // The ledger's item_instances.origin_type is its permanent home (#76); it
    // lives on the item because a mint does not yet know the item's map or
    // position — the caller places it immediately afterwards — so the creation
    // event cannot be fully formed until after this point.
    int32_t m_origin = 0;

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

    hb::shared::item::item_type::item_type get_item_type() const
    {
        return hb::shared::item::to_item_type(m_item_type);
    }

    void set_item_type(hb::shared::item::item_type::item_type type)
    {
        m_item_type = hb::shared::item::to_int(type);
    }

    hb::shared::item::item_sub_type::item_sub_type get_item_sub_type() const
    {
        return hb::shared::item::to_item_sub_type(m_item_sub_type);
    }

    void set_item_sub_type(hb::shared::item::item_sub_type::item_sub_type type)
    {
        m_item_sub_type = hb::shared::item::to_int(type);
    }

    hb::shared::item::weapon_class::weapon_class get_weapon_class() const
    {
        return hb::shared::item::to_weapon_class(m_weapon_class);
    }

    void set_weapon_class(hb::shared::item::weapon_class::weapon_class wc)
    {
        m_weapon_class = hb::shared::item::to_int(wc);
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
        return m_gender_requirement == 2;
    }

    hb::shared::item::TouchEffectType get_touch_effect_type() const
    {
        return hb::shared::item::to_touch_effect_type(m_instance.touch_effect_type);
    }

    void set_touch_effect_type(hb::shared::item::TouchEffectType type)
    {
        m_instance.touch_effect_type = hb::shared::item::to_int(type);
    }

    //------------------------------------------------------------------------
    // Attribute Helpers
    //------------------------------------------------------------------------

    // Types are unified modifier IDs (ModifierIds.h); prefix/secondary are
    // the legacy-strategy views over modifier slots [0]/[1].
    uint8_t get_prefix_type() const
    {
        return m_instance.get_prefix_type();
    }

    uint8_t get_secondary_type() const
    {
        return m_instance.get_secondary_type();
    }

    uint8_t get_tier() const
    {
        return m_instance.get_tier();
    }

    uint8_t get_prefix_value() const
    {
        return m_instance.get_prefix_value();
    }

    uint8_t get_secondary_value() const
    {
        return m_instance.get_secondary_value();
    }

    uint8_t get_enchant_bonus() const
    {
        return m_instance.get_enchant_bonus();
    }

    void set_enchant_bonus(uint8_t value)
    {
        m_instance.set_enchant_bonus(value);
    }

    void set_prefix(uint8_t type, uint8_t value)
    {
        m_instance.set_prefix(type, value);
    }

    void set_secondary(uint8_t type, uint8_t value)
    {
        m_instance.set_secondary(type, value);
    }

    bool has_prefix() const
    {
        return m_instance.has_prefix();
    }

    bool is_custom_made() const
    {
        return m_instance.is_custom_made();
    }

    void set_custom_made(bool custom)
    {
        m_instance.set_custom_made(custom);
    }

    bool has_special_attributes() const
    {
        return m_instance.has_attributes();
    }

    // The whole attribute POD — packets embed the same struct, so
    // encode/decode is plain assignment through these.
    const hb::shared::item::item_attribute_data& get_attributes() const
    {
        return m_instance.attributes;
    }

    void set_attributes(const hb::shared::item::item_attribute_data& attributes)
    {
        m_instance.attributes = attributes;
    }

    // Return a const reference to the instance data
    const hb::shared::item::item_instance_data& to_instance_data() const { return m_instance; }

    // Check if item is stackable (reads the config flag)
    bool is_stackable() const
    {
        return m_stackable != 0;
    }

    // Base damage range from dice values (throw D range + bonus)
    dice_range get_damage_range() const
    {
        return parse_dice(m_item_effect_value1, m_item_effect_value2, m_item_effect_value3);
    }

    //------------------------------------------------------------------------
    // Derived stats (Item Tiers §4.5 — stat-touchers compute from modifier
    // data; the roll never mutates the item's stored stats)
    //
    // Light and Agile are read-time derivations: base config value in,
    // modifier-adjusted value out, idempotent by construction. Callers pass
    // the modifier's catalog multiplier because only they own a catalog
    // (server m_modifier_multiplier[], client m_modifier_catalog[]) — the
    // stored roll value is in multiplier units, display units are value x it.
    //
    // Strong/Ancient max durability is NOT here: cur_durability is persisted
    // and clamped against the max, and creation/repair set cur = max, so the
    // effective max has to exist on the item before anything reads it. The
    // server recomputes m_durability from the config baseline at every
    // materialization point instead (ItemManager::apply_modifier_derived_stats).
    //------------------------------------------------------------------------

    // Stored roll value of a modifier this item carries, 0 if it has none.
    uint8_t get_modifier_value(uint8_t modifier_id) const
    {
        return m_instance.attributes.modifier_value(modifier_id);
    }

    bool has_modifier(uint8_t modifier_id) const
    {
        return m_instance.attributes.has_modifier(modifier_id);
    }

    // Light weight reduction in percent, capped at 100 so a data edit can
    // never invert the weight.
    int get_light_percent(int light_multiplier) const
    {
        int percent = get_modifier_value(hb::shared::item::modifier_id::light) * light_multiplier;
        return percent < 100 ? percent : 100;
    }

    // The one Light reduction formula. Callers that already hold a base weight
    // (the client reads it from the item config rather than the instance) use
    // this directly; get_effective_weight is the instance-bound form.
    static inline constexpr int apply_light_reduction(int base_weight, int light_percent)
    {
        if (light_percent <= 0) return base_weight;
        return base_weight * (100 - light_percent) / 100;
    }

    // Weight after the Light reduction — the value every weight gate and
    // carry-load sum must use.
    int get_effective_weight(int light_multiplier) const
    {
        return apply_light_reduction(m_weight, get_light_percent(light_multiplier));
    }

    // Swing speed after Agile. Agile is a value-less flag (catalog multiplier
    // 0, band 0..0) worth a flat -1, retail-faithful per spec §6.
    int get_effective_swing_speed() const
    {
        int speed = m_swing_speed;
        if (has_modifier(hb::shared::item::modifier_id::agile))
            --speed;
        return speed > 0 ? speed : 0;
    }

    // Total weight for a stack of items (effective_weight * count), 0 for zero-weight items
    static inline constexpr int calc_item_stack_weight(int effective_weight, int count)
    {
        if (effective_weight <= 0) return 0;
        return effective_weight * count;
    }

    // Clamp item count to int16_t range for wire protocol (v2 field)
    static inline constexpr short count_to_v2(uint64_t count)
    {
        return static_cast<short>(count > 32767 ? 32767 : count);
    }

    // Convert raw weight units to stones (float)
    static inline constexpr float weight_to_stones(int raw_weight)
    {
        return static_cast<float>(raw_weight) / hb::shared::balance::weight_units_per_stone;
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
