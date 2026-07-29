// Npc.h: interface for the CNpc class.

#pragma once


#include "CommonTypes.h"
#include "Magic.h"
#include "GameGeometry.h"
#include "Appearance.h"
#include "NpcConfigFields.h"
#include "PlayerStatusData.h"
#include "MarqueeEffects.h"
#include "DirectionHelpers.h"
using hb::shared::direction::direction;

namespace hb::server::npc
{

constexpr int MaxWaypoints = 10;

namespace MoveType
{
	enum : int
	{
		stop            = 0,
		SeqWaypoint     = 1,
		RandomWaypoint  = 2,
		Follow          = 3,
		RandomArea      = 4,
		Random          = 5,
		Guard           = 6,
	};
}

namespace Behavior
{
	enum : int
	{
		stop    = 0,
		Move    = 1,
		Attack  = 2,
		Flee    = 3,
		Dead    = 4,
	};
}

} // namespace hb::server::npc

// A stage-2 drop rolled at death but placed later, when the corpse decays.
// (dx, dy) is the tile offset from the corpse for scattered boss loot; (0, 0)
// for the ordinary single delayed drop.
struct PendingDrop
{
	int   item_id;
	int   min_count;
	int   max_count;
	short dx;
	short dy;
	// Whether this drop tier-rolls when it finally lands. Carried on the queue
	// because delay is a table property now (#73): a STAGE-1 table set to
	// corpse-decay still tier-rolls, and a stage-2 one still never does, so the
	// answer cannot be re-derived at placement time.
	bool  tier_rolls;
};

// Config-sourced fields live in the base (NpcConfigFields.h); everything below
// is state the creature acquires at spawn or in play. init_npc_attr copies the
// base wholesale, so a new config field needs no work here — see #64.
class CNpc : public hb::server::npc_config_fields
{
public:
	CNpc(const char * name5);
	virtual ~CNpc();

	// Auras
	char m_magic_config_list[100];

	char  m_name[6];
	char  m_map_index;
	short m_x, m_y;
	short m_prev_x, m_prev_y; // OPTIMIZATION FIX #3: Track previous position for delta detection
	short m_dx, m_dy;
	short m_vx, m_vy;
	int   m_tmp_error;
	hb::shared::geometry::GameRectangle  m_random_area;	// MOVETYPE_RANDOMAREA

	direction m_dir;
	char  m_action;
	char  m_turn;

	short m_original_type;
	short m_npc_config_id;
	hb::shared::entity::EntityAppearance m_appearance;
	hb::shared::entity::EntityStatus m_status;

	uint32_t m_time;
	uint32_t m_hp_up_time, m_mp_up_time;
	uint32_t m_dead_time;

	// Second-stage (stage-2) loot rolled at death and placed when the corpse
	// decays. Sized to the 5x5 scatter spiral. See CEntityManager drop logic.
	static constexpr int MaxPendingDrops = 25;
	PendingDrop m_pending_drops[MaxPendingDrops];
	int m_pending_drop_count = 0;

	int  m_hp, m_max_hp;						// Hit Point
	uint32_t  m_exp;                    // ? ? . ExpDice  .

	int m_attack_bonus;
	char m_bravery;
	int  m_mana;                   // MagicLevel*30

	char  m_move_type;
	char  m_behavior;
	short m_behavior_turn_count;

	int   m_follow_owner_index;
	char  m_follow_owner_type;		// (NPC or Player)
	bool  m_is_summoned;            // NPC? HP  .
	bool  m_bypass_mob_limit;        // GM-spawned: don't count toward map entity limit
	uint32_t m_summoned_time;

	int   m_target_index;
	char  m_target_type;			// (NPC or Player)
	char  m_cur_waypoint;
	char  m_total_waypoint;

	int   m_spot_mob_index;			// spot-mob-generator ?
	int   m_waypoint_index[hb::server::npc::MaxWaypoints+1];
	char  m_magic_effect_status[hb::server::config::MaxMagicEffects];

	// Item Tiers Marquee debuffs (spec §5). NPCs are legal victims of every
	// weapon exotic; the state machine is StatusEffectManager.
	hb::server::marquee_debuffs m_marquee_debuffs;

	bool  m_is_perm_attack_mode;
   	uint32_t   m_no_die_remain_exp;
	int   m_attack_strategy;
	int   m_ai_level;

	/*
		AI-Level
			1: ���� �ൿ 
			2: �������� ���� ���� ��ǥ���� ���� 
			3: ���� ��ȣ���� ��ǥ�� ���� ���ݴ�󿡼�? ���� 
	*/
	int   m_attack_count;
	bool  m_is_killed;
	bool  m_is_unsummoned;

	int   m_last_damage;
	int   m_summon_control_mode;		// ?: 0 Free, 1 Hold 2 Tgt

	int   m_item_ratio;
	int   m_assigned_item;

	char  m_special_ability;

									/*
case 0: break;
case 1:  "Penetrating Invisibility"
case 2:  "Breaking Magic Protection"
case 3:  "Absorbing Physical Damage"
case 4:  "Absorbing Magical Damage"
case 5:  "Poisonous"
case 6:  "Extremely Poisonous"
case 7:  "Explosive"
case 8:  "Hi-Explosive" 

 ���� �� ���� 60���� ũ�� NPC�� ȿ���ʹ� �����ϹǷ� �����Ѵ�.
									*/

	int	  m_build_count;			// ?      .  m_min_bravery.
	int   m_mana_stock;
	bool  m_is_master;

	char m_crop_type;
	char m_crop_skill;

	int   m_v1;
	char m_area;

	int m_npc_item_type;
	int m_npc_item_max;

};
