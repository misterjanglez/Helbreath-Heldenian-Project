#include "StatusEffectManager.h"
#include "Game.h"
#include "Client.h"
#include "Npc.h"
#include "Item.h"
#include "ItemManager.h"
#include "CombatManager.h"
#include "Packet/SharedPackets.h"
#include "ObjectIDRange.h"

#include <cstdio>
#include <cstring>

using namespace hb::shared::net;
using namespace hb::shared::action;
using namespace hb::server::net;
using namespace hb::server::config;
using namespace hb::server::npc;
namespace sock = hb::shared::net::socket;

extern char G_cTxt[512];

void StatusEffectManager::set_hero_flag(short owner_h, char owner_type, bool status)
{
	switch (owner_type) {
	case hb::shared::owner_class::Player:
		if (m_game->m_client_list[owner_h] == 0) return;
		if (status)
			m_game->m_client_list[owner_h]->m_status.hero = true;
		else m_game->m_client_list[owner_h]->m_status.hero = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Player, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;

	case hb::shared::owner_class::Npc:
		if (m_game->m_npc_list[owner_h] == 0) return;
		if (status)
			m_game->m_npc_list[owner_h]->m_status.hero = true;
		else m_game->m_npc_list[owner_h]->m_status.hero = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Npc, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;
	}
}

void StatusEffectManager::set_berserk_flag(short owner_h, char owner_type, bool status)
{
	switch (owner_type) {
	case hb::shared::owner_class::Player:
		if (m_game->m_client_list[owner_h] == 0) return;
		if (status)
			m_game->m_client_list[owner_h]->m_status.berserk = true;
		else m_game->m_client_list[owner_h]->m_status.berserk = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Player, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;

	case hb::shared::owner_class::Npc:
		if (m_game->m_npc_list[owner_h] == 0) return;
		if (status)
			m_game->m_npc_list[owner_h]->m_status.berserk = true;
		else m_game->m_npc_list[owner_h]->m_status.berserk = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Npc, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;
	}
}

void StatusEffectManager::set_haste_flag(short owner_h, char owner_type, bool status)
{
	switch (owner_type) {
	case hb::shared::owner_class::Player:
		if (m_game->m_client_list[owner_h] == 0) return;
		if (status)
			m_game->m_client_list[owner_h]->m_status.haste = true;
		else m_game->m_client_list[owner_h]->m_status.haste = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Player, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;

	case hb::shared::owner_class::Npc:
		break;
	}
}

void StatusEffectManager::set_poison_flag(short owner_h, char owner_type, bool status)
{
	switch (owner_type) {
	case hb::shared::owner_class::Player:
		if (m_game->m_client_list[owner_h] == 0) return;
		if (status)
			m_game->m_client_list[owner_h]->m_status.poisoned = true;
		else m_game->m_client_list[owner_h]->m_status.poisoned = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Player, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;

	case hb::shared::owner_class::Npc:
		if (m_game->m_npc_list[owner_h] == 0) return;
		if (status)
			m_game->m_npc_list[owner_h]->m_status.poisoned = true;
		else m_game->m_npc_list[owner_h]->m_status.poisoned = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Npc, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;
	}
}

void StatusEffectManager::set_defense_shield_flag(short owner_h, char owner_type, bool status)
{
	switch (owner_type) {
	case hb::shared::owner_class::Player:
		if (m_game->m_client_list[owner_h] == 0) return;
		if (status)
			m_game->m_client_list[owner_h]->m_status.defense_shield = true;
		else m_game->m_client_list[owner_h]->m_status.defense_shield = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Player, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;

	case hb::shared::owner_class::Npc:
		if (m_game->m_npc_list[owner_h] == 0) return;
		if (status)
			m_game->m_npc_list[owner_h]->m_status.defense_shield = true;
		else m_game->m_npc_list[owner_h]->m_status.defense_shield = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Npc, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;
	}
}

void StatusEffectManager::set_magic_protection_flag(short owner_h, char owner_type, bool status)
{
	switch (owner_type) {
	case hb::shared::owner_class::Player:
		if (m_game->m_client_list[owner_h] == 0) return;
		if (status)
			m_game->m_client_list[owner_h]->m_status.magic_protection = true;
		else m_game->m_client_list[owner_h]->m_status.magic_protection = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Player, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;

	case hb::shared::owner_class::Npc:
		if (m_game->m_npc_list[owner_h] == 0) return;
		if (status)
			m_game->m_npc_list[owner_h]->m_status.magic_protection = true;
		else m_game->m_npc_list[owner_h]->m_status.magic_protection = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Npc, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;
	}
}

void StatusEffectManager::set_protection_from_arrow_flag(short owner_h, char owner_type, bool status)
{
	switch (owner_type) {
	case hb::shared::owner_class::Player:
		if (m_game->m_client_list[owner_h] == 0) return;
		if (status)
			m_game->m_client_list[owner_h]->m_status.protection_from_arrow = true;
		else m_game->m_client_list[owner_h]->m_status.protection_from_arrow = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Player, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;

	case hb::shared::owner_class::Npc:
		if (m_game->m_npc_list[owner_h] == 0) return;
		if (status)
			m_game->m_npc_list[owner_h]->m_status.protection_from_arrow = true;
		else m_game->m_npc_list[owner_h]->m_status.protection_from_arrow = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Npc, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;
	}
}

void StatusEffectManager::set_illusion_movement_flag(short owner_h, char owner_type, bool status)
{
	switch (owner_type) {
	case hb::shared::owner_class::Player:
		if (m_game->m_client_list[owner_h] == 0) return;
		if (status)
			m_game->m_client_list[owner_h]->m_status.illusion_movement = true;
		else m_game->m_client_list[owner_h]->m_status.illusion_movement = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Player, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;
	}
}

void StatusEffectManager::set_illusion_flag(short owner_h, char owner_type, bool status)
{
	switch (owner_type) {
	case hb::shared::owner_class::Player:
		if (m_game->m_client_list[owner_h] == 0) return;
		if (status)
			m_game->m_client_list[owner_h]->m_status.illusion = true;
		else m_game->m_client_list[owner_h]->m_status.illusion = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Player, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;

	case hb::shared::owner_class::Npc:
		if (m_game->m_npc_list[owner_h] == 0) return;
		if (status)
			m_game->m_npc_list[owner_h]->m_status.illusion = true;
		else m_game->m_npc_list[owner_h]->m_status.illusion = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Npc, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;
	}
}

void StatusEffectManager::set_ice_flag(short owner_h, char owner_type, bool status)
{
	switch (owner_type) {
	case hb::shared::owner_class::Player:
		if (m_game->m_client_list[owner_h] == 0) return;
		if (status)
			m_game->m_client_list[owner_h]->m_status.frozen = true;
		else m_game->m_client_list[owner_h]->m_status.frozen = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Player, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;

	case hb::shared::owner_class::Npc:
		if (m_game->m_npc_list[owner_h] == 0) return;
		if (status)
			m_game->m_npc_list[owner_h]->m_status.frozen = true;
		else m_game->m_npc_list[owner_h]->m_status.frozen = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Npc, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;
	}
}

void StatusEffectManager::set_invisibility_flag(short owner_h, char owner_type, bool status)
{
	switch (owner_type) {
	case hb::shared::owner_class::Player:
		if (m_game->m_client_list[owner_h] == 0) return;
		if (status)
			m_game->m_client_list[owner_h]->m_status.invisibility = true;
		else m_game->m_client_list[owner_h]->m_status.invisibility = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Player, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;

	case hb::shared::owner_class::Npc:
		if (m_game->m_npc_list[owner_h] == 0) return;
		if (status)
			m_game->m_npc_list[owner_h]->m_status.invisibility = true;
		else m_game->m_npc_list[owner_h]->m_status.invisibility = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Npc, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;
	}
}

void StatusEffectManager::set_inhibition_casting_flag(short owner_h, char owner_type, bool status)
{
	switch (owner_type) {
	case hb::shared::owner_class::Player:
		if (m_game->m_client_list[owner_h] == 0) return;
		if (status)
			m_game->m_client_list[owner_h]->m_status.inhibition_casting = true;
		else m_game->m_client_list[owner_h]->m_status.inhibition_casting = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Player, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;

	case hb::shared::owner_class::Npc:
		if (m_game->m_npc_list[owner_h] == 0) return;
		if (status)
			m_game->m_npc_list[owner_h]->m_status.inhibition_casting = true;
		else m_game->m_npc_list[owner_h]->m_status.inhibition_casting = false;
		m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Npc, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
		break;
	}
}

void StatusEffectManager::set_angel_flag(short owner_h, char owner_type, int status, int temp)
{
	if (owner_type != hb::shared::owner_class::Player) return;
	if (m_game->m_client_list[owner_h] == 0) return;
	switch (status) {
	case 1: // STR Angel
		m_game->m_client_list[owner_h]->m_status.angel_str = true;
		break;
	case 2: // DEX Angel
		m_game->m_client_list[owner_h]->m_status.angel_dex = true;
		break;
	case 3: // INT Angel
		m_game->m_client_list[owner_h]->m_status.angel_int = true;
		break;
	case 4: // MAG Angel
		m_game->m_client_list[owner_h]->m_status.angel_mag = true;
		break;
	default:
	case 0: // Remove all Angels
		m_game->m_client_list[owner_h]->m_status.angel_percent = 0;
		m_game->m_client_list[owner_h]->m_status.angel_str = false;
		m_game->m_client_list[owner_h]->m_status.angel_dex = false;
		m_game->m_client_list[owner_h]->m_status.angel_int = false;
		m_game->m_client_list[owner_h]->m_status.angel_mag = false;
		break;
	}
	if (temp > 4)
	{
		int angelic_stars = (temp / 3) * (temp / 5);
		m_game->m_client_list[owner_h]->m_status.angel_percent = static_cast<uint8_t>(angelic_stars);
	}
	m_game->send_event_to_near_client_type_a(owner_h, hb::shared::owner_class::Player, MsgId::EventMotion, Type::NullAction, 0, 0, 0);
}

void StatusEffectManager::check_farming_action(short attacker_h, short target_h, bool type)
{
	char crop_type;
	int item_id;
	CItem* item;

	item_id = 0;
	crop_type = 0;

	crop_type = m_game->m_npc_list[target_h]->m_crop_type;
	switch (crop_type) {
	case 1: m_game->get_exp(attacker_h, m_game->dice(3, 10)); item_id = 820; break; // WaterMelon
	case 2: m_game->get_exp(attacker_h, m_game->dice(3, 10)); item_id = 821; break; // Pumpkin
	case 3: m_game->get_exp(attacker_h, m_game->dice(4, 10)); item_id = 822; break; // Garlic
	case 4: m_game->get_exp(attacker_h, m_game->dice(4, 10)); item_id = 823; break; // Barley
	case 5: m_game->get_exp(attacker_h, m_game->dice(5, 10)); item_id = 824; break; // Carrot
	case 6: m_game->get_exp(attacker_h, m_game->dice(5, 10)); item_id = 825; break; // Radish
	case 7: m_game->get_exp(attacker_h, m_game->dice(6, 10)); item_id = 826; break; // Corn
	case 8: m_game->get_exp(attacker_h, m_game->dice(6, 10)); item_id = 827; break; // ChineseBellflower
	case 9: m_game->get_exp(attacker_h, m_game->dice(7, 10)); item_id = 828; break; // Melone
	case 10: m_game->get_exp(attacker_h, m_game->dice(7, 10)); item_id = 829; break; // Tommato
	case 11: m_game->get_exp(attacker_h, m_game->dice(8, 10)); item_id = 830; break; // Grapes
	case 12: m_game->get_exp(attacker_h, m_game->dice(8, 10)); item_id = 831; break; // BlueGrapes
	case 13: m_game->get_exp(attacker_h, m_game->dice(9, 10)); item_id = 832; break; // Mushroom
	default: m_game->get_exp(attacker_h, m_game->dice(10, 10)); item_id = 721; break; // Ginseng

	}

	item = new CItem;
	if (m_game->m_item_manager->init_item_attr(item, item_id) == false) {
		delete item;
	}
	if (type == 0) {
		m_game->m_map_list[m_game->m_client_list[attacker_h]->m_map_index]->set_item(m_game->m_client_list[attacker_h]->m_x, m_game->m_client_list[attacker_h]->m_y, item);
		m_game->send_ground_item_event(CommonType::ItemDrop, m_game->m_client_list[attacker_h]->m_map_index,
			m_game->m_client_list[attacker_h]->m_x, m_game->m_client_list[attacker_h]->m_y, item);
	}
	else if (type == 1) {
		m_game->m_map_list[m_game->m_npc_list[target_h]->m_map_index]->set_item(m_game->m_npc_list[target_h]->m_x, m_game->m_npc_list[target_h]->m_y, item);
		m_game->send_ground_item_event(CommonType::ItemDrop, m_game->m_npc_list[target_h]->m_map_index,
			m_game->m_npc_list[target_h]->m_x, m_game->m_npc_list[target_h]->m_y, item);
	}

}

//////////////////////////////////////////////////////////////////////
// Item Tiers Marquee debuffs — state machine only (PLANS/ItemTiers_Plan.md §5).
// The bleed tick's damage and death consequences live in CombatManager, the
// way poison_effect already does for poison.
//////////////////////////////////////////////////////////////////////

hb::server::marquee_debuffs* StatusEffectManager::marquee_state(short owner_h, char owner_type)
{
	switch (owner_type) {
	case hb::shared::owner_class::Player:
		if (m_game->m_client_list[owner_h] == nullptr) return nullptr;
		return &m_game->m_client_list[owner_h]->m_marquee_debuffs;

	case hb::shared::owner_class::Npc:
		if (m_game->m_npc_list[owner_h] == nullptr) return nullptr;
		return &m_game->m_npc_list[owner_h]->m_marquee_debuffs;

	default:
		return nullptr;
	}
}

void StatusEffectManager::clear_marquee(short owner_h, char owner_type)
{
	if (auto* state = marquee_state(owner_h, owner_type)) state->clear();
}

void StatusEffectManager::apply_sunder(short target_h, char target_type, uint32_t now)
{
	auto* state = marquee_state(target_h, target_type);
	if (state == nullptr) return;

	const auto& marquee = m_game->get_tier_config().marquee;
	if (marquee.sunder_duration_ms <= 0) return;

	// Refresh is a plain overwrite: the debuff is a single fixed step, so a
	// re-proc restarts its clock rather than deepening it.
	state->sunder_delta = marquee.sunder_defense_delta;
	state->sunder_expire_time = now + static_cast<uint32_t>(marquee.sunder_duration_ms);

	if (target_type == hb::shared::owner_class::Player)
		m_game->send_notify_msg(0, target_h, Notify::NoticeMsg, 0, 0, 0, "Your armor is sundered!");
}

void StatusEffectManager::apply_bleed(short target_h, char target_type, int attacker_h, uint32_t now)
{
	auto* state = marquee_state(target_h, target_type);
	if (state == nullptr) return;

	const auto& marquee = m_game->get_tier_config().marquee;
	if ((marquee.bleed_tick_damage <= 0) || (marquee.bleed_tick_interval_ms <= 0)
		|| (marquee.bleed_duration_ms <= 0)) return;

	const bool was_bleeding = state->is_bleeding(now);

	state->bleed_damage = marquee.bleed_tick_damage;
	state->bleed_interval_ms = static_cast<uint32_t>(marquee.bleed_tick_interval_ms);
	state->bleed_expire_time = now + static_cast<uint32_t>(marquee.bleed_duration_ms);

	// Refresh extends the bleed but leaves the tick cadence alone. Restarting
	// the tick clock on every proc would let a fast weapon refresh the bleed
	// forever without a single tick ever landing.
	if (!was_bleeding) state->bleed_next_tick_time = now + state->bleed_interval_ms;

	state->bleed_attacker_h = attacker_h;
	std::memset(state->bleed_attacker_name, 0, sizeof(state->bleed_attacker_name));
	if (m_game->m_client_list[attacker_h] != nullptr)
		std::memcpy(state->bleed_attacker_name, m_game->m_client_list[attacker_h]->m_char_name,
			hb::shared::limits::CharNameLen - 1);

	if ((target_type == hb::shared::owner_class::Player) && !was_bleeding)
		m_game->send_notify_msg(0, target_h, Notify::NoticeMsg, 0, 0, 0, "You are bleeding!");
}

int StatusEffectManager::sunder_defense_delta(short target_h, char target_type, uint32_t now)
{
	const auto* state = marquee_state(target_h, target_type);
	return (state != nullptr && state->is_sundered(now)) ? state->sunder_delta : 0;
}

void StatusEffectManager::tick_bleed(short target_h, char target_type, uint32_t now)
{
	auto* state = marquee_state(target_h, target_type);
	if (state == nullptr) return;
	if (state->bleed_expire_time == 0) return;

	// A due tick is settled before expiry is considered. The constants put the
	// last tick exactly on the duration boundary (8 s / 1 s = eight ticks, 200
	// damage per proc since the 2026-07-28 retune), and expiring first would
	// silently swallow it every time the tick loop arrived a millisecond late.
	//
	// Note the shape this imposes on the callers: at most ONE tick is settled
	// per call, and the catch-up below drops a backlog rather than firing it,
	// so a caller must poll strictly faster than bleed_tick_interval_ms or the
	// knob quietly under-delivers. Both callers poll at 300 ms.
	bool alive = true;
	if (static_cast<int32_t>(state->bleed_next_tick_time - now) <= 0) {
		state->bleed_next_tick_time += state->bleed_interval_ms;
		// A stalled tick loop (long server hitch) catches up to the present
		// rather than firing the whole backlog at once.
		if (static_cast<int32_t>(state->bleed_next_tick_time - now) <= 0)
			state->bleed_next_tick_time = now + state->bleed_interval_ms;

		alive = m_game->m_combat_manager->bleed_effect(target_h, target_type, *state);
	}

	if (alive && state->is_bleeding(now)) return;

	state->bleed_expire_time = 0;
	state->bleed_next_tick_time = 0;

	// A victim who stopped bleeding because they died is told by other means.
	if (alive && (target_type == hb::shared::owner_class::Player))
		m_game->send_notify_msg(0, target_h, Notify::NoticeMsg, 0, 0, 0, "The bleeding stops.");
}
