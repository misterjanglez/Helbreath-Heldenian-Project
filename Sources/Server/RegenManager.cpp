// RegenManager.cpp: Player HP/MP/SP regen, hunger consumption, and poison ticks.

#include "RegenManager.h"
#include "Game.h"
#include "Client.h"
#include "CombatManager.h"

using namespace hb::shared::net;

void RegenManager::process_client_tick(int client_h, uint32_t time)
{
	if (m_game->m_client_list[client_h] == nullptr) return;

	tick_hunger(client_h, time);

	int hunger_delay = calc_hunger_delay(m_game->m_client_list[client_h]->m_hunger_status);

	tick_hp(client_h, time, hunger_delay);
	tick_mp(client_h, time, hunger_delay);
	tick_sp(client_h, time, hunger_delay);
	tick_poison(client_h, time);
}

bool RegenManager::is_regen_suppressed(int client_h) const
{
	if (m_game->m_client_list[client_h] == nullptr) return true;
	if (m_game->m_client_list[client_h]->m_is_killed) return true;
	if (m_game->m_client_list[client_h]->m_is_init_complete == false) return true;
	if (m_game->m_client_list[client_h]->m_hunger_status <= 0) return true;
	if (m_game->m_client_list[client_h]->m_skill_using_status[19]) return true;
	return false;
}

void RegenManager::tick_hunger(int client_h, uint32_t time)
{
	if (m_game->m_client_list[client_h]->m_is_killed) return;

	if ((time - m_game->m_client_list[client_h]->m_hunger_time) > (uint32_t)m_game->m_hunger_consume_interval)
	{
		m_game->m_client_list[client_h]->m_hunger_status--;
		if (m_game->m_client_list[client_h]->m_hunger_status <= 0)
			m_game->m_client_list[client_h]->m_hunger_status = 0;
		m_game->m_client_list[client_h]->m_hunger_time = time;
		m_game->send_notify_msg(0, client_h, Notify::Hunger, m_game->m_client_list[client_h]->m_hunger_status, 0, 0, 0);
	}
}

void RegenManager::tick_hp(int client_h, uint32_t time, int hunger_delay_ms)
{
	if ((time - m_game->m_client_list[client_h]->m_hp_time) > (uint32_t)(m_game->m_health_regen_interval + hunger_delay_ms))
	{
		if (!is_regen_suppressed(client_h))
		{
			int max_hp = m_game->get_max_hp(client_h);
			if (m_game->m_client_list[client_h]->m_hp < max_hp)
			{
				int amount = calc_hp_regen(
					m_game->m_client_list[client_h]->m_vit,
					m_game->m_client_list[client_h]->m_side_effect_max_hp_down,
					m_game->m_client_list[client_h]->m_hp_stock,
					m_game->m_client_list[client_h]->m_add_hp
				);
				m_game->m_client_list[client_h]->m_hp += amount;
				if (m_game->m_client_list[client_h]->m_hp > max_hp)
					m_game->m_client_list[client_h]->m_hp = max_hp;
				if (m_game->m_client_list[client_h]->m_hp <= 0)
					m_game->m_client_list[client_h]->m_hp = 0;
				m_game->send_notify_msg(0, client_h, Notify::Hp, 0, 0, 0, 0);
			}
		}
		m_game->m_client_list[client_h]->m_hp_stock = 0;
		m_game->m_client_list[client_h]->m_hp_time = time;
	}
}

void RegenManager::tick_mp(int client_h, uint32_t time, int hunger_delay_ms)
{
	if ((time - m_game->m_client_list[client_h]->m_mp_time) > (uint32_t)(m_game->m_mana_regen_interval + hunger_delay_ms))
	{
		if (!is_regen_suppressed(client_h))
		{
			int max_mp = m_game->get_max_mp(client_h);
			if (m_game->m_client_list[client_h]->m_mp < max_mp)
			{
				int amount = calc_mp_regen(
					m_game->m_client_list[client_h]->m_mag,
					m_game->m_client_list[client_h]->m_angelic_mag,
					m_game->m_client_list[client_h]->m_add_mp
				);
				m_game->m_client_list[client_h]->m_mp += amount;
				if (m_game->m_client_list[client_h]->m_mp > max_mp)
					m_game->m_client_list[client_h]->m_mp = max_mp;
				m_game->send_notify_msg(0, client_h, Notify::Mp, 0, 0, 0, 0);
			}
		}
		m_game->m_client_list[client_h]->m_mp_time = time;
	}
}

void RegenManager::tick_sp(int client_h, uint32_t time, int hunger_delay_ms)
{
	if ((time - m_game->m_client_list[client_h]->m_sp_time) > (uint32_t)(m_game->m_stamina_regen_interval + hunger_delay_ms))
	{
		if (!is_regen_suppressed(client_h))
		{
			int max_sp = m_game->get_max_sp(client_h);
			if (m_game->m_client_list[client_h]->m_sp < max_sp)
			{
				int amount = calc_sp_regen(
					m_game->m_client_list[client_h]->m_vit,
					m_game->m_client_list[client_h]->m_level,
					m_game->m_client_list[client_h]->m_add_sp
				);
				m_game->m_client_list[client_h]->m_sp += amount;
				if (m_game->m_client_list[client_h]->m_sp > max_sp)
					m_game->m_client_list[client_h]->m_sp = max_sp;
				m_game->send_notify_msg(0, client_h, Notify::Sp, 0, 0, 0, 0);
			}
		}
		m_game->m_client_list[client_h]->m_sp_time = time;
	}
}

void RegenManager::tick_poison(int client_h, uint32_t time)
{
	if (m_game->m_client_list[client_h]->m_is_poisoned &&
		(time - m_game->m_client_list[client_h]->m_poison_time) > (uint32_t)m_game->m_poison_damage_interval)
	{
		m_game->m_combat_manager->poison_effect(client_h, 0);
		m_game->m_client_list[client_h]->m_poison_time = time;
	}
}

int RegenManager::calc_hp_regen(int vit, int side_effect_divisor, int hp_stock, int add_hp_pct) const
{
	int base = m_game->dice(1, vit);
	if (base < (vit / 2)) base = (vit / 2);
	if (side_effect_divisor != 0)
		base -= (base / side_effect_divisor);
	int total = base + hp_stock;
	total = apply_percent_bonus(total, add_hp_pct);
	return total;
}

int RegenManager::calc_mp_regen(int mag, int angelic_mag, int add_mp_pct) const
{
	int total = m_game->dice(1, (mag + angelic_mag));
	total = apply_percent_bonus(total, add_mp_pct);
	return total;
}

int RegenManager::calc_sp_regen(int vit, int level, int add_sp_pct) const
{
	int total = m_game->dice(1, (vit / 3));
	total = apply_percent_bonus(total, add_sp_pct);
	if (level <= 20)
		total += 15;
	else if (level <= 40)
		total += 10;
	else if (level <= 60)
		total += 5;
	return total;
}

int RegenManager::calc_hunger_delay(int hunger_status) const
{
	if (hunger_status > 30 || hunger_status < 0) return 0;
	return (30 - hunger_status) * 1000;
}

int RegenManager::apply_percent_bonus(int base, int percent)
{
	if (percent == 0) return base;
	int bonus = (int)((double)percent / 100.0f * (double)base);
	return base + bonus;
}
