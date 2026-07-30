#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "Item/ItemAttributeData.h"
#include "DirectionHelpers.h"
#include "ItemProvenance.h"
using hb::shared::direction::direction;

namespace hb::server { struct drop_table; }
class CGame;
class CItem;
struct ShopData;

class ItemManager
{
public:
	ItemManager() = default;
	~ItemManager() = default;

	void set_game(CGame* game) { m_game = game; }

	//----------------------------------------------------------------------
	// Item creation — the only legal path (ADR 0003 coverage law)
	//
	// A raw `new CItem` in game code is a review defect: an audit log with gaps
	// is worse than none, so every item that enters the world is born here.
	// Scripts/check_item_factory.py enforces it mechanically.
	//----------------------------------------------------------------------

	// Creates an item instance from its config, mints a Serial when the item is
	// Instanced (non-stackable, per D2) and records how it entered the world.
	// Returns nullptr when the config lookup fails — callers get the
	// create / init / delete-on-failure dance for free rather than repeating it.
	CItem* create_item(int item_id, hb::server::item_origin::item_origin origin);
	CItem* create_item(const char* item_name, hb::server::item_origin::item_origin origin);

	// Rehydrates a persisted item. A save/load round trip is not a birth, so
	// this carries the stored Serial rather than minting a new one — minting
	// here would make one item read as a fresh instance after every restart.
	//
	// `serial` is 0 until #77 adds the persisted column; an Instanced item that
	// arrives unserialed is minted one, so the world never holds an unserialed
	// instance. Allocator reset across restarts is tolerated under D6.
	CItem* restore_item(int item_id, int64_t serial = 0);

	// Rebuilds an item as the next stage of an evolution chain (majestic items,
	// hero capes), carrying `from`'s identity onto the replacement instead of
	// minting. The physical item survives the change, so a fresh Serial would
	// split one item's Biography into two instances that never met — upgrading
	// is an event in a life, not a new one.
	//
	// Call it before destroying the original: the replacement has to exist
	// before the item it replaces stops existing.
	CItem* transform_item(int new_item_id, const CItem& from);

	// The two deliberate non-instances, named so the coverage law's exceptions
	// read as intentional in review instead of looking like stray allocations.
	// Neither is a world item, so neither carries a Serial.

	// A type template for the item-config catalog.
	static CItem* create_template();

	// A throwaway copy that exists only to describe an item in a log line, and
	// is deleted as soon as that line is written. It shares the source's Serial
	// because it *describes* that item rather than duplicating it.
	CItem* create_snapshot(CItem* source);

	// True when `item_id` indexes a loaded item-config row. The bounds and the
	// null-row check travel together everywhere they appear, so they live here
	// as one named question rather than three conditions repeated per caller.
	bool is_valid_item_id(int item_id) const;

	// Item config / init
	bool send_client_item_configs(int client_h);
	const hb::server::drop_table* get_drop_table(int id) const;
	void clear_item_config_list();
	bool init_item_attr(CItem* item, const char* item_name);
	bool init_item_attr(CItem* item, int item_id);
	void reload_item_configs();

	// Recomputes the item's max durability from its rolled Strong/Ancient
	// lines (spec §4.5). Callers set the durability baseline immediately
	// before calling — init_item_attr from item config, or the custom-made
	// override from special_effect_value1 — so this is idempotent as used and
	// never accumulates. Weight and swing speed are read-time derivations on
	// CItem instead; only durability needs the value materialized on the item
	// (cur_durability is persisted and clamped against it).
	void apply_modifier_derived_stats(CItem* item);

	// Display scale of a catalog modifier (display = stored value x this), 1
	// when the modifier has no row. The modifier catalog is the one multiplier
	// source — the legacy m_modifier_multiplier[] lookup covers only the two
	// legacy attribute tables and is dormant data in tiered mode.
	int modifier_multiplier(uint8_t modifier_id) const;

	// The one GM-mint venue (spec §10, cycle 3-G). Puts `count` copies of
	// item `item_id` carrying `requested` into the GM's inventory, with the
	// attribute set gated by the running mode's Roll strategy, and logs the
	// mint for the economy audit. Returns how many were created; `error` is
	// empty on a clean run and otherwise names why minting stopped — a
	// rejected request creates nothing, since legality does not vary between
	// two identical copies.
	int mint_gm_items(int client_h, int item_id, int count,
		const hb::shared::item::item_attribute_data& requested, std::string& error);

	// Inventory management
	bool add_item(int client_h, CItem* item, char mode);
	bool add_client_item_list(int client_h, CItem* item, int* del_req);
	// Bulk GM creation: `amount` distinct copies into free slots (no merging),
	// one bulk notification, one GmMint audit line. Returns how many landed.
	int add_client_bulk_item_list(int client_h, const char* item_name, int amount);
	void release_item_handler(int client_h, short item_index, bool notice);
	int set_item_count(int client_h, int item_index, uint64_t count);
	int set_item_count_by_id(int client_h, short item_id, uint64_t count);
	uint64_t get_item_count_by_id(int client_h, short item_id);
	int get_item_space_left(int client_h);
	void set_item_pos(int client_h, char* data);
	int get_item_weight(CItem* item, int count);
	bool check_item_receive_condition(int client_h, CItem* item);
	int send_item_notify_msg(int client_h, uint16_t msg_type, CItem* item, int v1);

	// Item use / effects
	void use_item_handler(int client_h, short item_index, short dX, short dY, short dest_item_id);
	void item_deplete_handler(int client_h, short item_index, bool is_use_item_result);
	bool deplete_dest_type_item_use_effect(int client_h, int dX, int dY, short item_index, short dest_item_id);
	int calculate_use_skill_item_effect(int owner_h, char owner_type, char owner_skill, int skill_num, char map_index, int dX, int dY);
	bool plant_seed_bag(int map_index, int dX, int dY, int item_effect_value1, int item_effect_value2, int client_h);

	// Equipment
	bool equip_item_handler(int client_h, short item_index, bool notify = true);
	void validate_equipped_items(int client_h);
	void calc_total_item_effect(int client_h, int equip_item_id, bool notify = true);
	void check_unique_item_equipment(int client_h);
	bool check_and_convert_plus_weapon_item(int client_h, int item_index);
	char check_hero_item_equipped(int client_h);
	int get_arrow_item_index(int client_h);
	void calculate_ssn_item_index(int client_h, short weapon_index, int value);

	// Drop / pickup
	void drop_item_handler(int client_h, short item_index, int amount, const char* item_name, bool by_player = true);
	int client_motion_get_item_handler(int client_h, short sX, short sY, direction dir);

	// Give / exchange
	void give_item_handler(int client_h, short item_index, int amount, short dX, short dY, uint16_t object_id, const char* item_name);
	void exchange_item_handler(int client_h, short item_index, int amount, short dX, short dY, uint16_t object_id, const char* item_name);
	void set_exchange_item(int client_h, int item_index, int amount);
	void confirm_exchange_item(int client_h);
	void cancel_exchange_item(int client_h);
	void clear_exchange_status(int to_h);

	// Bank
	bool set_item_to_bank_item(int client_h, CItem* item);
	bool set_item_to_bank_item(int client_h, short item_index);
	void request_retrieve_item_handler(int client_h, char* data);

	// Shop / purchase / sell
	void request_purchase_item_handler(int client_h, const char* item_name, int num, int item_id = 0);
	void request_sell_item_list_handler(int client_h, char* data);
	void req_sell_item_handler(int client_h, char item_id, char sell_to_whom, int num, const char* item_name);
	void req_sell_item_confirm_handler(int client_h, char item_id, int num, const char* string);

	// Repair
	void req_repair_item_handler(int client_h, char item_id, char repair_whom, const char* string);
	void req_repair_item_cofirm_handler(int client_h, char item_id, const char* string);
	void request_repair_all_items_handler(int client_h);
	void request_repair_all_items_delete_handler(int client_h, int index);
	void request_repair_all_items_confirm_handler(int client_h);

	// Crafting
	void build_item_handler(int client_h, char* data);

	// Upgrade
	void request_item_upgrade_handler(int client_h, int item_index);

	// Hero / special
	void get_hero_mantle_handler(int client_h, int item_id, const char* string);
	void get_dark_item_handler(int client_h, int item_id);
	bool transform_majestic_item(int client_h, int item_index, int new_item_id, int new_value, int glow_color);

	// Slate
	void req_create_slate_handler(int client_h, char* data);
	void set_slate_flag(int client_h, short type, bool flag);

	// Logging
	bool item_log(int action, int client_h, char* name, CItem* item);
	bool item_log(int action, int give_h, int recv_h, CItem* item, bool force_item_log = false);
	bool check_good_item(CItem* item);

private:
	// The four upgrade routes request_item_upgrade_handler dispatches to,
	// keyed off derive_tier_item_class. Only the stone route is data-driven
	// (enchant_categories/enchant_steps, spec §10); the other three are
	// priced in gizon crystals or war contribution and are unchanged.
	void attempt_stone_enchant(int client_h, int item_index, uint8_t category);
	bool roll_stone_enchant_success(int client_h, int item_index, uint8_t category, int success_pct);
	void upgrade_angelic_pendant(int client_h, int item_index);
	void upgrade_majestic_item(int client_h, int item_index);
	void upgrade_hero_cape(int client_h, int item_index);

	int find_inventory_item(int client_h, short item_id) const;

	// Assigns identity to a freshly created item: the origin always, and a
	// Serial only when the item is Instanced (non-stackable, per D2).
	void stamp_provenance(CItem* item, hb::server::item_origin::item_origin origin);

	// Field-by-field copy. `to` is the destination — private because
	// create_snapshot is the only sanctioned reason to copy an item: a copy that
	// outlives the log line it was made for is a Serial handed to two objects,
	// which is the exact anomaly Reconciliation exists to catch.
	bool copy_item_contents(CItem* to, CItem* from);

	CGame* m_game = nullptr;

	// Serial source for this server. In-RAM until #76 gives it a durable home.
	hb::server::serial_allocator m_serial_allocator;
};
