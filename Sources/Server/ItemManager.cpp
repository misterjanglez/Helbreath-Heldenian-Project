#include "ItemManager.h"
#include "Game.h"
#include "StatusEffectManager.h"
#include "WarManager.h"
#include "SkillManager.h"
#include "MagicManager.h"
#include "Item.h"
#include "CombatManager.h"
#include "EntityManager.h"
#include "DynamicObjectManager.h"
#include "DelayEventManager.h"
#include "LootManager.h"
#include "CraftingManager.h"
#include "QuestManager.h"
#include "FishingManager.h"
#include "MiningManager.h"
#include "Packet/SharedPackets.h"
#include "ObjectIDRange.h"
#include "Skill.h"
#include "GameConfigSqliteStore.h"
#include "SharedCalculations.h"
#include "BalanceConstants.h"
#include "ItemLedgerStore.h"
#include "LoginServer.h"   // g_login->save_players_atomic, for the two-party commits
#include "Log.h"
#include "ServerLogChannels.h"
#include "StringCompat.h"
#include "TimeUtils.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <iterator>
#include <string>
#include <utility>

using namespace hb::shared::net;
using namespace hb::shared::direction;

using hb::log_channel;
using namespace hb::shared::action;
using namespace hb::server::net;
using namespace hb::server::config;
using namespace hb::shared::item;
namespace sock = hb::shared::net::socket;
namespace item_origin = hb::server::item_origin;
namespace destroy_reason = hb::server::destroy_reason;
namespace despawn_reason = hb::server::despawn_reason;
namespace ledger_event = hb::server::ledger_event;
namespace dynamic_object = hb::shared::dynamic_object;
namespace smap = hb::server::map;
namespace sdelay = hb::server::delay_event;
using namespace hb::server::npc;
using namespace hb::server::skill;

extern char G_cTxt[512];
extern char G_cData50000[50000];

static std::string format_item_info(CItem* item)
{
	if (item == nullptr) return "(null)";
	char buf[256];
	std::snprintf(buf, sizeof(buf), "%s(count=%llu pfx=%d:%d sec=%d:%d enc=%d cm=%d touch=%d:%d:%d:%d)",
		item->m_name,
		static_cast<unsigned long long>(item->m_instance.count),
		item->get_prefix_type(), item->get_prefix_value(),
		item->get_secondary_type(), item->get_secondary_value(),
		item->get_enchant_bonus(), item->is_custom_made() ? 1 : 0,
		item->m_instance.touch_effect_type,
		item->m_instance.touch_effect_value1,
		item->m_instance.touch_effect_value2,
		item->m_instance.touch_effect_value3);
	std::string info = buf;

	// Tiered instances carry up to four lines, and pfx/sec above only ever
	// show the first two. Appended rather than substituted so a legacy line
	// reads exactly as it always has.
	const auto& attributes = item->get_attributes();
	if (attributes.tier != 0)
	{
		std::snprintf(buf, sizeof(buf), " (tier=%d", (int)attributes.tier);
		info += buf;
		for (const auto& mod : attributes.modifiers)
		{
			if (mod.type == 0) continue;
			std::snprintf(buf, sizeof(buf), " %d:%d:%d",
				(int)mod.type, (int)mod.value, (int)mod.value2);
			info += buf;
		}
		info += ")";
	}
	return info;
}

static bool is_item_suspicious(CItem* item)
{
	if (item == nullptr) return false;
	if (item->m_id_num == 90) return false; // Gold
	if (item->has_special_attributes() && item->get_touch_effect_type() != TouchEffectType::ID)
		return true;
	if (item->get_touch_effect_type() == TouchEffectType::None && item->has_special_attributes())
		return true;
	return false;
}

// Helper function (duplicated from Game.cpp - static file-scope function)
static void NormalizeItemName(const char* src, char* dst, size_t dstSize)
{
	size_t j = 0;
	for (size_t i = 0; src[i] && j < dstSize - 1; ++i) {
		if (src[i] != ' ' && src[i] != '_') {
			dst[j++] = src[i];
		}
	}
	dst[j] = '\0';
}

bool ItemManager::send_client_item_configs(int client_h)
{
	if (m_game->m_client_list[client_h] == 0) {
		return false;
	}

	// Calculate how many items per packet - keep packets small (~7KB) for reliable delivery
	constexpr size_t maxPacketSize = 7000;
	constexpr size_t headerSize = sizeof(hb::net::PacketItemConfigHeader);
	constexpr size_t entrySize = sizeof(hb::net::PacketItemConfigEntry);
	constexpr size_t maxEntriesPerPacket = (maxPacketSize - headerSize) / entrySize;

	// First count total items
	int totalItems = 0;
	for(int i = 0; i < MaxItemTypes; i++) {
		if (m_game->m_item_config_list[i] != 0) {
			totalItems++;
		}
	}

	// Send items in packets
	int itemsSent = 0;
	int packetIndex = 0;

	while (itemsSent < totalItems) {
		// Build packet
		std::memset(G_cData50000, 0, sizeof(G_cData50000));

		auto* pktHeader = reinterpret_cast<hb::net::PacketItemConfigHeader*>(G_cData50000);
		pktHeader->header.msg_id = MsgId::ItemConfigContents;
		pktHeader->header.msg_type = MsgType::Confirm;
		pktHeader->totalItems = static_cast<uint16_t>(totalItems);
		pktHeader->packetIndex = static_cast<uint16_t>(packetIndex);

		auto* entries = reinterpret_cast<hb::net::PacketItemConfigEntry*>(G_cData50000 + headerSize);

		uint16_t entriesInPacket = 0;
		int configIndex = 0;
		int skipped = 0;

		// Find items for this packet
		for(int i = 0; i < MaxItemTypes && entriesInPacket < maxEntriesPerPacket; i++) {
			if (m_game->m_item_config_list[i] == 0) {
				continue;
			}

			// Skip items already sent in previous packets
			if (skipped < itemsSent) {
				skipped++;
				continue;
			}

			const CItem* item = m_game->m_item_config_list[i];
			auto& entry = entries[entriesInPacket];

			entry.itemId = item->m_id_num;
			std::memset(entry.name, 0, sizeof(entry.name));
			std::snprintf(entry.name, sizeof(entry.name), "%s", item->m_name);
			entry.itemType = item->m_item_type;
			entry.itemSubType = item->m_item_sub_type;
			entry.equipPos = item->m_equip_pos;
			entry.weaponClass = item->m_weapon_class;
			entry.effectType = item->m_item_effect_type;
			entry.effectValue1 = item->m_item_effect_value1;
			entry.effectValue2 = item->m_item_effect_value2;
			entry.effectValue3 = item->m_item_effect_value3;
			entry.effectValue4 = item->m_item_effect_value4;
			entry.effectValue5 = item->m_item_effect_value5;
			entry.effectValue6 = item->m_item_effect_value6;
			entry.durability = item->m_durability;
			entry.specialEffect = item->m_special_effect;
			entry.sellPrice = item->m_sell_price;
			entry.weight = item->m_weight;
			entry.swingSpeed = item->m_swing_speed;
			entry.levelRequirement = item->m_level_requirement;
			entry.genderRequirement = item->m_gender_requirement;
			entry.specialEffectValue1 = item->m_special_effect_value1;
			entry.specialEffectValue2 = item->m_special_effect_value2;
			entry.relatedSkill = item->m_related_skill;
			entry.hideArmor = item->m_hide_armor;
			entry.isSkirt = item->m_is_skirt;
			entry.stackable = item->m_stackable;
			entry.isDyeable = item->m_is_dyeable;
			entry.armorClass = item->m_armor_class;
			entry.setId = item->m_set_id;
			entry.itemColor = item->m_instance.item_color;
			entry.displayId = item->m_display_id;

			entriesInPacket++;
		}

		pktHeader->itemCount = entriesInPacket;
		size_t packetSize = headerSize + (entriesInPacket * entrySize);

		int ret = m_game->m_client_list[client_h]->m_socket->send_msg(G_cData50000, static_cast<int>(packetSize));
		switch (ret) {
		case sock::Event::QueueFull:
		case sock::Event::SocketError:
		case sock::Event::CriticalError:
		case sock::Event::SocketClosed:
			hb::logger::log("Failed to send item configs: Client({}) Packet({})", client_h, packetIndex);
			m_game->delete_client(client_h, true, true);
			delete m_game->m_client_list[client_h];
			m_game->m_client_list[client_h] = 0;
			return false;
		}

		itemsSent += entriesInPacket;
		packetIndex++;
	}

	return true;
}

const hb::server::drop_table* ItemManager::get_drop_table(int id) const
{
	if (id <= 0) {
		return nullptr;
	}
	auto it = m_game->m_drop_tables.find(id);
	if (it == m_game->m_drop_tables.end()) {
		return nullptr;
	}
	return &it->second;
}

void ItemManager::clear_item_config_list()
{
	for(int i = 0; i < MaxItemTypes; i++) {
		if (m_game->m_item_config_list[i] != 0) {
			delete m_game->m_item_config_list[i];
			m_game->m_item_config_list[i] = 0;
		}
	}
}

//////////////////////////////////////////////////////////////////////
// Item creation (ADR 0003, plan P1.1) — the only legal way to make a CItem.
//
// Every item that enters the world is born in one of these, which makes the
// ledger's creation coverage a property of the code's shape rather than of
// anybody remembering to log at 45 separate call sites. init_item_attr stays
// the config-copy step underneath; what these add is identity and provenance.
//////////////////////////////////////////////////////////////////////

bool ItemManager::is_valid_item_id(int item_id) const
{
	return item_id >= 0
		&& item_id < MaxItemTypes
		&& m_game->m_item_config_list[item_id] != nullptr;
}

// Where a player stood when an item they caused came into the world (P3.1).
hb::server::birth_context ItemManager::birth_at(int client_h, const char* detail) const
{
	hb::server::birth_context birth;
	birth.detail = detail;
	if (const CClient* actor = client_at(client_h))
	{
		birth.map = actor->m_map_name;
		birth.x = actor->m_x;
		birth.y = actor->m_y;
	}
	return birth;
}

// Instanced iff non-stackable (D2). A stack has no identity worth tracking
// because merge and split dissolve it, so stackables stay unserialed and the
// Counted flow counters account for them in aggregate instead.
//
// This is also where the ledger learns an item exists (#78). Minting is the one
// act every creation venue has in common, so emitting the birth record from the
// same three lines that assign the Serial is what makes creation coverage a
// property of the code's shape rather than of 45 call sites remembering.
void ItemManager::stamp_provenance(CItem* item, item_origin::item_origin origin,
	const hb::server::birth_context& birth)
{
	item->m_origin = origin;
	if (item->is_stackable())
		return;

	item->m_serial = m_serial_allocator.mint();

	// The birth row is a snapshot of the item as it stands right now, which is
	// why this runs at the end of a creation rather than the start of one: the
	// tier byte it reads is only true if the venue has finished rolling, and the
	// location is only true if the venue said where. Both come from `birth`
	// (P3.1) — a mint on its own still cannot know either, so the funnel asks
	// instead of guessing, and a venue with nothing to say leaves them NULL.
	if (hb::server::item_ledger_store* store = ledger())
		store->record_mint(*item, birth.detail, birth.map, birth.x, birth.y);
}

CItem* ItemManager::create_item(int item_id, item_origin::item_origin origin,
	const hb::server::birth_context& birth)
{
	CItem* item = new CItem();
	if (init_item_attr(item, item_id) == false)
	{
		delete item;
		return nullptr;
	}

	stamp_provenance(item, origin, birth);
	return item;
}

CItem* ItemManager::create_item(const char* item_name, item_origin::item_origin origin,
	const hb::server::birth_context& birth)
{
	CItem* item = new CItem();
	if (init_item_attr(item, item_name) == false)
	{
		delete item;
		return nullptr;
	}

	stamp_provenance(item, origin, birth);
	return item;
}

CItem* ItemManager::create_loot_item(int item_id, const hb::server::roll_context& rolls,
	const hb::server::birth_context& birth)
{
	CItem* item = new CItem();
	if (init_item_attr(item, item_id) == false)
	{
		delete item;
		return nullptr;
	}

	// Roll, then mint. The order is the entire reason this entry point exists:
	// the tier the ledger records has to be the tier the player receives.
	//
	// It costs the drop path nothing, because the roll has not moved relative to
	// any other draw — minting and recording touch a counter and a vector, never
	// the RNG. What did move is the *count* draw, which the caller now takes
	// before asking for the item at all, so the sequence a kill consumes is
	// still count, then attributes, then touch effects.
	m_game->get_roll_strategy().roll(*item, rolls);

	stamp_provenance(item, item_origin::npc_drop, birth);
	return item;
}

// A destroyed event's `detail`. Shared by the two ways an item can end —
// destroy_item holds the object, the Trading Post (#80) holds only the Serial —
// so the two cannot come to disagree about how a reason is written down. The
// column is the half of the ledger a human reads directly, and a query filtering
// on it matches a string.
std::string ItemManager::destroyed_detail(destroy_reason::destroy_reason reason)
{
	return hb::server::detail_json("reason", destroy_reason::name(reason));
}

void ItemManager::destroy_item(CItem*& item, destroy_reason::destroy_reason reason, int actor_h)
{
	if (item == nullptr) return;

	// begin_ledger_event answers false for a Counted item and when no ledger is
	// open, which is the same door every other emitter goes through — so a
	// stackable being freed costs one comparison and, since #81, books an
	// aggregate flow instead of an event.
	//
	// A merge is the exception, and it is the reason the qty is a parameter at
	// all: `merged` frees a husk whose contents live on in the stack it joined,
	// so nothing left the world. Counting it would make every gold pickup read
	// as a destruction of exactly as much gold as the player just gained.
	// Instanced items never reach that reason (only stackables merge), so this
	// silences the flow without silencing any event.
	const int64_t qty = (reason == destroy_reason::merged) ? 0 : flow_qty_from_item;

	if (hb::server::ledger_event_record event;
		begin_ledger_event(ledger_event::destroyed, item, actor_h, event, qty))
	{
		event.detail = destroyed_detail(reason);
		ledger()->record_event(std::move(event));
	}

	delete item;
	item = nullptr;
}

void ItemManager::despawn_item(CItem*& item, despawn_reason::despawn_reason reason,
	const char* map, int x, int y)
{
	if (item == nullptr) return;

	// Actor 0: nobody did this. begin_ledger_event then leaves the actor columns
	// NULL, and the location is filled in here from the tile instead — a despawn
	// is the one event whose "where" is a property of the world rather than of
	// whoever caused it.
	if (hb::server::ledger_event_record event;
		begin_ledger_event(ledger_event::despawned, item, 0, event))
	{
		if (map != nullptr) event.map = map;
		event.x = x;
		event.y = y;
		event.detail = hb::server::detail_json("reason", despawn_reason::name(reason));
		ledger()->record_event(std::move(event));
	}

	delete item;
	item = nullptr;
}

CItem* ItemManager::restore_item(int item_id, int64_t serial)
{
	CItem* item = new CItem();
	if (init_item_attr(item, item_id) == false)
	{
		delete item;
		return nullptr;
	}

	if (serial != 0)
	{
		// A restored item keeps the identity it was saved with, and the
		// allocator has to be told about it — otherwise a fresh in-RAM counter
		// would hand the same Serial to a newly minted item.
		item->m_serial = serial;
		m_serial_allocator.resume_from(serial);
	}
	else
	{
		// No stored Serial. Since #77 that means a Counted row (stackables are
		// saved with 0, which is what they are) or a row written before the
		// column existed. stamp_provenance settles it the same way it settles
		// every other creation: Instanced items get identity, Counted ones do
		// not — so a non-stackable can never reach the world unserialed.
		stamp_provenance(item, item_origin::restored);
	}

	return item;
}

CItem* ItemManager::transform_item(int new_item_id, const CItem& from)
{
	CItem* item = new CItem();
	if (init_item_attr(item, new_item_id) == false)
	{
		delete item;
		return nullptr;
	}

	// No mint. The item that comes out of an evolution chain is the same
	// physical item that went in, so it keeps the Serial and the origin it was
	// born with, and the allocator's sequence stays gap-free.
	item->m_serial = from.m_serial;
	item->m_origin = from.m_origin;
	return item;
}

CItem* ItemManager::create_template()
{
	// A config template describes a type rather than an instance: no Serial, no
	// origin, and it never appears in the ledger.
	return new CItem();
}

CItem* ItemManager::create_snapshot(CItem* source)
{
	if (source == nullptr) return nullptr;

	// By id, not by name: the name overload rescans every item config and
	// normalises both sides of each comparison, and this runs per item on every
	// Exchange. m_id_num is the same lookup key without the search.
	CItem* snapshot = new CItem();
	if (init_item_attr(snapshot, source->m_id_num) == false)
	{
		delete snapshot;
		return nullptr;
	}

	// The init above is load-bearing, not redundant: copy_item_contents copies
	// every field except m_name, so without it the log line has no item name.
	//
	// It carries the Serial across, which is correct here and only here — the
	// snapshot describes `source` for the duration of one log line and is
	// deleted immediately, so the two never coexist as world items.
	copy_item_contents(snapshot, source);
	return snapshot;
}

bool ItemManager::init_item_attr(CItem* item, const char* item_name)
{

	char tmp_name[hb::shared::limits::NpcNameLen];
	char normalized_input[21];
	char normalized_config[21];

	std::memset(tmp_name, 0, sizeof(tmp_name));
	strcpy(tmp_name, item_name);

	// Normalize the input name for comparison (client may send "MagicStaff" while DB has "Magic Staff")
	NormalizeItemName(tmp_name, normalized_input, sizeof(normalized_input));

	for(int i = 0; i < MaxItemTypes; i++)
		if (m_game->m_item_config_list[i] != 0) {
			// Normalize the config name for comparison
			NormalizeItemName(m_game->m_item_config_list[i]->m_name, normalized_config, sizeof(normalized_config));
			if (hb_stricmp(normalized_input, normalized_config) == 0)
				return init_item_attr(item, i);
		}

	return false;
}

bool ItemManager::init_item_attr(CItem* item, int item_id)
{
	if (item_id < 0 || item_id >= MaxItemTypes) return false;
	if (m_game->m_item_config_list[item_id] == nullptr) return false;

	CItem* config = m_game->m_item_config_list[item_id];

	std::memset(item->m_name, 0, sizeof(item->m_name));
	strcpy(item->m_name, config->m_name);
	item->m_item_type = config->m_item_type;
	item->m_equip_pos = config->m_equip_pos;
	item->m_item_effect_type = config->m_item_effect_type;
	item->m_item_effect_value1 = config->m_item_effect_value1;
	item->m_item_effect_value2 = config->m_item_effect_value2;
	item->m_item_effect_value3 = config->m_item_effect_value3;
	item->m_item_effect_value4 = config->m_item_effect_value4;
	item->m_item_effect_value5 = config->m_item_effect_value5;
	item->m_item_effect_value6 = config->m_item_effect_value6;
	item->m_durability = config->m_durability;
	item->m_instance.cur_durability = item->m_durability;
	item->m_special_effect = config->m_special_effect;
	item->m_sell_price = config->m_sell_price;
	item->m_weight = config->m_weight;
	item->m_weapon_class = config->m_weapon_class;
	item->m_swing_speed = config->m_swing_speed;
	item->m_level_requirement = config->m_level_requirement;
	item->m_gender_requirement = config->m_gender_requirement;
	item->m_special_effect_value1 = config->m_special_effect_value1;
	item->m_special_effect_value2 = config->m_special_effect_value2;
	item->m_related_skill = config->m_related_skill;
	item->m_item_sub_type = config->m_item_sub_type;
	item->m_id_num = config->m_id_num;
	item->m_hide_armor = config->m_hide_armor;
	item->m_is_skirt = config->m_is_skirt;
	item->m_stackable = config->m_stackable;
	item->m_is_dyeable = config->m_is_dyeable;
	item->m_armor_class = config->m_armor_class;
	item->m_set_id = config->m_set_id;
	item->m_attribute_pool_id = config->m_attribute_pool_id;
	item->m_instance.item_color = config->m_instance.item_color;
	item->m_display_id = config->m_display_id;

	return true;
}

void ItemManager::drop_item_handler(int client_h, short item_index, int amount, const char* item_name, bool by_player)
{
	CItem* item;

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_is_on_server_change) return;
	if (m_game->m_client_list[client_h]->m_is_init_complete == false) return;
	if ((item_index < 0) || (item_index >= hb::shared::limits::MaxItems)) return;
	if (m_game->m_client_list[client_h]->m_item_list[item_index] == 0) return;
	if ((amount != -1) && (amount < 0)) return;

	if ((m_game->m_client_list[client_h]->m_item_list[item_index]->is_stackable()) &&
		(amount == -1))
		amount = static_cast<int>(m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.count);

	if ((m_game->m_client_list[client_h]->m_item_list[item_index]->is_stackable()) &&
		(m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.count > static_cast<uint64_t>(amount))) {
		// Splitting a stack is not a birth — the same Counted stuff now sits in
		// two places. Stackables carry no Serial, so there is no identity to
		// divide and no origin to inherit.
		item = create_item(m_game->m_client_list[client_h]->m_item_list[item_index]->m_name, item_origin::none);
		if (item == nullptr) return;

		if (amount <= 0) {
			destroy_item(item, destroy_reason::discarded, client_h);
			return;
		}
		item->m_instance.count = amount;

		if (static_cast<uint64_t>(amount) > m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.count) {
			destroy_item(item, destroy_reason::discarded, client_h);
			return;
		}

		// v1.41 !!!
		// flow_none: the moved portion is `item`, whose Drop below books it.
		// The remainder is passed as a target — the setter computes its delta
		// from the live count, so the decrease must not be pre-applied (#105).
		set_item_count(client_h, item_index, m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.count - amount, flow_none);

		m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->set_item(m_game->m_client_list[client_h]->m_x,
			m_game->m_client_list[client_h]->m_y, item);

		// v1.411 
		// v2.17 2002-7-31
		if (by_player)
			item_log(ItemLogAction::Drop, client_h, (int)-1, item);
		else
			item_log(ItemLogAction::Drop, client_h, (int)-1, item, true);

		m_game->send_ground_item_event(CommonType::ItemDrop, m_game->m_client_list[client_h]->m_map_index,
			m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y, item);

		m_game->send_notify_msg(0, client_h, Notify::DropItemFinCountChanged, item_index, amount, 0, 0);
	}
	else {

		release_item_handler(client_h, item_index, true);

		// v2.17
		if (m_game->m_client_list[client_h]->m_is_item_equipped[item_index])
			m_game->send_notify_msg(0, client_h, Notify::ItemReleased, m_game->m_client_list[client_h]->m_item_list[item_index]->m_equip_pos, item_index, 0, 0);

		// v1.432
		if ((m_game->m_client_list[client_h]->m_item_list[item_index]->get_item_effect_type() == ItemEffectType::AlterItemDrop) &&
			(m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.cur_durability == 0)) {
			// A worn-out alter-drop item breaks instead of landing on the ground:
			// it never reaches the tile, so this is an exit, not a drop.
			destroy_item(m_game->m_client_list[client_h]->m_item_list[item_index],
				destroy_reason::consumed, client_h);
		}
		else {
			m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->set_item(m_game->m_client_list[client_h]->m_x,
				m_game->m_client_list[client_h]->m_y,
				m_game->m_client_list[client_h]->m_item_list[item_index]);

			// v1.41
			// v2.17 2002-7-31
			if (by_player)
				item_log(ItemLogAction::Drop, client_h, (int)-1, m_game->m_client_list[client_h]->m_item_list[item_index]);
			else
				item_log(ItemLogAction::Drop, client_h, (int)-1, m_game->m_client_list[client_h]->m_item_list[item_index], true);

			m_game->send_ground_item_event(CommonType::ItemDrop, m_game->m_client_list[client_h]->m_map_index,
				m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y,
				m_game->m_client_list[client_h]->m_item_list[item_index]);
		}

		m_game->m_client_list[client_h]->m_item_list[item_index] = 0;
		m_game->m_client_list[client_h]->m_is_item_equipped[item_index] = false;

		m_game->send_notify_msg(0, client_h, Notify::DropItemFinEraseItem, item_index, amount, 0, 0);

		m_game->m_client_list[client_h]->m_arrow_index = get_arrow_item_index(client_h);
	}

	m_game->calc_total_weight(client_h);
}

int ItemManager::client_motion_get_item_handler(int client_h, short sX, short sY, direction dir)
{
	int   ret, erase_req;
	CItem* item;

	if (m_game->m_client_list[client_h] == 0) return 0;
	if ((dir <= 0) || (dir > 8))       return 0;
	if (m_game->m_client_list[client_h]->m_is_killed) return 0;
	if (m_game->m_client_list[client_h]->m_is_init_complete == false) return 0;

	if ((sX != m_game->m_client_list[client_h]->m_x) || (sY != m_game->m_client_list[client_h]->m_y)) return 2;

	int st_x, st_y;
	if (m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index] != 0) {
		st_x = m_game->m_client_list[client_h]->m_x / 20;
		st_y = m_game->m_client_list[client_h]->m_y / 20;
		m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_temp_sector_info[st_x][st_y].player_activity++;

		switch (m_game->m_client_list[client_h]->m_side) {
		case 0: m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_temp_sector_info[st_x][st_y].neutral_activity++; break;
		case 1: m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_temp_sector_info[st_x][st_y].aresden_activity++; break;
		case 2: m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_temp_sector_info[st_x][st_y].elvine_activity++;  break;
		}
	}

	m_game->m_skill_manager->clear_skill_using_status(client_h);

	m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->clear_owner(0, client_h, hb::shared::owner_class::Player, sX, sY);
	m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->set_owner(client_h, hb::shared::owner_class::Player, sX, sY);

	CItem* remain = nullptr;
	item = m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->get_item(sX, sY, &remain);
	if (item != 0) {
		if (add_client_item_list(client_h, item, &erase_req)) {

			item_log(ItemLogAction::get, client_h, 0, item);

			ret = send_item_notify_msg(client_h, Notify::ItemObtained, item, 0);

			// The pickup merged into a stack the character already had, so its
			// contents live on in that slot and only the husk is left. Freed here
			// rather than earlier because both sinks and the client packet above
			// still had to read it, and freed before the switch below because
			// that switch can return (#81).
			if (erase_req == 1) destroy_item(item, destroy_reason::merged, client_h);

			switch (ret) {
			case sock::Event::QueueFull:
			case sock::Event::SocketError:
			case sock::Event::CriticalError:
			case sock::Event::SocketClosed:
				m_game->delete_client(client_h, true, true);
				return 0;
			}

			// Broadcast remaining item state to nearby clients (clears tile if no items remain)
			m_game->send_ground_item_event(CommonType::SetItem,
				m_game->m_client_list[client_h]->m_map_index, sX, sY, remain);
		}
		else
		{
			m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->set_item(sX, sY, item);

			ret = send_item_notify_msg(client_h, Notify::CannotCarryMoreItem, 0, 0);
			switch (ret) {
			case sock::Event::QueueFull:
			case sock::Event::SocketError:
			case sock::Event::CriticalError:
			case sock::Event::SocketClosed:
				m_game->delete_client(client_h, true, true);
				return 0;
			}
		}
	}

	{
		hb::net::PacketResponseMotionHeader pkt{};
		pkt.header.msg_id = MsgId::ResponseMotion;
		pkt.header.msg_type = Confirm::MotionConfirm;
		ret = m_game->m_client_list[client_h]->m_socket->send_msg(reinterpret_cast<char*>(&pkt), sizeof(pkt));
	}
	switch (ret) {
	case sock::Event::QueueFull:
	case sock::Event::SocketError:
	case sock::Event::CriticalError:
	case sock::Event::SocketClosed:
		m_game->delete_client(client_h, true, true);
		return 0;
	}

	return 1;
}

bool ItemManager::add_client_item_list(int client_h, CItem* item, int* del_req)
{

	if (m_game->m_client_list[client_h] == 0) return false;
	if (item == 0) return false;

	if (item->is_stackable()) {
		if ((m_game->m_client_list[client_h]->m_cur_weight_load + get_item_weight(item, static_cast<int>(item->m_instance.count))) > m_game->calc_max_load(client_h))
			return false;
	}
	else {
		if ((m_game->m_client_list[client_h]->m_cur_weight_load + get_item_weight(item, 1)) > m_game->calc_max_load(client_h))
			return false;
	}

	if (item->is_stackable()) {
		for(int i = 0; i < hb::shared::limits::MaxItems; i++)
			if ((m_game->m_client_list[client_h]->m_item_list[i] != 0) &&
				(m_game->m_client_list[client_h]->m_item_list[i]->m_id_num == item->m_id_num)) {
				m_game->m_client_list[client_h]->m_item_list[i]->m_instance.count += item->m_instance.count;
				//delete item;
				*del_req = 1;

				m_game->calc_total_weight(client_h);

				return true;
			}
	}

	for(int i = 0; i < hb::shared::limits::MaxItems; i++)
		if (m_game->m_client_list[client_h]->m_item_list[i] == 0) {

			m_game->m_client_list[client_h]->m_item_list[i] = item;
			m_game->m_client_list[client_h]->m_item_pos_list[i].x = 40;
			m_game->m_client_list[client_h]->m_item_pos_list[i].y = 30;

			*del_req = 0;

			if (item->get_item_sub_type() == hb::shared::item::item_sub_type::ammo)
				m_game->m_client_list[client_h]->m_arrow_index = get_arrow_item_index(client_h);

			m_game->calc_total_weight(client_h);

			return true;
		}

	return false;
}

int ItemManager::add_client_bulk_item_list(int client_h, const char* item_name, int amount)
{
	if (m_game->m_client_list[client_h] == nullptr) return 0;
	if (item_name == nullptr || amount < 1) return 0;

	int created = 0;
	CItem* first_item = nullptr;

	for (int i = 0; i < amount; i++)
	{
		// The recipient's position. The GM who issued the command is recorded on
		// the GmMint event's actor columns, so the birth row does not repeat it.
		CItem* item = create_item(item_name, item_origin::gm_mint, birth_at(client_h));
		if (item == nullptr) break;

		// Weight check
		if ((m_game->m_client_list[client_h]->m_cur_weight_load + get_item_weight(item, 1)) > m_game->calc_max_load(client_h))
		{
			destroy_item(item, destroy_reason::discarded, client_h);
			break;
		}

		// Find an empty slot directly (no merge)
		bool added = false;
		for (int j = 0; j < hb::shared::limits::MaxItems; j++)
		{
			if (m_game->m_client_list[client_h]->m_item_list[j] == nullptr)
			{
				m_game->m_client_list[client_h]->m_item_list[j] = item;
				m_game->m_client_list[client_h]->m_item_pos_list[j].x = 40;
				m_game->m_client_list[client_h]->m_item_pos_list[j].y = 30;
				m_game->calc_total_weight(client_h);
				if (first_item == nullptr) first_item = item;
				created++;
				added = true;
				break;
			}
		}

		if (!added)
		{
			destroy_item(item, destroy_reason::discarded, client_h);
			break;
		}

		// Per copy, not per batch: each of these has its own birth row, so a
		// single event for the bundle would leave the rest held by nobody (#104).
		record_gm_mint(client_h, item);
	}

	// Send one bulk notification with total count
	if (created > 0 && first_item != nullptr)
	{
		hb::net::PacketNotifyItemObtained pkt{};
		pkt.header.msg_id = MsgId::Notify;
		pkt.header.msg_type = Notify::ItemObtainedBulk;
		pkt.is_new = 1;
		memcpy(pkt.name, first_item->m_name, sizeof(pkt.name));
		pkt.count = created;
		pkt.item_type = first_item->m_item_type;
		pkt.equip_pos = first_item->m_equip_pos;
		pkt.is_equipped = 0;
		pkt.level_limit = first_item->m_level_requirement;
		pkt.gender_limit = first_item->m_gender_requirement;
		pkt.cur_durability = first_item->m_instance.cur_durability;
		pkt.weight = first_item->m_weight;
		pkt.item_color = first_item->m_instance.item_color;
		pkt.spec_value2 = static_cast<uint8_t>(first_item->m_instance.special_effect_value2);
		pkt.attributes = first_item->get_attributes();
		pkt.item_id = first_item->m_id_num;
		pkt.max_durability = first_item->m_durability;
		m_game->m_client_list[client_h]->m_socket->send_msg(reinterpret_cast<char*>(&pkt), sizeof(pkt));

		// Every caller of this helper is a GM creation command, so the bundle
		// is a mint: one audit line for the batch (the copies are identical).
		log_gm_mint(client_h, created, first_item);
	}

	return created;
}

int ItemManager::mint_gm_items(int client_h, int item_id, int count,
	const item_attribute_data& requested, std::string& error)
{
	error.clear();
	if (m_game->m_client_list[client_h] == nullptr) return 0;
	if (is_valid_item_id(item_id) == false)
	{
		error = "invalid item id";
		return 0;
	}
	if (count < 1) count = 1;

	int created = 0;
	// The most recent copy, kept readable until the audit LINE is written — the
	// ledger half is already done by then, recorded per copy inside the loop.
	// A stackable copy merges into an existing slot and becomes ours to
	// delete; holding it one iteration longer is what lets the log describe
	// what was minted rather than what happened to survive.
	CItem* minted = nullptr;
	bool minted_is_ours = false;
	for (int i = 0; i < count; i++)
	{
		CItem* item = create_item(item_id, item_origin::gm_mint, birth_at(client_h));
		if (item == nullptr)
		{
			error = "item config init failed";
			break;
		}

		// The mode decides what a legal instance is. A rejection is a property
		// of the request, not of this copy, so the first one ends the run.
		error = m_game->get_roll_strategy().mint(*item, requested);
		if (!error.empty())
		{
			destroy_item(item, destroy_reason::discarded, client_h);
			break;
		}

		int erase_req = 0;
		if (add_client_item_list(client_h, item, &erase_req) == false)
		{
			destroy_item(item, destroy_reason::discarded, client_h);
			error = "no room to carry it";
			break;
		}

		send_item_notify_msg(client_h, Notify::ItemObtained, item, 0);
		// Per copy, and here rather than after the loop, because each copy has
		// its own birth row and its own Serial to be held by somebody (#104).
		// A merged Counted copy is still alive at this point, which is what its
		// aggregate flow is read off.
		record_gm_mint(client_h, item);
		// The previous copy, if it was a stack merge and therefore ours. Freeing
		// it is not an exit — the stack it merged into is carrying its contents —
		// and it is Counted anyway, so the funnel records nothing and only frees.
		if (minted_is_ours) destroy_item(minted, destroy_reason::merged, client_h);
		minted = item;
		minted_is_ours = (erase_req == 1);   // merged into an existing stack
		created++;
	}

	// One audit line per request: every copy is identical, so the quantity is
	// the only thing that varies across them.
	if (created > 0 && minted != nullptr)
		log_gm_mint(client_h, created, minted);
	if (minted_is_ours) destroy_item(minted, destroy_reason::merged, client_h);

	return created;
}

bool ItemManager::equip_item_handler(int client_h, short item_index, bool notify)
{
	char hero_armor_type;
	EquipPos equip_pos;

	if (m_game->m_client_list[client_h] == 0) return false;
	if ((item_index < 0) || (item_index >= hb::shared::limits::MaxItems)) return false;
	if (m_game->m_client_list[client_h]->m_item_list[item_index] == 0) return false;
	if (m_game->m_client_list[client_h]->m_item_list[item_index]->get_item_type() != hb::shared::item::item_type::equipment) return false;

	if (m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.cur_durability == 0) return false;

	if (!m_game->m_client_list[client_h]->m_item_list[item_index]->is_custom_made() &&
		(m_game->m_client_list[client_h]->m_item_list[item_index]->m_level_requirement > m_game->m_client_list[client_h]->m_level)) return false;

	if (m_game->m_client_list[client_h]->m_item_list[item_index]->m_gender_requirement != 0) {
		switch (m_game->m_client_list[client_h]->m_type) {
		case 1:
		case 2:
		case 3:
			if (m_game->m_client_list[client_h]->m_item_list[item_index]->m_gender_requirement != 1) return false;
			break;
		case 4:
		case 5:
		case 6:
			if (m_game->m_client_list[client_h]->m_item_list[item_index]->m_gender_requirement != 2) return false;
			break;
		}
	}

	if (get_item_weight(m_game->m_client_list[client_h]->m_item_list[item_index], 1) > (m_game->m_client_list[client_h]->effective_str() + m_game->m_client_list[client_h]->m_angelic_str) * hb::shared::balance::weight_units_per_stone) return false;

	equip_pos = m_game->m_client_list[client_h]->m_item_list[item_index]->get_equip_pos();

	if ((equip_pos == EquipPos::Body) || (equip_pos == EquipPos::Boots) ||
		(equip_pos == EquipPos::Arms) || (equip_pos == EquipPos::Head)) {
		switch (m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value4) {
		case 10: // Str
			if ((m_game->m_client_list[client_h]->effective_str() + m_game->m_client_list[client_h]->m_angelic_str) < m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value5) {
				m_game->send_notify_msg(0, client_h, Notify::ItemReleased, m_game->m_client_list[client_h]->m_item_list[item_index]->m_equip_pos, item_index, 0, 0);
				release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(equip_pos)], true);
				return false;
			}
			break;
		case 11: // Dex
			if ((m_game->m_client_list[client_h]->m_dex + m_game->m_client_list[client_h]->m_angelic_dex) < m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value5) {
				m_game->send_notify_msg(0, client_h, Notify::ItemReleased, m_game->m_client_list[client_h]->m_item_list[item_index]->m_equip_pos, item_index, 0, 0);
				release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(equip_pos)], true);
				return false;
			}
			break;
		case 12: // Vit
			if (m_game->m_client_list[client_h]->m_vit < m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value5) {
				m_game->send_notify_msg(0, client_h, Notify::ItemReleased, m_game->m_client_list[client_h]->m_item_list[item_index]->m_equip_pos, item_index, 0, 0);
				release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(equip_pos)], true);
				return false;
			}
			break;
		case 13: // Int
			if ((m_game->m_client_list[client_h]->m_int + m_game->m_client_list[client_h]->m_angelic_int) < m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value5) {
				m_game->send_notify_msg(0, client_h, Notify::ItemReleased, m_game->m_client_list[client_h]->m_item_list[item_index]->m_equip_pos, item_index, 0, 0);
				release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(equip_pos)], true);
				return false;
			}
			break;
		case 14: // Mag
			if ((m_game->m_client_list[client_h]->m_mag + m_game->m_client_list[client_h]->m_angelic_mag) < m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value5) {
				m_game->send_notify_msg(0, client_h, Notify::ItemReleased, m_game->m_client_list[client_h]->m_item_list[item_index]->m_equip_pos, item_index, 0, 0);
				release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(equip_pos)], true);
				return false;
			}
			break;
		case 15: // Chr
			if (m_game->m_client_list[client_h]->m_charisma < m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value5) {
				m_game->send_notify_msg(0, client_h, Notify::ItemReleased, m_game->m_client_list[client_h]->m_item_list[item_index]->m_equip_pos, item_index, 0, 0);
				release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(equip_pos)], true);
				return false;
			}
			break;
		}
	}

	if (equip_pos == EquipPos::TwoHand) {
		// Stormbringer
		if (m_game->m_client_list[client_h]->m_item_list[item_index]->m_id_num == 845) {
			if ((m_game->m_client_list[client_h]->m_int + m_game->m_client_list[client_h]->m_angelic_int) < 65) {
				m_game->send_notify_msg(0, client_h, Notify::ItemReleased, m_game->m_client_list[client_h]->m_special_ability_equip_pos, item_index, 0, 0);
				release_item_handler(client_h, item_index, true);
				return false;
			}
		}
	}

	if (equip_pos == EquipPos::RightHand) {
		// Resurrection wand(MS.10) or Resurrection wand(MS.20)
		if ((m_game->m_client_list[client_h]->m_item_list[item_index]->m_id_num == 865) || (m_game->m_client_list[client_h]->m_item_list[item_index]->m_id_num == 866)) {
			if ((m_game->m_client_list[client_h]->m_int + m_game->m_client_list[client_h]->m_angelic_int) > 99 && (m_game->m_client_list[client_h]->m_mag + m_game->m_client_list[client_h]->m_angelic_mag) > 99 && m_game->m_client_list[client_h]->m_special_ability_time < 1) {
				m_game->m_client_list[client_h]->m_magic_mastery[94] = true;
				m_game->send_notify_msg(0, client_h, Notify::StateChangeSuccess, 0, 0, 0, 0);
			}
		}
	}

	if ((m_game->m_client_list[client_h]->m_item_list[item_index]->get_item_effect_type() == ItemEffectType::AttackSpecAbility) ||
		(m_game->m_client_list[client_h]->m_item_list[item_index]->get_item_effect_type() == ItemEffectType::DefenseSpecAbility)) {

		if ((m_game->m_client_list[client_h]->m_special_ability_type != 0)) {
			if (m_game->m_client_list[client_h]->m_item_list[item_index]->m_equip_pos != m_game->m_client_list[client_h]->m_special_ability_equip_pos) {
				m_game->send_notify_msg(0, client_h, Notify::ItemReleased, m_game->m_client_list[client_h]->m_special_ability_equip_pos, m_game->m_client_list[client_h]->m_item_equipment_status[m_game->m_client_list[client_h]->m_special_ability_equip_pos], 0, 0);
				release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[m_game->m_client_list[client_h]->m_special_ability_equip_pos], true);
			}
		}
	}

	if (equip_pos == EquipPos::None) return false;

	if (equip_pos == EquipPos::TwoHand) {
		if (m_game->m_client_list[client_h]->m_item_equipment_status[to_int(equip_pos)] != -1)
			release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(equip_pos)], false);
		else {
			if (m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::RightHand)] != -1)
				release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::RightHand)], false);
			if (m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::LeftHand)] != -1)
				release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::LeftHand)], false);
		}
	}
	else {
		if ((equip_pos == EquipPos::LeftHand) || (equip_pos == EquipPos::RightHand)) {
			if (m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::TwoHand)] != -1)
				release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::TwoHand)], false);
		}

		if (m_game->m_client_list[client_h]->m_item_equipment_status[to_int(equip_pos)] != -1)
			release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(equip_pos)], false);
	}

	if (equip_pos == EquipPos::FullBody) {
		if (m_game->m_client_list[client_h]->m_item_equipment_status[to_int(equip_pos)] != -1) {
			release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(equip_pos)], false);
		}
		if (m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::Head)] != -1) {
			release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::Head)], false);
		}
		if (m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::Body)] != -1) {
			release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::Body)], false);
		}
		if (m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::Arms)] != -1) {
			release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::Arms)], false);
		}
		if (m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::Boots)] != -1) {
			release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::Boots)], false);
		}
		if (m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::Leggings)] != -1) {
			release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::Leggings)], false);
		}
		if (m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::Back)] != -1) {
			release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::Back)], false);
		}
	}
	else {
		if (equip_pos == EquipPos::Head || equip_pos == EquipPos::Body || equip_pos == EquipPos::Arms ||
			equip_pos == EquipPos::Boots || equip_pos == EquipPos::Leggings || equip_pos == EquipPos::Back) {
			if (m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::FullBody)] != -1) {
				release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::FullBody)], false);
			}
		}
		if (m_game->m_client_list[client_h]->m_item_equipment_status[to_int(equip_pos)] != -1)
			release_item_handler(client_h, m_game->m_client_list[client_h]->m_item_equipment_status[to_int(equip_pos)], false);
	}

	m_game->m_client_list[client_h]->m_item_equipment_status[to_int(equip_pos)] = item_index;
	m_game->m_client_list[client_h]->m_is_item_equipped[item_index] = true;

	// Set item color and armor visibility for this equip slot
	{
		auto& appr = m_game->m_client_list[client_h]->m_appearance;
		auto* item = m_game->m_client_list[client_h]->m_item_list[item_index];
		uint8_t color = static_cast<uint8_t>(item->m_instance.item_color);
		switch (equip_pos) {
		case EquipPos::Head:      appr.helm_color = color; break;
		case EquipPos::Body:
			appr.armor_color = color;
			appr.hide_armor = (item->m_hide_armor != 0);
			break;
		case EquipPos::Arms:      appr.arm_color = color; break;
		case EquipPos::Leggings:     appr.pants_color = color; break;
		case EquipPos::Boots:  appr.boots_color = color; break;
		case EquipPos::LeftHand:  appr.shield_color = color; break;
		case EquipPos::RightHand:
		case EquipPos::TwoHand:   appr.weapon_color = color; break;
		case EquipPos::Back:      appr.mantle_color = color; break;
		case EquipPos::FullBody:  appr.mantle_color = 0; break;
		default: break;
		}
	}

	// Weapon-specific: compute attack delay and reset combo
	if (equip_pos == EquipPos::RightHand || equip_pos == EquipPos::TwoHand) {
		m_game->m_client_list[client_h]->m_status.attack_delay = static_cast<uint8_t>(hb::shared::calc::attack_delay(
			m_game->m_client_list[client_h]->m_item_list[item_index]->get_effective_swing_speed(),
			m_game->m_client_list[client_h]->effective_str(),
			m_game->m_client_list[client_h]->m_angelic_str));
		m_game->m_client_list[client_h]->m_combo_attack_count = 0;
	}

	// AttackSpecAbility = offensive items (weapons) → weapon_glare
	if (m_game->m_client_list[client_h]->m_item_list[item_index]->get_item_effect_type() == ItemEffectType::AttackSpecAbility) {
		m_game->m_client_list[client_h]->m_appearance.weapon_glare = 0;
		switch (m_game->m_client_list[client_h]->m_item_list[item_index]->m_special_effect) {
		case 0: break;
		case 1:
			m_game->m_client_list[client_h]->m_appearance.weapon_glare = 1;
			break;
		case 2:
			m_game->m_client_list[client_h]->m_appearance.weapon_glare = 3;
			break;
		case 3:
			m_game->m_client_list[client_h]->m_appearance.weapon_glare = 2;
			break;
		}
	}

	// DefenseSpecAbility = defensive items (shields) → shield_glare
	if (m_game->m_client_list[client_h]->m_item_list[item_index]->get_item_effect_type() == ItemEffectType::DefenseSpecAbility) {
		m_game->m_client_list[client_h]->m_appearance.shield_glare = 0;
		switch (m_game->m_client_list[client_h]->m_item_list[item_index]->m_special_effect) {
		case 0:
			break;
		case 50:
		case 51:
		case 52:
			m_game->m_client_list[client_h]->m_appearance.shield_glare = 1;
			break;
		default:
			break;
		}
	}
	hero_armor_type = check_hero_item_equipped(client_h);
	if (hero_armor_type != 0x0FFFFFFFF) m_game->m_client_list[client_h]->m_hero_armour_bonus = hero_armor_type;

	m_game->send_event_to_near_client_type_a(client_h, hb::shared::owner_class::Player, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
	calc_total_item_effect(client_h, item_index, notify);
	validate_equipped_items(client_h);
	return true;

}

void ItemManager::validate_equipped_items(int client_h)
{
	auto* p = m_game->m_client_list[client_h];
	if (p == nullptr) return;

	for (int slot = 0; slot < hb::shared::item::DEF_MAXITEMEQUIPPOS; slot++)
	{
		short item_index = p->m_item_equipment_status[slot];
		if (item_index < 0) continue;
		if (p->m_item_list[item_index] == nullptr) continue;

		auto* item = p->m_item_list[item_index];
		bool must_unequip = false;

		// Level check (skip custom-made items — bit 0 of attribute)
		if (!item->is_custom_made() &&
			(item->m_level_requirement > p->m_level))
			must_unequip = true;

		// Weight check
		if (!must_unequip &&
			get_item_weight(item, 1) > (p->effective_str() + p->m_angelic_str) * hb::shared::balance::weight_units_per_stone)
			must_unequip = true;

		// Armor stat requirements (effect_value4 = stat type 10-15)
		if (!must_unequip && item->m_item_effect_value4 >= 10 &&
			item->m_item_effect_value4 <= 15)
		{
			int req = item->m_item_effect_value5;
			switch (item->m_item_effect_value4)
			{
			case 10: must_unequip = (p->effective_str() + p->m_angelic_str) < req; break;
			case 11: must_unequip = (p->m_dex + p->m_angelic_dex) < req; break;
			case 12: must_unequip = p->m_vit < req; break;
			case 13: must_unequip = (p->m_int + p->m_angelic_int) < req; break;
			case 14: must_unequip = (p->m_mag + p->m_angelic_mag) < req; break;
			case 15: must_unequip = p->m_charisma < req; break;
			}
		}

		if (must_unequip)
			release_item_handler(client_h, item_index, true);
	}
}

void ItemManager::request_purchase_item_handler(int client_h, const char* item_name, int num, int item_id)
{
	CItem* item;
	uint64_t gold_count;
	uint32_t item_count;
	uint16_t temp_price;
	int   ret, erase_req, gold_weight;
	int   cost, discount_ratio, discount_cost;
	double tmp1, tmp2, tmp3;

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_is_init_complete == false) return;
	//if ( (memcmp(m_game->m_client_list[client_h]->m_location, "NONE", 4) != 0) &&
	//	 (memcmp(m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_location_name, m_game->m_client_list[client_h]->m_location, 10) != 0) ) return;

	if (memcmp(m_game->m_client_list[client_h]->m_location, "NONE", 4) != 0) {
		if (memcmp(m_game->m_client_list[client_h]->m_location, "are", 3) == 0) {
			if ((memcmp(m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_location_name, "aresden", 7) == 0) ||
				(memcmp(m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_location_name, "arefarm", 7) == 0)) {

			}
			else return;
		}

		if (memcmp(m_game->m_client_list[client_h]->m_location, "elv", 3) == 0) {
			if ((memcmp(m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_location_name, "elvine", 6) == 0) ||
				(memcmp(m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_location_name, "elvfarm", 7) == 0)) {

			}
			else return;
		}
	}

	// New 18/05/2004
	if (m_game->m_client_list[client_h]->m_is_processing_allowed == false) return;

	// Determine item ID and count from client-provided item ID
	short resolved_item_id = 0;
	item_count = 1;

	if (item_id > 0 && item_id < MaxItemTypes) {
		resolved_item_id = static_cast<short>(item_id);
	}
	else {
		// No valid item ID provided
		return;
	}

	for(int i = 1; i <= num; i++) {

		item = create_item(resolved_item_id, item_origin::shop_buy, birth_at(client_h));
		if (item == nullptr) continue;

		if (item->m_sell_price == 0) {
			destroy_item(item, destroy_reason::discarded, client_h);
			return;
		}

		item->m_instance.count = item_count;

		cost = static_cast<int>(item->m_sell_price * item->m_instance.count);

		gold_count = get_item_count_by_id(client_h, hb::shared::item::ItemId::Gold);

		discount_ratio = ((m_game->m_client_list[client_h]->m_charisma - 10) / 4);

		// 2.03 Discount Method
		// Charisma
		// discount_ratio = (m_game->m_client_list[client_h]->m_charisma / 4) -1;
		// if (discount_ratio == 0) discount_ratio = 1;

		tmp1 = (double)(discount_ratio);
		tmp2 = tmp1 / 100.0f;
		tmp1 = (double)cost;
		tmp3 = tmp1 * tmp2;
		discount_cost = (int)tmp3;

		if (discount_cost >= (cost / 2)) discount_cost = (cost / 2) - 1;
		if (discount_cost < 0) discount_cost = 0;

		if (gold_count < static_cast<uint64_t>(cost - discount_cost)) {
			destroy_item(item, destroy_reason::discarded, client_h);

			{
				hb::net::PacketNotifyNotEnoughGold pkt{};
				pkt.header.msg_id = MsgId::Notify;
				pkt.header.msg_type = Notify::NotEnoughGold;
				pkt.item_index = -1;
				ret = m_game->m_client_list[client_h]->m_socket->send_msg(reinterpret_cast<char*>(&pkt), sizeof(pkt));
			}
			switch (ret) {
			case sock::Event::QueueFull:
			case sock::Event::SocketError:
			case sock::Event::CriticalError:
			case sock::Event::SocketClosed:
				m_game->delete_client(client_h, true, true);
				return;
			}
			return;
		}

		if (add_client_item_list(client_h, item, &erase_req)) {
			if (m_game->m_client_list[client_h]->m_cur_weight_load < 0) m_game->m_client_list[client_h]->m_cur_weight_load = 0;

			temp_price = (cost - discount_cost);
			ret = send_item_notify_msg(client_h, Notify::ItemPurchased, item, temp_price);

			// The purchase itself (P3.1). ItemLogAction::Buy has had a text-log
			// case since long before the ledger and nothing has ever called it,
			// so shop purchases were the one entry into the economy with a birth
			// row and no transition. Logged before the merge below frees `item`,
			// and the price rides the detail because what a thing cost is half of
			// what a shop-buy is worth knowing.
			item_log(ItemLogAction::Buy, client_h, (int)-1, item);

			if (erase_req == 1) destroy_item(item, destroy_reason::merged, client_h);

			// Gold  .      .
			// The other half of the purchase (#103). The item bought books Buy
			// above; the gold that paid for it books Buy too, under its own
			// item_id — so a shop trade is one number read from two sides.
			gold_weight = set_item_count_by_id(client_h, hb::shared::item::ItemId::Gold,
				gold_count - temp_price, ItemLogAction::Buy);
			m_game->calc_total_weight(client_h);

			m_game->m_city_status[m_game->m_client_list[client_h]->m_side].funds += temp_price;

			switch (ret) {
			case sock::Event::QueueFull:
			case sock::Event::SocketError:
			case sock::Event::CriticalError:
			case sock::Event::SocketClosed:
				m_game->delete_client(client_h, true, true);
				return;
			}
		}
		else
		{
			destroy_item(item, destroy_reason::discarded, client_h);

			m_game->calc_total_weight(client_h);

			ret = send_item_notify_msg(client_h, Notify::CannotCarryMoreItem, 0, 0);

			switch (ret) {
			case sock::Event::QueueFull:
			case sock::Event::SocketError:
			case sock::Event::CriticalError:
			case sock::Event::SocketClosed:
				m_game->delete_client(client_h, true, true);
				return;
			}
		}
	}
}

void ItemManager::give_item_handler(int client_h, short item_index, int amount, short dX, short dY, uint16_t object_id, const char* item_name)
{
	int ret, erase_req;
	short owner_h;
	char owner_type, char_name[hb::shared::limits::NpcNameLen];
	CItem* item;

	// Set by whichever branch actually hands the item to another player, so the
	// pair can be made durable together at the end (#82). A Give is the same
	// multi-account shape as an Exchange — two characters, one item — and it had
	// the same window: neither side was saved, so the transfer became durable at
	// two unrelated later moments and a crash between them duplicated or lost it.
	// Every other outcome here (the ground, an NPC, a rejection) touches one
	// character and leaves this at 0.
	int give_recipient_h = 0;

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_is_on_server_change) return;
	if (m_game->m_client_list[client_h]->m_is_init_complete == false) return;
	if (m_game->m_client_list[client_h]->m_item_list[item_index] == 0) return;
	if ((item_index < 0) || (item_index >= hb::shared::limits::MaxItems)) return;
	if (amount <= 0) return;

	std::memset(char_name, 0, sizeof(char_name));

	if ((m_game->m_client_list[client_h]->m_item_list[item_index]->is_stackable()) &&
		(m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.count > static_cast<uint64_t>(amount))) {

		// Split stack piece, as in drop_item_handler: Counted, so no identity
		// is created or divided here.
		item = create_item(m_game->m_client_list[client_h]->m_item_list[item_index]->m_name, item_origin::none);
		if (item == nullptr) return;
		item->m_instance.count = amount;

		// flow_none: the split piece books its own move at whichever destination
		// it reaches (a Give, a Deposit, a Drop). Target count, not a
		// pre-applied decrease, as in drop_item_handler (#105).
		set_item_count(client_h, item_index, m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.count - amount, flow_none);

		// dX, dY     .
		m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->get_owner(&owner_h, &owner_type, dX, dY);

		if (object_id != 0) {
			if (hb::shared::object_id::is_player_id(object_id)) {
				if ((object_id > 0) && (object_id < MaxClients)) {
					if (m_game->m_client_list[object_id] != 0) {
						if ((uint16_t)owner_h != object_id) owner_h = 0;
					}
				}
			}
			else {
				// NPC
				uint16_t npcIdx = hb::shared::object_id::ToNpcIndex(object_id);
				if (hb::shared::object_id::IsNpcID(object_id) && (npcIdx > 0) && (npcIdx < MaxNpcs)) {
					if (m_game->m_npc_list[npcIdx] != 0) {
						if ((uint16_t)owner_h != npcIdx) owner_h = 0;
					}
				}
			}
		}

		if (owner_h == 0) {
			m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->set_item(m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y, item);

			// v1.411
			item_log(ItemLogAction::Drop, client_h, 0, item);

			m_game->send_ground_item_event(CommonType::ItemDrop, m_game->m_client_list[client_h]->m_map_index,
				m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y, item);
		}
		else {
			if (owner_type == hb::shared::owner_class::Player) {
				memcpy(char_name, m_game->m_client_list[owner_h]->m_char_name, hb::shared::limits::CharNameLen - 1);

				if (owner_h == client_h) {
					destroy_item(item, destroy_reason::discarded, client_h);
					return;
				}

				if (add_client_item_list(owner_h, item, &erase_req)) {
					// The split-stack half of a Give, which recorded nothing at all
					// until #81 — only the whole-item branch below emitted, so one
					// player handing another 500 gold was invisible to both sinks
					// and to any dispute the custody chain exists to settle.
					item_log(ItemLogAction::Give, client_h, owner_h, item);
					give_recipient_h = owner_h;

					ret = send_item_notify_msg(owner_h, Notify::ItemObtained, item, 0);

					// Merged into the recipient's stack; the husk is spent.
					if (erase_req == 1) destroy_item(item, destroy_reason::merged, owner_h);

					switch (ret) {
					case sock::Event::QueueFull:
					case sock::Event::SocketError:
					case sock::Event::CriticalError:
					case sock::Event::SocketClosed:
						m_game->delete_client(owner_h, true, true);
						break;
					}

					// v1.4
					m_game->send_notify_msg(0, client_h, Notify::GiveItemFinCountChanged, item_index, amount, 0, char_name);
				}
				else {
					m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->set_item(m_game->m_client_list[client_h]->m_x,
						m_game->m_client_list[client_h]->m_y,
						item);

					// v1.411  
					item_log(ItemLogAction::Drop, client_h, 0, item);

					m_game->send_ground_item_event(CommonType::ItemDrop, m_game->m_client_list[client_h]->m_map_index,
						m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y, item);

					{
						ret = send_item_notify_msg(owner_h, Notify::CannotCarryMoreItem, 0, 0);
					}

					switch (ret) {
					case sock::Event::QueueFull:
					case sock::Event::SocketError:
					case sock::Event::CriticalError:
					case sock::Event::SocketClosed:
						m_game->delete_client(owner_h, true, true);
						break;
					}

					m_game->send_notify_msg(0, client_h, Notify::CannotGiveItem, item_index, amount, 0, char_name);
				}

			}
			else {
				// NPC  .
				memcpy(char_name, m_game->m_npc_list[owner_h]->m_npc_name, hb::shared::limits::NpcNameLen - 1);

				if (m_game->m_npc_list[owner_h]->m_npc_config_id == 58) { // Warehouse Keeper
					// NPC     .
					if (set_item_to_bank_item(client_h, item, bank_deposit::by_character) == false) {
						m_game->send_notify_msg(0, client_h, Notify::CannotItemToBank, 0, 0, 0, 0);

						m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->set_item(m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y, item);

						// v1.411
						item_log(ItemLogAction::Drop, client_h, 0, item);

						m_game->send_ground_item_event(CommonType::ItemDrop, m_game->m_client_list[client_h]->m_map_index,
							m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y, item);
					}
				}
				else {
					// NPC cannot receive items — restore count and reject.
					// flow_none: the count rises back to what it was.
					set_item_count(client_h, item_index, m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.count + amount, flow_none);
					m_game->send_notify_msg(0, client_h, Notify::CannotGiveItem, item_index, amount, 0, char_name);
					destroy_item(item, destroy_reason::discarded, client_h);
					m_game->calc_total_weight(client_h);
					return;
				}
			}
		}
	}
	else {

		release_item_handler(client_h, item_index, true);

		if (m_game->m_client_list[client_h]->m_item_list[item_index]->get_item_sub_type() == hb::shared::item::item_sub_type::ammo)
			m_game->m_client_list[client_h]->m_arrow_index = -1;

		// dX, dY     .
		m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->get_owner(&owner_h, &owner_type, dX, dY); // dX, dY   .         .

		if (object_id != 0) {
			if (hb::shared::object_id::is_player_id(object_id)) {
				if ((object_id > 0) && (object_id < MaxClients)) {
					if (m_game->m_client_list[object_id] != 0) {
						if ((uint16_t)owner_h != object_id) owner_h = 0;
					}
				}
			}
			else {
				// NPC
				uint16_t npcIdx = hb::shared::object_id::ToNpcIndex(object_id);
				if (hb::shared::object_id::IsNpcID(object_id) && (npcIdx > 0) && (npcIdx < MaxNpcs)) {
					if (m_game->m_npc_list[npcIdx] != 0) {
						if ((uint16_t)owner_h != npcIdx) owner_h = 0;
					}
				}
			}
		}

		if (owner_h == 0) {
			m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->set_item(m_game->m_client_list[client_h]->m_x,
				m_game->m_client_list[client_h]->m_y,
				m_game->m_client_list[client_h]->m_item_list[item_index]);
			// v1.411  
			item_log(ItemLogAction::Drop, client_h, 0, m_game->m_client_list[client_h]->m_item_list[item_index]);

			m_game->send_ground_item_event(CommonType::ItemDrop, m_game->m_client_list[client_h]->m_map_index,
				m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y,
				m_game->m_client_list[client_h]->m_item_list[item_index]);

			m_game->send_notify_msg(0, client_h, Notify::DropItemFinEraseItem, item_index, amount, 0, 0);
		}
		else {
			// . @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

			if (owner_type == hb::shared::owner_class::Player) {
				memcpy(char_name, m_game->m_client_list[owner_h]->m_char_name, hb::shared::limits::CharNameLen - 1);
				item = m_game->m_client_list[client_h]->m_item_list[item_index];

				{
					if (add_client_item_list(owner_h, item, &erase_req)) {

						item_log(ItemLogAction::Give, client_h, owner_h, item);
						give_recipient_h = owner_h;

						ret = send_item_notify_msg(owner_h, Notify::ItemObtained, item, 0);

						// A whole stackable slot handed over can still merge into
						// one the recipient already had.
						if (erase_req == 1) destroy_item(item, destroy_reason::merged, owner_h);

						switch (ret) {
						case sock::Event::QueueFull:
						case sock::Event::SocketError:
						case sock::Event::CriticalError:
						case sock::Event::SocketClosed:
							m_game->delete_client(owner_h, true, true);
							break;
						}
					}
					else {
						m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->set_item(m_game->m_client_list[client_h]->m_x,
							m_game->m_client_list[client_h]->m_y,
							m_game->m_client_list[client_h]->m_item_list[item_index]);
						item_log(ItemLogAction::Drop, client_h, 0, m_game->m_client_list[client_h]->m_item_list[item_index]);

						m_game->send_ground_item_event(CommonType::ItemDrop, m_game->m_client_list[client_h]->m_map_index,
							m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y,
							m_game->m_client_list[client_h]->m_item_list[item_index]);

						{
							ret = send_item_notify_msg(owner_h, Notify::CannotCarryMoreItem, 0, 0);
						}

						switch (ret) {
						case sock::Event::QueueFull:
						case sock::Event::SocketError:
						case sock::Event::CriticalError:
						case sock::Event::SocketClosed:
							m_game->delete_client(owner_h, true, true);
							break;
						}

						std::memset(char_name, 0, sizeof(char_name));
					}
				}
			}
			else {
				memcpy(char_name, m_game->m_npc_list[owner_h]->m_npc_name, hb::shared::limits::NpcNameLen - 1);

				if (m_game->m_npc_list[owner_h]->m_npc_config_id == 58) { // Warehouse Keeper
					if (set_item_to_bank_item(client_h, item_index) == false) {
						m_game->send_notify_msg(0, client_h, Notify::CannotItemToBank, 0, 0, 0, 0);

						m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->set_item(m_game->m_client_list[client_h]->m_x,
							m_game->m_client_list[client_h]->m_y,
							m_game->m_client_list[client_h]->m_item_list[item_index]);

						item_log(ItemLogAction::Drop, client_h, 0, m_game->m_client_list[client_h]->m_item_list[item_index]);

						m_game->send_ground_item_event(CommonType::ItemDrop, m_game->m_client_list[client_h]->m_map_index,
							m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y,
							m_game->m_client_list[client_h]->m_item_list[item_index]);
					}
				}
				else if (m_game->m_npc_list[owner_h]->m_npc_config_id == 56) { // Shop Keeper
					m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->set_item(m_game->m_client_list[client_h]->m_x,
						m_game->m_client_list[client_h]->m_y,
						m_game->m_client_list[client_h]->m_item_list[item_index]);

					item_log(ItemLogAction::Drop, client_h, 0, m_game->m_client_list[client_h]->m_item_list[item_index]);

					m_game->send_ground_item_event(CommonType::ItemDrop, m_game->m_client_list[client_h]->m_map_index,
						m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y,
						m_game->m_client_list[client_h]->m_item_list[item_index]);

					std::memset(char_name, 0, sizeof(char_name));
				}
				else {
					// NPC cannot receive items — reject and keep item in inventory
					m_game->send_notify_msg(0, client_h, Notify::CannotGiveItem, item_index, amount, 0, char_name);
					m_game->calc_total_weight(client_h);
					return;
				}
			}

			m_game->send_notify_msg(0, client_h, Notify::GiveItemFinEraseItem, item_index, amount, 0, char_name);
		}

		if (m_game->m_client_list[client_h] == 0) return;

		// . delete !
		m_game->m_client_list[client_h]->m_item_list[item_index] = 0;
		m_game->m_client_list[client_h]->m_is_item_equipped[item_index] = false;

		m_game->m_client_list[client_h]->m_arrow_index = get_arrow_item_index(client_h);
	}

	m_game->calc_total_weight(client_h);

	// Giver and recipient in one commit. Both handles are re-checked inside, so
	// a recipient whose socket died in the notify above (delete_client) simply
	// drops out of the batch instead of failing it.
	if (give_recipient_h != 0 && g_login != nullptr) {
		const int parties[2] = { client_h, give_recipient_h };
		g_login->save_players_atomic(parties, 2);
	}
}

int ItemManager::set_item_count(int client_h, int item_index, uint64_t count,
	int32_t flow_type, bool is_use_item_result)
{
	if (m_game->m_client_list[client_h] == 0) return -1;
	if (m_game->m_client_list[client_h]->m_item_list[item_index] == 0) return -1;

	CItem* stack = m_game->m_client_list[client_h]->m_item_list[item_index];
	uint16_t weight = get_item_weight(stack, 1);

	// The outflow, booked before either branch below runs (#103, #105). It has
	// to be first: the count == 0 branch destroys `stack`, so a booking after
	// it would read a freed item.
	//
	// The amount is what this call removes, not what remains and not what the
	// stack held. Nothing is booked when the count does not fall, whatever the
	// flow type says — a future *inflow* caller records its own gain at its own
	// venue, the shape the shop-sale proceeds already use, and the reason no
	// row here is ever negative.
	if (flow_type != flow_none && count < stack->m_instance.count)
		record_counted_flow(flow_type, *stack,
			static_cast<int64_t>(stack->m_instance.count - count));

	if (count == 0) {
		// Also books Deplete and destroyed for the whole remainder, as every
		// emptied stack does. Different questions, different numbers — the flow
		// above still fires, or "gold spent on repairs" would silently omit
		// exactly the repairs that emptied the purse, and "reagents consumed by
		// crafting" the recipes that finished a stack.
		item_deplete_handler(client_h, item_index, false);
	}
	else {
		stack->m_instance.count = count;
		m_game->send_notify_msg(0, client_h, Notify::set_item_count, item_index, count, (char)is_use_item_result, 0);
	}

	return weight;
}

uint64_t ItemManager::get_item_count_by_id(int client_h, short item_id)
{
	if (m_game->m_client_list[client_h] == nullptr) return 0;

	for(int i = 0; i < hb::shared::limits::MaxItems; i++) {
		if (m_game->m_client_list[client_h]->m_item_list[i] != nullptr &&
		    m_game->m_client_list[client_h]->m_item_list[i]->m_id_num == item_id) {
			return m_game->m_client_list[client_h]->m_item_list[i]->m_instance.count;
		}
	}

	return 0;
}

int ItemManager::set_item_count_by_id(int client_h, short item_id, uint64_t count,
	int32_t flow_type)
{
	if (m_game->m_client_list[client_h] == nullptr) return -1;

	for(int i = 0; i < hb::shared::limits::MaxItems; i++) {
		if (m_game->m_client_list[client_h]->m_item_list[i] != nullptr &&
		    m_game->m_client_list[client_h]->m_item_list[i]->m_id_num == item_id) {
			// One booking contract, one body: #105 gave the slot-keyed setter
			// the same pre-branch flow booking #103 gave this one, which left
			// the two bodies line-for-line twins. The id → slot lookup is the
			// only thing this function still owns.
			return set_item_count(client_h, i, count, flow_type);
		}
	}

	return -1;
}

void ItemManager::release_item_handler(int client_h, short item_index, bool notice)
{
	char hero_armor_type;
	EquipPos equip_pos;

	if (m_game->m_client_list[client_h] == 0) return;
	if ((item_index < 0) || (item_index >= hb::shared::limits::MaxItems)) return;
	if (m_game->m_client_list[client_h]->m_item_list[item_index] == 0) return;
	if (m_game->m_client_list[client_h]->m_item_list[item_index]->get_item_type() != hb::shared::item::item_type::equipment) return;

	if (m_game->m_client_list[client_h]->m_is_item_equipped[item_index] == false) return;

	hero_armor_type = check_hero_item_equipped(client_h);
	if (hero_armor_type != 0x0FFFFFFFF) m_game->m_client_list[client_h]->m_hero_armour_bonus = 0;

	equip_pos = m_game->m_client_list[client_h]->m_item_list[item_index]->get_equip_pos();
	if (equip_pos == EquipPos::RightHand) {
		if (m_game->m_client_list[client_h]->m_item_list[item_index] != 0) {
			if ((m_game->m_client_list[client_h]->m_item_list[item_index]->m_id_num == 865) || (m_game->m_client_list[client_h]->m_item_list[item_index]->m_id_num == 866)) {
				m_game->m_client_list[client_h]->m_magic_mastery[94] = false;
				m_game->send_notify_msg(0, client_h, Notify::StateChangeSuccess, 0, 0, 0, 0);
			}
		}
	}

	// Clear color and flags for unequipped slot
	switch (equip_pos) {
	case EquipPos::RightHand:
		m_game->m_client_list[client_h]->m_appearance.weapon_color = 0;
		m_game->m_client_list[client_h]->m_status.attack_delay = 0;
		break;
	case EquipPos::LeftHand:
		m_game->m_client_list[client_h]->m_appearance.shield_color = 0;
		break;
	case EquipPos::TwoHand:
		m_game->m_client_list[client_h]->m_appearance.weapon_color = 0;
		break;
	case EquipPos::Body:
		m_game->m_client_list[client_h]->m_appearance.hide_armor = false;
		m_game->m_client_list[client_h]->m_appearance.armor_color = 0;
		break;
	case EquipPos::Back:
		m_game->m_client_list[client_h]->m_appearance.mantle_color = 0;
		break;
	case EquipPos::Arms:
		m_game->m_client_list[client_h]->m_appearance.arm_color = 0;
		break;
	case EquipPos::Leggings:
		m_game->m_client_list[client_h]->m_appearance.pants_color = 0;
		break;
	case EquipPos::Boots:
		m_game->m_client_list[client_h]->m_appearance.boots_color = 0;
		break;
	case EquipPos::Head:
		m_game->m_client_list[client_h]->m_appearance.helm_color = 0;
		break;
	case EquipPos::FullBody:
		m_game->m_client_list[client_h]->m_appearance.mantle_color = 0;
		break;
	}

	// AttackSpecAbility = offensive items (weapons) → weapon_glare
	if (m_game->m_client_list[client_h]->m_item_list[item_index]->get_item_effect_type() == ItemEffectType::AttackSpecAbility) {
		m_game->m_client_list[client_h]->m_appearance.weapon_glare = 0;
	}

	// DefenseSpecAbility = defensive items (shields) → shield_glare
	if (m_game->m_client_list[client_h]->m_item_list[item_index]->get_item_effect_type() == ItemEffectType::DefenseSpecAbility) {
		m_game->m_client_list[client_h]->m_appearance.shield_glare = 0;
	}

	// Notify the owning client before clearing equip state
	if (notice)
		m_game->send_notify_msg(0, client_h, Notify::ItemReleased, to_int(equip_pos), item_index, 0, 0);

	m_game->m_client_list[client_h]->m_is_item_equipped[item_index] = false;
	m_game->m_client_list[client_h]->m_item_equipment_status[to_int(equip_pos)] = -1;

	if (notice)
		m_game->send_event_to_near_client_type_a(client_h, hb::shared::owner_class::Player, MsgId::EventMotion, Type::NullAction, 0, 0, 0);

	calc_total_item_effect(client_h, item_index, true);
}

void ItemManager::request_retrieve_item_handler(int client_h, char* data)
{
	char bank_item_index;
	int j, ret, item_weight;

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_is_init_complete == false) return;

	const auto* pkt = hb::net::PacketCast<hb::net::PacketRequestRetrieveItem>(
		data, sizeof(hb::net::PacketRequestRetrieveItem));
	if (!pkt) return;
	bank_item_index = static_cast<char>(pkt->item_slot);
	//wh remove
	//if (m_game->m_client_list[client_h]->m_is_inside_warehouse == false) return;

	if ((bank_item_index < 0) || (bank_item_index >= hb::shared::limits::MaxBankItems)) return;
	if (m_game->m_client_list[client_h]->m_item_in_bank_list[bank_item_index] == 0) {
		// Bank item missing.
		hb::net::PacketResponseRetrieveItem pkt{};
		pkt.header.msg_id = MsgId::ResponseRetrieveItem;
		pkt.header.msg_type = MsgType::Reject;
		pkt.bank_index = 0;
		pkt.item_index = 0;
		ret = m_game->m_client_list[client_h]->m_socket->send_msg(reinterpret_cast<char*>(&pkt), sizeof(pkt));
	}
	else {
		/*
		if ( m_game->m_client_list[client_h]->m_item_in_bank_list[bank_item_index]->is_stackable() ) {
			//item_weight = m_game->m_client_list[client_h]->m_item_in_bank_list[bank_item_index]->m_weight * m_game->m_client_list[client_h]->m_item_in_bank_list[bank_item_index]->m_instance.count;
			item_weight = get_item_weight(m_game->m_client_list[client_h]->m_item_in_bank_list[bank_item_index], m_game->m_client_list[client_h]->m_item_in_bank_list[bank_item_index]->m_instance.count);
		}
		else item_weight = get_item_weight(m_game->m_client_list[client_h]->m_item_in_bank_list[bank_item_index], 1); //m_game->m_client_list[client_h]->m_item_in_bank_list[bank_item_index]->m_weight;
		*/
		// v1.432
		item_weight = get_item_weight(m_game->m_client_list[client_h]->m_item_in_bank_list[bank_item_index], static_cast<int>(m_game->m_client_list[client_h]->m_item_in_bank_list[bank_item_index]->m_instance.count));

		if ((item_weight + m_game->m_client_list[client_h]->m_cur_weight_load) > m_game->calc_max_load(client_h)) {
		// Notify cannot carry more items.
			ret = send_item_notify_msg(client_h, Notify::CannotCarryMoreItem, 0, 0);
			switch (ret) {
			case sock::Event::QueueFull:
			case sock::Event::SocketError:
			case sock::Event::CriticalError:
			case sock::Event::SocketClosed:
				m_game->delete_client(client_h, true, true);
				break;
			}
			return;
		}

		if (m_game->m_client_list[client_h]->m_item_in_bank_list[bank_item_index]->is_stackable()) {
			for(int i = 0; i < hb::shared::limits::MaxItems; i++)
				if ((m_game->m_client_list[client_h]->m_item_list[i] != 0) &&
					(m_game->m_client_list[client_h]->m_item_list[i]->get_item_type() == m_game->m_client_list[client_h]->m_item_in_bank_list[bank_item_index]->get_item_type()) &&
					(m_game->m_client_list[client_h]->m_item_list[i]->m_id_num == m_game->m_client_list[client_h]->m_item_in_bank_list[bank_item_index]->m_id_num)) {
					// Before the merge, while the withdrawn stack still knows how
					// much it was: afterwards its count has been folded into the
					// inventory stack and the amount retrieved is unrecoverable.
					item_log(ItemLogAction::Retrieve, client_h, (int)-1,
						m_game->m_client_list[client_h]->m_item_in_bank_list[bank_item_index]);

					// v1.41 !!!
					// flow_none: the Retrieve above carried the amount, and this
					// is the merge's rise — nothing is leaving.
					set_item_count(client_h, i, m_game->m_client_list[client_h]->m_item_list[i]->m_instance.count + m_game->m_client_list[client_h]->m_item_in_bank_list[bank_item_index]->m_instance.count, flow_none);

					// The withdrawn stack merged into one the character already
					// had, so its contents are now in the inventory and only the
					// husk is freed. Counted, so the funnel records no exit — and
					// since #81 no flow either, because nothing left the world.
					destroy_item(m_game->m_client_list[client_h]->m_item_in_bank_list[bank_item_index],
						destroy_reason::merged, client_h);

					for (j = 0; j <= hb::shared::limits::MaxBankItems - 2; j++) {
						if ((m_game->m_client_list[client_h]->m_item_in_bank_list[j + 1] != 0) && (m_game->m_client_list[client_h]->m_item_in_bank_list[j] == 0)) {
							m_game->m_client_list[client_h]->m_item_in_bank_list[j] = m_game->m_client_list[client_h]->m_item_in_bank_list[j + 1];

							m_game->m_client_list[client_h]->m_item_in_bank_list[j + 1] = 0;
						}
					}

					// Send retrieve confirmation.
					hb::net::PacketResponseRetrieveItem pkt{};
					pkt.header.msg_id = MsgId::ResponseRetrieveItem;
					pkt.header.msg_type = MsgType::Confirm;
					pkt.bank_index = bank_item_index;
					pkt.item_index = static_cast<int8_t>(i);

					m_game->calc_total_weight(client_h);
					m_game->m_client_list[client_h]->m_arrow_index = get_arrow_item_index(client_h);

					ret = m_game->m_client_list[client_h]->m_socket->send_msg(reinterpret_cast<char*>(&pkt), sizeof(pkt));

					switch (ret) {
					case sock::Event::QueueFull:
					case sock::Event::SocketError:
					case sock::Event::CriticalError:
					case sock::Event::SocketClosed:
						m_game->delete_client(client_h, true, true);
						return;
					}
					return;
				}

		}

		{
			for(int i = 0; i < hb::shared::limits::MaxItems; i++)
				if (m_game->m_client_list[client_h]->m_item_list[i] == 0) {
					// The other half of the retrieve: this one takes a whole slot
					// rather than merging, so the object carries on and only its
					// custody changed.
					item_log(ItemLogAction::Retrieve, client_h, (int)-1,
						m_game->m_client_list[client_h]->m_item_in_bank_list[bank_item_index]);

					m_game->m_client_list[client_h]->m_item_list[i] = m_game->m_client_list[client_h]->m_item_in_bank_list[bank_item_index];
					// v1.3 1-27 12:22
					m_game->m_client_list[client_h]->m_item_pos_list[i].x = 40;
					m_game->m_client_list[client_h]->m_item_pos_list[i].y = 30;

					m_game->m_client_list[client_h]->m_is_item_equipped[i] = false;

					m_game->m_client_list[client_h]->m_item_in_bank_list[bank_item_index] = 0;

					for (j = 0; j <= hb::shared::limits::MaxBankItems - 2; j++) {
						if ((m_game->m_client_list[client_h]->m_item_in_bank_list[j + 1] != 0) && (m_game->m_client_list[client_h]->m_item_in_bank_list[j] == 0)) {
							m_game->m_client_list[client_h]->m_item_in_bank_list[j] = m_game->m_client_list[client_h]->m_item_in_bank_list[j + 1];

							m_game->m_client_list[client_h]->m_item_in_bank_list[j + 1] = 0;
						}
					}

					// Send retrieve confirmation.
					hb::net::PacketResponseRetrieveItem pktConfirm{};
					pktConfirm.header.msg_id = MsgId::ResponseRetrieveItem;
					pktConfirm.header.msg_type = MsgType::Confirm;
					pktConfirm.bank_index = bank_item_index;
					pktConfirm.item_index = static_cast<int8_t>(i);

					m_game->calc_total_weight(client_h);
					m_game->m_client_list[client_h]->m_arrow_index = get_arrow_item_index(client_h);

					ret = m_game->m_client_list[client_h]->m_socket->send_msg(reinterpret_cast<char*>(&pktConfirm), sizeof(pktConfirm));
					switch (ret) {
					case sock::Event::QueueFull:
					case sock::Event::SocketError:
					case sock::Event::CriticalError:
					case sock::Event::SocketClosed:
						m_game->delete_client(client_h, true, true);
						return;
					}
					return;
				}
			// No empty inventory slot.
			hb::net::PacketResponseRetrieveItem pktReject{};
			pktReject.header.msg_id = MsgId::ResponseRetrieveItem;
			pktReject.header.msg_type = MsgType::Reject;
			pktReject.bank_index = 0;
			pktReject.item_index = 0;
			ret = m_game->m_client_list[client_h]->m_socket->send_msg(reinterpret_cast<char*>(&pktReject), sizeof(pktReject));
		}
	}

	switch (ret) {
	case sock::Event::QueueFull:
	case sock::Event::SocketError:
	case sock::Event::CriticalError:
	case sock::Event::SocketClosed:
		m_game->delete_client(client_h, true, true);
		return;
	}
}

bool ItemManager::set_item_to_bank_item(int client_h, short item_index)
{
	int ret;
	CItem* item;

	if (m_game->m_client_list[client_h] == 0) return false;
	if ((item_index < 0) || (item_index >= hb::shared::limits::MaxItems)) return false;
	if (m_game->m_client_list[client_h]->m_item_list[item_index] == 0) return false;
	//wh remove
	//if (m_game->m_client_list[client_h]->m_is_inside_warehouse == false) return false;

	for(int i = 0; i < hb::shared::limits::MaxBankItems; i++)
		if (m_game->m_client_list[client_h]->m_item_in_bank_list[i] == 0) {

			// Always a character deposit — the Trading Post reaches the CItem*
			// overload, never this one — so there is no transition to choose.
			item_log(ItemLogAction::Deposit, client_h, (int)-1,
				m_game->m_client_list[client_h]->m_item_list[item_index]);

			m_game->m_client_list[client_h]->m_item_in_bank_list[i] = m_game->m_client_list[client_h]->m_item_list[item_index];
			item = m_game->m_client_list[client_h]->m_item_in_bank_list[i];
			// !!!      NULL .
			m_game->m_client_list[client_h]->m_item_list[item_index] = 0;

			m_game->calc_total_weight(client_h);

			{
				hb::net::PacketNotifyItemToBank pkt{};
				pkt.header.msg_id = MsgId::Notify;
				pkt.header.msg_type = Notify::ItemToBank;
				pkt.bank_index = static_cast<uint8_t>(i);
				pkt.is_new = 1;
				memcpy(pkt.name, item->m_name, sizeof(pkt.name));
				pkt.count = item->m_instance.count;
				pkt.item_type = item->m_item_type;
				pkt.equip_pos = item->m_equip_pos;
				pkt.is_equipped = 0;
				pkt.level_limit = item->m_level_requirement;
				pkt.gender_limit = item->m_gender_requirement;
				pkt.cur_durability = item->m_instance.cur_durability;
				pkt.weight = item->m_weight;
				pkt.item_color = item->m_instance.item_color;
				pkt.item_effect_value2 = item->m_item_effect_value2;
				pkt.attributes = item->get_attributes();
				pkt.spec_effect_value2 = static_cast<uint8_t>(item->m_instance.special_effect_value2);
				pkt.item_id = item->m_id_num;
				pkt.max_durability = item->m_durability;
				ret = m_game->m_client_list[client_h]->m_socket->send_msg(reinterpret_cast<char*>(&pkt), sizeof(pkt));
			}
			switch (ret) {
			case sock::Event::QueueFull:
			case sock::Event::SocketError:
			case sock::Event::CriticalError:
			case sock::Event::SocketClosed:
				// . v1.41  .
				// m_game->delete_client(client_h, true, true);
				return true;
			}

			return true;
		}

	return false;
}

void ItemManager::calculate_ssn_item_index(int client_h, short weapon_index, int value)
{
	short skill_index;
	int   old_ssn, ss_npoint, weap_idx;

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_is_init_complete == false) return;
	if (m_game->m_client_list[client_h]->m_item_list[weapon_index] == 0) return;
	if (m_game->m_client_list[client_h]->m_is_killed) return;

	skill_index = m_game->m_client_list[client_h]->m_item_list[weapon_index]->m_related_skill;
	if ((skill_index < 0) || (skill_index >= hb::shared::limits::MaxSkillType)) return;
	if (m_game->m_client_list[client_h]->m_skill_mastery[skill_index] == 0) return;

	old_ssn = m_game->m_client_list[client_h]->m_skill_progress[skill_index];
	m_game->m_client_list[client_h]->m_skill_progress[skill_index] += value;

	ss_npoint = m_game->m_skill_progress_threshold[m_game->m_client_list[client_h]->m_skill_mastery[skill_index] + 1];

	if ((m_game->m_client_list[client_h]->m_skill_mastery[skill_index] < 100) &&
		(m_game->m_client_list[client_h]->m_skill_progress[skill_index] > ss_npoint)) {

		m_game->m_client_list[client_h]->m_skill_mastery[skill_index]++;

		switch (skill_index) {
		case 0:  // Mining
		case 5:  // Hand-Attack
		case 13: // Manufacturing
			if (m_game->m_client_list[client_h]->m_skill_mastery[skill_index] > ((m_game->m_client_list[client_h]->effective_str() + m_game->m_client_list[client_h]->m_angelic_str) * 2)) {
				m_game->m_client_list[client_h]->m_skill_mastery[skill_index]--;
				m_game->m_client_list[client_h]->m_skill_progress[skill_index] = old_ssn;
			}
			else m_game->m_client_list[client_h]->m_skill_progress[skill_index] = 0;
			break;

		case 3: // Magic-Resistance
			if (m_game->m_client_list[client_h]->m_skill_mastery[skill_index] > (m_game->m_client_list[client_h]->m_level * 2)) {
				m_game->m_client_list[client_h]->m_skill_mastery[skill_index]--;
				m_game->m_client_list[client_h]->m_skill_progress[skill_index] = old_ssn;
			}
			else m_game->m_client_list[client_h]->m_skill_progress[skill_index] = 0;
			break;

		case 4:  // Magic
		case 18: // Crafting
		case 21: // Staff-Attack
			if (m_game->m_client_list[client_h]->m_skill_mastery[skill_index] > ((m_game->m_client_list[client_h]->m_mag + m_game->m_client_list[client_h]->m_angelic_mag) * 2)) {
				m_game->m_client_list[client_h]->m_skill_mastery[skill_index]--;
				m_game->m_client_list[client_h]->m_skill_progress[skill_index] = old_ssn;
			}
			else m_game->m_client_list[client_h]->m_skill_progress[skill_index] = 0;
			break;

		case 1:  // Fishing
		case 6:  // Archery
		case 7:  // Short-Sword
		case 8:  // Long-Sword
		case 9:  // Fencing 
		case 10: // Axe-Attack
		case 11: // Shield        	
		case 14: // Hammer 
			if (m_game->m_client_list[client_h]->m_skill_mastery[skill_index] > ((m_game->m_client_list[client_h]->m_dex + m_game->m_client_list[client_h]->m_angelic_dex) * 2)) {
				m_game->m_client_list[client_h]->m_skill_mastery[skill_index]--;
				m_game->m_client_list[client_h]->m_skill_progress[skill_index] = old_ssn;
			}
			else m_game->m_client_list[client_h]->m_skill_progress[skill_index] = 0;
			break;

		case 2:	 // Farming
		case 12: // Alchemy
		case 15:
		case 19: // Pretend-Corpse
		case 20: // Enchanting
			if (m_game->m_client_list[client_h]->m_skill_mastery[skill_index] > ((m_game->m_client_list[client_h]->m_int + m_game->m_client_list[client_h]->m_angelic_int) * 2)) {
				m_game->m_client_list[client_h]->m_skill_mastery[skill_index]--;
				m_game->m_client_list[client_h]->m_skill_progress[skill_index] = old_ssn;
			}
			else m_game->m_client_list[client_h]->m_skill_progress[skill_index] = 0;
			break;

		case 23: // Poison-Resistance
			if (m_game->m_client_list[client_h]->m_skill_mastery[skill_index] > (m_game->m_client_list[client_h]->m_vit * 2)) {
				m_game->m_client_list[client_h]->m_skill_mastery[skill_index]--;
				m_game->m_client_list[client_h]->m_skill_progress[skill_index] = old_ssn;
			}
			else m_game->m_client_list[client_h]->m_skill_progress[skill_index] = 0;
			break;

		default:
			m_game->m_client_list[client_h]->m_skill_progress[skill_index] = 0;
			break;
		}

		if (m_game->m_client_list[client_h]->m_skill_progress[skill_index] == 0) {
			if (m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::TwoHand)] != -1) {
				weap_idx = m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::TwoHand)];
				if (m_game->m_client_list[client_h]->m_item_list[weap_idx]->m_related_skill == skill_index) {
					m_game->m_client_list[client_h]->m_hit_ratio++;
				}
			}

			if (m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::RightHand)] != -1) {
				weap_idx = m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::RightHand)];
				if (m_game->m_client_list[client_h]->m_item_list[weap_idx]->m_related_skill == skill_index) {
					// Mace    .  1 .
					m_game->m_client_list[client_h]->m_hit_ratio++;
				}
			}
		}

		if (m_game->m_client_list[client_h]->m_skill_progress[skill_index] == 0) {
			// SKill  600     1 .
			m_game->m_skill_manager->check_total_skill_mastery_points(client_h, skill_index);
			// Skill    .
			m_game->send_notify_msg(0, client_h, Notify::Skill, skill_index, m_game->m_client_list[client_h]->m_skill_mastery[skill_index], 0, 0);
		}
	}
}

int ItemManager::get_arrow_item_index(int client_h)
{
	

	if (m_game->m_client_list[client_h] == 0) return -1;

	for(int i = 0; i < hb::shared::limits::MaxItems; i++)
		if (m_game->m_client_list[client_h]->m_item_list[i] != 0) {

			// Arrow  1     .
			if ((m_game->m_client_list[client_h]->m_item_list[i]->get_item_sub_type() == hb::shared::item::item_sub_type::ammo) &&
				(m_game->m_client_list[client_h]->m_item_list[i]->m_instance.count > 0))
				return i;
		}

	return -1;
}

void ItemManager::item_deplete_handler(int client_h, short item_index, bool is_use_item_result,
	destroy_reason::destroy_reason reason)
{

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_is_init_complete == false) return;
	if ((item_index < 0) || (item_index >= hb::shared::limits::MaxItems)) return;
	if (m_game->m_client_list[client_h]->m_item_list[item_index] == 0) return;

	item_log(ItemLogAction::Deplete, client_h, 0, m_game->m_client_list[client_h]->m_item_list[item_index]);

	release_item_handler(client_h, item_index, true);

	m_game->send_notify_msg(0, client_h, Notify::ItemDepletedEraseItem, item_index, (int)is_use_item_result, 0, 0);

	// Deplete says the slot emptied; destroyed says the item left the world.
	// They coincide here, but only here — a drop empties a slot too — so the
	// exit gets its own event rather than being inferred from the action that
	// happened to cause it.
	destroy_item(m_game->m_client_list[client_h]->m_item_list[item_index], reason, client_h);

	m_game->m_client_list[client_h]->m_is_item_equipped[item_index] = false;

	// !!! BUG POINT
	// . ArrowIndex     .
	m_game->m_client_list[client_h]->m_arrow_index = get_arrow_item_index(client_h);

	m_game->calc_total_weight(client_h);
}

void ItemManager::use_item_handler(int client_h, short item_index, short dX, short dY, short dest_item_id)
{
	int max, v1, v2, v3, sev1, effect_result = 0;
	uint32_t time;
	short temp_short, tmp_type;
	char slate_type[20];

	time = GameClock::GetTimeMS();
	std::memset(slate_type, 0, sizeof(slate_type));

	//testcode
	//std::snprintf(G_cTxt, sizeof(G_cTxt), "%d", dest_item_id);
	//PutLogList(G_cTxt);

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_is_killed) return;
	if (m_game->m_client_list[client_h]->m_is_init_complete == false) return;

	if ((item_index < 0) || (item_index >= hb::shared::limits::MaxItems)) return;
	if (m_game->m_client_list[client_h]->m_item_list[item_index] == 0) return;

	namespace item_type = hb::shared::item::item_type;
	namespace item_sub_type = hb::shared::item::item_sub_type;
	auto it = m_game->m_client_list[client_h]->m_item_list[item_index]->get_item_type();
	auto ist = m_game->m_client_list[client_h]->m_item_list[item_index]->get_item_sub_type();

	if (it != item_type::consumable && it != item_type::tool && ist != item_sub_type::ammo) return;

	if (it == item_type::consumable && ist != item_sub_type::target) {

		switch (m_game->m_client_list[client_h]->m_item_list[item_index]->get_item_effect_type()) {
		case ItemEffectType::Warm:

			if (m_game->m_client_list[client_h]->m_magic_effect_status[hb::shared::magic::Ice] == 1) {
				//	m_game->m_status_effect_manager->set_ice_flag(client_h, hb::shared::owner_class::Player, false);

				m_game->m_delay_event_manager->remove_from_delay_event_list(client_h, hb::shared::owner_class::Player, hb::shared::magic::Ice);

				m_game->m_delay_event_manager->register_delay_event(sdelay::Type::MagicRelease, hb::shared::magic::Ice, time + (1 * 1000),
					client_h, hb::shared::owner_class::Player, 0, 0, 0, 1, 0, 0);

				//				m_game->send_notify_msg(0, client_h, Notify::MagicEffectOff, hb::shared::magic::Ice, 0, 0, 0);
			}

			m_game->m_client_list[client_h]->m_warm_effect_time = time;
			break;

		case ItemEffectType::Lottery:
			// EV1(:  100) EV2( ) EV3( )
			temp_short = m_game->dice(1, m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value1);
			if (temp_short == m_game->dice(1, m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value1)) {

			}
			else {

			}
			break;

		case ItemEffectType::Slates:
			if (m_game->m_client_list[client_h]->m_item_list[item_index] != 0) {
				// Full Ancient Slate ??
				if (m_game->m_client_list[client_h]->m_item_list[item_index]->m_id_num == 867) {
					// Slates dont work on Heldenian Map
					switch (m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value2) {
					case 2: // Bezerk slate
						m_game->m_client_list[client_h]->m_magic_effect_status[hb::shared::magic::Berserk] = true;
						m_game->m_status_effect_manager->set_berserk_flag(client_h, hb::shared::owner_class::Player, true);
						m_game->m_delay_event_manager->register_delay_event(sdelay::Type::MagicRelease, hb::shared::magic::Berserk, time + (1000 * 600),
							client_h, hb::shared::owner_class::Player, 0, 0, 0, 1, 0, 0);
						m_game->send_notify_msg(0, client_h, Notify::MagicEffectOn, hb::shared::magic::Berserk, 1, 0, 0);
						strcpy(slate_type, "Berserk");
						break;

					case 1: // Invincible slate
						if (strlen(slate_type) == 0) {
							strcpy(slate_type, "Invincible");
						}
					case 3: // Mana slate
						if (strlen(slate_type) == 0) {
							strcpy(slate_type, "Mana");
						}
					case 4: // Exp slate
						if (strlen(slate_type) == 0) {
							strcpy(slate_type, "Exp");
						}
						set_slate_flag(client_h, m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value2, true);
						m_game->m_delay_event_manager->register_delay_event(sdelay::Type::AncientTablet, m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value2,
							time + (1000 * 600), client_h, hb::shared::owner_class::Player, 0, 0, 0, 1, 0, 0);
						switch (m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value2) {
						case 1:
							effect_result = 4;
							break;
						case 3:
							effect_result = 5;
							break;
						case 4:
							effect_result = 6;
							break;
						}
					}
					//if (strlen(slate_type) > 0)
					//	item_log(ItemLogAction::Use, client_h, strlen(slate_type), m_game->m_client_list[client_h]->m_item_list[item_index]);
				}
			}
			break;
		case ItemEffectType::HP:
			max = m_game->get_max_hp(client_h);
			if (m_game->m_client_list[client_h]->m_hp < max) {

				if (m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value1 == 0) {
					v1 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1;
					v2 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2;
					v3 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value3;
				}
				else {
					v1 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value1;
					v2 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value2;
					v3 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value3;
				}

				m_game->m_client_list[client_h]->m_hp += (m_game->dice(v1, v2) + v3);
				if (m_game->m_client_list[client_h]->m_hp > max) m_game->m_client_list[client_h]->m_hp = max;
				if (m_game->m_client_list[client_h]->m_hp <= 0)   m_game->m_client_list[client_h]->m_hp = 1;

				effect_result = 1;
			}
			break;

		case ItemEffectType::MP:
			max = m_game->get_max_mp(client_h);

			if (m_game->m_client_list[client_h]->m_mp < max) {

				if (m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value1 == 0) {
					v1 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1;
					v2 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2;
					v3 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value3;
				}
				else
				{
					v1 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value1;
					v2 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value2;
					v3 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value3;
				}

				m_game->m_client_list[client_h]->m_mp += (m_game->dice(v1, v2) + v3);
				if (m_game->m_client_list[client_h]->m_mp > max)
					m_game->m_client_list[client_h]->m_mp = max;

				effect_result = 2;
			}
			break;
		case ItemEffectType::CritKomm:
			//CritInc(client_h);
			break;
		case ItemEffectType::SP:
			max = m_game->get_max_sp(client_h);

			if (m_game->m_client_list[client_h]->m_sp < max) {

				if (m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value1 == 0) {
					v1 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1;
					v2 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2;
					v3 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value3;
				}
				else {
					v1 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value1;
					v2 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value2;
					v3 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value3;
				}

				m_game->m_client_list[client_h]->m_sp += (m_game->dice(v1, v2) + v3);
				if (m_game->m_client_list[client_h]->m_sp > max)
					m_game->m_client_list[client_h]->m_sp = max;

				effect_result = 3;
			}

			if (m_game->m_client_list[client_h]->m_is_poisoned) {
				m_game->m_client_list[client_h]->m_is_poisoned = false;
				m_game->m_status_effect_manager->set_poison_flag(client_h, hb::shared::owner_class::Player, false);
				m_game->send_notify_msg(0, client_h, Notify::MagicEffectOff, hb::shared::magic::Poison, 0, 0, 0);
				m_game->send_notify_msg(0, client_h, Notify::NoticeMsg, 0, 0, 0, "Poison has been cured.");
			}
			break;

		case ItemEffectType::HPStock:
			v1 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1;
			v2 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2;
			v3 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value3;

			m_game->m_client_list[client_h]->m_hp_stock += m_game->dice(v1, v2) + v3;
			if (m_game->m_client_list[client_h]->m_hp_stock < 0)   m_game->m_client_list[client_h]->m_hp_stock = 0;
			if (m_game->m_client_list[client_h]->m_hp_stock > 500) m_game->m_client_list[client_h]->m_hp_stock = 500;

			m_game->m_client_list[client_h]->m_hunger_status += m_game->dice(v1, v2) + v3;
			if (m_game->m_client_list[client_h]->m_hunger_status > 100) m_game->m_client_list[client_h]->m_hunger_status = 100;
			if (m_game->m_client_list[client_h]->m_hunger_status < 0)   m_game->m_client_list[client_h]->m_hunger_status = 0;
			m_game->send_notify_msg(0, client_h, Notify::Hunger, m_game->m_client_list[client_h]->m_hunger_status, 0, 0, 0);
			break;

		case ItemEffectType::StudySkill:
			v1 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1;
			v2 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2;
			sev1 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value1;
			// v1  Skill . v2  , sev1    ()
			if (sev1 == 0) {
				m_game->m_skill_manager->train_skill_response(true, client_h, v1, v2);
			}
			else {
				m_game->m_skill_manager->train_skill_response(true, client_h, v1, sev1);
			}
			break;

		case ItemEffectType::StudyMagic:
			// v1   .
			v1 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1;
			if (m_game->m_magic_config_list[v1] != 0)
				m_game->m_magic_manager->request_study_magic_handler(client_h, m_game->m_magic_config_list[v1]->m_name, false);
			break;

			/*case ItemEffectType::Lottery:
				lottery = m_game->dice(1, m_game->m_client_list[client_h]->m_item_list[item_index]->
				break;*/

				// New 15/05/2004 Changed
		case ItemEffectType::Magic:
			if (m_game->m_client_list[client_h]->m_status.invisibility) {
				m_game->m_status_effect_manager->set_invisibility_flag(client_h, hb::shared::owner_class::Player, false);

				m_game->m_delay_event_manager->remove_from_delay_event_list(client_h, hb::shared::owner_class::Player, hb::shared::magic::Invisibility);
				m_game->m_client_list[client_h]->m_magic_effect_status[hb::shared::magic::Invisibility] = 0;
			}

			switch (m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1) {
			case 1:
				// Recall    .
				// testcode
				m_game->request_teleport_handler(client_h, "1   ");
				break;

			case 2:
				m_game->m_magic_manager->player_magic_handler(client_h, m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y, 32, true);
				break;

			case 3:
				if (m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_is_fight_zone == false)
					m_game->m_magic_manager->player_magic_handler(client_h, m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y, 34, true);
				break;

			case 4:
				// fixed location teleportation:
				switch (m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2) {
				case 1:
					if (memcmp(m_game->m_client_list[client_h]->m_map_name, "bisle", 5) != 0) {
						//v1.42
						item_deplete_handler(client_h, item_index, true);
						m_game->request_teleport_handler(client_h, "2   ", "bisle", -1, -1);
					}
					break;
				case 2: //lotery
					item_deplete_handler(client_h, item_index, true);
					m_game->lotery_handler(client_h);
					break;

				case 11:
				case 12:
				case 13:
				case 14:
				case 15:
				case 16:
				case 17:
				case 18:
				case 19:
					hb::time::local_time SysTime{};

					SysTime = hb::time::local_time::now();
					if ((m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.touch_effect_value1 != SysTime.month) ||
						(m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.touch_effect_value2 != SysTime.day) ||
						(m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.touch_effect_value3 <= SysTime.hour)) {
					}
					else {
						char dest_map_name[hb::shared::limits::MapNameLen]{};
						int zoneNum = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2 - 10;
						std::snprintf(dest_map_name, sizeof(dest_map_name), "fightzone%c", '0' + (zoneNum % 10));
						if (memcmp(m_game->m_client_list[client_h]->m_map_name, dest_map_name, 10) != 0) {
							//v1.42
							item_deplete_handler(client_h, item_index, true);
							m_game->request_teleport_handler(client_h, "2   ", dest_map_name, -1, -1);
						}
					}
					break;
				}
				break;

			case 5:
				m_game->m_magic_manager->player_magic_handler(client_h, m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y, 31, true,
					m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2);
				break;
			}
			break;

		case ItemEffectType::FirmStamina:
			m_game->m_client_list[client_h]->m_time_left_firm_stamina += m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1;
			if (m_game->m_client_list[client_h]->m_time_left_firm_stamina > 20 * 30) m_game->m_client_list[client_h]->m_time_left_firm_stamina = 20 * 30;
			break;

		case ItemEffectType::ChangeAttr:
			switch (m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1) {
			case 1:
				m_game->m_client_list[client_h]->m_hair_color++;
				if (m_game->m_client_list[client_h]->m_hair_color > 15) m_game->m_client_list[client_h]->m_hair_color = 0;

				m_game->m_client_list[client_h]->m_appearance.hair_style = m_game->m_client_list[client_h]->m_hair_style;
				m_game->m_client_list[client_h]->m_appearance.hair_color = m_game->m_client_list[client_h]->m_hair_color;
				m_game->m_client_list[client_h]->m_appearance.underwear_type = m_game->m_client_list[client_h]->m_underwear;
				break;

			case 2:
				m_game->m_client_list[client_h]->m_hair_style++;
				if (m_game->m_client_list[client_h]->m_hair_style > 7) m_game->m_client_list[client_h]->m_hair_style = 0;

				m_game->m_client_list[client_h]->m_appearance.hair_style = m_game->m_client_list[client_h]->m_hair_style;
				m_game->m_client_list[client_h]->m_appearance.hair_color = m_game->m_client_list[client_h]->m_hair_color;
				m_game->m_client_list[client_h]->m_appearance.underwear_type = m_game->m_client_list[client_h]->m_underwear;
				break;

			case 3:
				// Appearance , .
				m_game->m_client_list[client_h]->m_skin++;
				if (m_game->m_client_list[client_h]->m_skin > 3)
					m_game->m_client_list[client_h]->m_skin = 1;

				if (m_game->m_client_list[client_h]->m_sex == 1)      temp_short = 1;
				else if (m_game->m_client_list[client_h]->m_sex == 2) temp_short = 4;

				switch (m_game->m_client_list[client_h]->m_skin) {
				case 2:	temp_short += 1; break;
				case 3:	temp_short += 2; break;
				}
				m_game->m_client_list[client_h]->m_type = temp_short;
				break;

			case 4:
			{
				auto* client = m_game->m_client_list[client_h];

				// Toggle sex
				if (client->m_sex == 1)
					client->m_sex = 2;
				else
					client->m_sex = 1;

				// Update character type based on new sex + skin
				tmp_type = (client->m_sex == 1) ? 1 : 4;
				switch (client->m_skin)
				{
				case 2: tmp_type += 1; break;
				case 3: tmp_type += 2; break;
				}
				client->m_type = tmp_type;
				client->m_appearance.hair_style = client->m_hair_style;
				client->m_appearance.hair_color = client->m_hair_color;
				client->m_appearance.underwear_type = client->m_underwear;

				// Swap all gendered hero items (403-426) to the correct sex variant.
				// Covers inventory (including equipped) and warehouse.
				bool is_female = (client->m_sex == 2);
				constexpr int gendered_first = ItemId::AresdenHeroHelmM;
				constexpr int gendered_last = ItemId::ElvineHeroLeggingsW;
				constexpr int group_size = 4;

				auto swap_hero_item = [&](CItem* item) -> bool
				{
					if (item == nullptr) return false;
					if (item->m_id_num < gendered_first || item->m_id_num > gendered_last) return false;

					int group_base = gendered_first
						+ ((item->m_id_num - gendered_first) / group_size) * group_size;
					int current_offset = item->m_id_num - group_base;
					int side_offset = (current_offset >= 2) ? 2 : 0;
					int sex_offset = is_female ? 1 : 0;
					int new_id = group_base + side_offset + sex_offset;

					if (new_id == item->m_id_num) return false;

					CItem* config = m_game->m_item_config_list[new_id];
					if (config == nullptr) return false;

					std::memcpy(item->m_name, config->m_name, sizeof(item->m_name));
					item->m_id_num = config->m_id_num;
					item->m_gender_requirement = config->m_gender_requirement;
					item->m_weapon_class = config->m_weapon_class;
					item->m_hide_armor = config->m_hide_armor;
					item->m_is_skirt = config->m_is_skirt;
					return true;
				};

				// Swap inventory items and notify client in-place (preserves positions)
				for (int i = 0; i < hb::shared::limits::MaxItems; i++)
				{
					if (swap_hero_item(client->m_item_list[i]))
					{
						CItem* item = client->m_item_list[i];
						m_game->send_gizon_item_change(client_h, i, item);
					}
				}

				// Swap bank items and notify client per-item
				for (int i = 0; i < hb::shared::limits::MaxBankItems; i++)
				{
					if (swap_hero_item(client->m_item_in_bank_list[i]))
					{
						CItem* item = client->m_item_in_bank_list[i];
						hb::net::PacketNotifyItemToBank pkt{};
						pkt.header.msg_id = MsgId::Notify;
						pkt.header.msg_type = Notify::ItemToBank;
						pkt.bank_index = static_cast<uint8_t>(i);
						pkt.is_new = 0;
						std::memcpy(pkt.name, item->m_name, sizeof(pkt.name));
						pkt.count = item->m_instance.count;
						pkt.item_type = item->m_item_type;
						pkt.equip_pos = item->m_equip_pos;
						pkt.is_equipped = 0;
						pkt.level_limit = item->m_level_requirement;
						pkt.gender_limit = item->m_gender_requirement;
						pkt.cur_durability = item->m_instance.cur_durability;
						pkt.weight = item->m_weight;
						pkt.item_color = item->m_instance.item_color;
						pkt.item_effect_value2 = item->m_item_effect_value2;
						pkt.attributes = item->get_attributes();
						pkt.spec_effect_value2 = static_cast<uint8_t>(item->m_instance.special_effect_value2);
						pkt.item_id = item->m_id_num;
						pkt.max_durability = item->m_durability;
						client->m_socket->send_msg(
							reinterpret_cast<char*>(&pkt), sizeof(pkt));
					}
				}

				// Unequip all non-hero equipped items (except neck, rings, angels)
				// so the client refreshes their sprites with the new gender.
				// Hero items are already swapped above. Accessories have no gender sprites.
				for (int i = 0; i < hb::shared::limits::MaxItems; i++)
				{
					if (client->m_item_list[i] == nullptr) continue;
					if (client->m_is_item_equipped[i] == false) continue;

					// Skip hero items (already handled by swap above)
					int id = client->m_item_list[i]->m_id_num;
					if (id >= gendered_first && id <= gendered_last) continue;

					// Skip accessories: neck, rings, angel pendants
					EquipPos pos = client->m_item_list[i]->get_equip_pos();
					if (pos == EquipPos::Neck || pos == EquipPos::RightFinger || pos == EquipPos::LeftFinger) continue;

					release_item_handler(client_h, static_cast<short>(i), false);
					m_game->send_notify_msg(0, client_h, Notify::ItemReleased, static_cast<int>(pos), i, 0, 0);
				}

				break;
			}
			}

			m_game->send_event_to_near_client_type_a(client_h, hb::shared::owner_class::Player, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
			break;
		}
		// *** Request Teleport Handler           .
		item_deplete_handler(client_h, item_index, true);

		switch (effect_result) {
		case 1:
			m_game->send_notify_msg(0, client_h, Notify::Hp, 0, 0, 0, 0);
			break;
		case 2:
			m_game->send_notify_msg(0, client_h, Notify::Mp, 0, 0, 0, 0);
			break;
		case 3:
			m_game->send_notify_msg(0, client_h, Notify::Sp, 0, 0, 0, 0);
			break;
		case 4: // Invincible
			m_game->send_notify_msg(0, client_h, Notify::SlateInvincible, 0, 0, 0, 0);
			break;
		case 5: // Mana
			m_game->send_notify_msg(0, client_h, Notify::SlateMana, 0, 0, 0, 0);
			break;
		case 6: // EXP
			m_game->send_notify_msg(0, client_h, Notify::SlateExp, 0, 0, 0, 0);
			break;
		default:
			break;
		}
	}
	else if (ist == item_sub_type::target) {
		if (deplete_dest_type_item_use_effect(client_h, dX, dY, item_index, dest_item_id))
			item_deplete_handler(client_h, item_index, true);
	}
	else if (ist == item_sub_type::ammo) {
		m_game->m_client_list[client_h]->m_arrow_index = get_arrow_item_index(client_h);
	}
	else if (it == item_type::tool && m_game->m_client_list[client_h]->m_item_list[item_index]->m_durability == 0) {
		// .     . (ex: )
		switch (m_game->m_client_list[client_h]->m_item_list[item_index]->get_item_effect_type()) {
		case ItemEffectType::ShowLocation:
			v1 = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1;
			switch (v1) {
			case 1:
				if (strcmp(m_game->m_client_list[client_h]->m_map_name, "aresden") == 0)
					m_game->send_notify_msg(0, client_h, Notify::ShowMap, v1, 1, 0, 0);
				else if (strcmp(m_game->m_client_list[client_h]->m_map_name, "elvine") == 0)
					m_game->send_notify_msg(0, client_h, Notify::ShowMap, v1, 2, 0, 0);
				else if (strcmp(m_game->m_client_list[client_h]->m_map_name, "middleland") == 0)
					m_game->send_notify_msg(0, client_h, Notify::ShowMap, v1, 3, 0, 0);
				else if (strcmp(m_game->m_client_list[client_h]->m_map_name, "default") == 0)
					m_game->send_notify_msg(0, client_h, Notify::ShowMap, v1, 4, 0, 0);
				else if (strcmp(m_game->m_client_list[client_h]->m_map_name, "huntzone2") == 0)
					m_game->send_notify_msg(0, client_h, Notify::ShowMap, v1, 5, 0, 0);
				else if (strcmp(m_game->m_client_list[client_h]->m_map_name, "huntzone1") == 0)
					m_game->send_notify_msg(0, client_h, Notify::ShowMap, v1, 6, 0, 0);
				else if (strcmp(m_game->m_client_list[client_h]->m_map_name, "huntzone4") == 0)
					m_game->send_notify_msg(0, client_h, Notify::ShowMap, v1, 7, 0, 0);
				else if (strcmp(m_game->m_client_list[client_h]->m_map_name, "huntzone3") == 0)
					m_game->send_notify_msg(0, client_h, Notify::ShowMap, v1, 8, 0, 0);
				else if (strcmp(m_game->m_client_list[client_h]->m_map_name, "arefarm") == 0)
					m_game->send_notify_msg(0, client_h, Notify::ShowMap, v1, 9, 0, 0);
				else if (strcmp(m_game->m_client_list[client_h]->m_map_name, "elvfarm") == 0)
					m_game->send_notify_msg(0, client_h, Notify::ShowMap, v1, 10, 0, 0);
				else m_game->send_notify_msg(0, client_h, Notify::ShowMap, v1, 0, 0, 0);
				break;
			}
			break;
		}
	}
	else if (it == item_type::tool && m_game->m_client_list[client_h]->m_item_list[item_index]->m_durability > 0) {

		if ((m_game->m_client_list[client_h]->m_item_list[item_index] == 0) ||
			(m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.cur_durability <= 0) ||
			(m_game->m_client_list[client_h]->m_skill_using_status[m_game->m_client_list[client_h]->m_item_list[item_index]->m_related_skill])) {
			return;
		}
		else {
			if (m_game->m_client_list[client_h]->m_item_list[item_index]->m_durability != 0) {
				m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.cur_durability--;
				m_game->send_notify_msg(0, client_h, Notify::CurDurability, item_index, m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.cur_durability, 0, 0);
				if (m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.cur_durability <= 0) {
					m_game->send_notify_msg(0, client_h, Notify::ItemDurabilityEnd, to_int(EquipPos::None), item_index, 0, 0);
				}
				else {
					// ID . v1.12
					int skill_using_time_id = (int)GameClock::GetTimeMS();

					m_game->m_delay_event_manager->register_delay_event(sdelay::Type::UseItemSkill, m_game->m_client_list[client_h]->m_item_list[item_index]->m_related_skill,
						time + m_game->m_skill_config_list[m_game->m_client_list[client_h]->m_item_list[item_index]->m_related_skill]->m_value_2 * 1000,
						client_h, hb::shared::owner_class::Player, m_game->m_client_list[client_h]->m_map_index, dX, dY,
						m_game->m_client_list[client_h]->m_skill_mastery[m_game->m_client_list[client_h]->m_item_list[item_index]->m_related_skill], skill_using_time_id, 0);

					m_game->m_client_list[client_h]->m_skill_using_status[m_game->m_client_list[client_h]->m_item_list[item_index]->m_related_skill] = true;
					m_game->m_client_list[client_h]->m_skill_using_time_id[m_game->m_client_list[client_h]->m_item_list[item_index]->m_related_skill] = skill_using_time_id; //v1.12
				}
			}
		}
	}
}

bool ItemManager::set_item_to_bank_item(int client_h, CItem* item, bank_deposit how)
{
	int ret;

	if (m_game->m_client_list[client_h] == 0) return false;
	if (item == 0) return false;
	//wh remove
	//if (m_game->m_client_list[client_h]->m_is_inside_warehouse == false) return false;

	for(int i = 0; i < hb::shared::limits::MaxBankItems; i++)
		if (m_game->m_client_list[client_h]->m_item_in_bank_list[i] == 0) {

			// Once the slot is found the deposit has happened, so this is where
			// it is recorded: the failure return below means the Warehouse was
			// full and the item never moved.
			if (how == bank_deposit::by_character)
				item_log(ItemLogAction::Deposit, client_h, (int)-1, item);

			m_game->m_client_list[client_h]->m_item_in_bank_list[i] = item;

			{
				hb::net::PacketNotifyItemToBank pkt{};
				pkt.header.msg_id = MsgId::Notify;
				pkt.header.msg_type = Notify::ItemToBank;
				pkt.bank_index = static_cast<uint8_t>(i);
				pkt.is_new = 1;
				memcpy(pkt.name, item->m_name, sizeof(pkt.name));
				pkt.count = item->m_instance.count;
				pkt.item_type = item->m_item_type;
				pkt.equip_pos = item->m_equip_pos;
				pkt.is_equipped = 0;
				pkt.level_limit = item->m_level_requirement;
				pkt.gender_limit = item->m_gender_requirement;
				pkt.cur_durability = item->m_instance.cur_durability;
				pkt.weight = item->m_weight;
				pkt.item_color = item->m_instance.item_color;
				pkt.item_effect_value2 = item->m_item_effect_value2;
				pkt.attributes = item->get_attributes();
				pkt.spec_effect_value2 = static_cast<uint8_t>(item->m_instance.special_effect_value2);
				pkt.item_id = item->m_id_num;
				pkt.max_durability = item->m_durability;
				ret = m_game->m_client_list[client_h]->m_socket->send_msg(reinterpret_cast<char*>(&pkt), sizeof(pkt));
			}
			switch (ret) {
			case sock::Event::QueueFull:
			case sock::Event::SocketError:
			case sock::Event::CriticalError:
			case sock::Event::SocketClosed:
				// . v1.41  .
				// m_game->delete_client(client_h, true, true);
				return true;
			}

			return true;
		}

	return false;
}

int ItemManager::calculate_use_skill_item_effect(int owner_h, char owner_type, char owner_skill, int skill_num, char map_index, int dX, int dY)
{
	CItem* item;
	char  item_name[hb::shared::limits::ItemNameLen];
	short lX, lY;
	int   result, fish;

	switch (owner_type) {
	case hb::shared::owner_class::Player:
		if (m_game->m_client_list[owner_h] == 0) return 0;
		if (m_game->m_client_list[owner_h]->m_map_index != map_index) return 0;
		lX = m_game->m_client_list[owner_h]->m_x;
		lY = m_game->m_client_list[owner_h]->m_y;
		break;

	case hb::shared::owner_class::Npc:
		if (m_game->m_npc_list[owner_h] == 0) return 0;
		if (m_game->m_npc_list[owner_h]->m_map_index != map_index) return 0;
		lX = m_game->m_npc_list[owner_h]->m_x;
		lY = m_game->m_npc_list[owner_h]->m_y;
		break;
	}

	if (owner_skill == 0) return 0;

	// 100       1D105
	result = m_game->dice(1, 105);
	if (owner_skill <= result)	return 0;

	if (m_game->m_map_list[map_index]->get_is_water(dX, dY) == false) return 0;

	if (owner_type == hb::shared::owner_class::Player)
		m_game->m_skill_manager->calculate_ssn_skill_index(owner_h, skill_num, 1);

	switch (m_game->m_skill_config_list[skill_num]->m_type) {
	case EffectType::Taming:
		// : dX, dY   .
		m_game->m_skill_manager->taming_handler(owner_h, skill_num, map_index, dX, dY);
		break;

	case EffectType::get:
		std::memset(item_name, 0, sizeof(item_name));
		bool is_fish = false;
		switch (m_game->m_skill_config_list[skill_num]->m_value_1) {
		case 1:
			std::snprintf(item_name, sizeof(item_name), "Meat");
			break;

		case 2:
			if (owner_type == hb::shared::owner_class::Player) {
				fish = m_game->m_fishing_manager->check_fish(owner_h, map_index, dX, dY);
				if (fish == 0) {
					std::snprintf(item_name, sizeof(item_name), "Fish");
					is_fish = true;
				}
			}
			else {
				std::snprintf(item_name, sizeof(item_name), "Fish");
				is_fish = true;
			}
			break;
		}

		if (strlen(item_name) != 0) {

			if (is_fish) {
				m_game->send_notify_msg(0, owner_h, Notify::FishSuccess, 0, 0, 0, 0);
				m_game->m_client_list[owner_h]->m_exp_stock += m_game->dice(1, 2);
			}

			// Butchered meat, or a caught fish. The old form leaked the item when
			// init_item_attr failed; the factory owns that failure now.
			//
			// The birth location is the tile it lands on, taken from the same
			// three values used to place it below — this venue is reached by an
			// NPC owner as well as a player, so an actor lookup would come back
			// empty exactly where the drop is most interesting.
			item = create_item(item_name, item_origin::harvest,
				hb::server::birth_context{
					.map = m_game->m_map_list[map_index]->m_name, .x = lX, .y = lY });
			if (item != nullptr) {
				m_game->m_map_list[map_index]->set_item(lX, lY, item);

				m_game->send_ground_item_event(CommonType::ItemDrop, map_index, lX, lY, item);
			}
		}
		break;
	}

	return 1;
}

void ItemManager::req_sell_item_handler(int client_h, char item_id, char sell_to_whom, int num, const char* item_name)
{
	short remain_life;
	int   price;
	double d1, d2, d3;
	bool   neutral;
	uint32_t  swe_type, swe_value, add_price1, add_price2, mul1, mul2;

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_is_init_complete == false) return;
	if ((item_id < 0) || (item_id >= 50)) return;
	if (m_game->m_client_list[client_h]->m_item_list[item_id] == 0) return;
	if (num <= 0) return;
	if (m_game->m_client_list[client_h]->m_item_list[item_id]->m_instance.count < static_cast<uint32_t>(num)) return;

	// Can't sell gold
	if (m_game->m_client_list[client_h]->m_item_list[item_id]->m_id_num == hb::shared::item::ItemId::Gold)
	{
		m_game->send_notify_msg(0, client_h, Notify::CannotSellItem, item_id, 1, 0, m_game->m_client_list[client_h]->m_item_list[item_id]->m_name);
		return;
	}

	m_game->calc_total_weight(client_h);

	// Gold's per-unit weight comes straight off its config row. This used to
	// allocate a throwaway CItem purely to measure it — harmless before, but
	// under the ledger a created item is an event, and a measuring stick is not
	// one. Nothing here enters the world, so nothing here should look like it.
	CItem* gold_config = m_game->m_item_config_list[hb::shared::item::ItemId::Gold];
	if (gold_config == nullptr) return;

	// v1.42
	neutral = false;
	if (memcmp(m_game->m_client_list[client_h]->m_location, "NONE", 4) == 0) neutral = true;
	switch (sell_to_whom) {
	case 15:
	case 24:
		if (m_game->m_client_list[client_h]->m_item_list[item_id]->m_sell_price == 0) {
			m_game->send_notify_msg(0, client_h, Notify::CannotSellItem, item_id, 1, 0, m_game->m_client_list[client_h]->m_item_list[item_id]->m_name);
			break;
		}

		if (m_game->m_client_list[client_h]->m_item_list[item_id]->m_durability > 0) {
			// Equipment with durability: price scaled by remaining durability
			remain_life = m_game->m_client_list[client_h]->m_item_list[item_id]->m_instance.cur_durability;

			if (remain_life == 0) {
				m_game->send_notify_msg(0, client_h, Notify::CannotSellItem, item_id, 2, 0, m_game->m_client_list[client_h]->m_item_list[item_id]->m_name);
				break;
			}

			d1 = (double)remain_life;
			if (m_game->m_client_list[client_h]->m_item_list[item_id]->m_durability != 0)
				d2 = (double)m_game->m_client_list[client_h]->m_item_list[item_id]->m_durability;
			else d2 = 1.0f;
			d3 = (d1 / d2) * 0.5f;
			d2 = (double)m_game->m_client_list[client_h]->m_item_list[item_id]->m_sell_price;
			d3 = d3 * d2;

			price = (int)d3;
			price = price * num;

			// Attribute bonus pricing for equipment with special attributes
			add_price1 = 0;
			add_price2 = 0;
			if (m_game->m_client_list[client_h]->m_item_list[item_id]->get_prefix_type() != hb::shared::item::modifier_id::empty) {
				swe_type = m_game->m_client_list[client_h]->m_item_list[item_id]->get_prefix_type();
				swe_value = m_game->m_client_list[client_h]->m_item_list[item_id]->get_prefix_value();

				switch (swe_type) {
				case modifier_id::light:     mul1 = 2; break;
				case modifier_id::strong:    mul1 = 2; break;
				case modifier_id::agile:     mul1 = 3; break;
				case modifier_id::critical:  mul1 = 4; break;
				case modifier_id::sharp:     mul1 = 5; break;
				case modifier_id::poisoning: mul1 = 6; break;
				case modifier_id::righteous: mul1 = 15; break;
				case modifier_id::ancient:   mul1 = 20; break;
				default: mul1 = 1; break;
				}

				d1 = (double)price * mul1;
				switch (swe_value) {
				case 1: d2 = 10.0f; break;
				case 2: d2 = 20.0f; break;
				case 3: d2 = 30.0f; break;
				case 4: d2 = 35.0f; break;
				case 5: d2 = 40.0f; break;
				case 6: d2 = 50.0f; break;
				case 7: d2 = 100.0f; break;
				case 8: d2 = 200.0f; break;
				case 9: d2 = 300.0f; break;
				case 10: d2 = 400.0f; break;
				case 11: d2 = 500.0f; break;
				case 12: d2 = 700.0f; break;
				case 13: d2 = 900.0f; break;
				default: d2 = 0.0f; break;
				}
				d3 = d1 * (d2 / 100.0f);

				add_price1 = (int)(d1 + d3);
			}

			if (m_game->m_client_list[client_h]->m_item_list[item_id]->get_secondary_type() != hb::shared::item::modifier_id::empty) {
				swe_type = m_game->m_client_list[client_h]->m_item_list[item_id]->get_secondary_type();
				swe_value = m_game->m_client_list[client_h]->m_item_list[item_id]->get_secondary_value();

				switch (swe_type) {
				case modifier_id::poison_resist:
				case modifier_id::gold: mul2 = 2; break;

				case modifier_id::hitting_probability:
				case modifier_id::defense_ratio:
				case modifier_id::hp_recovery:
				case modifier_id::sp_recovery:
				case modifier_id::mp_recovery:
				case modifier_id::magic_resist: mul2 = 4; break;

				case modifier_id::physical_absorb:
				case modifier_id::magic_absorb:
				case modifier_id::consecutive_attack:
				case modifier_id::experience: mul2 = 6; break;
				}

				d1 = (double)price * mul2;
				switch (swe_value) {
				case 1: d2 = 10.0f; break;
				case 2: d2 = 20.0f; break;
				case 3: d2 = 30.0f; break;
				case 4: d2 = 35.0f; break;
				case 5: d2 = 40.0f; break;
				case 6: d2 = 50.0f; break;
				case 7: d2 = 100.0f; break;
				case 8: d2 = 200.0f; break;
				case 9: d2 = 300.0f; break;
				case 10: d2 = 400.0f; break;
				case 11: d2 = 500.0f; break;
				case 12: d2 = 700.0f; break;
				case 13: d2 = 900.0f; break;
				default: d2 = 0.0f; break;
				}
				d3 = d1 * (d2 / 100.0f);

				add_price2 = (int)(d1 + d3);
			}

			price = price + (add_price1 - (add_price1 / 3)) + (add_price2 - (add_price2 / 3));

			if (neutral) price = price / 2;
			if (price <= 0) price = 1;
			if (price > 1000000) price = 1000000;

			if (m_game->m_client_list[client_h]->m_cur_weight_load + get_item_weight(gold_config, price) > m_game->calc_max_load(client_h)) {
				m_game->send_notify_msg(0, client_h, Notify::CannotSellItem, item_id, 4, 0, m_game->m_client_list[client_h]->m_item_list[item_id]->m_name);
			}
			else m_game->send_notify_msg(0, client_h, Notify::SellItemPrice, item_id, remain_life, price, m_game->m_client_list[client_h]->m_item_list[item_id]->m_name, num);
		}
		else {
			// Non-durability items: flat half-price
			price = m_game->m_client_list[client_h]->m_item_list[item_id]->m_sell_price / 2;
			price = price * num;

			if (neutral) price = price / 2;
			if (price <= 0) price = 1;
			if (price > 1000000) price = 1000000;

			if (m_game->m_client_list[client_h]->m_cur_weight_load + get_item_weight(gold_config, price) > m_game->calc_max_load(client_h)) {
				m_game->send_notify_msg(0, client_h, Notify::CannotSellItem, item_id, 4, 0, m_game->m_client_list[client_h]->m_item_list[item_id]->m_name);
			}
			else m_game->send_notify_msg(0, client_h, Notify::SellItemPrice, item_id, 0, price, m_game->m_client_list[client_h]->m_item_list[item_id]->m_name, num);
		}
		break;

	default:
		break;
	}
}

void ItemManager::req_sell_item_confirm_handler(int client_h, char item_id, int num, const char* string)
{
	CItem* item_gold;
	short remain_life;
	int   price;
	double d1, d2, d3;
	uint32_t mul1, mul2, swe_type, swe_value, add_price1, add_price2;
	int    erase_req, ret;
	bool   neutral;

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_is_init_complete == false) return;
	if ((item_id < 0) || (item_id >= 50)) return;
	if (m_game->m_client_list[client_h]->m_item_list[item_id] == 0) return;
	if (num <= 0) return;
	if (m_game->m_client_list[client_h]->m_item_list[item_id]->m_instance.count < static_cast<uint32_t>(num)) return;

	// Can't sell gold
	if (m_game->m_client_list[client_h]->m_item_list[item_id]->m_id_num == hb::shared::item::ItemId::Gold) return;

	// New 18/05/2004
	if (m_game->m_client_list[client_h]->m_is_processing_allowed == false) return;

	if (m_game->m_client_list[client_h]->m_item_list[item_id]->m_sell_price == 0) return;

	m_game->calc_total_weight(client_h);

	// v1.42
	neutral = false;
	if (memcmp(m_game->m_client_list[client_h]->m_location, "NONE", 4) == 0) neutral = true;

	price = 0;
	if (m_game->m_client_list[client_h]->m_item_list[item_id]->m_durability > 0) {
		// Equipment with durability: price scaled by remaining durability + attribute bonuses
		remain_life = m_game->m_client_list[client_h]->m_item_list[item_id]->m_instance.cur_durability;

		if (remain_life <= 0) {
			return;
		}
		else {
			d1 = (double)remain_life;
			if (m_game->m_client_list[client_h]->m_item_list[item_id]->m_durability != 0)
				d2 = (double)m_game->m_client_list[client_h]->m_item_list[item_id]->m_durability;
			else d2 = 1.0f;
			d3 = (d1 / d2) * 0.5f;
			d2 = (double)m_game->m_client_list[client_h]->m_item_list[item_id]->m_sell_price;
			d3 = d3 * d2;

			price = (short)d3;
			price = price * num;

			add_price1 = 0;
			add_price2 = 0;
			if (m_game->m_client_list[client_h]->m_item_list[item_id]->get_prefix_type() != hb::shared::item::modifier_id::empty) {
				swe_type = m_game->m_client_list[client_h]->m_item_list[item_id]->get_prefix_type();
				swe_value = m_game->m_client_list[client_h]->m_item_list[item_id]->get_prefix_value();

				switch (swe_type) {
				case modifier_id::light:     mul1 = 2; break;
				case modifier_id::strong:    mul1 = 2; break;
				case modifier_id::agile:     mul1 = 3; break;
				case modifier_id::critical:  mul1 = 4; break;
				case modifier_id::sharp:     mul1 = 5; break;
				case modifier_id::poisoning: mul1 = 6; break;
				case modifier_id::righteous: mul1 = 15; break;
				case modifier_id::ancient:   mul1 = 20; break;
				default: mul1 = 1; break;
				}

				d1 = (double)price * mul1;
				switch (swe_value) {
				case 1: d2 = 10.0f; break;
				case 2: d2 = 20.0f; break;
				case 3: d2 = 30.0f; break;
				case 4: d2 = 35.0f; break;
				case 5: d2 = 40.0f; break;
				case 6: d2 = 50.0f; break;
				case 7: d2 = 100.0f; break;
				case 8: d2 = 200.0f; break;
				case 9: d2 = 300.0f; break;
				case 10: d2 = 400.0f; break;
				case 11: d2 = 500.0f; break;
				case 12: d2 = 700.0f; break;
				case 13: d2 = 900.0f; break;
				default: d2 = 0.0f; break;
				}
				d3 = d1 * (d2 / 100.0f);
				add_price1 = (int)(d1 + d3);
			}

			if (m_game->m_client_list[client_h]->m_item_list[item_id]->get_secondary_type() != hb::shared::item::modifier_id::empty) {
				swe_type = m_game->m_client_list[client_h]->m_item_list[item_id]->get_secondary_type();
				swe_value = m_game->m_client_list[client_h]->m_item_list[item_id]->get_secondary_value();

				// (1),  (2),  (3), HP  (4), SP  (5)
				// MP  (6),  (7),   (8),   (9)
				// (10),   (11),  Gold(12)
				switch (swe_type) {
				case modifier_id::poison_resist:
				case modifier_id::gold: mul2 = 2; break;

				case modifier_id::hitting_probability:
				case modifier_id::defense_ratio:
				case modifier_id::hp_recovery:
				case modifier_id::sp_recovery:
				case modifier_id::mp_recovery:
				case modifier_id::magic_resist: mul2 = 4; break;

				case modifier_id::physical_absorb:
				case modifier_id::magic_absorb:
				case modifier_id::consecutive_attack:
				case modifier_id::experience: mul2 = 6; break;
				}

				d1 = (double)price * mul2;
				switch (swe_value) {
				case 1: d2 = 10.0f; break;
				case 2: d2 = 20.0f; break;
				case 3: d2 = 30.0f; break;
				case 4: d2 = 35.0f; break;
				case 5: d2 = 40.0f; break;
				case 6: d2 = 50.0f; break;
				case 7: d2 = 100.0f; break;
				case 8: d2 = 200.0f; break;
				case 9: d2 = 300.0f; break;
				case 10: d2 = 400.0f; break;
				case 11: d2 = 500.0f; break;
				case 12: d2 = 700.0f; break;
				case 13: d2 = 900.0f; break;
				default: d2 = 0.0f; break;
				}
				d3 = d1 * (d2 / 100.0f);
				add_price2 = (int)(d1 + d3);
			}

			price = price + (add_price1 - (add_price1 / 3)) + (add_price2 - (add_price2 / 3));

			if (neutral) price = price / 2;
			if (price <= 0) price = 1;
			if (price > 1000000) price = 1000000; // New 06/05/2004

			m_game->send_notify_msg(0, client_h, Notify::ItemSold, item_id, 0, 0, 0);

			// `num`, not the stack: the item handed over is the whole slot and the
			// sale takes part of it, so the Counted flow has to be told the amount
			// or selling 5 arrows out of 100 books a sale of 100 (#81). The
			// Instanced path below is unaffected — it sells one whole item.
			item_log(ItemLogAction::Sell, client_h, (int)-1, m_game->m_client_list[client_h]->m_item_list[item_id], false, num);

			if (m_game->m_client_list[client_h]->m_item_list[item_id]->is_stackable()) {
				// v1.41 !!!
				// flow_none: the Sell above carried `num`; a flow here would
				// count the sale twice.
				set_item_count(client_h, item_id, m_game->m_client_list[client_h]->m_item_list[item_id]->m_instance.count - num, flow_none);
			}
			else item_deplete_handler(client_h, item_id, false, destroy_reason::sold);
		}
	}
	else {
		// Non-durability items: flat half-price
		price = m_game->m_client_list[client_h]->m_item_list[item_id]->m_sell_price / 2;
		price = price * num;

		if (neutral) price = price / 2;
		if (price <= 0) price = 1;
		if (price > 1000000) price = 1000000;

		m_game->send_notify_msg(0, client_h, Notify::ItemSold, item_id, 0, 0, 0);

		// The sold amount, for the same reason as the durability branch above.
		item_log(ItemLogAction::Sell, client_h, (int)-1, m_game->m_client_list[client_h]->m_item_list[item_id], false, num);

		if (m_game->m_client_list[client_h]->m_item_list[item_id]->is_stackable()) {
			// flow_none: the Sell above carried `num`, as in the branch above.
			set_item_count(client_h, item_id, m_game->m_client_list[client_h]->m_item_list[item_id]->m_instance.count - num, flow_none);
		}
		else item_deplete_handler(client_h, item_id, false, destroy_reason::sold);
	}

	// Gold .    0     .
	if (price <= 0) return;

	// Sale proceeds. Gold is Counted: no Serial, and no origin either — an
	// origin is an item_instances column, and Counted items get flow rows keyed
	// by event instead of an instance row. The venue lives on the event.
	item_gold = create_item(hb::shared::item::ItemId::Gold, item_origin::none);
	if (item_gold == nullptr) return;
	item_gold->m_instance.count = price;

	// Gold minted into the economy. Recorded here rather than at the minting
	// funnel because the funnel cannot know the amount: every creation venue
	// sets m_instance.count *after* create_item returns, so a flow booked inside
	// stamp_provenance would be a stack of zero (#81). record_counted_flow
	// rather than item_log so the shop text channel does not gain a second
	// "Sell" line per sale describing the payout as an item the player sold.
	record_counted_flow(hb::server::ledger_event::created, *item_gold, price);

	if (add_client_item_list(client_h, item_gold, &erase_req)) {

		ret = send_item_notify_msg(client_h, Notify::ItemObtained, item_gold, 0);

		// Merged into gold the character already had; the husk is spent. The
		// original freed it here too (HB382_CENTUU Game.cpp:30900) — the port
		// dropped it, which leaked a CItem on every sale (#81).
		if (erase_req == 1) destroy_item(item_gold, destroy_reason::merged, client_h);

		m_game->calc_total_weight(client_h);

		switch (ret) {
		case sock::Event::QueueFull:
		case sock::Event::SocketError:
		case sock::Event::CriticalError:
		case sock::Event::SocketClosed:
			m_game->delete_client(client_h, true, true);
			break;
		}
	}
	else {
		m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->set_item(m_game->m_client_list[client_h]->m_x,
			m_game->m_client_list[client_h]->m_y, item_gold);

		m_game->send_ground_item_event(CommonType::ItemDrop, m_game->m_client_list[client_h]->m_map_index,
			m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y, item_gold);

		m_game->calc_total_weight(client_h);

		ret = send_item_notify_msg(client_h, Notify::CannotCarryMoreItem, 0, 0);

		switch (ret) {
		case sock::Event::QueueFull:
		case sock::Event::SocketError:
		case sock::Event::CriticalError:
		case sock::Event::SocketClosed:
			m_game->delete_client(client_h, true, true);
			return;
		}
	}
}

void ItemManager::req_repair_item_handler(int client_h, char item_id, char repair_whom, const char* string)
{
	int32_t remain_life, price;
	double d1, d2, d3;

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_is_init_complete == false) return;
	if ((item_id < 0) || (item_id >= 50)) return;
	if (m_game->m_client_list[client_h]->m_item_list[item_id] == 0) return;

	if (m_game->m_client_list[client_h]->m_item_list[item_id]->m_durability > 0) {

		if (repair_whom != 24) {
			m_game->send_notify_msg(0, client_h, Notify::CannotRepairItem, item_id, 2, 0, m_game->m_client_list[client_h]->m_item_list[item_id]->m_name);
			return;
		}

		remain_life = m_game->m_client_list[client_h]->m_item_list[item_id]->m_instance.cur_durability;
		if (remain_life == 0) {
			price = static_cast<short>(m_game->m_client_list[client_h]->m_item_list[item_id]->m_sell_price / 2);
		}
		else {
			d1 = (double)remain_life;
			if (m_game->m_client_list[client_h]->m_item_list[item_id]->m_durability != 0)
				d2 = (double)m_game->m_client_list[client_h]->m_item_list[item_id]->m_durability;
			else d2 = 1.0f;
			d3 = (d1 / d2) * 0.5f;
			d2 = (double)m_game->m_client_list[client_h]->m_item_list[item_id]->m_sell_price;
			d3 = d3 * d2;

			price = static_cast<short>((m_game->m_client_list[client_h]->m_item_list[item_id]->m_sell_price / 2) - d3);
		}

		m_game->send_notify_msg(0, client_h, Notify::RepairItemPrice, item_id, remain_life, price, m_game->m_client_list[client_h]->m_item_list[item_id]->m_name);
	}
	else {
		m_game->send_notify_msg(0, client_h, Notify::CannotRepairItem, item_id, 1, 0, m_game->m_client_list[client_h]->m_item_list[item_id]->m_name);
	}
}

void ItemManager::req_repair_item_cofirm_handler(int client_h, char item_id, const char* string)
{
	int32_t  remain_life, price;
	double   d1, d2, d3;
	uint64_t gold_count;
	int      ret, gold_weight;

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_is_init_complete == false) return;

	if ((item_id < 0) || (item_id >= 50)) return;
	if (m_game->m_client_list[client_h]->m_item_list[item_id] == 0) return;

	// New 18/05/2004
	if (m_game->m_client_list[client_h]->m_is_processing_allowed == false) return;

	if (m_game->m_client_list[client_h]->m_item_list[item_id]->m_durability > 0) {

		remain_life = m_game->m_client_list[client_h]->m_item_list[item_id]->m_instance.cur_durability;
		if (remain_life == 0) {
			price = static_cast<short>(m_game->m_client_list[client_h]->m_item_list[item_id]->m_sell_price / 2);
		}
		else {
			d1 = (double)abs(remain_life);
			if (m_game->m_client_list[client_h]->m_item_list[item_id]->m_durability != 0)
				d2 = (double)abs(m_game->m_client_list[client_h]->m_item_list[item_id]->m_durability);
			else d2 = 1.0f;
			d3 = (d1 / d2) * 0.5f;
			d2 = (double)m_game->m_client_list[client_h]->m_item_list[item_id]->m_sell_price;
			d3 = d3 * d2;

			price = static_cast<short>((m_game->m_client_list[client_h]->m_item_list[item_id]->m_sell_price / 2) - d3);
		}

		// price         .
		gold_count = get_item_count_by_id(client_h, hb::shared::item::ItemId::Gold);

		if (gold_count < static_cast<uint64_t>(price)) {
			// Gold     .   .
			{
				hb::net::PacketNotifyNotEnoughGold pkt{};
				pkt.header.msg_id = MsgId::Notify;
				pkt.header.msg_type = Notify::NotEnoughGold;
				pkt.item_index = static_cast<int8_t>(item_id);
				ret = m_game->m_client_list[client_h]->m_socket->send_msg(reinterpret_cast<char*>(&pkt), sizeof(pkt));
			}
			switch (ret) {
			case sock::Event::QueueFull:
			case sock::Event::SocketError:
			case sock::Event::CriticalError:
			case sock::Event::SocketClosed:
				m_game->delete_client(client_h, true, true);
				return;
			}
			return;
		}
		else {

			// . !BUG POINT  .      .
			m_game->m_client_list[client_h]->m_item_list[item_id]->m_instance.cur_durability = m_game->m_client_list[client_h]->m_item_list[item_id]->m_durability;
			m_game->send_notify_msg(0, client_h, Notify::ItemRepaired, item_id, m_game->m_client_list[client_h]->m_item_list[item_id]->m_instance.cur_durability, 0, 0);

			// The repair bill (#103). `Repair` had a text-sink switch arm and no
			// caller in the whole server before this, so this is the first
			// meaning ever stored against the number: gold that left a player to
			// an NPC smith. The repair itself stays unrecorded — it is a
			// durability change on an item that never changes hands.
			gold_weight = set_item_count_by_id(client_h, hb::shared::item::ItemId::Gold,
				gold_count - price, ItemLogAction::Repair);

			m_game->calc_total_weight(client_h);

			m_game->m_city_status[m_game->m_client_list[client_h]->m_side].funds += price;
		}
	}
	else {
	}
}

namespace {

// Per-recalc accumulation of every rolled modifier line across the equipped
// set (Item Tiers spec §6.5 Aggregate caps).
//
// Lines are summed here during the equipment walk and applied only afterwards,
// so the cap clamps the TOTAL of a modifier. Clamping the destination
// accumulator instead would also clamp angel, unique built-in and hero-armor
// bonuses, which §6.5 exempts. That exemption needs no roster check and no
// mode branch: only rolled modifier slots reach this struct, while every
// config-sourced bonus (add_effect cases, m_item_effect_value*) goes straight
// to its accumulator and is never counted or clamped here.
struct modifier_totals
{
	// Display units, keyed by unified modifier ID. The cap is not stored
	// alongside: the apply pass refetches the catalog row anyway, and one
	// source for aggregate_cap cannot drift from the other.
	int by_modifier[256] = {};

	// The ATTRIBUTES ladder caps per attribute, not per modifier row: a single,
	// a pair half and ALL STATS all feed the same +20 (spec §7, "ALL counts
	// into each"). Indexed by tier_attribute.
	int attribute[tier_attribute::charisma + 1] = {};
	int attribute_cap[tier_attribute::charisma + 1] = {};

	// Physical Absorb lands in a per-slot accumulator, so it sums per equip
	// position rather than set-wide. Its cap (spec §7) is the historical
	// per-hit-location clamp, and the authoritative one still runs at damage
	// time in CombatManager, which merges Leggings+Boots into one location.
	int physical_absorb_by_pos[hb::shared::item::DEF_MAXITEMEQUIPPOS] = {};
	int physical_absorb_cap = 0;

	// Set when the equipped weapon cannot attack (a bow with no arrows), so
	// weapon damage lines stay off the zeroed attack values.
	bool attack_disabled = false;

	// The held weapon's signature line — Critical / Poisoning / Righteous on
	// melee and bows, Spell Success on wands. Combat and magic read it through
	// m_special_weapon_effect_*; the DAMAGE and CASTING Buckets are exclusive,
	// so a weapon carries at most one.
	uint8_t signature_type = modifier_id::empty;
	uint8_t signature_value = 0;

	void add_attribute(int attr, int value, int row_cap)
	{
		if (attr <= tier_attribute::none || attr > tier_attribute::charisma) return;
		attribute[attr] += value;
		// Harsher wins when contributing rows disagree (ratchet law, §6.4).
		attribute_cap[attr] = (attribute_cap[attr] == 0 || row_cap < attribute_cap[attr])
			? row_cap : attribute_cap[attr];
	}

	static int clamped(int sum, int cap)
	{
		return (cap > 0 && sum > cap) ? cap : sum;
	}
};

// Records one equipped item's rolled lines. Slot position is meaningless
// (spec §4.2) — legacy items park two lines in [0]/[1], tiered items fill 1-4
// in roll order, and both are read the same way here.
void collect_item_modifiers(const hb::server::tier_config& config, const CItem& item,
	int equip_pos, modifier_totals& totals)
{
	// Only the held weapon publishes a signature line. Melee, bows and wands
	// all occupy these two slots; a shield is LeftHand and never does.
	const bool is_held_weapon = (equip_pos == to_int(EquipPos::RightHand))
		|| (equip_pos == to_int(EquipPos::TwoHand));

	for (const auto& mod : item.get_attributes().modifiers)
	{
		if (mod.type == modifier_id::empty) continue;

		const auto* row = config.find_modifier(mod.type);
		// An ID with no catalog row cannot be interpreted; the 2-D boot
		// validator refuses such a dataset, so this only guards live edits.
		if (row == nullptr) continue;

		const int value = mod.value * row->multiplier;

		switch (row->effect_id)
		{
		case effect_id::critical:
		case effect_id::poisoning:
		case effect_id::righteous:
		case effect_id::spell_success:
			// Consumed at their point of use (CombatManager hit resolution,
			// MagicManager cast), so they are published rather than summed.
			if (is_held_weapon)
			{
				totals.signature_type = mod.type;
				totals.signature_value = mod.value;
			}
			break;

		case effect_id::add_attribute:
			totals.add_attribute(row->effect_param1, value, row->aggregate_cap);
			break;

		case effect_id::add_attribute_pair:
			// Two independent rolls, one per named attribute (spec §4.4).
			totals.add_attribute(row->effect_param1, value, row->aggregate_cap);
			totals.add_attribute(row->effect_param2, mod.value2 * row->multiplier, row->aggregate_cap);
			break;

		case effect_id::add_all_attributes:
			// One shared N applied to every attribute (spec §4.4).
			for (int attr = tier_attribute::strength; attr <= tier_attribute::charisma; attr++)
				totals.add_attribute(attr, value, row->aggregate_cap);
			break;

		case effect_id::physical_absorb:
			if (equip_pos >= 0 && equip_pos < hb::shared::item::DEF_MAXITEMEQUIPPOS)
			{
				totals.physical_absorb_by_pos[equip_pos] += value;
				totals.physical_absorb_cap = row->aggregate_cap;
			}
			break;

		case effect_id::sunder:
		case effect_id::bleed:
		case effect_id::mp_drain:
		case effect_id::sp_drain:
		case effect_id::cast_time_reduction:
			// The weapon and wand Marquee lines act only through the piece they
			// are rolled on — a swing procs them, an off-hand slot never does.
			if (is_held_weapon) totals.by_modifier[mod.type] += value;
			break;

		case effect_id::move_speed:
			// Spec §5 clamps movement speed twice: 10% per item, 20% across the
			// set. This is the one modifier whose aggregate_cap exceeds its band,
			// so the per-item half needs saying — everywhere else the aggregate
			// clamp already implies it.
			totals.by_modifier[mod.type] += std::min(value, row->band_max);
			break;

		default:
			totals.by_modifier[mod.type] += value;
			break;
		}
	}
}

// Applies every collected line at its capped total. Dispatch is on the
// catalog's effect_id, never on the modifier ID, so a new catalog row that
// reuses an existing behavior needs no code change (spec §2, the code/data line).
void apply_modifier_totals(const hb::server::tier_config& config, CClient* client,
	const modifier_totals& totals)
{
	// The two speed lines land in bytes on the status broadcast, so they are
	// summed here in full width and clamped once at the end.
	int cast_reduction_pct = 0;
	int move_speed_pct = 0;

	for (int id = 1; id < 256; id++)
	{
		const int sum = totals.by_modifier[id];
		if (sum == 0) continue;

		const auto* row = config.find_modifier(static_cast<uint8_t>(id));
		if (row == nullptr) continue;

		const int value = modifier_totals::clamped(sum, row->aggregate_cap);

		switch (row->effect_id)
		{
		case effect_id::sharp:
		case effect_id::ancient:
			if (!totals.attack_disabled)
			{
				client->m_attack_bonus_sm += value;
				client->m_attack_bonus_l += value;
			}
			break;

		case effect_id::poison_resist:       client->m_add_poison_resistance += value; break;
		case effect_id::hitting_probability: client->m_add_attack_ratio += value; break;
		case effect_id::defense_ratio:       client->m_add_defense_ratio += value; break;
		case effect_id::hp_recovery:         client->m_add_hp += value; break;
		case effect_id::sp_recovery:         client->m_add_sp += value; break;
		case effect_id::mp_recovery:         client->m_add_mp += value; break;
		case effect_id::magic_resist:        client->m_add_magic_resistance += value; break;
		case effect_id::magic_absorb:        client->m_add_abs_magical_defense += value; break;
		case effect_id::consecutive_attack:  client->m_add_combo_damage += value; break;
		case effect_id::experience_bonus:    client->m_add_exp += value; break;
		case effect_id::gold_bonus:          client->m_add_gold += value; break;
		case effect_id::mana_converting:     client->m_add_trans_mana += value; break;
		case effect_id::crit_chance:         client->m_add_charge_critical += value; break;

		// Marquee (spec §5). The weapon exotics are read per landed hit in
		// CombatManager; the two speed lines ride the status broadcast so
		// remote clients can render true cast/walk/run timing (Cycle 4-C).
		case effect_id::sunder:              client->m_marquee_weapon.sunder_pct += value; break;
		case effect_id::bleed:               client->m_marquee_weapon.bleed_pct += value; break;
		case effect_id::mp_drain:            client->m_marquee_weapon.mp_drain += value; break;
		case effect_id::sp_drain:            client->m_marquee_weapon.sp_drain += value; break;
		case effect_id::cast_time_reduction: cast_reduction_pct += value; break;
		case effect_id::move_speed:          move_speed_pct += value; break;

		// Everything else is consumed where it acts rather than summed here:
		// agile / light / strong / ancient are derived item stats
		// (CItem::get_effective_*, ItemManager::apply_modifier_derived_stats)
		// and the signature lines ride m_special_weapon_effect_* (collected above).
		default: break;
		}
	}

	// A reduction at or past 100% would invert the cast floor and the client's
	// animation scale alike; the catalog caps these far below, so this only
	// guards a hand-edited dataset.
	client->m_status.cast_reduction_pct = static_cast<uint8_t>(std::clamp(cast_reduction_pct, 0, 99));
	client->m_status.move_speed_pct = static_cast<uint8_t>(std::clamp(move_speed_pct, 0, 99));

	for (int attr = tier_attribute::strength; attr <= tier_attribute::charisma; attr++)
		client->m_add_attribute[attr] +=
			modifier_totals::clamped(totals.attribute[attr], totals.attribute_cap[attr]);

	client->m_special_weapon_effect_type = totals.signature_type;
	client->m_special_weapon_effect_value = totals.signature_value;

	for (int pos = 0; pos < hb::shared::item::DEF_MAXITEMEQUIPPOS; pos++)
	{
		if (totals.physical_absorb_by_pos[pos] == 0) continue;
		client->m_damage_absorption_armor[pos] +=
			modifier_totals::clamped(totals.physical_absorb_by_pos[pos], totals.physical_absorb_cap);
	}
}

} // namespace

void ItemManager::calc_total_item_effect(int client_h, int equip_item_id, bool notify)
{
	short item_index;
	int arrow_index, prev_sa_type, temp;
	EquipPos equip_pos;
	double v1, v2, v3;

	if (m_game->m_client_list[client_h] == 0) return;

	if ((m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::RightHand)] != -1) &&
		(m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::TwoHand)] != -1)) {

		if (m_game->m_client_list[client_h]->m_item_list[m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::RightHand)]] != 0) {
			m_game->m_client_list[client_h]->m_is_item_equipped[m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::RightHand)]] = false;
			m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::RightHand)] = -1;
		}
	}

	m_game->m_client_list[client_h]->m_angelic_str = 0; // By Snoopy81
	m_game->m_client_list[client_h]->m_angelic_int = 0; // By Snoopy81
	m_game->m_client_list[client_h]->m_angelic_dex = 0; // By Snoopy81
	m_game->m_client_list[client_h]->m_angelic_mag = 0; // By Snoopy81
	m_game->m_status_effect_manager->set_angel_flag(client_h, hb::shared::owner_class::Player, 0, 0);

	// Snapshotted before the zeroing below so the recalc can tell whether the
	// equipped set actually changed anything the wearer is shown — the client is
	// only told when something moves, the same way the two speed bytes are
	// handled further down.
	//
	// The WHOLE packet payload, not a chosen subset of it. This used to snapshot
	// the six gear attributes alone, which was right when that was all the packet
	// carried and silently wrong the moment it grew to twenty-two combat totals:
	// a hit-ratio necklace, a damage ring and a defence necklace all move numbers
	// without touching an attribute, so equipping one changed the panel not at
	// all. Comparing the payload against itself is the only version of this test
	// that cannot fall behind the packet again.
	const hb::net::PacketNotifyDerivedStats prev_derived =
		m_game->build_derived_stats(*m_game->m_client_list[client_h]);

	for (int& gear_attribute : m_game->m_client_list[client_h]->m_add_attribute) gear_attribute = 0;

	// Marquee lines from the equipped set (spec §5). The two speed values also
	// travel to nearby clients, so their pre-walk values are kept to decide
	// whether this recalc owes a status rebroadcast; apply_modifier_totals
	// assigns them outright, so they need no zeroing here.
	m_game->m_client_list[client_h]->m_marquee_weapon.clear();
	const uint8_t prev_cast_reduction_pct = m_game->m_client_list[client_h]->m_status.cast_reduction_pct;
	const uint8_t prev_move_speed_pct = m_game->m_client_list[client_h]->m_status.move_speed_pct;

	// Every rolled modifier line on the equipped set lands here first and is
	// applied once the walk finishes, so aggregate caps clamp totals rather
	// than individual lines (spec §6.5).
	modifier_totals totals;

	m_game->m_client_list[client_h]->m_attack_dice_throw_sm = 0;
	m_game->m_client_list[client_h]->m_attack_dice_range_sm = 0;
	m_game->m_client_list[client_h]->m_attack_bonus_sm = 0;

	m_game->m_client_list[client_h]->m_attack_dice_throw_l = 0;
	m_game->m_client_list[client_h]->m_attack_dice_range_l = 0;
	m_game->m_client_list[client_h]->m_attack_bonus_l = 0;

	m_game->m_client_list[client_h]->m_hit_ratio = 0;
	m_game->m_client_list[client_h]->m_defense_ratio = m_game->m_client_list[client_h]->m_dex * 2;
	m_game->m_client_list[client_h]->m_damage_absorption_shield = 0;

	for(int i = 0; i < DEF_MAXITEMEQUIPPOS; i++) {
		m_game->m_client_list[client_h]->m_damage_absorption_armor[i] = 0;
	}

	m_game->m_client_list[client_h]->m_mana_save_ratio = 0;
	m_game->m_client_list[client_h]->m_add_resist_magic = 0;

	m_game->m_client_list[client_h]->m_add_physical_damage = 0;
	m_game->m_client_list[client_h]->m_add_magical_damage = 0;

	m_game->m_client_list[client_h]->m_is_lucky_effect = false;
	m_game->m_client_list[client_h]->m_magic_damage_save_item_index = -1;
	m_game->m_client_list[client_h]->m_side_effect_max_hp_down = 0;

	m_game->m_client_list[client_h]->m_add_abs_air = 0;
	m_game->m_client_list[client_h]->m_add_abs_earth = 0;
	m_game->m_client_list[client_h]->m_add_abs_fire = 0;
	m_game->m_client_list[client_h]->m_add_abs_water = 0;

	m_game->m_client_list[client_h]->m_custom_item_value_attack = 0;
	m_game->m_client_list[client_h]->m_custom_item_value_defense = 0;

	m_game->m_client_list[client_h]->m_min_attack_power_sm = 0;
	m_game->m_client_list[client_h]->m_min_attack_power_l = 0;

	m_game->m_client_list[client_h]->m_max_attack_power_sm = 0;
	m_game->m_client_list[client_h]->m_max_attack_power_l = 0;

	m_game->m_client_list[client_h]->m_special_weapon_effect_type = 0;	// : 0-None 1- 2- 3- 4-
	m_game->m_client_list[client_h]->m_special_weapon_effect_value = 0;

	m_game->m_client_list[client_h]->m_add_hp = m_game->m_client_list[client_h]->m_add_sp = m_game->m_client_list[client_h]->m_add_mp = 0;
	m_game->m_client_list[client_h]->m_add_attack_ratio = m_game->m_client_list[client_h]->m_add_poison_resistance = m_game->m_client_list[client_h]->m_add_defense_ratio = 0;
	m_game->m_client_list[client_h]->m_add_magic_resistance = m_game->m_client_list[client_h]->m_add_abs_physical_defense = m_game->m_client_list[client_h]->m_add_abs_magical_defense = 0;
	m_game->m_client_list[client_h]->m_add_combo_damage = m_game->m_client_list[client_h]->m_add_exp = m_game->m_client_list[client_h]->m_add_gold = 0;

	prev_sa_type = m_game->m_client_list[client_h]->m_special_ability_type;

	m_game->m_client_list[client_h]->m_special_ability_type = 0;
	m_game->m_client_list[client_h]->m_special_ability_last_sec = 0;
	m_game->m_client_list[client_h]->m_special_ability_equip_pos = 0;

	m_game->m_client_list[client_h]->m_add_trans_mana = 0;
	m_game->m_client_list[client_h]->m_add_charge_critical = 0;

	m_game->m_client_list[client_h]->m_alter_item_drop_index = -1;
	for (item_index = 0; item_index < hb::shared::limits::MaxItems; item_index++)
	{
		if (m_game->m_client_list[client_h]->m_item_list[item_index] != 0) {
			switch (m_game->m_client_list[client_h]->m_item_list[item_index]->get_item_effect_type()) {
			case ItemEffectType::AlterItemDrop:
				if (m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.cur_durability > 0) {
					m_game->m_client_list[client_h]->m_alter_item_drop_index = item_index;
				}
				break;
			}
		}
	}

	for (item_index = 0; item_index < hb::shared::limits::MaxItems; item_index++)
	{
		if ((m_game->m_client_list[client_h]->m_item_list[item_index] != 0) &&
			(m_game->m_client_list[client_h]->m_is_item_equipped[item_index])) {

			equip_pos = m_game->m_client_list[client_h]->m_item_list[item_index]->get_equip_pos();

			// Rolled lines are collected for every equipped piece regardless of
			// its base effect type — modifiers are item-instance data, not a
			// property of the item's role.
			collect_item_modifiers(m_game->get_tier_config(),
				*m_game->m_client_list[client_h]->m_item_list[item_index],
				m_game->m_client_list[client_h]->m_item_list[item_index]->m_equip_pos, totals);

			switch (m_game->m_client_list[client_h]->m_item_list[item_index]->get_item_effect_type()) {

			case ItemEffectType::MagicDamageSave:
				m_game->m_client_list[client_h]->m_magic_damage_save_item_index = item_index;
				break;

			case ItemEffectType::AttackSpecAbility:
			case ItemEffectType::AttackDefense:
			case ItemEffectType::AttackManaSave:
			case ItemEffectType::AttackMaxHPDown:
			case ItemEffectType::Attack:
				m_game->m_client_list[client_h]->m_attack_dice_throw_sm = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1;
				m_game->m_client_list[client_h]->m_attack_dice_range_sm = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2;
				m_game->m_client_list[client_h]->m_attack_bonus_sm = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value3;
				m_game->m_client_list[client_h]->m_attack_dice_throw_l = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value4;
				m_game->m_client_list[client_h]->m_attack_dice_range_l = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value5;
				m_game->m_client_list[client_h]->m_attack_bonus_l = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value6;

				temp = m_game->m_client_list[client_h]->m_item_list[item_index]->get_enchant_bonus();
				//testcode
				//std::snprintf(G_cTxt, sizeof(G_cTxt), "Add Damage: %d", temp);
				//PutLogList(G_cTxt);

				m_game->m_client_list[client_h]->m_add_physical_damage += temp;
				m_game->m_client_list[client_h]->m_add_magical_damage += temp;

				m_game->m_client_list[client_h]->m_hit_ratio += m_game->m_client_list[client_h]->m_skill_mastery[m_game->m_client_list[client_h]->m_item_list[item_index]->m_related_skill];

				//m_game->m_client_list[client_h]->m_iHitRatio_ItemEffect_SM += m_game->m_client_list[client_h]->m_item_list[item_index]->m_sSM_HitRatio;
				//m_game->m_client_list[client_h]->m_iHitRatio_ItemEffect_L  += m_game->m_client_list[client_h]->m_item_list[item_index]->m_sL_HitRatio;
				m_game->m_client_list[client_h]->m_using_weapon_skill = m_game->m_client_list[client_h]->m_item_list[item_index]->m_related_skill;

				if (m_game->m_client_list[client_h]->m_item_list[item_index]->is_custom_made()) {
					m_game->m_client_list[client_h]->m_custom_item_value_attack += m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value2;
					if (m_game->m_client_list[client_h]->m_custom_item_value_attack > 100)
						m_game->m_client_list[client_h]->m_custom_item_value_attack = 100;

					if (m_game->m_client_list[client_h]->m_custom_item_value_attack < -100)
						m_game->m_client_list[client_h]->m_custom_item_value_attack = -100;

					if (m_game->m_client_list[client_h]->m_custom_item_value_attack > 0) {
						v2 = (double)m_game->m_client_list[client_h]->m_custom_item_value_attack;
						v1 = (v2 / 100.0f) * (5.0f);
						m_game->m_client_list[client_h]->m_min_attack_power_sm = m_game->m_client_list[client_h]->m_attack_dice_throw_sm +
							m_game->m_client_list[client_h]->m_attack_bonus_sm + (int)v1;

						m_game->m_client_list[client_h]->m_min_attack_power_l = m_game->m_client_list[client_h]->m_attack_dice_throw_l +
							m_game->m_client_list[client_h]->m_attack_bonus_l + (int)v1;

						if (m_game->m_client_list[client_h]->m_min_attack_power_sm < 1) m_game->m_client_list[client_h]->m_min_attack_power_sm = 1;
						if (m_game->m_client_list[client_h]->m_min_attack_power_l < 1)  m_game->m_client_list[client_h]->m_min_attack_power_l = 1;

						if (m_game->m_client_list[client_h]->m_min_attack_power_sm > (m_game->m_client_list[client_h]->m_attack_dice_throw_sm * m_game->m_client_list[client_h]->m_attack_dice_range_sm + m_game->m_client_list[client_h]->m_attack_bonus_sm))
							m_game->m_client_list[client_h]->m_min_attack_power_sm = (m_game->m_client_list[client_h]->m_attack_dice_throw_sm * m_game->m_client_list[client_h]->m_attack_dice_range_sm + m_game->m_client_list[client_h]->m_attack_bonus_sm);

						if (m_game->m_client_list[client_h]->m_min_attack_power_l > (m_game->m_client_list[client_h]->m_attack_dice_throw_l * m_game->m_client_list[client_h]->m_attack_dice_range_l + m_game->m_client_list[client_h]->m_attack_bonus_l))
							m_game->m_client_list[client_h]->m_min_attack_power_l = (m_game->m_client_list[client_h]->m_attack_dice_throw_l * m_game->m_client_list[client_h]->m_attack_dice_range_l + m_game->m_client_list[client_h]->m_attack_bonus_l);

						//testcode
						//std::snprintf(G_cTxt, sizeof(G_cTxt), "MinAP: %d %d +(%d)", m_game->m_client_list[client_h]->m_min_attack_power_sm, m_game->m_client_list[client_h]->m_min_attack_power_l, (int)v1);
						//PutLogList(G_cTxt);
					}
					else if (m_game->m_client_list[client_h]->m_custom_item_value_attack < 0) {
						v2 = (double)m_game->m_client_list[client_h]->m_custom_item_value_attack;
						v1 = (v2 / 100.0f) * (5.0f);
						m_game->m_client_list[client_h]->m_max_attack_power_sm = m_game->m_client_list[client_h]->m_attack_dice_throw_sm * m_game->m_client_list[client_h]->m_attack_dice_range_sm
							+ m_game->m_client_list[client_h]->m_attack_bonus_sm + (int)v1;

						m_game->m_client_list[client_h]->m_max_attack_power_l = m_game->m_client_list[client_h]->m_attack_dice_throw_l * m_game->m_client_list[client_h]->m_attack_dice_range_l
							+ m_game->m_client_list[client_h]->m_attack_bonus_l + (int)v1;

						if (m_game->m_client_list[client_h]->m_max_attack_power_sm < 1) m_game->m_client_list[client_h]->m_max_attack_power_sm = 1;
						if (m_game->m_client_list[client_h]->m_max_attack_power_l < 1)  m_game->m_client_list[client_h]->m_max_attack_power_l = 1;

						if (m_game->m_client_list[client_h]->m_max_attack_power_sm < (m_game->m_client_list[client_h]->m_attack_dice_throw_sm * m_game->m_client_list[client_h]->m_attack_dice_range_sm + m_game->m_client_list[client_h]->m_attack_bonus_sm))
							m_game->m_client_list[client_h]->m_max_attack_power_sm = (m_game->m_client_list[client_h]->m_attack_dice_throw_sm * m_game->m_client_list[client_h]->m_attack_dice_range_sm + m_game->m_client_list[client_h]->m_attack_bonus_sm);

						if (m_game->m_client_list[client_h]->m_max_attack_power_l < (m_game->m_client_list[client_h]->m_attack_dice_throw_l * m_game->m_client_list[client_h]->m_attack_dice_range_l + m_game->m_client_list[client_h]->m_attack_bonus_l))
							m_game->m_client_list[client_h]->m_max_attack_power_l = (m_game->m_client_list[client_h]->m_attack_dice_throw_l * m_game->m_client_list[client_h]->m_attack_dice_range_l + m_game->m_client_list[client_h]->m_attack_bonus_l);

						//testcode
						//std::snprintf(G_cTxt, sizeof(G_cTxt), "MaxAP: %d %d +(%d)", m_game->m_client_list[client_h]->m_max_attack_power_sm, m_game->m_client_list[client_h]->m_max_attack_power_l, (int)v1);
						//PutLogList(G_cTxt);
					}
				}

				// The weapon's signature line (Critical / Poisoning / Righteous
				// on melee and bows, Spell Success on wands) drives combat and
				// magic through m_special_weapon_effect_*. The DAMAGE and
				// CASTING Buckets are exclusive, so an item carries at most one.
				for (const auto& mod : m_game->m_client_list[client_h]->m_item_list[item_index]->get_attributes().modifiers) {
					if (mod.type == modifier_id::empty) continue;
					const auto* row = m_game->get_tier_config().find_modifier(mod.type);
					if (row == nullptr) continue;

					switch (row->effect_id) {
					case effect_id::critical:
					case effect_id::poisoning:
					case effect_id::righteous:
					case effect_id::spell_success:
						m_game->m_client_list[client_h]->m_special_weapon_effect_type = (int)mod.type;
						m_game->m_client_list[client_h]->m_special_weapon_effect_value = (int)mod.value;
						break;
					default: break;
					}
				}

				switch (m_game->m_client_list[client_h]->m_item_list[item_index]->get_item_effect_type()) {
				case ItemEffectType::AttackMaxHPDown:
					m_game->m_client_list[client_h]->m_side_effect_max_hp_down = m_game->m_client_list[client_h]->m_item_list[item_index]->m_special_effect;
					break;

				case ItemEffectType::AttackManaSave:
					// :    80%
					m_game->m_client_list[client_h]->m_mana_save_ratio += m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value4;
					if (m_game->m_client_list[client_h]->m_mana_save_ratio > 80) m_game->m_client_list[client_h]->m_mana_save_ratio = 80;
					break;

				case ItemEffectType::AttackDefense:
					m_game->m_client_list[client_h]->m_damage_absorption_armor[to_int(EquipPos::Body)] += m_game->m_client_list[client_h]->m_item_list[item_index]->m_special_effect;
					break;

				case ItemEffectType::AttackSpecAbility:
					m_game->m_client_list[client_h]->m_special_ability_type = m_game->m_client_list[client_h]->m_item_list[item_index]->m_special_effect;
					m_game->m_client_list[client_h]->m_special_ability_last_sec = m_game->m_client_list[client_h]->m_item_list[item_index]->m_special_effect_value1;
					m_game->m_client_list[client_h]->m_special_ability_equip_pos = to_int(equip_pos);

					if ((notify) && (equip_item_id == (int)item_index))
						m_game->send_notify_msg(0, client_h, Notify::SpecialAbilityStatus, 2, m_game->m_client_list[client_h]->m_special_ability_type, m_game->m_client_list[client_h]->m_special_ability_time, 0);
					break;
				}
				break;

			case ItemEffectType::add_effect:
				switch (m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1) {
				case 1:
					m_game->m_client_list[client_h]->m_add_resist_magic += m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2;
					break;

				case 2:
					m_game->m_client_list[client_h]->m_mana_save_ratio += m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2;
					if (m_game->m_client_list[client_h]->m_mana_save_ratio > 80) m_game->m_client_list[client_h]->m_mana_save_ratio = 80;
					break;

				case 3:
					m_game->m_client_list[client_h]->m_add_physical_damage += m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2;
					break;

				case 4:
					m_game->m_client_list[client_h]->m_defense_ratio += m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2;
					break;

				case 5:
					if (m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2 != 0)
						m_game->m_client_list[client_h]->m_is_lucky_effect = true;
					else m_game->m_client_list[client_h]->m_is_lucky_effect = false;
					break;

				case 6:
					m_game->m_client_list[client_h]->m_add_magical_damage += m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2;
					break;

				case 7:
					m_game->m_client_list[client_h]->m_add_abs_air += m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2;
					break;

				case 8:
					m_game->m_client_list[client_h]->m_add_abs_earth += m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2;
					break;

				case 9:
					m_game->m_client_list[client_h]->m_add_abs_fire += m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2;
					break;

				case 10:
					// . (2  )
					m_game->m_client_list[client_h]->m_add_abs_water += m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2;
					break;

				case 11:
					m_game->m_client_list[client_h]->m_add_poison_resistance += m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2;
					break;

				case 12:
					m_game->m_client_list[client_h]->m_hit_ratio += m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2;
					break;

				case 13: // Magin Ruby		Characters Hp recovery rate(% applied) added by the purity formula.
					m_game->m_client_list[client_h]->m_add_hp += (m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value2 / 5);
					break;

				case 14: // Magin Diamond	Attack probability(physical&magic) added by the purity formula.
					m_game->m_client_list[client_h]->m_add_attack_ratio += (m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value2 / 5);
					break;

				case 15: // Magin Emerald	Magical damage decreased(% applied) by the purity formula.	
					m_game->m_client_list[client_h]->m_add_abs_magical_defense += (m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value2 / 10);
					if (m_game->m_client_list[client_h]->m_add_abs_magical_defense > 80) m_game->m_client_list[client_h]->m_add_abs_magical_defense = 80;
					break;

				case 30: // Magin Sapphire	Phisical damage decreased(% applied) by the purity formula.	
					temp = (m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value2 / 10);
					m_game->m_client_list[client_h]->m_damage_absorption_armor[to_int(EquipPos::Head)] += temp;
					m_game->m_client_list[client_h]->m_damage_absorption_armor[to_int(EquipPos::Body)] += temp;
					m_game->m_client_list[client_h]->m_damage_absorption_armor[to_int(EquipPos::Arms)] += temp;
					m_game->m_client_list[client_h]->m_damage_absorption_armor[to_int(EquipPos::Leggings)] += temp;
					break;

					/*Functions rates confirm.
					Magic Diamond: Completion rate / 5 = Functions rate. ? Maximum 20. (not%)
					Magic Ruby: Completion rate / 5 = Functions rate.(%) ? Maximum 20%.
					Magic Emerald: Completion rate / 10 = Functions rate.(%) ? Maximum 10%.
					Magic Sapphire: Completion rate / 10 = Functions rate.(%) ? Maximum 10%.*/

					// ******* Angel Code - Begin ******* //			
				case 16: // Angel STR//AngelicPendant(STR)
					temp = m_game->m_client_list[client_h]->m_item_list[item_index]->get_enchant_bonus();
					m_game->m_client_list[client_h]->m_angelic_str = temp + 1;
					m_game->m_status_effect_manager->set_angel_flag(client_h, hb::shared::owner_class::Player, 1, temp);
					m_game->send_notify_msg(0, client_h, Notify::SettingSuccess, 0, 0, 0, 0);
					break;
				case 17: // Angel DEX //AngelicPendant(DEX)
					temp = m_game->m_client_list[client_h]->m_item_list[item_index]->get_enchant_bonus();
					m_game->m_client_list[client_h]->m_angelic_dex = temp + 1;
					m_game->m_status_effect_manager->set_angel_flag(client_h, hb::shared::owner_class::Player, 2, temp);
					m_game->send_notify_msg(0, client_h, Notify::SettingSuccess, 0, 0, 0, 0);
					break;
				case 18: // Angel INT//AngelicPendant(INT)
					temp = m_game->m_client_list[client_h]->m_item_list[item_index]->get_enchant_bonus();
					m_game->m_client_list[client_h]->m_angelic_int = temp + 1;
					m_game->m_status_effect_manager->set_angel_flag(client_h, hb::shared::owner_class::Player, 3, temp);
					m_game->send_notify_msg(0, client_h, Notify::SettingSuccess, 0, 0, 0, 0);
					break;
				case 19: // Angel MAG//AngelicPendant(MAG)
					temp = m_game->m_client_list[client_h]->m_item_list[item_index]->get_enchant_bonus();
					m_game->m_client_list[client_h]->m_angelic_mag = temp + 1;
					m_game->m_status_effect_manager->set_angel_flag(client_h, hb::shared::owner_class::Player, 4, temp);
					m_game->send_notify_msg(0, client_h, Notify::SettingSuccess, 0, 0, 0, 0);
					break;

				}
				break;

			case ItemEffectType::AttackArrow:
				if ((m_game->m_client_list[client_h]->m_arrow_index != -1) &&
					(m_game->m_client_list[client_h]->m_item_list[m_game->m_client_list[client_h]->m_arrow_index] == 0)) {
					// ArrowIndex  . ( )
					m_game->m_client_list[client_h]->m_arrow_index = get_arrow_item_index(client_h);
				}
				else if (m_game->m_client_list[client_h]->m_arrow_index == -1)
					m_game->m_client_list[client_h]->m_arrow_index = get_arrow_item_index(client_h);

				if (m_game->m_client_list[client_h]->m_arrow_index == -1) {
					m_game->m_client_list[client_h]->m_attack_dice_throw_sm = 0;
					m_game->m_client_list[client_h]->m_attack_dice_range_sm = 0;
					m_game->m_client_list[client_h]->m_attack_bonus_sm = 0;
					m_game->m_client_list[client_h]->m_attack_dice_throw_l = 0;
					m_game->m_client_list[client_h]->m_attack_dice_range_l = 0;
					m_game->m_client_list[client_h]->m_attack_bonus_l = 0;
					// Bows became full tier-system members (spec §4.8), so Sharp
					// now reaches this branch — keep a quiverless bow at zero
					// damage instead of letting the bonus stand alone.
					totals.attack_disabled = true;
				}
				else {
					arrow_index = m_game->m_client_list[client_h]->m_arrow_index;
					m_game->m_client_list[client_h]->m_attack_dice_throw_sm = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1;
					m_game->m_client_list[client_h]->m_attack_dice_range_sm = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2;
					m_game->m_client_list[client_h]->m_attack_bonus_sm = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value3;
					m_game->m_client_list[client_h]->m_attack_dice_throw_l = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value4;
					m_game->m_client_list[client_h]->m_attack_dice_range_l = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value5;
					m_game->m_client_list[client_h]->m_attack_bonus_l = m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value6;
				}

				m_game->m_client_list[client_h]->m_hit_ratio += m_game->m_client_list[client_h]->m_skill_mastery[m_game->m_client_list[client_h]->m_item_list[item_index]->m_related_skill];
				break;

			case ItemEffectType::DefenseSpecAbility:
			case ItemEffectType::Defense:
				m_game->m_client_list[client_h]->m_defense_ratio += m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1;

				if (m_game->m_client_list[client_h]->m_item_list[item_index]->is_custom_made()) {
					m_game->m_client_list[client_h]->m_custom_item_value_defense += m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value2;

					v2 = (double)m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.special_effect_value2;
					v3 = (double)m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1;
					v1 = (double)(v2 / 100.0f) * v3;

					v1 = v1 / 2.0f;
					m_game->m_client_list[client_h]->m_defense_ratio += (int)v1;
					if (m_game->m_client_list[client_h]->m_defense_ratio <= 0) m_game->m_client_list[client_h]->m_defense_ratio = 1;

					//testcode
					//std::snprintf(G_cTxt, sizeof(G_cTxt), "Custom-Defense: %d", (int)v1);
					//PutLogList(G_cTxt);
				}

				switch (equip_pos) {
				case EquipPos::LeftHand:
					// .  70%
					m_game->m_client_list[client_h]->m_damage_absorption_shield = (m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1) - (m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1) / 3;
					break;
				default:
					// .  70%  <- v1.43 100% . V2!
					m_game->m_client_list[client_h]->m_damage_absorption_armor[m_game->m_client_list[client_h]->m_item_list[item_index]->m_equip_pos] += (m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2);
					break;
				}

				switch (m_game->m_client_list[client_h]->m_item_list[item_index]->get_item_effect_type()) {
				case ItemEffectType::DefenseSpecAbility:
					m_game->m_client_list[client_h]->m_special_ability_type = m_game->m_client_list[client_h]->m_item_list[item_index]->m_special_effect;
					m_game->m_client_list[client_h]->m_special_ability_last_sec = m_game->m_client_list[client_h]->m_item_list[item_index]->m_special_effect_value1;
					m_game->m_client_list[client_h]->m_special_ability_equip_pos = to_int(equip_pos);

					if ((notify) && (equip_item_id == (int)item_index))
						m_game->send_notify_msg(0, client_h, Notify::SpecialAbilityStatus, 2, m_game->m_client_list[client_h]->m_special_ability_type, m_game->m_client_list[client_h]->m_special_ability_time, 0);
					break;
				}
				break;
			}
		}
	}

	// Every rolled line at its capped total (spec §6.5). Runs after the walk so
	// the caps see the whole equipped set, and after the base-item values the
	// walk assigned, which the damage lines add on top of.
	apply_modifier_totals(m_game->get_tier_config(), m_game->m_client_list[client_h], totals);

	// Remote clients animate cast and locomotion from these two bytes, so a
	// change has to reach them the way every other status change does. Pre-init
	// recalcs are skipped — the login handshake already sends the full status.
	if (m_game->m_client_list[client_h]->m_is_init_complete &&
		((m_game->m_client_list[client_h]->m_status.cast_reduction_pct != prev_cast_reduction_pct) ||
		 (m_game->m_client_list[client_h]->m_status.move_speed_pct != prev_move_speed_pct)))
	{
		m_game->send_event_to_near_client_type_a(client_h, hb::shared::owner_class::Player,
			MsgId::EventMotion, Type::NullAction, 0, 0, 0);
	}

	// These totals are private to the wearer, so unlike the speed bytes this goes
	// to the owner alone. Sent from here because this is the one place they are
	// recomputed — level-up is not the only thing that moves them, which is
	// exactly why the character screen used to show nothing on equip.
	// Pre-init recalcs are skipped; the login path sends the first one.
	//
	// Compared byte for byte: the payload is packed on both compilers, both
	// copies are value-initialized, and build_derived_stats leaves the header
	// blank in each — so nothing but the stats themselves is under comparison.
	if (m_game->m_client_list[client_h]->m_is_init_complete)
	{
		const hb::net::PacketNotifyDerivedStats now =
			m_game->build_derived_stats(*m_game->m_client_list[client_h]);

		if (std::memcmp(&now, &prev_derived, sizeof(now)) != 0)
			m_game->send_notify_msg(0, client_h, Notify::DerivedStats, 0, 0, 0, 0);
	}

	// Combined ceiling, preserved from pre-3-C behavior. The catalog's
	// aggregate_cap already bounds the rolled lines at 80; this bounds rolled
	// PLUS exempt sources (Magin Emerald and kin), because the value is a
	// percentage of magic damage and must never approach immunity. It is a
	// hard property of the damage formula, not a balance knob — hence code.
	if (m_game->m_client_list[client_h]->m_add_abs_magical_defense > 80)
		m_game->m_client_list[client_h]->m_add_abs_magical_defense = 80;

	// Snoopy: Bonus for Angels — and the gear DEX lines, which ride the same
	// x2 the base defense ratio is built from at the top of this function.
	m_game->m_client_list[client_h]->m_defense_ratio += m_game->m_client_list[client_h]->m_angelic_dex * 2;
	m_game->m_client_list[client_h]->m_defense_ratio +=
		m_game->m_client_list[client_h]->m_add_attribute[tier_attribute::dexterity] * 2;
	if (m_game->m_client_list[client_h]->m_hp > m_game->get_max_hp(client_h)) m_game->m_client_list[client_h]->m_hp = m_game->get_max_hp(client_h);
	if (m_game->m_client_list[client_h]->m_mp > m_game->get_max_mp(client_h)) m_game->m_client_list[client_h]->m_mp = m_game->get_max_mp(client_h);
	if (m_game->m_client_list[client_h]->m_sp > m_game->get_max_sp(client_h)) m_game->m_client_list[client_h]->m_sp = m_game->get_max_sp(client_h);
	m_game->send_notify_msg(0, client_h, Notify::Sp, 0, 0, 0, 0);

	//v1.432
	if ((prev_sa_type != 0) && (m_game->m_client_list[client_h]->m_special_ability_type == 0) && (notify)) {
		m_game->send_notify_msg(0, client_h, Notify::SpecialAbilityStatus, 4, 0, 0, 0);
		if (m_game->m_client_list[client_h]->m_is_special_ability_enabled) {
			m_game->m_client_list[client_h]->m_is_special_ability_enabled = false;
			m_game->m_client_list[client_h]->m_special_ability_time = SpecialAbilityTimeSec;
			m_game->m_client_list[client_h]->m_appearance.effect_type = 0;
			m_game->send_event_to_near_client_type_a(client_h, hb::shared::owner_class::Player, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		}
	}

	if ((prev_sa_type != 0) && (m_game->m_client_list[client_h]->m_special_ability_type != 0) &&
		(prev_sa_type != m_game->m_client_list[client_h]->m_special_ability_type) && (notify)) {
		if (m_game->m_client_list[client_h]->m_is_special_ability_enabled) {
			m_game->send_notify_msg(0, client_h , Notify::SpecialAbilityStatus, 3, 0, 0, 0);
			m_game->m_client_list[client_h]->m_is_special_ability_enabled = false;
			m_game->m_client_list[client_h]->m_special_ability_time = SpecialAbilityTimeSec;
			m_game->m_client_list[client_h]->m_appearance.effect_type = 0;
			m_game->send_event_to_near_client_type_a(client_h, hb::shared::owner_class::Player, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		}
	}
}

bool ItemManager::deplete_dest_type_item_use_effect(int client_h, int dX, int dY, short item_index, short dest_item_id)
{
	int ret;

	if (m_game->m_client_list[client_h] == 0) return false;
	if ((item_index < 0) || (item_index >= hb::shared::limits::MaxItems)) return false;
	if (m_game->m_client_list[client_h]->m_item_list[item_index] == 0) return false;

	switch (m_game->m_client_list[client_h]->m_item_list[item_index]->get_item_effect_type()) {
	case ItemEffectType::OccupyFlag:
		ret = m_game->m_war_manager->set_occupy_flag(m_game->m_client_list[client_h]->m_map_index, dX, dY,
			m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1,
			0, client_h);
		if (ret) {
			m_game->get_exp(client_h, (m_game->dice(m_game->m_client_list[client_h]->m_level, 10)));
		}
		else {
			m_game->send_notify_msg(0, client_h, Notify::NotFlagSpot, 0, 0, 0, 0);
		}
		return ret;

		// crusade
	case ItemEffectType::ConstructionKit:
		// .   . m_item_effect_value1:  , m_item_effect_value2:
		ret = m_game->m_war_manager->set_construction_kit(m_game->m_client_list[client_h]->m_map_index, dX, dY,
			m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1,
			m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2,
			client_h);
		if (ret) {
		}
		else {
		}
		return ret;

	case ItemEffectType::Dye:
		if ((dest_item_id >= 0) && (dest_item_id < hb::shared::limits::MaxItems)) {
			if (m_game->m_client_list[client_h]->m_item_list[dest_item_id] != 0) {
				auto* dest = m_game->m_client_list[client_h]->m_item_list[dest_item_id];
				bool is_dye_removal = (m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1 == 0);
				if (dest->m_is_dyeable != 0
					&& (is_dye_removal || dest->m_armor_class == armor_class::clothing))
				{
					dest->m_instance.item_color =
						static_cast<char>(m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1);
					m_game->send_notify_msg(0, client_h, Notify::ItemColorChange, dest_item_id, dest->m_instance.item_color, 0, 0);
					return true;
				}
				else {
					m_game->send_notify_msg(0, client_h, Notify::ItemColorChange, dest_item_id, -1, 0, 0);
					return false;
				}
			}
		}
		break;

	case ItemEffectType::ArmorDye:
		if ((dest_item_id >= 0) && (dest_item_id < hb::shared::limits::MaxItems)) {
			if (m_game->m_client_list[client_h]->m_item_list[dest_item_id] != 0) {
				auto* dest = m_game->m_client_list[client_h]->m_item_list[dest_item_id];
				if (dest->m_is_dyeable != 0 && dest->m_armor_class == armor_class::armor) {
					dest->m_instance.item_color =
						static_cast<char>(m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1);
					m_game->send_notify_msg(0, client_h, Notify::ItemColorChange, dest_item_id, dest->m_instance.item_color, 0, 0);
					return true;
				}
				else {
					m_game->send_notify_msg(0, client_h, Notify::ItemColorChange, dest_item_id, -1, 0, 0);
					return false;
				}
			}
		}
		break;

	case ItemEffectType::WeaponDye:
		if ((dest_item_id >= 0) && (dest_item_id < hb::shared::limits::MaxItems)) {
			if (m_game->m_client_list[client_h]->m_item_list[dest_item_id] != 0) {
				auto* dest = m_game->m_client_list[client_h]->m_item_list[dest_item_id];
				if (dest->m_is_dyeable != 0 && is_weapon_slot(dest->get_equip_pos())) {
					dest->m_instance.item_color =
						static_cast<char>(m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1);
					m_game->send_notify_msg(0, client_h, Notify::ItemColorChange, dest_item_id, dest->m_instance.item_color, 0, 0);
					return true;
				}
				else {
					m_game->send_notify_msg(0, client_h, Notify::ItemColorChange, dest_item_id, -1, 0, 0);
					return false;
				}
			}
		}
		break;

	case ItemEffectType::Farming:
		ret = plant_seed_bag(m_game->m_client_list[client_h]->m_map_index, dX, dY,
			m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value1,
			m_game->m_client_list[client_h]->m_item_list[item_index]->m_item_effect_value2,
			client_h);
		return ret;

	default:
		break;
	}

	return true;
}

void ItemManager::get_hero_mantle_handler(int client_h, int item_id, const char* string)
{
	int   num, ret, erase_req;
	CItem* item;

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_enemy_kill_count < 100) return;
	if (m_game->m_client_list[client_h]->m_side == 0) return;
	if (get_item_space_left(client_h) == 0) {
		send_item_notify_msg(client_h, Notify::CannotCarryMoreItem, 0, 0);
		return;
	}

	//Prevents a crash if item dosent exist
	if (m_game->m_item_config_list[item_id] == 0)  return;

	switch (item_id) {
		// Hero Cape
	case 400: //Aresden HeroCape
	case 401: //Elvine HeroCape
		if (m_game->m_client_list[client_h]->m_enemy_kill_count < 300) return;
		m_game->m_client_list[client_h]->m_enemy_kill_count -= 300;
		break;

		// Hero Helm
	case 403: //Aresden HeroHelm(M)
	case 404: //Aresden HeroHelm(W)
	case 405: //Elvine HeroHelm(M)
	case 406: //Elvine HeroHelm(W)
		if (m_game->m_client_list[client_h]->m_enemy_kill_count < 150) return;
		m_game->m_client_list[client_h]->m_enemy_kill_count -= 150;
		if (m_game->m_client_list[client_h]->m_contribution < 20) return;
		m_game->m_client_list[client_h]->m_contribution -= 20;
		break;

		// Hero Cap
	case 407: //Aresden HeroCap(M)
	case 408: //Aresden HeroCap(W)
	case 409: //Elvine HeroHelm(M)
	case 410: //Elvine HeroHelm(W)
		if (m_game->m_client_list[client_h]->m_enemy_kill_count < 100) return;
		m_game->m_client_list[client_h]->m_enemy_kill_count -= 100;
		if (m_game->m_client_list[client_h]->m_contribution < 20) return;
		m_game->m_client_list[client_h]->m_contribution -= 20;
		break;

		// Hero Armour
	case 411: //Aresden HeroArmour(M)
	case 412: //Aresden HeroArmour(W)
	case 413: //Elvine HeroArmour(M)
	case 414: //Elvine HeroArmour(W)
		if (m_game->m_client_list[client_h]->m_enemy_kill_count < 300) return;
		m_game->m_client_list[client_h]->m_enemy_kill_count -= 300;
		if (m_game->m_client_list[client_h]->m_contribution < 30) return;
		m_game->m_client_list[client_h]->m_contribution -= 30;
		break;

		// Hero Robe
	case 415: //Aresden HeroRobe(M)
	case 416: //Aresden HeroRobe(W)
	case 417: //Elvine HeroRobe(M)
	case 418: //Elvine HeroRobe(W)
		if (m_game->m_client_list[client_h]->m_enemy_kill_count < 200) return;
		m_game->m_client_list[client_h]->m_enemy_kill_count -= 200;
		if (m_game->m_client_list[client_h]->m_contribution < 20) return;
		m_game->m_client_list[client_h]->m_contribution -= 20;
		break;

		// Hero Hauberk
	case 419: //Aresden HeroHauberk(M)
	case 420: //Aresden HeroHauberk(W)
	case 421: //Elvine HeroHauberk(M)
	case 422: //Elvine HeroHauberk(W)
		if (m_game->m_client_list[client_h]->m_enemy_kill_count < 100) return;
		m_game->m_client_list[client_h]->m_enemy_kill_count -= 100;
		if (m_game->m_client_list[client_h]->m_contribution < 10) return;
		m_game->m_client_list[client_h]->m_contribution -= 10;
		break;

		// Hero Leggings
	case 423: //Aresden HeroLeggings(M)
	case 424: //Aresden HeroLeggings(W)
	case 425: //Elvine HeroLeggings(M)
	case 426: //Elvine HeroLeggings(W)
		if (m_game->m_client_list[client_h]->m_enemy_kill_count < 150) return;
		m_game->m_client_list[client_h]->m_enemy_kill_count -= 150;
		if (m_game->m_client_list[client_h]->m_contribution < 15) return;
		m_game->m_client_list[client_h]->m_contribution -= 15;
		break;

	default:
		return;
		break;
	}

	// Server-authoritative correction for all hero items.
	// Don't trust the client's item_id — derive the correct variant
	// from the server's own m_side and m_sex.
	bool is_elvine = (m_game->m_client_list[client_h]->m_side == 2);
	bool is_female = (m_game->m_client_list[client_h]->m_sex == 2);

	// Cape (400-401): [Aresden, Elvine] — no sex variant
	if (item_id == ItemId::AresdenHeroCape || item_id == ItemId::ElvineHeroCape)
	{
		item_id = is_elvine ? ItemId::ElvineHeroCape : ItemId::AresdenHeroCape;
	}
	// Gendered items (403-426): groups of 4 [Aresden M, Aresden W, Elvine M, Elvine W]
	else if (item_id >= ItemId::AresdenHeroHelmM && item_id <= ItemId::ElvineHeroLeggingsW)
	{
		constexpr int gendered_first = ItemId::AresdenHeroHelmM;
		constexpr int group_size = 4;
		int group_base = gendered_first
			+ ((item_id - gendered_first) / group_size) * group_size;
		int side_offset = is_elvine ? 2 : 0;
		int sex_offset = is_female ? 1 : 0;
		item_id = group_base + side_offset + sex_offset;
	}

	num = 1;
	for(int i = 1; i <= num; i++)
	{
		// Hero mantle / cape, earned with EK and contribution.
		item = create_item(item_id, item_origin::hero_reward, birth_at(client_h));
		if (item == nullptr) continue;

		if (add_client_item_list(client_h, item, &erase_req)) {
			if (m_game->m_client_list[client_h]->m_cur_weight_load < 0) m_game->m_client_list[client_h]->m_cur_weight_load = 0;

			hb::logger::log<log_channel::events>("get HeroItem : Char({}) Player-EK({}) Player-Contr({}) Hero Obtained({})", m_game->m_client_list[client_h]->m_char_name, m_game->m_client_list[client_h]->m_enemy_kill_count, m_game->m_client_list[client_h]->m_contribution, item->m_name);

			item->set_touch_effect_type(TouchEffectType::UniqueOwner);
			item->m_instance.touch_effect_value1 = m_game->m_client_list[client_h]->m_char_id_num1;
			item->m_instance.touch_effect_value2 = m_game->m_client_list[client_h]->m_char_id_num2;
			item->m_instance.touch_effect_value3 = m_game->m_client_list[client_h]->m_char_id_num3;

			ret = send_item_notify_msg(client_h, Notify::ItemObtained, item, 0);

			if (erase_req == 1) destroy_item(item, destroy_reason::merged, client_h);

			m_game->calc_total_weight(client_h);

			switch (ret) {
			case sock::Event::QueueFull:
			case sock::Event::SocketError:
			case sock::Event::CriticalError:
			case sock::Event::SocketClosed:
				m_game->delete_client(client_h, true, true);
				return;
			}

			m_game->send_notify_msg(0, client_h, Notify::EnemyKills, m_game->m_client_list[client_h]->m_enemy_kill_count, 0, 0, 0);
			m_game->send_notify_msg(0, client_h, Notify::Contribution, m_game->m_client_list[client_h]->m_contribution, 0, 0, 0);
		}
		else
		{
			destroy_item(item, destroy_reason::discarded, client_h);

			m_game->calc_total_weight(client_h);

			ret = send_item_notify_msg(client_h, Notify::CannotCarryMoreItem, 0, 0);

			switch (ret) {
			case sock::Event::QueueFull:
			case sock::Event::SocketError:
			case sock::Event::CriticalError:
			case sock::Event::SocketClosed:

				m_game->delete_client(client_h, true, true);
				return;
			}
		}
	}
}

void ItemManager::get_dark_item_handler(int client_h, int item_id)
{
	// Max-level City Hall handout. No cost, re-claimable; granted items are
	// unique-owner bound so they can never be traded or used by anyone else.
	// The client requests the base (male) id; the sex variant is resolved here.
	// Only base forms are claimable — the Giant Sword and Magic Wand are earned
	// through the majestic evolution chains, and the Templars end them.
	static constexpr struct { short base; short female; } dark_item_variants[] =
	{
		{ ItemId::DarkKnightHauberkM,    ItemId::DarkKnightHauberkW },
		{ ItemId::DarkKnightFullHelmM,   ItemId::DarkKnightFullHelmW },
		{ ItemId::DarkKnightLeggingsM,   ItemId::DarkKnightLeggingsW },
		{ ItemId::DarkKnightPlateMailM,  ItemId::DarkKnightPlateMailW },
		{ ItemId::DarkKnightFlameberge,  ItemId::DarkKnightFlameberge },
		{ ItemId::DarkKnightGreatSword,  ItemId::DarkKnightGreatSword },
		{ ItemId::DarkKnightRapier,      ItemId::DarkKnightRapier },
		{ ItemId::DarkMageHauberkM,      ItemId::DarkMageHauberkW },
		{ ItemId::DarkMageChainMailM,    ItemId::DarkMageChainMailW },
		{ ItemId::DarkMageLeggingsM,     ItemId::DarkMageLeggingsW },
		{ ItemId::DarkMageRobeM,         ItemId::DarkMageRobeW },
		{ ItemId::DarkMageMagicStaff,    ItemId::DarkMageMagicStaffW },
	};

	int erase_req, ret;
	CItem* item;

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_level < m_game->m_max_level) return;
	if (m_game->m_client_list[client_h]->m_side == 0) return;

	int resolved_id = 0;
	for (const auto& variant : dark_item_variants)
	{
		if (variant.base == item_id)
		{
			resolved_id = (m_game->m_client_list[client_h]->m_sex == 2) ? variant.female : variant.base;
			break;
		}
	}
	if (resolved_id == 0) return;
	if (m_game->m_item_config_list[resolved_id] == 0) return;

	item = create_item(resolved_id, item_origin::dark_claim, birth_at(client_h));
	if (item == nullptr) return;

	if (add_client_item_list(client_h, item, &erase_req))
	{
		if (m_game->m_client_list[client_h]->m_cur_weight_load < 0) m_game->m_client_list[client_h]->m_cur_weight_load = 0;

		hb::logger::log<log_channel::events>("get DarkItem : Char({}) Level({}) Obtained({})", m_game->m_client_list[client_h]->m_char_name, m_game->m_client_list[client_h]->m_level, item->m_name);

		item->set_touch_effect_type(TouchEffectType::UniqueOwner);
		item->m_instance.touch_effect_value1 = m_game->m_client_list[client_h]->m_char_id_num1;
		item->m_instance.touch_effect_value2 = m_game->m_client_list[client_h]->m_char_id_num2;
		item->m_instance.touch_effect_value3 = m_game->m_client_list[client_h]->m_char_id_num3;

		ret = send_item_notify_msg(client_h, Notify::ItemObtained, item, 0);

		if (erase_req == 1) destroy_item(item, destroy_reason::merged, client_h);

		m_game->calc_total_weight(client_h);
	}
	else
	{
		destroy_item(item, destroy_reason::discarded, client_h);
		ret = send_item_notify_msg(client_h, Notify::CannotCarryMoreItem, 0, 0);
	}

	switch (ret) {
	case sock::Event::QueueFull:
	case sock::Event::SocketError:
	case sock::Event::CriticalError:
	case sock::Event::SocketClosed:
		m_game->delete_client(client_h, true, true);
		return;
	}
}

// Replaces the item in-place with the next stage of a majestic evolution chain,
// keeping its bag position and re-binding it to the owner. glow_color != 0 also
// sets the instance color (9 drives the client's Templar glare).
bool ItemManager::transform_majestic_item(int client_h, int item_index, int new_item_id, int new_value, int glow_color)
{
	CItem* old_item = m_game->m_client_list[client_h]->m_item_list[item_index];
	if (old_item == nullptr) return false;

	// Built before the original is destroyed. The old order deleted first and
	// left a blank CItem in the slot when the next stage had no config row —
	// an item vanishing with nothing to explain it, which is precisely what the
	// ledger exists to make impossible.
	CItem* item = transform_item(new_item_id, *old_item);
	if (item == nullptr) return false;

	int item_x = m_game->m_client_list[client_h]->m_item_pos_list[item_index].x;
	int item_y = m_game->m_client_list[client_h]->m_item_pos_list[item_index].y;

	delete old_item;
	m_game->m_client_list[client_h]->m_item_list[item_index] = item;
	m_game->m_client_list[client_h]->m_item_pos_list[item_index].x = item_x;
	m_game->m_client_list[client_h]->m_item_pos_list[item_index].y = item_y;

	item->set_touch_effect_type(TouchEffectType::UniqueOwner);
	item->m_instance.touch_effect_value1 = m_game->m_client_list[client_h]->m_char_id_num1;
	item->m_instance.touch_effect_value2 = m_game->m_client_list[client_h]->m_char_id_num2;
	item->m_instance.touch_effect_value3 = m_game->m_client_list[client_h]->m_char_id_num3;
	item->set_enchant_bonus(new_value);
	if (glow_color != 0) item->m_instance.item_color = glow_color;

	m_game->send_gizon_item_change(client_h, item_index, item);
	item_log(ItemLogAction::UpgradeSuccess, client_h, (int)-1, item);
	return true;
}

void ItemManager::set_item_pos(int client_h, char* data)
{
	char item_index;
	short sX, sY;

	if (m_game->m_client_list[client_h] == 0) return;

	const auto* req = hb::net::PacketCast<hb::net::PacketRequestSetItemPos>(data, sizeof(hb::net::PacketRequestSetItemPos));
	if (!req) return;
	item_index = static_cast<char>(req->dir);
	sX = req->x;
	sY = req->y;

	if (sY < -10) sY = -10;

	if ((item_index < 0) || (item_index >= hb::shared::limits::MaxItems)) return;
	if (m_game->m_client_list[client_h]->m_item_list[item_index] != 0) {
		m_game->m_client_list[client_h]->m_item_pos_list[item_index].x = sX;
		m_game->m_client_list[client_h]->m_item_pos_list[item_index].y = sY;
	}
}

void ItemManager::check_unique_item_equipment(int client_h)
{
	int damage;

	if (m_game->m_client_list[client_h] == 0) return;

	for(int i = 0; i < hb::shared::limits::MaxItems; i++)
		if (m_game->m_client_list[client_h]->m_item_list[i] != 0) {
			if ((m_game->m_client_list[client_h]->m_item_list[i]->get_touch_effect_type() == TouchEffectType::UniqueOwner) &&
				(m_game->m_client_list[client_h]->m_is_item_equipped[i])) {
				// Touch Effect Type DEF_ITET_OWNER Touch Effect Value 1, 2, 3    .

				if ((m_game->m_client_list[client_h]->m_item_list[i]->m_instance.touch_effect_value1 == m_game->m_client_list[client_h]->m_char_id_num1) &&
					(m_game->m_client_list[client_h]->m_item_list[i]->m_instance.touch_effect_value2 == m_game->m_client_list[client_h]->m_char_id_num2) &&
					(m_game->m_client_list[client_h]->m_item_list[i]->m_instance.touch_effect_value3 == m_game->m_client_list[client_h]->m_char_id_num3)) {
				}
				else {
					m_game->send_notify_msg(0, client_h, Notify::ItemReleased, m_game->m_client_list[client_h]->m_item_list[i]->m_equip_pos, i, 0, 0);
					release_item_handler(client_h, i, true);
					damage = m_game->dice(10, 10);
					m_game->m_client_list[client_h]->m_hp -= damage;
					if (m_game->m_client_list[client_h]->m_hp <= 0) {
						m_game->m_combat_manager->client_killed_handler(client_h, 0, 0, damage);
					}
				}
			}
		}
}

void ItemManager::exchange_item_handler(int client_h, short item_index, int amount, short dX, short dY, uint16_t object_id, const char* item_name)
{
	short owner_h;
	char  owner_type;

	if (m_game->m_client_list[client_h] == 0) return;
	if ((item_index < 0) || (item_index >= hb::shared::limits::MaxItems)) return;
	if (m_game->m_client_list[client_h]->m_item_list[item_index] == 0) return;
	if (m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.count < static_cast<uint32_t>(amount)) return;
	if (m_game->m_client_list[client_h]->m_is_on_server_change) return;
	if (m_game->m_client_list[client_h]->m_is_exchange_mode) return;
	if (object_id >= MaxClients) return;

	// dX, dY     .
	m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->get_owner(&owner_h, &owner_type, dX, dY);

	if ((owner_h != 0) && (owner_type == hb::shared::owner_class::Player)) {

		if (object_id != 0) {
			if (hb::shared::object_id::is_player_id(object_id)) {
				if (m_game->m_client_list[object_id] != 0) {
					if ((uint16_t)owner_h != object_id) owner_h = 0;
				}
			}
			else owner_h = 0;
		}

		if ((owner_h == 0) || (m_game->m_client_list[owner_h] == 0)) {
			clear_exchange_status(client_h);
		}
		else {
			if ((m_game->m_client_list[owner_h]->m_is_exchange_mode) || (m_game->m_client_list[owner_h]->m_appearance.is_walking) ||
				(m_game->m_map_list[m_game->m_client_list[owner_h]->m_map_index]->m_is_fight_zone)) {
				clear_exchange_status(client_h);
			}
			else {
				m_game->m_client_list[client_h]->m_is_exchange_mode = true;
				m_game->m_client_list[client_h]->m_exchange_h = owner_h;
				std::memset(m_game->m_client_list[client_h]->m_exchange_name, 0, sizeof(m_game->m_client_list[client_h]->m_exchange_name));
				strcpy(m_game->m_client_list[client_h]->m_exchange_name, m_game->m_client_list[owner_h]->m_char_name);

				//Clear items in the list
				m_game->m_client_list[client_h]->exchange_count = 0;
				m_game->m_client_list[owner_h]->exchange_count = 0;
				for(int i = 0; i < 4; i++) {
					//Clear the trader
					m_game->m_client_list[client_h]->m_exchange_item_id[i] = 0;
					m_game->m_client_list[client_h]->m_exchange_item_index[i] = -1;
					m_game->m_client_list[client_h]->m_exchange_item_amount[i] = 0;
					//Clear the guy we're trading with
					m_game->m_client_list[owner_h]->m_exchange_item_id[i] = 0;
					m_game->m_client_list[owner_h]->m_exchange_item_index[i] = -1;
					m_game->m_client_list[owner_h]->m_exchange_item_amount[i] = 0;
				}

				m_game->m_client_list[client_h]->m_exchange_item_index[m_game->m_client_list[client_h]->exchange_count] = (char)item_index;
				m_game->m_client_list[client_h]->m_exchange_item_amount[m_game->m_client_list[client_h]->exchange_count] = amount;

				m_game->m_client_list[client_h]->m_exchange_item_id[m_game->m_client_list[client_h]->exchange_count] = m_game->m_client_list[client_h]->m_item_list[item_index]->m_id_num;

				m_game->m_client_list[owner_h]->m_is_exchange_mode = true;
				m_game->m_client_list[owner_h]->m_exchange_h = client_h;
				std::memset(m_game->m_client_list[owner_h]->m_exchange_name, 0, sizeof(m_game->m_client_list[owner_h]->m_exchange_name));
				strcpy(m_game->m_client_list[owner_h]->m_exchange_name, m_game->m_client_list[client_h]->m_char_name);

				m_game->m_client_list[client_h]->exchange_count++;
				m_game->send_exchange_item_notify(client_h, client_h, Notify::OpenExchangeWindow, item_index + 1000,
					m_game->m_client_list[client_h]->m_item_list[item_index], amount);

				m_game->send_exchange_item_notify(client_h, owner_h, Notify::OpenExchangeWindow, item_index,
					m_game->m_client_list[client_h]->m_item_list[item_index], amount);
			}
		}
	}
	else {
		// NPC    .
		clear_exchange_status(client_h);

	}
}

void ItemManager::set_exchange_item(int client_h, int item_index, int amount)
{
	int ex_h;

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_is_on_server_change) return;
	if (m_game->m_client_list[client_h]->exchange_count > 4) return;	//only 4 items trade

	if ((m_game->m_client_list[client_h]->m_is_exchange_mode) && (m_game->m_client_list[client_h]->m_exchange_h != 0)) {
		ex_h = m_game->m_client_list[client_h]->m_exchange_h;
		if ((m_game->m_client_list[ex_h] == 0) || (hb_strnicmp(m_game->m_client_list[client_h]->m_exchange_name, m_game->m_client_list[ex_h]->m_char_name, hb::shared::limits::CharNameLen - 1) != 0)) {

		}
		else {
			if ((item_index < 0) || (item_index >= hb::shared::limits::MaxItems)) return;
			if (m_game->m_client_list[client_h]->m_item_list[item_index] == 0) return;
			if (m_game->m_client_list[client_h]->m_item_list[item_index]->m_instance.count < static_cast<uint32_t>(amount)) return;

			//No Duplicate items
			for(int i = 0; i < m_game->m_client_list[client_h]->exchange_count; i++) {
				if (m_game->m_client_list[client_h]->m_exchange_item_index[i] == (char)item_index) {
					clear_exchange_status(ex_h);
					clear_exchange_status(client_h);
					return;
				}
			}

			m_game->m_client_list[client_h]->m_exchange_item_index[m_game->m_client_list[client_h]->exchange_count] = (char)item_index;
			m_game->m_client_list[client_h]->m_exchange_item_amount[m_game->m_client_list[client_h]->exchange_count] = amount;

			m_game->m_client_list[client_h]->m_exchange_item_id[m_game->m_client_list[client_h]->exchange_count] = m_game->m_client_list[client_h]->m_item_list[item_index]->m_id_num;

			m_game->m_client_list[client_h]->exchange_count++;
			m_game->send_exchange_item_notify(client_h, client_h, Notify::set_exchange_item, item_index + 1000,
				m_game->m_client_list[client_h]->m_item_list[item_index], amount);

			m_game->send_exchange_item_notify(client_h, ex_h, Notify::set_exchange_item, item_index,
				m_game->m_client_list[client_h]->m_item_list[item_index], amount);
		}
	}
	else {
	}
}

void ItemManager::confirm_exchange_item(int client_h)
{
	int ex_h;
	int item_weight_a, item_weight_b, weight_left_a, weight_left_b, amount_left;
	CItem* item_a[4], * item_b[4], * item_acopy[4], * item_bcopy[4];

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_is_on_server_change) return;

	if ((m_game->m_client_list[client_h]->m_is_exchange_mode) && (m_game->m_client_list[client_h]->m_exchange_h != 0)) {
		ex_h = m_game->m_client_list[client_h]->m_exchange_h;

		if (client_h == ex_h) return;

		if (m_game->m_client_list[ex_h] != 0) {
			if ((hb_strnicmp(m_game->m_client_list[client_h]->m_exchange_name, m_game->m_client_list[ex_h]->m_char_name, hb::shared::limits::CharNameLen - 1) != 0) ||
				(m_game->m_client_list[ex_h]->m_is_exchange_mode != true) ||
				(hb_strnicmp(m_game->m_client_list[ex_h]->m_exchange_name, m_game->m_client_list[client_h]->m_char_name, hb::shared::limits::CharNameLen - 1) != 0)) {
				clear_exchange_status(client_h);
				clear_exchange_status(ex_h);
				return;
			}
			else {
				m_game->m_client_list[client_h]->m_is_exchange_confirm = true;
				if (m_game->m_client_list[ex_h]->m_is_exchange_confirm) {

					//Check all items
					for(int i = 0; i < m_game->m_client_list[client_h]->exchange_count; i++) {
						if ((m_game->m_client_list[client_h]->m_item_list[m_game->m_client_list[client_h]->m_exchange_item_index[i]] == 0) ||
							(m_game->m_client_list[client_h]->m_item_list[m_game->m_client_list[client_h]->m_exchange_item_index[i]]->m_id_num != m_game->m_client_list[client_h]->m_exchange_item_id[i])) {
							clear_exchange_status(client_h);
							clear_exchange_status(ex_h);
							return;
						}
					}
					for(int i = 0; i < m_game->m_client_list[ex_h]->exchange_count; i++) {
						if ((m_game->m_client_list[ex_h]->m_item_list[m_game->m_client_list[ex_h]->m_exchange_item_index[i]] == 0) ||
							(m_game->m_client_list[ex_h]->m_item_list[m_game->m_client_list[ex_h]->m_exchange_item_index[i]]->m_id_num != m_game->m_client_list[ex_h]->m_exchange_item_id[i])) {
							clear_exchange_status(client_h);
							clear_exchange_status(ex_h);
							return;
						}
					}

					weight_left_a = m_game->calc_max_load(client_h) - m_game->calc_total_weight(client_h);
					weight_left_b = m_game->calc_max_load(ex_h) - m_game->calc_total_weight(ex_h);

					//Calculate weight for items
					item_weight_a = 0;
					for(int i = 0; i < m_game->m_client_list[client_h]->exchange_count; i++) {
						item_weight_a = get_item_weight(m_game->m_client_list[client_h]->m_item_list[m_game->m_client_list[client_h]->m_exchange_item_index[i]],
							m_game->m_client_list[client_h]->m_exchange_item_amount[i]);
					}
					item_weight_b = 0;
					for(int i = 0; i < m_game->m_client_list[ex_h]->exchange_count; i++) {
						item_weight_b = get_item_weight(m_game->m_client_list[ex_h]->m_item_list[m_game->m_client_list[ex_h]->m_exchange_item_index[i]],
							m_game->m_client_list[ex_h]->m_exchange_item_amount[i]);
					}

					//See if the other person can take the item weightload
					if ((weight_left_a < item_weight_b) || (weight_left_b < item_weight_a)) {
						clear_exchange_status(client_h);
						clear_exchange_status(ex_h);
						return;
					}

					for(int i = 0; i < m_game->m_client_list[client_h]->exchange_count; i++) {
						if (m_game->m_client_list[client_h]->m_item_list[m_game->m_client_list[client_h]->m_exchange_item_index[i]]->is_stackable()) {

							if (static_cast<uint32_t>(m_game->m_client_list[client_h]->m_exchange_item_amount[i]) >
								m_game->m_client_list[client_h]->m_item_list[m_game->m_client_list[client_h]->m_exchange_item_index[i]]->m_instance.count) {
								clear_exchange_status(client_h);
								clear_exchange_status(ex_h);
								return;
							}
							// Only the traded portion moves; the stack it came from stays
							// put. Counted, so there is no identity to divide.
							item_a[i] = create_item(m_game->m_client_list[client_h]->m_item_list[m_game->m_client_list[client_h]->m_exchange_item_index[i]]->m_name, item_origin::none);
							if (item_a[i] == nullptr) {
								clear_exchange_status(client_h);
								clear_exchange_status(ex_h);
								return;
							}
							item_a[i]->m_instance.count = m_game->m_client_list[client_h]->m_exchange_item_amount[i];

							item_acopy[i] = create_snapshot(item_a[i]);
						}
						else {
							// The item object itself changes hands, Serial and all: the
							// giver's slot is nulled below rather than deleted.
							item_a[i] = m_game->m_client_list[client_h]->m_item_list[m_game->m_client_list[client_h]->m_exchange_item_index[i]];
							item_a[i]->m_instance.count = m_game->m_client_list[client_h]->m_exchange_item_amount[i];

							item_acopy[i] = create_snapshot(item_a[i]);
						}
					}

					for(int i = 0; i < m_game->m_client_list[ex_h]->exchange_count; i++) {
						if (m_game->m_client_list[ex_h]->m_item_list[m_game->m_client_list[ex_h]->m_exchange_item_index[i]]->is_stackable()) {

							if (static_cast<uint32_t>(m_game->m_client_list[ex_h]->m_exchange_item_amount[i]) >
								m_game->m_client_list[ex_h]->m_item_list[m_game->m_client_list[ex_h]->m_exchange_item_index[i]]->m_instance.count) {
								clear_exchange_status(client_h);
								clear_exchange_status(ex_h);
								return;
							}
							// Mirror of the client_h side above.
							item_b[i] = create_item(m_game->m_client_list[ex_h]->m_item_list[m_game->m_client_list[ex_h]->m_exchange_item_index[i]]->m_name, item_origin::none);
							if (item_b[i] == nullptr) {
								clear_exchange_status(client_h);
								clear_exchange_status(ex_h);
								return;
							}
							item_b[i]->m_instance.count = m_game->m_client_list[ex_h]->m_exchange_item_amount[i];

							item_bcopy[i] = create_snapshot(item_b[i]);
						}
						else {
							item_b[i] = m_game->m_client_list[ex_h]->m_item_list[m_game->m_client_list[ex_h]->m_exchange_item_index[i]];
							item_b[i]->m_instance.count = m_game->m_client_list[ex_h]->m_exchange_item_amount[i];

							item_bcopy[i] = create_snapshot(item_b[i]);
						}
					}

					for(int i = 0; i < m_game->m_client_list[ex_h]->exchange_count; i++) {
						add_item(client_h, item_b[i], 0);
						item_log(ItemLogAction::Exchange, ex_h, client_h, item_bcopy[i]);
						delete item_bcopy[i];
						item_bcopy[i] = 0;
						if (m_game->m_client_list[ex_h]->m_item_list[m_game->m_client_list[ex_h]->m_exchange_item_index[i]]->is_stackable()) {
							amount_left = static_cast<int>(m_game->m_client_list[ex_h]->m_item_list[m_game->m_client_list[ex_h]->m_exchange_item_index[i]]->m_instance.count) - m_game->m_client_list[ex_h]->m_exchange_item_amount[i];
							if (amount_left < 0) amount_left = 0;
							// v1.41 !!!
							// flow_none: the Exchange above carried the moved copy.
							set_item_count(ex_h, m_game->m_client_list[ex_h]->m_exchange_item_index[i], amount_left, flow_none);
							// m_game->m_client_list[ex_h]->m_item_list[m_game->m_client_list[ex_h]->m_exchange_item_index]->m_name, amount_left);
						}
						else {
							release_item_handler(ex_h, m_game->m_client_list[ex_h]->m_exchange_item_index[i], true);
							m_game->send_notify_msg(0, ex_h, Notify::GiveItemFinEraseItem, m_game->m_client_list[ex_h]->m_exchange_item_index[i], m_game->m_client_list[ex_h]->m_exchange_item_amount[i], 0, m_game->m_client_list[client_h]->m_char_name);
							m_game->m_client_list[ex_h]->m_item_list[m_game->m_client_list[ex_h]->m_exchange_item_index[i]] = 0;
						}
					}

					for(int i = 0; i < m_game->m_client_list[client_h]->exchange_count; i++) {
						add_item(ex_h, item_a[i], 0);
						item_log(ItemLogAction::Exchange, client_h, ex_h, item_acopy[i]);
						delete item_acopy[i];
						item_acopy[i] = 0;

						if (m_game->m_client_list[client_h]->m_item_list[m_game->m_client_list[client_h]->m_exchange_item_index[i]]->is_stackable()) {
							amount_left = static_cast<int>(m_game->m_client_list[client_h]->m_item_list[m_game->m_client_list[client_h]->m_exchange_item_index[i]]->m_instance.count) - m_game->m_client_list[client_h]->m_exchange_item_amount[i];
							if (amount_left < 0) amount_left = 0;
							// v1.41 !!!
							// flow_none: the Exchange above carried the moved copy.
							set_item_count(client_h, m_game->m_client_list[client_h]->m_exchange_item_index[i], amount_left, flow_none);
							// m_game->m_client_list[client_h]->m_item_list[m_game->m_client_list[client_h]->m_exchange_item_index]->m_name, amount_left);
						}
						else {
							release_item_handler(client_h, m_game->m_client_list[client_h]->m_exchange_item_index[i], true);
							m_game->send_notify_msg(0, client_h, Notify::GiveItemFinEraseItem, m_game->m_client_list[client_h]->m_exchange_item_index[i], m_game->m_client_list[client_h]->m_exchange_item_amount[i], 0, m_game->m_client_list[ex_h]->m_char_name);
							m_game->m_client_list[client_h]->m_item_list[m_game->m_client_list[client_h]->m_exchange_item_index[i]] = 0;
						}
					}

					m_game->m_client_list[client_h]->m_is_exchange_mode = false;
					m_game->m_client_list[client_h]->m_is_exchange_confirm = false;
					std::memset(m_game->m_client_list[client_h]->m_exchange_name, 0, sizeof(m_game->m_client_list[client_h]->m_exchange_name));
					m_game->m_client_list[client_h]->m_exchange_h = 0;
					m_game->m_client_list[client_h]->exchange_count = 0;

					m_game->m_client_list[ex_h]->m_is_exchange_mode = false;
					m_game->m_client_list[ex_h]->m_is_exchange_confirm = false;
					std::memset(m_game->m_client_list[ex_h]->m_exchange_name, 0, sizeof(m_game->m_client_list[ex_h]->m_exchange_name));
					m_game->m_client_list[ex_h]->m_exchange_h = 0;
					m_game->m_client_list[ex_h]->exchange_count = 0;

					for(int i = 0; i < 4; i++) {
						m_game->m_client_list[client_h]->m_exchange_item_index[i] = -1;
						m_game->m_client_list[ex_h]->m_exchange_item_index[i] = -1;
					}

					// Both halves of the trade become durable in ONE transaction
					// (#82, plan P3.4). Until this, an Exchange wrote nothing at
					// all: each side became durable whenever that character next
					// happened to be saved, so a player who logged out straight
					// after receiving an item — a single-character save — and a
					// crash before their partner was saved left the item on both
					// characters. That is the dupe window ADR 0004 consolidated
					// the account files to close, and one commit is what closes it.
					if (g_login != nullptr) {
						const int traders[2] = { client_h, ex_h };
						g_login->save_players_atomic(traders, 2);
					}

					m_game->send_notify_msg(0, client_h, Notify::ExchangeItemComplete, 0, 0, 0, 0);
					m_game->send_notify_msg(0, ex_h, Notify::ExchangeItemComplete, 0, 0, 0, 0);

					m_game->calc_total_weight(client_h);
					m_game->calc_total_weight(ex_h);
					return;
				}
			}
		}
		else {
			clear_exchange_status(client_h);
			return;
		}
	}
}

int ItemManager::get_item_space_left(int client_h)
{
	int total_item;

	total_item = 0;
	for(int i = 0; i < hb::shared::limits::MaxItems; i++)
		if (m_game->m_client_list[client_h]->m_item_list[i] != 0) total_item++;

	return (hb::shared::limits::MaxItems - total_item);
}

bool ItemManager::add_item(int client_h, CItem* item, char mode)
{
	int ret, erase_req;

	if (add_client_item_list(client_h, item, &erase_req)) {
		ret = send_item_notify_msg(client_h, Notify::ItemObtained, item, 0);

		if (erase_req == 1) destroy_item(item, destroy_reason::merged, client_h);

		return true;
	}
	else {
		m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->set_item(m_game->m_client_list[client_h]->m_x,
			m_game->m_client_list[client_h]->m_y,
			item);

		m_game->send_ground_item_event(CommonType::ItemDrop, m_game->m_client_list[client_h]->m_map_index,
			m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y, item);

		ret = send_item_notify_msg(client_h, Notify::CannotCarryMoreItem, 0, 0);

		return true;
	}

	return false;
}

int ItemManager::send_item_notify_msg(int client_h, uint16_t msg_type, CItem* item, int v1)
{
	int ret = 0;

	if (m_game->m_client_list[client_h] == 0) return 0;

	switch (msg_type) {
	case Notify::ItemObtained:
	{
		hb::net::PacketNotifyItemObtained pkt{};
		pkt.header.msg_id = MsgId::Notify;
		pkt.header.msg_type = msg_type;
		pkt.is_new = 1;
		memcpy(pkt.name, item->m_name, sizeof(pkt.name));
		pkt.count = item->m_instance.count;
		pkt.item_type = item->m_item_type;
		pkt.equip_pos = item->m_equip_pos;
		pkt.is_equipped = 0;
		pkt.level_limit = item->m_level_requirement;
		pkt.gender_limit = item->m_gender_requirement;
		pkt.cur_durability = item->m_instance.cur_durability;
		pkt.weight = item->m_weight;
		pkt.item_color = item->m_instance.item_color;
		pkt.spec_value2 = static_cast<uint8_t>(item->m_instance.special_effect_value2);
		pkt.attributes = item->get_attributes();
		pkt.item_id = item->m_id_num;
		pkt.max_durability = item->m_durability;
		ret = m_game->m_client_list[client_h]->m_socket->send_msg(reinterpret_cast<char*>(&pkt), sizeof(pkt));
	}
	break;

	case Notify::ItemPurchased:
	{
		hb::net::PacketNotifyItemPurchased pkt{};
		pkt.header.msg_id = MsgId::Notify;
		pkt.header.msg_type = msg_type;
		pkt.is_new = 1;
		memcpy(pkt.name, item->m_name, sizeof(pkt.name));
		pkt.count = item->m_instance.count;
		pkt.item_type = item->m_item_type;
		pkt.equip_pos = item->m_equip_pos;
		pkt.is_equipped = 0;
		pkt.level_limit = item->m_level_requirement;
		pkt.gender_limit = item->m_gender_requirement;
		pkt.cur_durability = item->m_instance.cur_durability;
		pkt.weight = item->m_weight;
		pkt.item_color = item->m_instance.item_color;
		pkt.cost = static_cast<uint16_t>(v1);
		pkt.item_id = item->m_id_num;
		pkt.max_durability = item->m_durability;
		ret = m_game->m_client_list[client_h]->m_socket->send_msg(reinterpret_cast<char*>(&pkt), sizeof(pkt));
	}
	break;

	case Notify::CannotCarryMoreItem:
	{
		hb::net::PacketNotifyEmpty pkt{};
		pkt.header.msg_id = MsgId::Notify;
		pkt.header.msg_type = msg_type;
		ret = m_game->m_client_list[client_h]->m_socket->send_msg(reinterpret_cast<char*>(&pkt), sizeof(pkt));
	}
	break;
	}

	return ret;
}

bool ItemManager::check_item_receive_condition(int client_h, CItem* item)
{
	

	if (m_game->m_client_list[client_h] == 0) return false;

	if (m_game->m_client_list[client_h]->m_cur_weight_load + get_item_weight(item, static_cast<int>(item->m_instance.count)) > m_game->calc_max_load(client_h))
		return false;

	for(int i = 0; i < hb::shared::limits::MaxItems; i++)
		if (m_game->m_client_list[client_h]->m_item_list[i] == 0) return true;

	return false;
}

void ItemManager::build_item_handler(int client_h, char* data)
{
	char name[hb::shared::limits::ItemNameLen], element_item_id[6];
	int    x, z, match, count, player_skill_level, result, total_value, result_value, temp, item_count[hb::shared::limits::MaxItems];
	CItem* item;
	bool   flag, item_flag[6];
	double v1, v2, v3;
	uint32_t  dw_temp;
	uint16_t   w_temp;

	if (m_game->m_client_list[client_h] == 0) return;
	m_game->m_client_list[client_h]->m_skill_msg_recv_count++;

	const auto* pkt = hb::net::PacketCast<hb::net::PacketCommandCommonBuild>(
		data, sizeof(hb::net::PacketCommandCommonBuild));
	if (!pkt) return;
	std::memset(name, 0, sizeof(name));
	memcpy(name, pkt->name, sizeof(pkt->name));

	//testcode
	//PutLogList(name);

	std::memset(element_item_id, 0, sizeof(element_item_id));
	for(int i = 0; i < 6; i++) {
		element_item_id[i] = static_cast<char>(pkt->item_ids[i]);
	}

	flag = true;
	while (flag) {
		flag = false;
		for(int i = 0; i <= 4; i++)
			if ((element_item_id[i] == -1) && (element_item_id[i + 1] != -1)) {
				element_item_id[i] = element_item_id[i + 1];
				element_item_id[i + 1] = -1;
				flag = true;
			}
	}

	for(int i = 0; i < 6; i++) item_flag[i] = false;

	//testcode
	//std::snprintf(G_cTxt, sizeof(G_cTxt), "%d %d %d %d %d %d", element_item_id[0], element_item_id[1], element_item_id[2],
	//	     element_item_id[3], element_item_id[4], element_item_id[5]);
	//PutLogList(G_cTxt);

	player_skill_level = m_game->m_client_list[client_h]->m_skill_mastery[13];
	result = m_game->dice(1, 100);

	if (result > player_skill_level) {
		m_game->send_notify_msg(0, client_h, Notify::BuildItemFail, 0, 0, 0, 0);
		return;
	}

	for(int i = 0; i < 6; i++)
		if (element_item_id[i] != -1) {
			// Item ID.
			if ((element_item_id[i] < 0) || (element_item_id[i] > hb::shared::limits::MaxItems)) return;
			if (m_game->m_client_list[client_h]->m_item_list[element_item_id[i]] == 0) return;
		}

	for(int i = 0; i < hb::shared::limits::MaxBuildItems; i++)
		if (m_game->m_build_item_list[i] != 0) {
			if (memcmp(m_game->m_build_item_list[i]->m_name, name, hb::shared::limits::ItemNameLen - 1) == 0) {

				if (m_game->m_build_item_list[i]->m_skill_limit > m_game->m_client_list[client_h]->m_skill_mastery[13]) return;

				for (x = 0; x < hb::shared::limits::MaxItems; x++)
					if (m_game->m_client_list[client_h]->m_item_list[x] != 0)
						item_count[x] = static_cast<int>(m_game->m_client_list[client_h]->m_item_list[x]->m_instance.count);
					else item_count[x] = 0;

				match = 0;
				total_value = 0;

				for (x = 0; x < 6; x++) {
					if (m_game->m_build_item_list[i]->m_material_item_count[x] == 0) {
						match++;
					}
					else {
						for (z = 0; z < 6; z++)
							if ((element_item_id[z] != -1) && (item_flag[z] == false)) {

								if ((m_game->m_client_list[client_h]->m_item_list[element_item_id[z]]->m_id_num == m_game->m_build_item_list[i]->m_material_item_id[x]) &&
									(m_game->m_client_list[client_h]->m_item_list[element_item_id[z]]->m_instance.count >=
										static_cast<uint32_t>(m_game->m_build_item_list[i]->m_material_item_count[x])) &&
									(item_count[element_item_id[z]] > 0)) {
									dw_temp = m_game->m_client_list[client_h]->m_item_list[element_item_id[z]]->m_instance.special_effect_value2;
									if (dw_temp > m_game->m_client_list[client_h]->m_skill_mastery[13]) {
										dw_temp = dw_temp - (dw_temp - m_game->m_client_list[client_h]->m_skill_mastery[13]) / 2;
									}

									total_value += (dw_temp * m_game->m_build_item_list[i]->m_material_item_value[x]);
									item_count[element_item_id[z]] -= m_game->m_build_item_list[i]->m_material_item_count[x];
									match++;
									item_flag[z] = true;

									break;
								}
							}
					}
				}

				// match 6     .
				if (match != 6) {
					m_game->send_notify_msg(0, client_h, Notify::BuildItemFail, 0, 0, 0, 0);
					return;
				}

				v2 = (double)m_game->m_build_item_list[i]->m_max_value;
				if (total_value <= 0)
					v3 = 1.0f;
				else v3 = (double)total_value;
				v1 = (double)(v3 / v2) * 100.0f;

				total_value = (int)v1;

				item = create_item(m_game->m_build_item_list[i]->m_name, item_origin::craft,
					birth_at(client_h));
				if (item == nullptr) return;

				// Custom-Made
				item->set_custom_made(true);

				if (item->get_item_type() == hb::shared::item::item_type::material) {
					temp = m_game->dice(1, (player_skill_level / 2) + 1) - 1;
					item->m_instance.special_effect_value2 = (player_skill_level / 2) + temp;
					item->set_touch_effect_type(TouchEffectType::ID);
					item->m_instance.touch_effect_value1 = static_cast<short>(m_game->dice(1, 100000));
					item->m_instance.touch_effect_value2 = static_cast<short>(m_game->dice(1, 100000));
					item->m_instance.touch_effect_value3 = static_cast<short>(GameClock::GetTimeMS());

				}
				else {
					// Copy prefix attributes from build item definition.
					// Recipe nibbles stay in the legacy 1-12 prefix space;
					// translate to unified modifier IDs at this boundary.
					uint16_t build_attr = m_game->m_build_item_list[i]->m_attribute;
					item->set_prefix(legacy_prefix_to_modifier_id((build_attr >> 4) & 0x0F), static_cast<uint8_t>(build_attr & 0x0F));
					item->set_enchant_bonus(static_cast<uint8_t>((build_attr >> 12) & 0x0F));

					result_value = (total_value - m_game->m_build_item_list[i]->m_average_value);
					// : SpecEffectValue1 , SpecEffectValue2

					// 1.   ()
					if (result_value > 0) {
						v2 = (double)result_value;
						v3 = (double)(100 - m_game->m_build_item_list[i]->m_average_value);
						v1 = (v2 / v3) * 100.0f;
						item->m_instance.special_effect_value2 = (int)v1;
					}
					else if (result_value < 0) {
						v2 = (double)(result_value);
						v3 = (double)(m_game->m_build_item_list[i]->m_average_value);
						v1 = (v2 / v3) * 100.0f;
						item->m_instance.special_effect_value2 = (int)v1;
					}
					else item->m_instance.special_effect_value2 = 0;

					v2 = (double)item->m_instance.special_effect_value2;
					v3 = (double)item->m_durability;
					v1 = (v2 / 100.0f) * v3;

					w_temp = (int)item->m_durability;
					w_temp += (int)v1;

					item->set_touch_effect_type(TouchEffectType::ID);
					item->m_instance.touch_effect_value1 = static_cast<short>(m_game->dice(1, 100000));
					item->m_instance.touch_effect_value2 = static_cast<short>(m_game->dice(1, 100000));
					item->m_instance.touch_effect_value3 = static_cast<short>(GameClock::GetTimeMS());

					if (w_temp <= 0)
						w_temp = 1;
					else w_temp = (uint16_t)w_temp;

					if (w_temp <= item->m_durability * 2) {
						item->m_durability = w_temp;
						item->m_instance.special_effect_value1 = (short)w_temp;
						item->m_instance.cur_durability = item->m_durability;
					}
					else item->m_instance.special_effect_value1 = (short)item->m_durability;

					// Custom-Item  2.
					item->m_instance.item_color = 2;
				}

				//testcode
				hb::logger::log("Custom-Item({}) Value({}) Life({}/{})", item->m_name, item->m_instance.special_effect_value2, item->m_instance.cur_durability, item->m_durability);

				add_item(client_h, item, 0);
				m_game->send_notify_msg(0, client_h, Notify::BuildItemSuccess, item->m_instance.special_effect_value2, item->m_item_type, 0, 0); // Integer

				for (x = 0; x < 6; x++)
					if (element_item_id[x] != -1) {
						if (m_game->m_client_list[client_h]->m_item_list[element_item_id[x]] == 0) {
							// ### BUG POINT!!!
							hb::logger::log<log_channel::events>("(?) Char({}) ElementItemID({})", m_game->m_client_list[client_h]->m_char_name, element_item_id[x]);
						}
						else {
							count = static_cast<int>(m_game->m_client_list[client_h]->m_item_list[element_item_id[x]]->m_instance.count) - m_game->m_build_item_list[i]->m_material_item_count[x];
							if (count < 0) count = 0;
							// Element consumption books Make (#105). The clamp
							// above means a short stack books what it actually
							// held, not the recipe's demand — the delta is
							// computed inside the call.
							set_item_count(client_h, element_item_id[x], count, ItemLogAction::Make);
						}
					}

				if (m_game->m_build_item_list[i]->m_max_skill > m_game->m_client_list[client_h]->m_skill_mastery[13])
					m_game->m_skill_manager->calculate_ssn_skill_index(client_h, 13, 1);

				m_game->get_exp(client_h, m_game->dice(1, (m_game->m_build_item_list[i]->m_skill_limit / 4))); //m_game->m_client_list[client_h]->m_exp_stock += m_game->dice(1, (m_game->m_build_item_list[i]->m_skill_limit/4));

				return;
			}
		}

}

int ItemManager::modifier_multiplier(uint8_t modifier_id) const
{
	const auto* row = m_game->get_tier_config().find_modifier(modifier_id);
	return row ? row->multiplier : 1;
}

void ItemManager::apply_modifier_derived_stats(CItem* item)
{
	// Strong and Ancient both carry the endurance rider; the Bucket law makes
	// them mutually exclusive on an item, but summing is the honest reading of
	// "modifiers are independent lines" and costs nothing.
	const auto& config = m_game->get_tier_config();
	int growth_pct = 0;

	for (const auto& mod : item->get_attributes().modifiers)
	{
		if (mod.type == modifier_id::empty) continue;

		const auto* row = config.find_modifier(mod.type);
		if (row == nullptr) continue;

		switch (row->effect_id)
		{
		case effect_id::strong:
		case effect_id::ancient:
			growth_pct += mod.value * row->multiplier;
			break;
		default:
			break;
		}
	}

	if (growth_pct <= 0) return;

	double base = (double)item->m_durability;
	item->m_durability += (int)((double)growth_pct / 100.0f * base);
}

void ItemManager::request_sell_item_list_handler(int client_h, char* data)
{
	int amount;
	char index;

	if (m_game->m_client_list[client_h] == 0) return;

	const auto* req = hb::net::PacketCast<hb::net::PacketRequestSellItemList>(data, sizeof(hb::net::PacketRequestSellItemList));
	if (!req) return;

	for(int i = 0; i < 12; i++) {
		index = static_cast<char>(req->entries[i].index);
		amount = req->entries[i].amount;

		if ((index == -1) || (index < 0) || (index >= hb::shared::limits::MaxItems)) return;
		if (m_game->m_client_list[client_h]->m_item_list[index] == 0) return;

		// index   .
		req_sell_item_confirm_handler(client_h, index, amount, 0);
		if (m_game->m_client_list[client_h] == 0) return;
	}
}

int ItemManager::get_item_weight(CItem* item, int count)
{
	if (count < 0) count = 1;
	return CItem::calc_item_stack_weight(
		item->get_effective_weight(modifier_multiplier(modifier_id::light)), count);
}

bool ItemManager::copy_item_contents(CItem* copy, CItem* original)
{
	if (original == 0) return false;
	if (copy == 0) return false;

	// Provenance travels with the contents. The only caller is create_snapshot,
	// whose copy describes the original rather than duplicating it — see the
	// note there for why sharing a Serial is safe in that one case.
	copy->m_serial = original->m_serial;
	copy->m_origin = original->m_origin;

	copy->m_id_num = original->m_id_num;
	copy->m_item_type = original->m_item_type;
	copy->m_equip_pos = original->m_equip_pos;
	copy->m_item_effect_type = original->m_item_effect_type;
	copy->m_item_effect_value1 = original->m_item_effect_value1;
	copy->m_item_effect_value2 = original->m_item_effect_value2;
	copy->m_item_effect_value3 = original->m_item_effect_value3;
	copy->m_item_effect_value4 = original->m_item_effect_value4;
	copy->m_item_effect_value5 = original->m_item_effect_value5;
	copy->m_item_effect_value6 = original->m_item_effect_value6;
	copy->m_durability = original->m_durability;
	copy->m_special_effect = original->m_special_effect;

	//short m_sSM_HitRatio, m_sL_HitRatio;
	copy->m_special_effect_value1 = original->m_special_effect_value1;
	copy->m_special_effect_value2 = original->m_special_effect_value2;

	copy->m_weapon_class = original->m_weapon_class;
	copy->m_swing_speed = original->m_swing_speed;

	copy->m_sell_price = original->m_sell_price;
	copy->m_weight = original->m_weight;
	copy->m_level_requirement = original->m_level_requirement;
	copy->m_gender_requirement = original->m_gender_requirement;

	copy->m_related_skill = original->m_related_skill;

	copy->m_item_sub_type = original->m_item_sub_type;
	copy->m_hide_armor = original->m_hide_armor;
	copy->m_is_skirt = original->m_is_skirt;
	copy->m_stackable = original->m_stackable;
	copy->m_is_dyeable = original->m_is_dyeable;
	copy->m_armor_class = original->m_armor_class;
	copy->m_set_id = original->m_set_id;

	copy->m_instance = original->m_instance;
	copy->m_display_id = original->m_display_id;

	return true;
}

//////////////////////////////////////////////////////////////////////
// item_log — the dual sink (#78, plan P2.2, decision D5)
//
// The text channels below are unchanged; what is new is that every call also
// appends a structured ledger event. The ledger half runs FIRST in both
// overloads, and that ordering is the whole point: each text sink drops what it
// is not interested in — an action its switch does not name, a client handle
// that no longer resolves, an item check_good_item() does not consider worth a
// line — and every one of those is a hole in a log whose only value is not
// having any. The filters stay exactly where they were; they just stop deciding
// for both sinks at once.
//////////////////////////////////////////////////////////////////////

hb::server::item_ledger_store* ItemManager::ledger() const
{
	if (m_game == nullptr) return nullptr;
	return m_game->m_item_ledger_store.get();
}

CClient* ItemManager::client_at(int client_h) const
{
	if (m_game == nullptr) return nullptr;
	if (client_h <= 0 || client_h >= hb::server::config::MaxClients) return nullptr;
	return m_game->m_client_list[client_h];
}

void ItemManager::record_counted_flow(int event_type, const CItem& item, int64_t qty) const
{
	// Instanced items are events, not flows. Guarded here as well as at the one
	// caller so a later emitter cannot reach this by a route that forgot.
	if (item.m_serial != 0) return;

	// flow_none is an API sentinel, never a stored number (#105). Guarded at
	// the funnel for the reason the serial is: "no flow row ever carries a 0"
	// must not depend on which door a future emitter comes through.
	if (event_type == flow_none) return;

	const int64_t moved = (qty == flow_qty_from_item)
		? static_cast<int64_t>(item.m_instance.count)
		: qty;

	// A zero-quantity flow says nothing, and one caller means it: a stack merge
	// passes 0 because nothing left the world — the husk's contents live on in
	// the stack it merged into (destroy_reason::merged).
	if (moved == 0) return;

	if (hb::server::item_ledger_store* store = ledger())
		store->record_flow(item.m_id_num, event_type, moved);
}

bool ItemManager::begin_ledger_event(int action, const CItem* item, int actor_h,
	hb::server::ledger_event_record& event, int64_t qty) const
{
	if (item == nullptr) return false;

	// A Counted item has no identity for an event to hang off (D2), so the same
	// transition is booked as an aggregate instead and the caller is told there
	// is no event to build. Recording here rather than at the call sites is what
	// makes the Counted tier free: every emitter in the server — both item_log
	// overloads, destroy_item, despawn_item — already comes through this door.
	if (item->m_serial == 0)
	{
		record_counted_flow(action, *item, qty);
		return false;
	}

	if (ledger() == nullptr) return false;

	event.serial = item->m_serial;
	event.event_type = action;   // 1..99 is ItemLogAction verbatim (ItemLedgerStore.h)

	// Where the event happened is where the actor was standing. An actorless
	// event — an NPC drop, or a handle that has already gone — keeps those
	// columns NULL rather than inventing a location for a query to trust.
	if (const CClient* actor = client_at(actor_h))
		stamp_actor(*actor, event);
	return true;
}

void ItemManager::stamp_actor(const CClient& actor, hb::server::ledger_event_record& event)
{
	event.actor_account = actor.m_account_name;
	event.actor_char = actor.m_char_name;
	event.map = actor.m_map_name;
	event.x = actor.m_x;
	event.y = actor.m_y;
}

hb::server::ledger_event_record ItemManager::ledger_actor(const char* actor_char,
	const CClient* actor) const
{
	hb::server::ledger_event_record base;
	if (actor != nullptr)
		stamp_actor(*actor, base);
	else if (actor_char != nullptr)
		base.actor_char = actor_char;
	return base;
}

void ItemManager::record_ledger_event(int event_type, int64_t serial,
	const hb::server::ledger_event_record& base)
{
	// Both guards ahead of the copy. A bundle of gold reaches here once per
	// stack and must cost a comparison, not six string copies for a row that is
	// then dropped at the store's door.
	if (serial == 0) return;
	hb::server::item_ledger_store* store = ledger();
	if (store == nullptr) return;

	hb::server::ledger_event_record event = base;
	event.serial = serial;
	event.event_type = event_type;
	store->record_event(std::move(event));
}

bool ItemManager::item_log(int action, int give_h, int recv_h, CItem* item, bool force_item_log,
	int64_t qty)
{
	if (hb::server::ledger_event_record event; begin_ledger_event(action, item, give_h, event, qty))
	{
		// `recv_h` is a counterparty handle in every action that reaches here.
		// It was not always: GM minting used to pass its quantity through this
		// parameter, so the reading of a column depended on another column. #104
		// gave minting its own door (record_gm_mint) and took the exception away.
		if (const CClient* recv = client_at(recv_h))
			event.counterparty_char = recv->m_char_name;

		ledger()->record_event(std::move(event));
	}

	if (item == 0) return false;
	if (m_game->m_client_list[give_h] == 0) return false;

	switch (action) {

	case ItemLogAction::Exchange:
		if (m_game->m_client_list[recv_h] == 0) return false;
		hb::logger::log<log_channel::trade>("{}{} IP({}) Exchange {} at {}({},{}) -> {}", is_item_suspicious(item) ? "[SUSPICIOUS] " : "", m_game->m_client_list[give_h]->m_char_name, m_game->m_client_list[give_h]->m_ip_address, format_item_info(item), m_game->m_client_list[give_h]->m_map_name, m_game->m_client_list[give_h]->m_x, m_game->m_client_list[give_h]->m_y, m_game->m_client_list[recv_h]->m_char_name);
		break;

	case ItemLogAction::Give:
		if (m_game->m_client_list[recv_h] == 0) return false;
		hb::logger::log<log_channel::trade>("{}{} IP({}) Give {} at {}({},{}) -> {}", is_item_suspicious(item) ? "[SUSPICIOUS] " : "", m_game->m_client_list[give_h]->m_char_name, m_game->m_client_list[give_h]->m_ip_address, format_item_info(item), m_game->m_client_list[give_h]->m_map_name, m_game->m_client_list[give_h]->m_x, m_game->m_client_list[give_h]->m_y, m_game->m_client_list[recv_h]->m_char_name);
		break;

	case ItemLogAction::Drop:
		hb::logger::log<log_channel::drops>("{} IP({}) Drop {} at {}({},{})", m_game->m_client_list[give_h]->m_char_name, m_game->m_client_list[give_h]->m_ip_address, format_item_info(item), m_game->m_client_list[give_h]->m_map_name, m_game->m_client_list[give_h]->m_x, m_game->m_client_list[give_h]->m_y);
		break;

	case ItemLogAction::get:
		hb::logger::log<log_channel::drops>("{} IP({}) get {} at {}({},{})", m_game->m_client_list[give_h]->m_char_name, m_game->m_client_list[give_h]->m_ip_address, format_item_info(item), m_game->m_client_list[give_h]->m_map_name, m_game->m_client_list[give_h]->m_x, m_game->m_client_list[give_h]->m_y);
		break;

	case ItemLogAction::Make:
		hb::logger::log<log_channel::crafting>("{} IP({}) Make {} at {}({},{})", m_game->m_client_list[give_h]->m_char_name, m_game->m_client_list[give_h]->m_ip_address, format_item_info(item), m_game->m_client_list[give_h]->m_map_name, m_game->m_client_list[give_h]->m_x, m_game->m_client_list[give_h]->m_y);
		break;

	case ItemLogAction::Deplete:
		hb::logger::log<log_channel::items_misc>("{} IP({}) {} {} at {}({},{})", m_game->m_client_list[give_h]->m_char_name, m_game->m_client_list[give_h]->m_ip_address, "Deplete", format_item_info(item), m_game->m_client_list[give_h]->m_map_name, m_game->m_client_list[give_h]->m_x, m_game->m_client_list[give_h]->m_y);
		break;

	case ItemLogAction::Buy:
		hb::logger::log<log_channel::shop>("{} IP({}) {} {} at {}({},{})", m_game->m_client_list[give_h]->m_char_name, m_game->m_client_list[give_h]->m_ip_address, "Buy", format_item_info(item), m_game->m_client_list[give_h]->m_map_name, m_game->m_client_list[give_h]->m_x, m_game->m_client_list[give_h]->m_y);
		break;

	case ItemLogAction::Sell:
		hb::logger::log<log_channel::shop>("{} IP({}) {} {} at {}({},{})", m_game->m_client_list[give_h]->m_char_name, m_game->m_client_list[give_h]->m_ip_address, "Sell", format_item_info(item), m_game->m_client_list[give_h]->m_map_name, m_game->m_client_list[give_h]->m_x, m_game->m_client_list[give_h]->m_y);
		break;

	case ItemLogAction::Retrieve:
		hb::logger::log<log_channel::bank>("{} IP({}) {} {} at {}({},{})", m_game->m_client_list[give_h]->m_char_name, m_game->m_client_list[give_h]->m_ip_address, "Retrieve", format_item_info(item), m_game->m_client_list[give_h]->m_map_name, m_game->m_client_list[give_h]->m_x, m_game->m_client_list[give_h]->m_y);
		break;

	case ItemLogAction::Deposit:
		hb::logger::log<log_channel::bank>("{} IP({}) {} {} at {}({},{})", m_game->m_client_list[give_h]->m_char_name, m_game->m_client_list[give_h]->m_ip_address, "Deposit", format_item_info(item), m_game->m_client_list[give_h]->m_map_name, m_game->m_client_list[give_h]->m_x, m_game->m_client_list[give_h]->m_y);
		break;

	case ItemLogAction::UpgradeFail:
		hb::logger::log<log_channel::upgrades>("{} IP({}) Upgrade {} {} at {}({},{})", m_game->m_client_list[give_h]->m_char_name, m_game->m_client_list[give_h]->m_ip_address, false ? "Success" : "Fail", format_item_info(item), m_game->m_client_list[give_h]->m_map_name, m_game->m_client_list[give_h]->m_x, m_game->m_client_list[give_h]->m_y);
		break;

	case ItemLogAction::UpgradeSuccess:
		hb::logger::log<log_channel::upgrades>("{} IP({}) Upgrade {} {} at {}({},{})", m_game->m_client_list[give_h]->m_char_name, m_game->m_client_list[give_h]->m_ip_address, true ? "Success" : "Fail", format_item_info(item), m_game->m_client_list[give_h]->m_map_name, m_game->m_client_list[give_h]->m_x, m_game->m_client_list[give_h]->m_y);
		break;

	// GmMint has no case here: a mint is one line for a request of N copies and
	// N events, so it writes through log_gm_mint / record_gm_mint instead (#104).

	default:
		return false;
	}
	return true;
}

//////////////////////////////////////////////////////////////////////
// GM minting — the two doors (#104)
//
// Split out of item_log because the sinks count different things. See the
// declarations in ItemManager.h for which unit belongs to which sink and why
// the quantity no longer travels in a handle parameter.
//////////////////////////////////////////////////////////////////////

void ItemManager::record_gm_mint(int client_h, CItem* item)
{
	// One copy, one event. There is no counterparty: the character the copy
	// landed on is the actor, which is also what makes GmMint a locating event
	// for Reconciliation — it says who holds it, and now it says that about
	// every copy rather than about one of them.
	if (hb::server::ledger_event_record event;
		begin_ledger_event(ItemLogAction::GmMint, item, client_h, event))
	{
		ledger()->record_event(std::move(event));
	}
}

bool ItemManager::log_gm_mint(int client_h, int quantity, CItem* sample)
{
	if (sample == nullptr) return false;

	const CClient* actor = client_at(client_h);
	if (actor == nullptr) return false;

	hb::logger::log<log_channel::trade>("{} IP({}) GmMint {}x {} at {}({},{})",
		actor->m_char_name, actor->m_ip_address, quantity, format_item_info(sample),
		actor->m_map_name, actor->m_x, actor->m_y);
	return true;
}

bool ItemManager::item_log(int action, int client_h, char* name, CItem* item)
{
	if (hb::server::ledger_event_record event; begin_ledger_event(action, item, client_h, event))
	{
		// `name` is the NPC that dropped it on the NewGenDrop path — the only
		// birth context this overload carries, and the reason the drop's origin
		// is answerable at all until #79 puts it on the birth row. The other
		// actions pass a name none of them print.
		if (action == ItemLogAction::NewGenDrop && name != nullptr)
			event.detail = hb::server::detail_json("npc", name);

		ledger()->record_event(std::move(event));
	}

	if (item == 0) return false;
	if (check_good_item(item) == false) return false;
	if (action != ItemLogAction::NewGenDrop)
	{
		if (m_game->m_client_list[client_h] == 0) return false;
	}
	char temp1[120];
	std::memset(temp1, 0, sizeof(temp1));
	if (m_game->m_client_list[client_h] != 0) m_game->m_client_list[client_h]->m_socket->get_peer_address(temp1);

	switch (action) {

	case ItemLogAction::NewGenDrop:
		hb::logger::log<log_channel::items_misc>("{} IP({}) {} {} at {}({},{})", name ? name : "Unknown", "", "NpcDrop", format_item_info(item), "", 0, 0);
		break;

	case ItemLogAction::SkillLearn:
	case ItemLogAction::MagicLearn:
		if (name == 0) return false;
		if (m_game->m_client_list[client_h] == 0) return false;
		hb::logger::log<log_channel::items_misc>("{} IP({}) {} {} at {}({},{})", m_game->m_client_list[client_h]->m_char_name, temp1, "Learn", format_item_info(item), m_game->m_client_list[client_h]->m_map_name, m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y);
		break;

	case ItemLogAction::SummonMonster:
		if (name == 0) return false;
		if (m_game->m_client_list[client_h] == 0) return false;
		hb::logger::log<log_channel::items_misc>("{} IP({}) {} {} at {}({},{})", m_game->m_client_list[client_h]->m_char_name, temp1, "Summon", format_item_info(item), m_game->m_client_list[client_h]->m_map_name, m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y);
		break;

	case ItemLogAction::Poisoned:
		if (m_game->m_client_list[client_h] == 0) return false;
		hb::logger::log<log_channel::items_misc>("{} IP({}) {} {} at {}({},{})", m_game->m_client_list[client_h]->m_char_name, temp1, "Poisoned", format_item_info(item), m_game->m_client_list[client_h]->m_map_name, m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y);
		break;

	case ItemLogAction::Repair:
		if (name == 0) return false;
		if (m_game->m_client_list[client_h] == 0) return false;
		hb::logger::log<log_channel::items_misc>("{} IP({}) {} {} at {}({},{})", m_game->m_client_list[client_h]->m_char_name, temp1, "Repair", format_item_info(item), m_game->m_client_list[client_h]->m_map_name, m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y);
		break;

	default:
		return false;
	}
	return true;
}

bool ItemManager::check_good_item(CItem* item)
{
	if (item == 0) return false;

	if (item->m_id_num == 90)
	{
		if (item->m_instance.count > 10000) return true;  // Gold  10000   .
		else return false;
	}
	switch (item->m_id_num) {
		// case 90: // Gold
	case 259:
	case 290:
	case 291:
	case 292:
	case 300:
	case 305:
	case 308:
	case 311:
	case 334:
	case 335:
	case 336:
	case 338:
	case 380:
	case 381:
	case 382:
	case 391:
	case 400:
	case 401:
	case 490:
	case 491:
	case 492:
	case 508:
	case 581:
	case 610:
	case 611:
	case 612:
	case 613:
	case 614:
	case 616:
	case 618:

	case 620:
	case 621:
	case 622:

	case 630:
	case 631:

	case 632:
	case 633:
	case 634:
	case 635:
	case 636:
	case 637:
	case 638:
	case 639:
	case 640:
	case 641:

	case 642:
	case 643:

	case 644:
	case 645:
	case 646:
	case 647:

	case 650:
	case 656:
	case 657:

	case 700:
	case 701:
	case 702:
	case 703:
	case 704:
	case 705:
	case 706:
	case 707:
	case 708:
	case 709:
	case 710:
	case 711:
	case 712:
	case 713:
	case 714:
	case 715:

	case 720:
	case 721:
	case 722:
	case 723:

	case 724:
	case 725:
	case 726:
	case 727:
	case 728:
	case 729:
	case 730:
	case 731:
	case 732:
	case 733:

	case 734:
	case 735:

	case 736:
	case 737:
	case 738:
	case 924:

		return true;
		break;
	default:
		if (!(item->has_special_attributes())) return false;
		else if (item->m_id_num > 30) return true;
		else return false;
	}
}

bool ItemManager::check_and_convert_plus_weapon_item(int client_h, int item_index)
{
	if (m_game->m_client_list[client_h] == 0) return false;
	if (m_game->m_client_list[client_h]->m_item_list[item_index] == 0) return false;

	switch (m_game->m_client_list[client_h]->m_item_list[item_index]->m_id_num) {
	case 4:  // Dagger +1
	case 9:  // Short Sword +1
	case 13: // Main Gauge +1
	case 16: // Gradius +1
	case 18: // Long Sword +1
	case 19: // Long Sword +2
	case 21: // Excaliber +1
	case 24: // Sabre +1
	case 26: // Scimitar +1
	case 27: // Scimitar +2
	case 29: // Falchoin +1
	case 30: // Falchion +2
	case 32: // Esterk +1
	case 33: // Esterk +2
	case 35: // Rapier +1
	case 36: // Rapier +2
	case 39: // Broad Sword +1
	case 40: // Broad Sword +2
	case 43: // Bastad Sword +1
	case 44: // Bastad Sword +2
	case 47: // Claymore +1
	case 48: // Claymore +2
	case 51: // Great Sword +1
	case 52: // Great Sword +2
	case 55: // Flameberge +1
	case 56: // Flameberge +2
	case 60: // Light Axe +1
	case 61: // Light Axe +2
	case 63: // Tomahoc +1
	case 64: // Tomohoc +2
	case 66: // Sexon Axe +1
	case 67: // Sexon Axe +2
	case 69: // Double Axe +1
	case 70: // Double Axe +2
	case 72: // War Axe +1
	case 73: // War Axe +2

	case 580: // Battle Axe +1
	case 581: // Battle Axe +2
	case 582: // Sabre +2
		return true;
		break;
	}
	return false;
}

void ItemManager::req_create_slate_handler(int client_h, char* data)
{
	int ret;
	char item_id[4], ctr[4];
	char slate_colour;
	bool is_slate_present = false;
	CItem* item;
	int slate_type, erase_req;

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_is_on_server_change) return;

	for(int i = 0; i < 4; i++) {
		item_id[i] = 0;
		ctr[i] = 0;
	}
	const auto* pkt = hb::net::PacketCast<hb::net::PacketCommandCommonItems>(
		data, sizeof(hb::net::PacketCommandCommonItems));
	if (!pkt) return;

	// 14% chance of creating slates
	if (m_game->dice(1, 100) < static_cast<uint32_t>(m_game->m_slate_success_rate)) is_slate_present = true;

	try {
		// make sure slates really exist
		for(int i = 0; i < 4; i++) {
			item_id[i] = static_cast<char>(pkt->item_ids[i]);

			if (m_game->m_client_list[client_h]->m_item_list[item_id[i]] == 0 || item_id[i] > hb::shared::limits::MaxItems) {
				is_slate_present = false;
				m_game->send_notify_msg(0, client_h, Notify::SlateCreateFail, 0, 0, 0, 0);
				return;
			}

			//No duping
			if (m_game->m_client_list[client_h]->m_item_list[item_id[i]]->m_id_num == 868)
				ctr[0] = 1;
			else if (m_game->m_client_list[client_h]->m_item_list[item_id[i]]->m_id_num == 869)
				ctr[1] = 1;
			else if (m_game->m_client_list[client_h]->m_item_list[item_id[i]]->m_id_num == 870)
				ctr[2] = 1;
			else if (m_game->m_client_list[client_h]->m_item_list[item_id[i]]->m_id_num == 871)
				ctr[3] = 1;
		}
	}
	catch (...) {
		//Crash Hacker Caught
		is_slate_present = false;
		m_game->send_notify_msg(0, client_h, Notify::SlateCreateFail, 0, 0, 0, 0);
		hb::logger::warn<log_channel::security>("Slate hack: IP={} player={}, creating slates without required item", m_game->m_client_list[client_h]->m_ip_address, m_game->m_client_list[client_h]->m_char_name);
		m_game->delete_client(client_h, true, true);
		return;
	}

	// Are all 4 slates present ??
	if (ctr[0] != 1 || ctr[1] != 1 || ctr[2] != 1 || ctr[3] != 1) {
		is_slate_present = false;
		return;
	}

	// if we failed, kill everything
	if (!is_slate_present) {
		for(int i = 0; i < 4; i++) {
			if (m_game->m_client_list[client_h]->m_item_list[item_id[i]] != 0) {
				item_deplete_handler(client_h, item_id[i], false);
			}
		}
		m_game->send_notify_msg(0, client_h, Notify::SlateCreateFail, 0, 0, 0, 0);
		return;
	}

	// make the slates
	for(int i = 0; i < 4; i++) {
		if (m_game->m_client_list[client_h]->m_item_list[item_id[i]] != 0) {
			item_deplete_handler(client_h, item_id[i], false);
		}
	}

	int i = m_game->dice(1, 1000);

	if (i < 50) { // Hp slate
		slate_type = 1;
		slate_colour = 32;
	}
	else if (i < 250) { // Bezerk slate
		slate_type = 2;
		slate_colour = 3;
	}
	else if (i < 750) { // Exp slate
		slate_type = 4;
		slate_colour = 7;
	}
	else if (i < 950) { // Mana slate
		slate_type = 3;
		slate_colour = 37;
	}
	else if (i < 1001) { // Hp slate
		slate_type = 1;
		slate_colour = 32;
	}

	// Notify client
	m_game->send_notify_msg(0, client_h, Notify::SlateCreateSuccess, slate_type, 0, 0, 0);

	// Create slates
	// 867 is the slate item; the roll above only chose which flavour it reads as.
	item = create_item(867, item_origin::craft, birth_at(client_h));
	if (item == nullptr) return;

	item->set_touch_effect_type(TouchEffectType::ID);
	item->m_instance.touch_effect_value1 = static_cast<short>(m_game->dice(1, 100000));
	item->m_instance.touch_effect_value2 = static_cast<short>(m_game->dice(1, 100000));
	item->m_instance.touch_effect_value3 = (short)GameClock::GetTimeMS();

	item_log(ItemLogAction::get, client_h, -1, item);

	item->m_instance.special_effect_value2 = slate_type;
	item->m_instance.item_color = slate_colour;
	if (add_client_item_list(client_h, item, &erase_req)) {
		ret = send_item_notify_msg(client_h, Notify::ItemObtained, item, 0);

		if (erase_req == 1) destroy_item(item, destroy_reason::merged, client_h);

		switch (ret) {
		case sock::Event::QueueFull:
		case sock::Event::SocketError:
		case sock::Event::CriticalError:
		case sock::Event::SocketClosed:
			m_game->delete_client(client_h, true, true);
			return;
		}
	}
	else {
		m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->set_item(m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y, item);
		m_game->send_ground_item_event(CommonType::ItemDrop, m_game->m_client_list[client_h]->m_map_index,
			m_game->m_client_list[client_h]->m_x, m_game->m_client_list[client_h]->m_y, item);
		ret = send_item_notify_msg(client_h, Notify::CannotCarryMoreItem, 0, 0);

		switch (ret) {
		case sock::Event::QueueFull:
		case sock::Event::SocketError:
		case sock::Event::CriticalError:
		case sock::Event::SocketClosed:
			m_game->delete_client(client_h, true, true);
			break;
		}
	}
	return;
}

void ItemManager::set_slate_flag(int client_h, short type, bool flag)
{
	if (m_game->m_client_list[client_h] == 0) return;

	if (type == SlateClearNotify) {
		m_game->m_client_list[client_h]->m_status.slate_invincible = false;
		m_game->m_client_list[client_h]->m_status.slate_mana = false;
		m_game->m_client_list[client_h]->m_status.slate_exp = false;
		return;
	}

	if (flag) {
		if (type == 1) { // Invincible slate
			m_game->m_client_list[client_h]->m_status.slate_invincible = true;
		}
		else if (type == 3) { // Mana slate
			m_game->m_client_list[client_h]->m_status.slate_mana = true;
		}
		else if (type == 4) { // Exp slate
			m_game->m_client_list[client_h]->m_status.slate_exp = true;
		}
	}
	else {
		if (m_game->m_client_list[client_h]->m_status.slate_invincible) {
			m_game->m_client_list[client_h]->m_status.slate_invincible = false;
		}
		else if (m_game->m_client_list[client_h]->m_status.slate_mana) {
			m_game->m_client_list[client_h]->m_status.slate_mana = false;
		}
		else if (m_game->m_client_list[client_h]->m_status.slate_exp) {
			m_game->m_client_list[client_h]->m_status.slate_exp = false;
		}
	}

	m_game->send_event_to_near_client_type_a(client_h, hb::shared::owner_class::Player, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
}

void ItemManager::clear_exchange_status(int to_h)
{
	if ((to_h <= 0) || (to_h >= MaxClients)) return;
	if (m_game->m_client_list[to_h] == 0) return;

	if (m_game->m_client_list[to_h]->m_exchange_name)
		m_game->send_notify_msg(0, to_h, Notify::cancel_exchange_item, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0);

	// m_game->m_client_list[to_h]->m_exchange_name    = false;
	m_game->m_client_list[to_h]->m_initial_check_time = false;
	m_game->m_client_list[to_h]->m_alter_item_drop_index = 0;
	//m_game->m_client_list[to_h]->m_exchange_item_index = -1;
	m_game->m_client_list[to_h]->m_exchange_h = 0;

	m_game->m_client_list[to_h]->m_is_exchange_mode = false;

	std::memset(m_game->m_client_list[to_h]->m_exchange_name, 0, sizeof(m_game->m_client_list[to_h]->m_exchange_name));

}

void ItemManager::cancel_exchange_item(int client_h)
{
	int ex_h;

	ex_h = m_game->m_client_list[client_h]->m_exchange_h;
	clear_exchange_status(ex_h);
	clear_exchange_status(client_h);
}

void ItemManager::reload_item_configs()
{
	sqlite3* configDb = nullptr;
	std::string configDbPath;
	bool configDbCreated = false;
	if (!EnsureGameConfigDatabase(&configDb, configDbPath, &configDbCreated) || configDbCreated)
	{
		hb::logger::log("Item config reload failed: gamedata.db unavailable");
		return;
	}

	for(int i = 0; i < MaxItemTypes; i++)
	{
		if (m_game->m_item_config_list[i] != 0)
		{
			delete m_game->m_item_config_list[i];
			m_game->m_item_config_list[i] = 0;
		}
	}

	if (!LoadItemConfigs(configDb, m_game->m_item_config_list, MaxItemTypes))
	{
		hb::logger::log("Item config reload failed");
		CloseGameConfigDatabase(configDb);
		return;
	}

	CloseGameConfigDatabase(configDb);
	m_game->build_magic_manual_index();
	m_game->compute_config_hashes();
	hb::logger::log("Item configs reloaded successfully");
}

// Instance color the client's dk_glare renders as the pulsating Templar glow
constexpr int dark_templar_glow_color = 9;

namespace
{
// Which stone an enchant category consumes. The Merien route is shields and
// armor — verified against the original handler, where those two pass the
// Merien stone with the bonus flag and weapons/wands pass Xelima without it.
// That doubling is already folded into the seeded per-step rates, so the flag
// survives here only to price the crafted-quality uplift correctly.
constexpr bool is_merien_enchant_route(uint8_t category)
{
	return category == enchant_category::shield || category == enchant_category::armor;
}

constexpr short enchant_stone_for_category(uint8_t category)
{
	return is_merien_enchant_route(category) ? ItemId::StoneOfMerien : ItemId::StoneOfXelima;
}

// Endurance growth for a custom-made piece, in percent. The per-category base
// is data (enchant_categories.endurance_growth_pct); this uplift stays
// code-side beside the crafted success bonus (plan Q1).
constexpr int crafted_endurance_growth_pct = 20;

// The two majestic lines — a weapon or wand that evolves every two upgrade
// steps instead of taking a stone. An item on the route with no row here (the
// Dark Knight Great Sword, the LLF Magic Wand) just gains +2 forever.
struct majestic_upgrade_step
{
	short item_id;
	int   evolve_at;     // enchant bonus at which the item changes form
	short evolves_to;    // 0 = final form: it starts glowing instead
};

constexpr majestic_upgrade_step majestic_upgrade_steps[] = {
	{ ItemId::DarkKnightFlameberge,  2, ItemId::DarkKnightGiantSword },
	{ ItemId::DarkKnightGiantSword,  6, ItemId::DarkKnightSword      },
	{ ItemId::DarkKnightSword,      14, 0                            },
	{ ItemId::DarkMageMagicStaff,    2, ItemId::DarkMageMagicWand    },
	{ ItemId::DarkMageMagicStaffW,   2, ItemId::DarkMageMagicWand    },
	{ ItemId::DarkMageMagicWand,     6, ItemId::DarkMageDragonWand   },
	{ ItemId::DarkMageDragonWand,   14, 0                            },
};

const majestic_upgrade_step* find_majestic_upgrade_step(short item_id)
{
	for (const auto& step : majestic_upgrade_steps)
		if (step.item_id == item_id) return &step;
	return nullptr;
}

// Whether this weapon or wand takes the gizon-priced majestic route rather
// than a stone. The Great Sword (battle-mage weapon) and the LLF Magic Wand
// ride the route without ever evolving.
constexpr bool is_majestic_upgrade_item(short item_id)
{
	return item_id == ItemId::DarkKnightGreatSword
		|| item_id == ItemId::DarkKnightFlameberge
		|| item_id == ItemId::DarkKnightGiantSword
		|| item_id == ItemId::DarkKnightSword
		|| item_id == ItemId::MagicWandMS30LLF
		|| item_id == ItemId::DarkMageMagicStaff
		|| item_id == ItemId::DarkMageMagicStaffW
		|| item_id == ItemId::DarkMageMagicWand
		|| item_id == ItemId::DarkMageDragonWand;
}

// Armor and shields the handler refuses outright: the Merien and GM shields,
// and the Merien / Dark Knight / Dark Mage sets, whose upgrade is a quest
// rather than a stone.
constexpr bool is_unenchantable_armor(short item_id)
{
	switch (item_id)
	{
	case ItemId::MerienShield:
	case ItemId::MerienPlateMailM:
	case ItemId::MerienPlateMailW:
	case 700: case 701: case 702: case 704:   // retail ids with no row in items
	case ItemId::DarkKnightHauberkM:
	case ItemId::DarkKnightFullHelmM:
	case ItemId::DarkKnightLeggingsM:
	case ItemId::DarkKnightPlateMailM:
	case ItemId::DarkMageHauberkM:
	case ItemId::DarkMageChainMailM:
	case ItemId::DarkMageLeggingsM:
	case ItemId::DarkKnightHauberkW:
	case ItemId::DarkKnightFullHelmW:
	case ItemId::DarkKnightLeggingsW:
	case ItemId::DarkKnightPlateMailW:
	case ItemId::DarkMageHauberkW:
	case ItemId::DarkMageChainMailW:
	case ItemId::DarkMageLeggingsW:
		return true;
	default:
		return false;
	}
}
} // namespace

// The last inventory slot holding `item_id`, or -1. Last rather than first
// only to keep the retail scan's choice among duplicate stones.
int ItemManager::find_inventory_item(int client_h, short item_id) const
{
	auto* client = m_game->m_client_list[client_h];
	int found = -1;
	for (int i = 0; i < hb::shared::limits::MaxItems; i++)
		if ((client->m_item_list[i] != 0) && (client->m_item_list[i]->m_id_num == item_id))
			found = i;
	return found;
}

// One enchant roll against the seeded per-step rate (basis points out of
// 10000). The old hardcoded 30/25/20/15/10/10/8/8/5/3% ladder now lives in
// `enchant_steps`, with the Merien route's doubling already folded into the
// stored value (spec §10, plan Q1).
//
// The crafted-quality uplift stays code-side, as Q1 ruled. Retail adds it in
// whole percent *before* the Merien doubling, so on that route the uplift
// doubles with everything else — which is why the undoubled percent has to be
// recovered here.
bool ItemManager::roll_stone_enchant_success(int client_h, int item_index,
	uint8_t category, int success_pct)
{
	CItem* item = m_game->m_client_list[client_h]->m_item_list[item_index];
	const bool merien_route = is_merien_enchant_route(category);

	int prob = success_pct;

	const int quality = item->m_instance.special_effect_value2;
	if (item->is_custom_made() && quality > 100)
	{
		const int base_pct = prob / (merien_route ? 200 : 100);
		const int uplift_pct = (base_pct > 20) ? quality / 10
			: (base_pct > 7) ? quality / 20
			: quality / 40;
		prob += uplift_pct * 100 * (merien_route ? 2 : 1);
	}

	if (prob >= static_cast<int>(m_game->dice(1, 10000)))
	{
		item_log(ItemLogAction::UpgradeSuccess, client_h, (int)-1, item);
		return true;
	}

	item_log(ItemLogAction::UpgradeFail, client_h, (int)-1, item);
	return false;
}

// The stone-enchant route: weapons, shields, armor and wands. Caps, per-step
// success and the destroy-on-fail rule all come from enchant_categories /
// enchant_steps (spec §10, §13), so retail's +15-via-stones ceiling and its
// "any failure past +1 destroys the item" rule both die here. Endurance
// growth is kept.
void ItemManager::attempt_stone_enchant(int client_h, int item_index, uint8_t category)
{
	auto* client = m_game->m_client_list[client_h];
	CItem* item = client->m_item_list[item_index];

	const auto& config = m_game->get_tier_config();
	const auto* category_row = config.find_enchant_category(category);
	if (category_row == nullptr)
	{
		// The boot validator requires these rows in both modes, so this only
		// covers a live DB edit that removed one.
		m_game->send_notify_msg(0, client_h, Notify::ItemUpgradeFail, 2, 0, 0, 0);
		return;
	}

	const int value = item->get_enchant_bonus();
	const auto* step = config.find_enchant_step(category, value + 1);
	if ((value >= category_row->cap) || (step == nullptr))
	{
		// Already at this category's cap. Retail rolled anyway, so a maxed
		// item could still be destroyed on the failure branch; the cap is a
		// gate now.
		m_game->send_notify_msg(0, client_h, Notify::ItemUpgradeFail, 1, 0, 0, 0);
		return;
	}

	const int stone_h = find_inventory_item(client_h, enchant_stone_for_category(category));
	if (stone_h == -1)
	{
		// No stone: the attempt is a silent no-op, as it always was.
		m_game->send_item_attribute_change(client_h, item_index, item);
		return;
	}

	if (roll_stone_enchant_success(client_h, item_index, category, step->success_pct) == false)
	{
		m_game->send_item_attribute_change(client_h, item_index, item);
		if (step->destroy_on_fail) item_deplete_handler(client_h, item_index, false,
			destroy_reason::upgrade_break);
		item_deplete_handler(client_h, stone_h, false);
		return;
	}

	item->set_enchant_bonus(value + 1);

	int growth_pct = category_row->endurance_growth_pct;
	if ((growth_pct > 0) && item->is_custom_made()) growth_pct = crafted_endurance_growth_pct;
	if (growth_pct > 0)
	{
		const int grown = item->m_durability + (item->m_durability * growth_pct) / 100;
		item->m_instance.special_effect_value1 = static_cast<short>(grown);
		if (item->m_instance.special_effect_value1 < 0)
			item->m_instance.special_effect_value1 = item->m_durability;
		item->m_durability = item->m_instance.special_effect_value1;
	}

	item_deplete_handler(client_h, stone_h, false);
	m_game->send_item_attribute_change(client_h, item_index, item,
		item->m_instance.special_effect_value1, item->m_instance.special_effect_value2);
}

// Angelic pendants: priced on a per-step gizon-crystal ladder at a flat 70%
// success, capped at +10. Accessories are outside tier scope (spec §1), so
// none of the drift fixes touch this route.
void ItemManager::upgrade_angelic_pendant(int client_h, int item_index)
{
	auto* client = m_game->m_client_list[client_h];
	CItem* item = client->m_item_list[item_index];
	const int value = item->get_enchant_bonus();

	if (item->get_item_type() != item_type::equipment)
	{
		m_game->send_notify_msg(0, client_h, Notify::ItemUpgradeFail, 2, 0, 0, 0);
		return; // Pendants are type Equip
	}
	if (item->m_equip_pos < 11)
	{
		m_game->send_notify_msg(0, client_h, Notify::ItemUpgradeFail, 2, 0, 0, 0);
		return; // Pendants are left finger or more
	}
	if (item->get_item_effect_type() != ItemEffectType::add_effect)
	{
		m_game->send_notify_msg(0, client_h, Notify::ItemUpgradeFail, 2, 0, 0, 0);
		return; // Pendants are EffectType add_effect
	}

	switch (item->m_item_effect_value1)
	{
	case 16: // AngelicPendant(STR)
	case 17: // AngelicPendant(DEX)
	case 18: // AngelicPendant(INT)
	case 19: // AngelicPendant(MAG)
		break;
	default: // Other items are not upgradable
		m_game->send_notify_msg(0, client_h, Notify::ItemUpgradeFail, 2, 0, 0, 0);
		return;
	}

	if (client->m_gizon_item_upgrade_left <= 0)
	{
		m_game->send_notify_msg(0, client_h, Notify::ItemUpgradeFail, 3, 0, 0, 0);
		return;
	}

	static constexpr short pendant_upgrade_cost[] = { 10, 11, 13, 16, 20, 25, 31, 38, 46, 55 };
	static constexpr int pendant_upgrade_cap = 10;   // rows above; the pendant cap
	if (value >= pendant_upgrade_cap)
	{
		m_game->send_notify_msg(0, client_h, Notify::ItemUpgradeFail, 3, 0, 0, 0);
		return;
	}
	const short item_upgrade = pendant_upgrade_cost[value];

	if ((item->m_instance.touch_effect_value1 != client->m_char_id_num1)
		|| (item->m_instance.touch_effect_value2 != client->m_char_id_num2)
		|| (item->m_instance.touch_effect_value3 != client->m_char_id_num3))
	{
		m_game->send_notify_msg(0, client_h, Notify::ItemUpgradeFail, 2, 0, 0, 0);
		return;
	}
	if ((client->m_gizon_item_upgrade_left - item_upgrade) < 0)
	{
		m_game->send_notify_msg(0, client_h, Notify::ItemUpgradeFail, 3, 0, 0, 0);
		return;
	}

	if (m_game->dice(1, 100) <= 70)
	{
		client->m_gizon_item_upgrade_left -= item_upgrade;
		m_game->send_notify_msg(0, client_h, Notify::GizonItemUpgradeLeft, client->m_gizon_item_upgrade_left, 0, 0, 0);
		item->set_enchant_bonus(value + 1);

		m_game->send_item_attribute_change(client_h, item_index, item);
		item_log(ItemLogAction::UpgradeSuccess, client_h, (int)-1, item);
	}
	else
	{
		client->m_gizon_item_upgrade_left--;
		m_game->send_notify_msg(0, client_h, Notify::GizonItemUpgradeLeft, client->m_gizon_item_upgrade_left, 0, 0, 0);
		m_game->send_notify_msg(0, client_h, Notify::ItemUpgradeFail, 3, 0, 0, 0);
	}
}

// The Dark Knight blade and Dark Mage staff/wand lines: +2 per upgrade on the
// official x(x+6)/8+2 gizon curve, evolving at fixed steps and glowing at
// +15. Never fails, never takes a stone.
void ItemManager::upgrade_majestic_item(int client_h, int item_index)
{
	auto* client = m_game->m_client_list[client_h];
	CItem* item = client->m_item_list[item_index];
	const int value = item->get_enchant_bonus();

	if (client->m_gizon_item_upgrade_left <= 0)
	{
		m_game->send_notify_msg(0, client_h, Notify::ItemUpgradeFail, 3, 0, 0, 0);
		return;
	}

	// The first upgrade binds the item to its owner, so only an already-bound
	// item has to match; an unclaimed one is claimed below.
	if ((value != 0)
		&& ((item->m_instance.touch_effect_value1 != client->m_char_id_num1)
			|| (item->m_instance.touch_effect_value2 != client->m_char_id_num2)
			|| (item->m_instance.touch_effect_value3 != client->m_char_id_num3)))
	{
		m_game->send_notify_msg(0, client_h, Notify::ItemUpgradeFail, 2, 0, 0, 0);
		return;
	}

	const int item_upgrade = (value * (value + 6) / 8) + 2;
	if ((client->m_gizon_item_upgrade_left - item_upgrade) < 0)
	{
		m_game->send_notify_msg(0, client_h, Notify::ItemUpgradeFail, 3, 0, 0, 0);
		return;
	}

	client->m_gizon_item_upgrade_left -= item_upgrade;
	m_game->send_notify_msg(0, client_h, Notify::GizonItemUpgradeLeft, client->m_gizon_item_upgrade_left, 0, 0, 0);

	if (value == 0)
	{
		item->set_touch_effect_type(TouchEffectType::UniqueOwner);
		item->m_instance.touch_effect_value1 = client->m_char_id_num1;
		item->m_instance.touch_effect_value2 = client->m_char_id_num2;
		item->m_instance.touch_effect_value3 = client->m_char_id_num3;
	}

	const int next_value = (value + 2 > 15) ? 15 : value + 2;
	const auto* step = find_majestic_upgrade_step(item->m_id_num);

	if ((step != nullptr) && (value >= step->evolve_at))
	{
		if (step->evolves_to != 0)
		{
			transform_majestic_item(client_h, item_index, step->evolves_to, next_value, 0);
			return;
		}

		// Maxed out — the final form starts glowing.
		item->set_enchant_bonus(15);
		item->m_instance.item_color = dark_templar_glow_color;

		m_game->send_gizon_item_change(client_h, item_index, item);
		item_log(ItemLogAction::UpgradeSuccess, client_h, (int)-1, item);
		return;
	}

	item->set_enchant_bonus(next_value);
	m_game->send_item_attribute_change(client_h, item_index, item);
	item_log(ItemLogAction::UpgradeSuccess, client_h, (int)-1, item);
}

// The hero cape: a one-shot transform into its +1 form, priced in war
// contribution and enemy kills plus a Stone of Merien. Reachable again now
// that the route derives from derive_tier_item_class — the old hand-rolled
// chain tested item_sub_type::armor before EquipPos::Back, so capes fell into
// the armor route and this one was dead code. A cape that already carries a
// bonus (only reachable through that bug) drops out before anything is spent;
// retail charged the contribution first and then did nothing.
void ItemManager::upgrade_hero_cape(int client_h, int item_index)
{
	auto* client = m_game->m_client_list[client_h];
	const short cape_id = client->m_item_list[item_index]->m_id_num;

	if ((cape_id != ItemId::AresdenHeroCape) && (cape_id != ItemId::ElvineHeroCape)) return;
	if (client->m_item_list[item_index]->get_enchant_bonus() != 0) return;

	const int stone_h = find_inventory_item(client_h, ItemId::StoneOfMerien);
	if (stone_h == -1) return;

	if ((client->m_item_list[item_index]->m_instance.touch_effect_value1 != client->m_char_id_num1)
		|| (client->m_item_list[item_index]->m_instance.touch_effect_value2 != client->m_char_id_num2)
		|| (client->m_item_list[item_index]->m_instance.touch_effect_value3 != client->m_char_id_num3))
	{
		m_game->send_notify_msg(0, client_h, Notify::ItemUpgradeFail, 2, 0, 0, 0);
		return;
	}

	if ((client->m_contribution < 50) || (client->m_enemy_kill_count < 50))
	{
		m_game->send_notify_msg(0, client_h, Notify::ItemUpgradeFail, 3, 0, 0, 0);
		return;
	}

	CItem* old_cape = client->m_item_list[item_index];
	if (old_cape == nullptr) return;

	const short upgraded_id = (cape_id == ItemId::AresdenHeroCape)
		? ItemId::AresdenHeroCapePlus1 : ItemId::ElvineHeroCapePlus1;

	// Built before anything is spent or destroyed. The old order deducted the
	// contribution, deleted the cape, and only then discovered the upgraded id
	// had no config row — leaving the player short 50 EK and holding a blank
	// item. Nothing is committed until the replacement exists.
	CItem* upgraded = transform_item(upgraded_id, *old_cape);
	if (upgraded == nullptr) return;

	client->m_contribution -= 50;
	client->m_enemy_kill_count -= 50;
	m_game->send_notify_msg(0, client_h, Notify::EnemyKills, client->m_enemy_kill_count, 0, 0, 0);

	const int item_x = client->m_item_pos_list[item_index].x;
	const int item_y = client->m_item_pos_list[item_index].y;

	delete old_cape;
	client->m_item_list[item_index] = upgraded;

	client->m_item_pos_list[item_index].x = item_x;
	client->m_item_pos_list[item_index].y = item_y;

	upgraded->set_touch_effect_type(TouchEffectType::UniqueOwner);
	upgraded->m_instance.touch_effect_value1 = client->m_char_id_num1;
	upgraded->m_instance.touch_effect_value2 = client->m_char_id_num2;
	upgraded->m_instance.touch_effect_value3 = client->m_char_id_num3;

	item_deplete_handler(client_h, stone_h, false);

	m_game->send_gizon_item_change(client_h, item_index, upgraded);
	item_log(ItemLogAction::UpgradeSuccess, client_h, (int)-1, upgraded);
}

void ItemManager::request_item_upgrade_handler(int client_h, int item_index)
{
	if (m_game->m_client_list[client_h] == 0) return;
	if ((item_index < 0) || (item_index >= hb::shared::limits::MaxItems)) return;
	if (m_game->m_client_list[client_h]->m_item_list[item_index] == 0) return;

	CItem* item = m_game->m_client_list[client_h]->m_item_list[item_index];

	const int value = item->get_enchant_bonus();
	if ((value >= 15) || (value < 0))
	{
		m_game->send_notify_msg(0, client_h, Notify::ItemUpgradeFail, 1, 0, 0, 0);
		return;
	}

	// Accessories sit outside tier scope, so derive_tier_item_class folds them
	// in with boots and the rest; the gizon pendant route keys off the
	// sub-type directly.
	if (item->get_item_sub_type() == item_sub_type::accessory)
	{
		upgrade_angelic_pendant(client_h, item_index);
		return;
	}

	const auto item_class = derive_tier_item_class(item->get_item_sub_type(),
		item->get_weapon_class(), item->get_equip_pos());

	switch (item_class)
	{
	case tier_item_class::melee_weapon:
	case tier_item_class::wand:
		if (is_majestic_upgrade_item(item->m_id_num))
		{
			upgrade_majestic_item(client_h, item_index);
			return;
		}
		// Retail refuses to enchant an Ancient-prefixed weapon. In tiered mode
		// Ancient is a rollable prefix like any other, so the block would
		// arbitrarily bar a whole slice of dropped gear (spec §13).
		if ((m_game->get_tier_config().item_system != item_system_mode::tiered)
			&& (item->get_prefix_type() == modifier_id::ancient))
		{
			m_game->send_notify_msg(0, client_h, Notify::ItemUpgradeFail, 2, 0, 0, 0);
			return;
		}
		break;

	case tier_item_class::shield:
	case tier_item_class::helm:
	case tier_item_class::body_armor:
	case tier_item_class::leggings:
		if (is_unenchantable_armor(item->m_id_num)
			|| (item->get_prefix_type() == modifier_id::strong))
		{
			m_game->send_notify_msg(0, client_h, Notify::ItemUpgradeFail, 2, 0, 0, 0);
			return;
		}
		break;

	case tier_item_class::cape:
		upgrade_hero_cape(client_h, item_index);
		return;

	default: // bows never had an upgrade route; boots and the rest have none
		m_game->send_item_attribute_change(client_h, item_index, item);
		return;
	}

	attempt_stone_enchant(client_h, item_index,
		enchant_category_for_item_class(item_class, item->is_custom_made()));
}

char ItemManager::check_hero_item_equipped(int client_h)
{
	short hero_leggings, hero_hauberk, hero_armor, hero_helm;

	if (m_game->m_client_list[client_h] == 0) return 0;

	hero_helm = m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::Head)];
	hero_armor = m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::Body)];
	hero_hauberk = m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::Arms)];
	hero_leggings = m_game->m_client_list[client_h]->m_item_equipment_status[to_int(EquipPos::Leggings)];

	if ((hero_helm < 0) || (hero_leggings < 0) || (hero_armor < 0) || (hero_hauberk < 0)) return 0;

	if (m_game->m_client_list[client_h]->m_item_list[hero_helm] == 0) return 0;
	if (m_game->m_client_list[client_h]->m_item_list[hero_leggings] == 0) return 0;
	if (m_game->m_client_list[client_h]->m_item_list[hero_armor] == 0) return 0;
	if (m_game->m_client_list[client_h]->m_item_list[hero_hauberk] == 0) return 0;

	if ((m_game->m_client_list[client_h]->m_item_list[hero_helm]->m_id_num == 403) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_armor]->m_id_num == 411) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_hauberk]->m_id_num == 419) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_leggings]->m_id_num == 423)) return 1;

	if ((m_game->m_client_list[client_h]->m_item_list[hero_helm]->m_id_num == 407) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_armor]->m_id_num == 415) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_hauberk]->m_id_num == 419) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_leggings]->m_id_num == 423)) return 2;

	if ((m_game->m_client_list[client_h]->m_item_list[hero_helm]->m_id_num == 404) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_armor]->m_id_num == 412) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_hauberk]->m_id_num == 420) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_leggings]->m_id_num == 424)) return 1;

	if ((m_game->m_client_list[client_h]->m_item_list[hero_helm]->m_id_num == 408) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_armor]->m_id_num == 416) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_hauberk]->m_id_num == 420) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_leggings]->m_id_num == 424)) return 2;

	if ((m_game->m_client_list[client_h]->m_item_list[hero_helm]->m_id_num == 405) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_armor]->m_id_num == 413) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_hauberk]->m_id_num == 421) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_leggings]->m_id_num == 425)) return 1;

	if ((m_game->m_client_list[client_h]->m_item_list[hero_helm]->m_id_num == 409) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_armor]->m_id_num == 417) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_hauberk]->m_id_num == 421) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_leggings]->m_id_num == 425)) return 2;

	if ((m_game->m_client_list[client_h]->m_item_list[hero_helm]->m_id_num == 406) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_armor]->m_id_num == 414) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_hauberk]->m_id_num == 422) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_leggings]->m_id_num == 426)) return 1;

	if ((m_game->m_client_list[client_h]->m_item_list[hero_helm]->m_id_num == 410) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_armor]->m_id_num == 418) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_hauberk]->m_id_num == 422) &&
		(m_game->m_client_list[client_h]->m_item_list[hero_leggings]->m_id_num == 426)) return 2;

	return 0;
}

bool ItemManager::plant_seed_bag(int map_index, int dX, int dY, int item_effect_value1, int item_effect_value2, int client_h)
{
	int naming_value, tX, tY;
	short owner_h;
	char owner_type, npc_name[hb::shared::limits::NpcNameLen], name[hb::shared::limits::NpcNameLen], npc_waypoint_index[11];
	int ret;

	if (m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_total_agriculture >= 200) {
		m_game->send_notify_msg(0, client_h, Notify::NoMoreAgriculture, 0, 0, 0, 0);
		return false;
	}

	if (item_effect_value2 > m_game->m_client_list[client_h]->m_skill_mastery[2]) {
		m_game->send_notify_msg(0, client_h, Notify::AgricultureSkillLimit, 0, 0, 0, 0);
		return false;
	}

	naming_value = m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->get_empty_naming_value();

	if (naming_value == -1) {
	}
	else {
		m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->get_owner(&owner_h, &owner_type, dX, dY);
		if (owner_h != 0 && owner_h == hb::shared::owner_class::Npc && m_game->m_npc_list[owner_h]->m_action_limit == 5) {
			m_game->send_notify_msg(0, client_h, Notify::AgricultureNoArea, 0, 0, 0, 0);
			return false;
		}
		else {
			if (m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->get_is_farm(dX, dY) == false) {
				m_game->send_notify_msg(0, client_h, Notify::AgricultureNoArea, 0, 0, 0, 0);
				return false;
			}

			int npc_config_id = m_game->get_npc_config_id_by_name("Crops");
			std::memset(name, 0, sizeof(name));
			std::snprintf(name, sizeof(name), "XX%d", naming_value);
			name[0] = '_';
			name[1] = map_index + 65;

			std::memset(npc_waypoint_index, 0, sizeof(npc_waypoint_index));
			tX = dX;
			tY = dY;

			ret = m_game->create_new_npc(npc_config_id, name, m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_name, 0, 0, MoveType::Random, &tX, &tY, npc_waypoint_index, 0, 0, 0, false, true);
			if (ret == false) {
				m_game->m_map_list[map_index]->set_naming_value_empty(naming_value);
			}
			else {
				m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->get_owner(&owner_h, &owner_type, tX, tY);
				if (m_game->m_npc_list[owner_h] == 0) return 0;
				m_game->m_npc_list[owner_h]->m_crop_type = item_effect_value1;
				switch (item_effect_value1) {
				case 1: m_game->m_npc_list[owner_h]->m_crop_skill = item_effect_value2; break;
				case 2: m_game->m_npc_list[owner_h]->m_crop_skill = item_effect_value2; break;
				case 3: m_game->m_npc_list[owner_h]->m_crop_skill = item_effect_value2; break;
				case 4: m_game->m_npc_list[owner_h]->m_crop_skill = item_effect_value2; break;
				case 5: m_game->m_npc_list[owner_h]->m_crop_skill = item_effect_value2; break;
				case 6: m_game->m_npc_list[owner_h]->m_crop_skill = item_effect_value2; break;
				case 7: m_game->m_npc_list[owner_h]->m_crop_skill = item_effect_value2; break;
				case 8: m_game->m_npc_list[owner_h]->m_crop_skill = item_effect_value2; break;
				case 9: m_game->m_npc_list[owner_h]->m_crop_skill = item_effect_value2; break;
				case 10: m_game->m_npc_list[owner_h]->m_crop_skill = item_effect_value2; break;
				case 11: m_game->m_npc_list[owner_h]->m_crop_skill = item_effect_value2; break;
				case 12: m_game->m_npc_list[owner_h]->m_crop_skill = item_effect_value2; break;
				case 13: m_game->m_npc_list[owner_h]->m_crop_skill = item_effect_value2; break;
				default: m_game->m_npc_list[owner_h]->m_crop_skill = 100; break;
				}
				m_game->m_npc_list[owner_h]->m_appearance.special_frame = 1;
				m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Npc, MsgId::EventLog, MsgType::Confirm, 0, 0, 0);
				hb::logger::log("Agriculture: skill={} type={} plant={} at ({},{}) total={}", m_game->m_npc_list[owner_h]->m_crop_skill, m_game->m_npc_list[owner_h]->m_crop_type, npc_name, tX, tY, m_game->m_map_list[m_game->m_client_list[client_h]->m_map_index]->m_total_agriculture);
				return true;
			}
		}
	}
	return false;
}

void ItemManager::request_repair_all_items_handler(int client_h)
{
	int price;
	double d1, d2, d3;
	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_is_init_complete == false) return;

	m_game->m_client_list[client_h]->total_item_repair = 0;

	for(int i = 0; i < hb::shared::limits::MaxItems; i++) {
		if (m_game->m_client_list[client_h]->m_item_list[i] != 0) {

			if (m_game->m_client_list[client_h]->m_item_list[i]->m_durability > 0)
			{
				if (m_game->m_client_list[client_h]->m_item_list[i]->m_instance.cur_durability == m_game->m_client_list[client_h]->m_item_list[i]->m_durability)
					continue;
				if (m_game->m_client_list[client_h]->m_item_list[i]->m_instance.cur_durability <= 0)
					price = (m_game->m_client_list[client_h]->m_item_list[i]->m_sell_price / 2);
				else
				{
					d1 = (double)(m_game->m_client_list[client_h]->m_item_list[i]->m_instance.cur_durability);
					if (m_game->m_client_list[client_h]->m_item_list[i]->m_durability != 0)
						d2 = (double)(m_game->m_client_list[client_h]->m_item_list[i]->m_durability);
					else
						d2 = (double)1.0f;
					d3 = (double)((d1 / d2) * 0.5f);
					d2 = (double)(m_game->m_client_list[client_h]->m_item_list[i]->m_sell_price);
					d3 = (d3 * d2);
					price = ((m_game->m_client_list[client_h]->m_item_list[i]->m_sell_price / 2) - static_cast<int32_t>(d3));
				}
				m_game->m_client_list[client_h]->m_repair_all[m_game->m_client_list[client_h]->total_item_repair].index = i;
				m_game->m_client_list[client_h]->m_repair_all[m_game->m_client_list[client_h]->total_item_repair].price = price;
				m_game->m_client_list[client_h]->total_item_repair++;
			}
		}
	}
	m_game->send_notify_msg(0, client_h, Notify::RepairAllPrices, 0, 0, 0, 0);
}

void ItemManager::request_repair_all_items_delete_handler(int client_h, int index)
{
	
	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_is_init_complete == false) return;

	for(int i = index; i < m_game->m_client_list[client_h]->total_item_repair; i++) {
		m_game->m_client_list[client_h]->m_repair_all[i] = m_game->m_client_list[client_h]->m_repair_all[i + 1];
	}
	m_game->m_client_list[client_h]->total_item_repair--;
	m_game->send_notify_msg(0, client_h, Notify::RepairAllPrices, 0, 0, 0, 0);
}

void ItemManager::request_repair_all_items_confirm_handler(int client_h)
{
	int      ret, totalPrice = 0;

	if (m_game->m_client_list[client_h] == 0) return;
	if (m_game->m_client_list[client_h]->m_is_init_complete == false) return;
	if (m_game->m_client_list[client_h]->m_is_processing_allowed == false) return;

	for(int i = 0; i < m_game->m_client_list[client_h]->total_item_repair; i++) {
		totalPrice += m_game->m_client_list[client_h]->m_repair_all[i].price;
	}

	if (get_item_count_by_id(client_h, hb::shared::item::ItemId::Gold) < static_cast<uint64_t>(totalPrice))
	{
		{
			hb::net::PacketNotifyNotEnoughGold pkt{};
			pkt.header.msg_id = MsgId::Notify;
			pkt.header.msg_type = Notify::NotEnoughGold;
			pkt.item_index = 0;
			ret = m_game->m_client_list[client_h]->m_socket->send_msg(reinterpret_cast<char*>(&pkt), sizeof(pkt));
		}
		switch (ret) {
		case sock::Event::QueueFull:
		case sock::Event::SocketError:
		case sock::Event::CriticalError:
		case sock::Event::SocketClosed:
			m_game->delete_client(client_h, true, true);
			break;
		}

	}
	else
	{
		for(int i = 0; i < m_game->m_client_list[client_h]->total_item_repair; i++)
		{
			if (m_game->m_client_list[client_h]->m_item_list[m_game->m_client_list[client_h]->m_repair_all[i].index] != 0) {
				m_game->m_client_list[client_h]->m_item_list[m_game->m_client_list[client_h]->m_repair_all[i].index]->m_instance.cur_durability = m_game->m_client_list[client_h]->m_item_list[m_game->m_client_list[client_h]->m_repair_all[i].index]->m_durability;
				m_game->send_notify_msg(0, client_h, Notify::ItemRepaired, m_game->m_client_list[client_h]->m_repair_all[i].index, m_game->m_client_list[client_h]->m_item_list[m_game->m_client_list[client_h]->m_repair_all[i].index]->m_instance.cur_durability, 0, 0);
			}
		}
		// The same bill as the single-item repair above, so the same number
		// (#103): one query answers "gold spent on repairs" whichever door the
		// player used.
		//
		// !!! BUG POINT — calc_total_weight wants a client handle and is handed
		// set_item_count_by_id's return, which is a weight. The player's carried
		// load is therefore not recalculated after a repair-all. Faithful to the
		// original (HGServer Game.cpp:57630 has the identical line), so it is
		// left as-is here rather than fixed in passing by a ledger ticket.
		m_game->calc_total_weight(set_item_count_by_id(client_h, hb::shared::item::ItemId::Gold,
			get_item_count_by_id(client_h, hb::shared::item::ItemId::Gold) - totalPrice,
			ItemLogAction::Repair));
	}
}
