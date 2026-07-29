// NpcConfigFields.h: an `npc_configs` row, as a struct.

#pragma once

#include <cstdint>

#include "NetConstants.h"

namespace hb::server
{

// Every field an NPC takes from its `npc_configs` row, and nothing else.
//
// CNpc derives from this, so a spawned NPC receives its whole configuration in
// ONE memberwise assignment (CGame::init_npc_attr) instead of a hand-written
// field-by-field copy. That hand-written copy is what shipped #63: the compiler
// cannot report a field nobody mentioned, and CNpc's default initialisers made
// the omitted loot grade read as plausible data rather than as a fault. Every
// monster in the game rolled grade 2 for as long as the tiered roll existed.
//
// Adding a column to `npc_configs` therefore means adding a field HERE, and it
// reaches every spawned NPC for free. Adding it to CNpc instead does not
// compile: the load and save paths in GameConfigSqliteStore bind through
// npc_config_fields, not through CNpc, so the new member is not in scope where
// the database would have to fill it.
//
// Field order follows the `npc_configs` column order so the struct reads as the
// row. Derived state — rolled HP, current mana, gained exp, AI level — belongs
// in CNpc: this is the row, not the creature.
struct npc_config_fields
{
	char     m_npc_name[hb::shared::limits::NpcNameLen] = {};
	short    m_type = 0;
	int      m_hp_min = 0;
	int      m_hp_max = 0;
	int      m_hold_resist = 0;
	int      m_defense_ratio = 0;
	int      m_hit_ratio = 0;
	int      m_min_bravery = 0;
	uint32_t m_exp_dice_min = 0;
	uint32_t m_exp_dice_max = 0;
	uint32_t m_gold_dice_min = 0;
	uint32_t m_gold_dice_max = 0;
	int      m_min_damage = 0;
	int      m_max_damage = 0;
	char     m_size = 0;					// 0: Small-Medium 1: Large
	char     m_side = 0;
	char     m_action_limit = 0;
	uint32_t m_action_time = 0;
	char     m_resist_magic = 0;
	char     m_magic_level = 0;
	char     m_day_of_week_limit = 0;
	char     m_chat_msg_presence = 0;
	char     m_target_search_range = 0;
	uint32_t m_regen_time = 0;
	char     m_attribute = 0;
	int      m_abs_damage = 0;
	int      m_max_mana = 0;
	int      m_magic_hit_ratio = 0;
	int      m_attack_range = 1;
	// The two drop-table slots (#73). Referencing a table in a slot is the ONLY
	// thing "stage" means: the roll routine, the rate model, the multipliers and
	// the validator are all identical for both, and the slot decides only which
	// stage multiplier applies and whether the result tier-rolls (spec §8 keeps
	// stage 2 out of the tier roll). 0 = this monster has no table in that slot.
	int      m_stage1_table_id = 0;
	int      m_stage2_table_id = 0;
	// Loot grade 1..5 vermin/standard/veteran/elite/boss (loot_grades table,
	// spec §8) — the tiered drop-economics dial. Default 2 = standard.
	int      m_loot_grade = 2;
};

} // namespace hb::server
