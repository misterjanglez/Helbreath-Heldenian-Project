#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "Item/ItemAttributeData.h"
#include "DirectionHelpers.h"
#include "ItemProvenance.h"
using hb::shared::direction::direction;

namespace hb::server { struct drop_table; struct ledger_event_record; struct roll_context; class item_ledger_store; }
class CClient;
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
	//
	// `birth` is what the venue knows and the funnel cannot (P3.1): where the
	// item came into the world and, where it means anything, who or what made
	// it. Defaulted, so a venue with nothing to add reads exactly as it did
	// before and the ledger records NULLs instead of an invented location.
	CItem* create_item(int item_id, hb::server::item_origin::item_origin origin,
		const hb::server::birth_context& birth = {});
	CItem* create_item(const char* item_name, hb::server::item_origin::item_origin origin,
		const hb::server::birth_context& birth = {});

	// The loot venue, which is the one creation whose birth is not finished when
	// the item is constructed: an NPC drop rolls its Item Tiers attributes onto
	// the fresh instance, so a mint taken before the roll writes a birth row
	// reading Tier 0 for what may be a Tier 3 sword — and tier-by-source is
	// precisely what the drop-rate telemetry exists to answer.
	//
	// Rolling inside the entry point puts the mint after the roll without moving
	// the roll relative to any other draw the drop path makes. The alternative —
	// patching the birth row after the fact — would have the ledger rewriting
	// rows it promises are append-only.
	CItem* create_loot_item(int item_id, const hb::server::roll_context& rolls,
		const hb::server::birth_context& birth);

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

	//----------------------------------------------------------------------
	// Item destruction — the other half of the coverage law (#79, plan P3.1)
	//
	// Creation funnels through the factory above so no item is born without a
	// Serial. Destruction funnels through here so no item dies without saying
	// so: a Serial with a birth row, no exit event and no holder is exactly the
	// shape of a duplication anomaly, and Reconciliation cannot tell that
	// signal apart from an item somebody quietly `delete`d.
	//
	// Scripts/check_item_destroy.py enforces it, with an allowlist for the
	// deletes that are genuinely not destructions — a snapshot being freed, a
	// logged-out character's inventory being released from RAM while the rows
	// stay in the Game DB, a config template at shutdown.
	//----------------------------------------------------------------------

	// Records the exit and frees the item. Takes the pointer by reference and
	// nulls it, so the funnel visibly takes ownership and no caller is left
	// holding a dangling pointer it could log from a line later.
	//
	// Safe for a Counted item: those carry no Serial, so there is no instance
	// for an event to point at and this is then only a delete. Callers do not
	// have to know which kind they hold — that is the point of a funnel.
	//
	// `actor_h` is whoever caused it, or 0 for the world itself. It supplies the
	// event's actor and location the same way every other ledger event gets
	// them, which is why a destruction knows where it happened without the
	// caller assembling anything.
	void destroy_item(CItem*& item, hb::server::destroy_reason::destroy_reason reason,
		int actor_h = 0);

	// The same funnel for an item that left the ground with nobody taking it.
	//
	// Separate from destroy_item because it is a different event type and a
	// different question: destroyed says something was used up or broken,
	// despawned says nobody wanted it — which is the attrition half of the
	// drop-tuning telemetry the ledger exists to produce.
	//
	// There is no actor by definition, so the location cannot come from one:
	// the tile supplies it, which is also the only place it was ever true.
	void despawn_item(CItem*& item, hb::server::despawn_reason::despawn_reason reason,
		const char* map, int x, int y);

	// True when `item_id` indexes a loaded item-config row. The bounds and the
	// null-row check travel together everywhere they appear, so they live here
	// as one named question rather than three conditions repeated per caller.
	bool is_valid_item_id(int item_id) const;

	//----------------------------------------------------------------------
	// Serial durability (#76)
	//
	// The allocator mints in RAM; itemledger.db is where the sequence survives
	// a restart. These two calls are the whole seam — boot lifts the counter
	// above the recovered mark, and each ledger flush persists where it got to.
	//----------------------------------------------------------------------

	// Highest Serial handed out so far. What the ledger writes to
	// meta.serial_high_water on every flush.
	int64_t serial_high_water() const { return m_serial_allocator.high_water(); }

	// Boot recovery: never lowers the counter, so a stale durable mark cannot
	// re-issue a Serial that a loaded item already holds.
	void resume_serials_from(int64_t high_water) { m_serial_allocator.resume_from(high_water); }

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
	// one bulk notification, one GmMint audit line — and one ledger event per
	// copy, which is not the same count (#104). Returns how many landed.
	int add_client_bulk_item_list(int client_h, const char* item_name, int amount);
	void release_item_handler(int client_h, short item_index, bool notice);

	// "No transition to book here" — the value set_item_count callers pass when
	// the count rises, or when the transition is already booked at the call
	// site: a preceding item_log carrying the moved quantity, an escrow
	// recorder tied to the DB commit, or a split whose moved portion is its own
	// item and books its own move. Each site passing it says which of those it
	// is. Never stored: set_item_count skips it and record_counted_flow refuses
	// it at the funnel, so no flow row ever carries a 0.
	static constexpr int32_t flow_none = 0;

	// Sets a stackable's count by slot index and books what left as a Counted
	// flow, the way set_item_count_by_id does by item id — this sibling kept the
	// hole for one more ticket (#105): it booked only the count-reaches-0 case,
	// through item_deplete_handler's exit rows, so a recipe taking 10 reagents
	// out of a stack of 50 recorded nothing. The three crafting venues (potion
	// brewing, jewel and necklace crafting, BuildItem elements) pass `Make`, and
	// what is stored against that number is the consumption of the materials —
	// the made item books its own birth at the factory, so the two are one
	// recipe seen from its two ends.
	//
	// The quantity booked is the DECREASE this call makes, computed inside for
	// the reason #103 ratified: a caller-supplied amount can disagree with what
	// actually changed; a delta cannot. That contract has one rule for callers
	// — pass the TARGET count, never m_instance.count after mutating it
	// yourself. A pre-applied decrease reads as a delta of zero and books
	// nothing while looking correct; no in-tree caller does that (#105
	// converted the splits that did), and coveragecheck pins the books-nothing
	// behavior in case the shape reappears. No default: omitting the parameter
	// is a compile error, which is what keeps the hole from reopening.
	int set_item_count(int client_h, int item_index, uint64_t count,
		int32_t flow_type);

	// Sets a stackable's count by item id and books what left as a Counted flow
	// (#103). Every caller it has is a payment to an NPC — gold spent on a
	// purchase, a repair bill, a spell — which is why the transition is a
	// parameter here rather than something each venue remembers to record: this
	// call *is* the outflow, and before #103 it carried no transition at all, so
	// the currency loop was closed on the inflow side only.
	//
	// `flow_type` is the same numbering everything else in the ledger uses
	// (ItemLedgerStore.h): `Buy` for a purchase, `Repair` for a repair bill,
	// `MagicLearn` for a spell. Route-specific rather than one shared sink
	// number, mirroring the inflow side — gold enters as `created` at the shop
	// and as `NewGenDrop` off a monster — so "how much gold left, and by which
	// route" is one query per route and their sum is the total. No default:
	// omitting it is a compile error, which is what keeps the hole from
	// reopening the next time somebody needs to take gold off a player.
	//
	// The quantity booked is the DECREASE this call makes, computed inside
	// set_item_count — to which this delegates after the id → slot lookup
	// (#105), so the booking contract has one body. A caller-supplied amount
	// could disagree with what actually changed; a delta cannot. A count that
	// does not fall books nothing — quantities are positive, always, because
	// direction is a property of the flow number and never of a stored sign
	// (D6, #81).
	int set_item_count_by_id(int client_h, short item_id, uint64_t count,
		int32_t flow_type);
	uint64_t get_item_count_by_id(int client_h, short item_id);
	int get_item_space_left(int client_h);
	void set_item_pos(int client_h, char* data);
	int get_item_weight(CItem* item, int count);
	bool check_item_receive_condition(int client_h, CItem* item);
	int send_item_notify_msg(int client_h, uint16_t msg_type, CItem* item, int v1);

	// Item use / effects
	void use_item_handler(int client_h, short item_index, short dX, short dY, short dest_item_id);
	// Empties an inventory slot and destroys what was in it — the one path every
	// consuming action already shares (potions, arrows, craft materials, a sale,
	// a broken upgrade), which is why the population-exit event is emitted from
	// here instead of from twenty callers.
	//
	// `reason` defaults to consumed because that is what most of those callers
	// are; the two that are not — selling to a shop, and an upgrade destroying
	// the item — say so, and nothing else has to change.
	void item_deplete_handler(int client_h, short item_index, bool is_use_item_result,
		hb::server::destroy_reason::destroy_reason reason
			= hb::server::destroy_reason::consumed);
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
	//
	// Why the deposit door takes a transition (#81): a Warehouse deposit is a
	// custody move the ledger has always been meant to carry — the plan names
	// "Warehouse deposit/retrieve" in its opening paragraph — but until this
	// ticket both ItemLogAction::Deposit and ::Retrieve had a text-sink switch
	// arm and no caller anywhere, so the transition was silent in both sinks.
	//
	// It cannot simply emit for everyone, because the Trading Post reaches the
	// same door to land a won bundle, and that move is already recorded as
	// TpTradeIn/TpRefund by deliver_to_bank (#80). Emitting here as well would
	// give one custody move two events, which the coverage law forbids as
	// firmly as it forbids none. So the caller names which it is, and a mandatory
	// parameter is what stops the next caller being silent by omission — the
	// same reason deliver_to_bank made its escrow_move mandatory.
	enum class bank_deposit
	{
		// A character put this in their Warehouse. Emits Deposit.
		by_character,

		// The item arrived by a route that already recorded the custody move
		// (Trading Post delivery). Emits nothing.
		already_recorded,
	};

	bool set_item_to_bank_item(int client_h, CItem* item, bank_deposit how);
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

	//----------------------------------------------------------------------
	// Logging — a dual sink (#78, plan P2.2, decision D5)
	//
	// Both overloads write the human text channels they always wrote AND append
	// a structured Provenance Ledger event. The two sinks answer different
	// questions: the channels are for an operator tailing a file, the ledger is
	// for a query run months later, so what the channels leave out on purpose
	// (uninteresting items, unknown actions) the ledger still has to carry.
	//
	// Since #81 they are also the Counted tier's recorders: an item with no
	// Serial books an item_flows aggregate instead of an item_events row, from
	// the same call.
	//----------------------------------------------------------------------

	// How much of a Counted stack the transition moved.
	//
	// The default reads the item's own count, which is right wherever the item
	// handed to the emitter IS the portion that moved — a split-off drop, a
	// picked-up pile, an escrowed bundle. A caller passes an explicit amount
	// when it is not: a shop sale hands over the whole stack and takes `num`
	// out of it, so the stack's count would over-report the sale by everything
	// the player kept.
	//
	// Ignored entirely for an Instanced item, whose transitions are events.
	static constexpr int64_t flow_qty_from_item = -1;

	bool item_log(int action, int client_h, char* name, CItem* item);
	bool item_log(int action, int give_h, int recv_h, CItem* item, bool force_item_log = false,
		int64_t qty = flow_qty_from_item);

	//----------------------------------------------------------------------
	// GM minting — the one action with two doors (#104)
	//
	// Every other action moves one thing, so one call feeds both sinks. A mint
	// makes N things at once, and the two sinks disagree about what N is:
	//
	//   - The ledger's unit is the Serial. N copies are N events, because the
	//     factory gives each copy its own birth row and an event that names one
	//     of them leaves the other N-1 held by nobody — which Reconciliation
	//     reads as `held_unrecorded`, the one question the ledger exists to
	//     answer. That was the #104 defect: one event for the whole batch.
	//   - The operator channel's unit is the request. One line, whether the GM
	//     asked for one copy or a hundred; a hundred lines for one command is a
	//     different kind of unreadable log.
	//
	// So a minting venue calls record_gm_mint per copy and log_gm_mint once.
	// Splitting them is also what retires the quantity that used to travel in
	// item_log's `recv_h` (#78 deferred it to "whoever adds the next emitter"):
	// that parameter is now a counterparty handle in every action without
	// exception, and a quantity is a parameter that says so.

	// The ledger door. Call it as each copy lands and while that copy is still
	// alive — a Counted copy that merged into an existing stack is freed a line
	// later, and its aggregate flow is read off the husk before it goes.
	void record_gm_mint(int client_h, CItem* item);

	// The operator door: one trade-channel line for a whole mint request.
	// `quantity` is what was actually created, and `sample` any one of the
	// copies, since a batch is identical by construction. False when there is
	// nothing to describe or nobody to attribute it to, matching item_log.
	bool log_gm_mint(int client_h, int quantity, CItem* sample);

	//----------------------------------------------------------------------
	// Recording for a Serial the caller does not hold as a CItem (#80)
	//
	// The Trading Post is why these exist. Under ADR 0001 an escrowed bundle is
	// database rows, not objects: the CItem is destroyed on the way in and
	// rebuilt on the way out, so between those two moments the only thing that
	// identifies the item is its Serial. Every other emitter has an object to
	// point at; escrow has a number.
	//
	// They come as a pair — prepare the shared half once, then record each
	// Serial against it — because escrow moves in bundles. The alternative,
	// a self-contained call per item, would re-resolve the same character and
	// rebuild the same JSON once per item in the bundle.
	//----------------------------------------------------------------------

	// The half of a ledger event that a whole bundle shares: who did it and,
	// when they are online, where they were standing.
	//
	// `actor` is the caller's already-resolved client, or null when the
	// character is offline. It is passed in rather than looked up from the name
	// because resolving a name means scanning every client slot, and the callers
	// that record a bundle have the answer in hand — the escrow-out path had to
	// resolve it to know where to deliver.
	//
	// An offline actor contributes their name and no location. That is the
	// honest answer — an escrowed item is returned to a character, not to a
	// place — and it is what the handle-based path already does with a handle
	// that no longer resolves.
	hb::server::ledger_event_record ledger_actor(const char* actor_char,
		const CClient* actor) const;

	// One transition of an already-minted item, against a base record the caller
	// prepared with ledger_actor (and whose counterparty and detail it filled
	// in). Records nothing for a Counted item — serial 0 has no instance for an
	// event to point at — and copies the base only for a row that will exist.
	void record_ledger_event(int event_type, int64_t serial,
		const hb::server::ledger_event_record& base);

	// A destroyed event's `detail`. Public because a caller ending a whole
	// bundle builds it once, and shared with destroy_item so the two ways an
	// item can end cannot come to disagree about how a reason is written down.
	static std::string destroyed_detail(hb::server::destroy_reason::destroy_reason reason);

	// The text sink's noise filter: a whitelist of the item types worth a line in
	// a channel a human reads. Deliberately NOT consulted by the ledger half —
	// an audit log that only records the items somebody once thought interesting
	// has a hole shaped exactly like the item a dispute will be about.
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
	// Serial only when the item is Instanced (non-stackable, per D2). Minting is
	// also where the ledger's birth record is emitted, so every creation venue
	// lands one without knowing the ledger exists.
	//
	// Call it when the item is finished, not merely constructed — the birth row
	// snapshots the item as it is at this instant, so a venue that still has
	// attributes to roll (loot) must roll them first or record a lie.
	void stamp_provenance(CItem* item, hb::server::item_origin::item_origin origin,
		const hb::server::birth_context& birth = {});

public:
	// A birth_context standing where a player is. Most creation venues are
	// something a character did — bought, crafted, fished, mined, turned in a
	// quest — so "where did this item enter the world" is answered by the actor's
	// position, and having one helper say so keeps twenty call sites from each
	// reaching into m_client_list to spell out the same three fields.
	//
	// An unresolvable handle yields an empty context rather than a fabricated
	// one, which is the same promise begin_ledger_event makes for events.
	hb::server::birth_context birth_at(int client_h, const char* detail = nullptr) const;

private:

	// The Provenance Ledger, or null when there is nothing to record into —
	// which is every call before boot has opened the store, and every call in a
	// tool that runs without one.
	hb::server::item_ledger_store* ledger() const;

	// m_client_list index 0 is never a client, and the log calls pass 0 and -1
	// as "nobody". Null for anything that does not resolve, so the ledger path
	// never indexes on trust. (Until #104 one action also passed a quantity
	// where a handle goes, which this survived by not trusting the number.)
	CClient* client_at(int client_h) const;

	// Fills what every ledger event shares — Serial, type, and the actor's name,
	// account and position — and answers whether there is an event to record at
	// all. False for a Counted item (no identity to key on, D2) and when no
	// ledger is open, which is also the callers' cue not to build a detail
	// string for a row that is not going to exist.
	//
	// `event_type` takes either band (ItemLedgerStore.h): an ItemLogAction from
	// the dual sink, or a ledger-only value like destroyed from #79's exits. The
	// bands are disjoint by static_assert, so one parameter can carry both
	// without the two ever having to be told apart here.
	//
	// Answering false for a Counted item is not the same as recording nothing
	// (#81). Before it declines, it books the transition as an aggregate flow —
	// which is why the Counted tier costs zero new call sites: every emitter that
	// has an EVENT to build comes through this one door, so a stackable moving is
	// recorded by the same line that records an Instanced one.
	//
	// Two Counted-only venues call record_counted_flow directly instead, and both
	// have the same reason: they know a quantity this door would get wrong. Shop
	// sale proceeds are minted with a count set after the factory returns (#81),
	// and a payment to an NPC books the decrease it makes rather than the stack
	// it came out of (#103). Neither has an Instanced counterpart to build an
	// event for, so neither loses anything by not passing here.
	bool begin_ledger_event(int event_type, const CItem* item, int actor_h,
		hb::server::ledger_event_record& event,
		int64_t qty = flow_qty_from_item) const;

	// One Counted transition, as an item_flows aggregate (D2). `event_type` is
	// the same number an Instanced item would have written to
	// item_events.event_type — there is deliberately no second taxonomy, see
	// item_ledger_store::record_flow.
	//
	// Silent for an Instanced item, for qty 0, and when no ledger is open, so
	// the guards live here rather than at each of the callers begin_ledger_event
	// fans out to.
	void record_counted_flow(int event_type, const CItem& item, int64_t qty) const;

	// Writes an online character's identity and position onto an event. One
	// definition of what those five columns mean, shared by both openings, so a
	// column added later cannot reach one emitter and silently miss the other.
	static void stamp_actor(const CClient& actor, hb::server::ledger_event_record& event);

	// Field-by-field copy. `to` is the destination — private because
	// create_snapshot is the only sanctioned reason to copy an item: a copy that
	// outlives the log line it was made for is a Serial handed to two objects,
	// which is the exact anomaly Reconciliation exists to catch.
	bool copy_item_contents(CItem* to, CItem* from);

	CGame* m_game = nullptr;

	// Serial source for this server. In-RAM until #76 gives it a durable home.
	hb::server::serial_allocator m_serial_allocator;
};
