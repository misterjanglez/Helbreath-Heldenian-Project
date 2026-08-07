#pragma once

#include "PacketHeaders.h"
#include "Item/ItemAttributeData.h"
#include "Item/ModifierIds.h"
#include "NetConstants.h"

#include <cstddef>
#include <cstdint>

namespace hb {
namespace net {
	HB_PACK_BEGIN
	constexpr std::size_t kMaxItems = 50;
	constexpr std::size_t kMaxMagicType = 100;
	constexpr std::size_t kMaxSkillType = 60;
	struct HB_PACKED PacketNotifyCraftingFail {
		PacketHeader header;
		int32_t reason;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyAngelicStats {
		PacketHeader header;
		int32_t str;
		int32_t intel;
		int32_t dex;
		int32_t mag;
	};

	struct HB_PACKED PacketNotifyAngelFailed {
		PacketHeader header;
		int32_t reason;
	};

	struct HB_PACKED PacketNotifyApocGateOpen {
		PacketHeader header;
		int32_t gate_x;
		int32_t gate_y;
		char map_name[hb::shared::limits::MapNameLen];
	};

	struct HB_PACKED PacketNotifyQuestCounter {
		PacketHeader header;
		int32_t current_count;
		uint8_t padding[16];
	};

	struct HB_PACKED PacketNotifyMonsterCount {
		PacketHeader header;
		int16_t count;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyAbaddonKilled {
		PacketHeader header;
		char killer_name[hb::shared::limits::CharNameLen];
		uint8_t padding[10];
	};

	struct HB_PACKED PacketNotifyHeldenianVictory {
		PacketHeader header;
		int16_t side;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyHeldenianCount {
		PacketHeader header;
		int16_t aresden_tower_left;
		int16_t elvine_tower_left;
		// Death tallies (the type-1 tie-break data). The original client called
		// these fields "flags", but the server has always filled them from the
		// per-side death counters (#96).
		int16_t aresden_dead;
		int16_t elvine_dead;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifySpawnEvent {
		PacketHeader header;
		uint8_t monster_id;
		int16_t x;
		int16_t y;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyChangePlayMode {
		PacketHeader header;
		char location[hb::shared::limits::MapNameLen];
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyCurDurability {
		PacketHeader header;
		int32_t item_index;
		int32_t cur_durability;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyForceRecallTime {
		PacketHeader header;
		uint16_t seconds_left;
	};

	struct HB_PACKED PacketNotifyLevelUpPoints {
		PacketHeader header;
		uint16_t lu_point;
	};

	struct HB_PACKED PacketNotifyItemUpgradeFail {
		PacketHeader header;
		int16_t reason;
	};

	struct HB_PACKED PacketNotifyGizonItemUpgradeLeft {
		PacketHeader header;
		int16_t left;
		int32_t reason;
	};

	struct HB_PACKED PacketNotifyItemAttributeChange {
		PacketHeader header;
		int16_t item_index;
		hb::shared::item::item_attribute_data attributes;
		uint32_t spec_value1;
		uint32_t spec_value2;
	};

	struct HB_PACKED PacketNotifyGizonItemChange {
		PacketHeader header;
		int16_t item_index;
		uint8_t item_type;
		int16_t cur_durability;
		uint8_t item_color;
		uint8_t spec_value2;
		hb::shared::item::item_attribute_data attributes;
		char item_name[hb::shared::limits::ItemNameLen];
		int16_t item_id;
	};

	struct HB_PACKED PacketNotifyTCLoc {
		PacketHeader header;
		int16_t dest_x;
		int16_t dest_y;
		char teleport_map[hb::shared::limits::MapNameLen];
		int16_t construct_x;
		int16_t construct_y;
		char construct_map[hb::shared::limits::MapNameLen];
	};

	struct HB_PACKED PacketNotifyConstructionPoint {
		PacketHeader header;
		int16_t construction_point;
		int16_t war_contribution;
		int16_t notify_type;
	};

	struct HB_PACKED PacketNotifyMeteorStrikeComing {
		PacketHeader header;
		int16_t phase;
	};

	struct HB_PACKED PacketNotifyLockedMap {
		PacketHeader header;
		int16_t seconds_left;
		char map_name[hb::shared::limits::MapNameLen];
	};

	struct HB_PACKED PacketNotifyCannotConstruct {
		PacketHeader header;
		int16_t reason;
	};

	struct HB_PACKED PacketNotifySpecialAbilityStatus {
		PacketHeader header;
		int16_t status_type;
		int16_t ability_type;
		int16_t seconds_left;
	};

	struct HB_PACKED PacketNotifyDamageMove {
		PacketHeader header;
		uint8_t dir;
		int32_t amount;
		uint8_t weapon;
	};

	struct HB_PACKED PacketNotifyResponseCreateNewParty {
		PacketHeader header;
		int16_t result;
	};

	struct HB_PACKED PacketNotifyQueryJoinParty {
		PacketHeader header;
		char name[1];
	};

	struct HB_PACKED PacketNotifyEnergySphereCreated {
		PacketHeader header;
		int16_t x;
		int16_t y;
	};

	struct HB_PACKED PacketNotifyEnergySphereGoalIn {
		PacketHeader header;
		int16_t result;
		int16_t side;
		int16_t goal;
		char name[hb::shared::limits::NpcNameLen];
	};

	struct HB_PACKED PacketNotifySuperAttackLeft {
		PacketHeader header;
		int16_t left;
	};

	struct HB_PACKED PacketNotifySafeAttackMode {
		PacketHeader header;
		uint8_t enabled;
	};

	struct HB_PACKED PacketNotifyObserverMode {
		PacketHeader header;
		int16_t enabled;
	};

	struct HB_PACKED PacketNotifyCrusade {
		PacketHeader header;
		int32_t crusade_mode;
		int32_t crusade_duty;
		int32_t v3;
		int32_t v4;
	};

	struct HB_PACKED PacketNotifyBuildItemResult {
		PacketHeader header;
		int16_t item_id;
		int16_t item_count;
	};

	struct HB_PACKED PacketNotifyItemPosList {
		PacketHeader header;
		int16_t positions[kMaxItems * 2];
	};

	struct HB_PACKED PacketNotifyEnemyKills {
		PacketHeader header;
		int32_t count;
	};

	struct HB_PACKED PacketNotifyIpAccountInfo {
		PacketHeader header;
		char text[1];
	};

	struct HB_PACKED PacketNotifyRewardGold {
		PacketHeader header;
		uint32_t gold;
	};

	struct HB_PACKED PacketNotifyServerShutdown {
		PacketHeader header;
		uint8_t mode;
		uint16_t seconds;
		char message[128];
	};

	struct HB_PACKED PacketNotifyFishCanceled {
		PacketHeader header;
		uint16_t reason;
	};

	struct HB_PACKED PacketNotifyNotEnoughGold {
		PacketHeader header;
		int8_t item_index;
	};

	struct HB_PACKED PacketNotifySpellSkill {
		PacketHeader header;
		uint8_t magic_mastery[kMaxMagicType];
		uint8_t skill_mastery[kMaxSkillType];
	};

	struct HB_PACKED PacketNotifyStateChangeSuccess {
		PacketHeader header;
		uint8_t magic_mastery[kMaxMagicType];
		uint8_t skill_mastery[kMaxSkillType];
	};

	struct HB_PACKED PacketNotifyPartyBasic {
		PacketHeader header;
		int16_t type;
		int16_t v2;
		int16_t v3;
		int16_t v4;
	};

	struct HB_PACKED PacketNotifyPartyName {
		PacketHeader header;
		int16_t type;
		int16_t v2;
		int16_t v3;
		char name[hb::shared::limits::CharNameLen];
	};

	struct HB_PACKED PacketNotifyPartyList {
		PacketHeader header;
		int16_t type;
		int16_t v2;
		int16_t count;
		char names[1];
	};

	struct HB_PACKED PacketNotifyGrandMagicResult {
		PacketHeader header;
		uint16_t crashed_structures;
		uint16_t structure_damage;
		uint16_t casualities;
		char map_name[hb::shared::limits::MapNameLen];
		uint16_t active_structure;
		uint16_t value_count;
		uint16_t values[1];
	};

	struct HB_PACKED PacketNotifyGrandMagicResultHeader {
		PacketHeader header;
		uint16_t crashed_structures;
		uint16_t structure_damage;
		uint16_t casualities;
		char map_name[hb::shared::limits::MapNameLen];
		uint16_t active_structure;
		uint16_t value_count;
	};

	struct HB_PACKED PacketNotifyItemObtained {
		PacketHeader header;
		uint8_t is_new;
		char name[hb::shared::limits::ItemNameLen];
		uint64_t count;
		uint8_t item_type;
		uint8_t equip_pos;
		uint8_t is_equipped;
		int16_t level_limit;
		uint8_t gender_limit;
		uint16_t cur_durability;
		uint16_t weight;
		uint8_t item_color;
		uint8_t spec_value2;
		hb::shared::item::item_attribute_data attributes;
		int16_t item_id;           // Item ID for config lookup
		uint16_t max_durability;

	};

	struct HB_PACKED PacketNotifyItemPurchased {
		PacketHeader header;
		uint8_t is_new;
		char name[hb::shared::limits::ItemNameLen];
		uint64_t count;
		uint8_t item_type;
		uint8_t equip_pos;
		uint8_t is_equipped;
		int16_t level_limit;
		uint8_t gender_limit;
		uint16_t cur_durability;
		uint16_t weight;
		uint8_t item_color;
		uint16_t cost;
		int16_t item_id;           // Item ID for config lookup
		uint16_t max_durability;
	};

	struct HB_PACKED PacketNotifyItemToBank {
		PacketHeader header;
		uint8_t bank_index;
		uint8_t is_new;
		char name[hb::shared::limits::ItemNameLen];
		uint64_t count;
		uint8_t item_type;
		uint8_t equip_pos;
		uint8_t is_equipped;
		int16_t level_limit;
		uint8_t gender_limit;
		uint16_t cur_durability;
		uint16_t weight;
		uint8_t item_color;
		int16_t item_effect_value2;
		hb::shared::item::item_attribute_data attributes;
		uint8_t spec_effect_value2;
		uint8_t padding;
		int16_t item_id;           // Item ID for config lookup
		uint16_t max_durability;
	};

	struct HB_PACKED PacketNotifyRatingPlayer {
		PacketHeader header;
		uint8_t result;
		char name[hb::shared::limits::CharNameLen];
		int32_t rating;
		uint8_t padding;
	};

	struct HB_PACKED PacketNotifyPlayerShutUp {
		PacketHeader header;
		uint16_t time;
		char name[hb::shared::limits::CharNameLen];
		uint8_t padding;
	};

	struct HB_PACKED PacketNotifyPlayerNotOnGame {
		PacketHeader header;
		char name[hb::shared::limits::CharNameLen];
		char filler[10];
		uint8_t padding;
	};

	struct HB_PACKED PacketNotifyPlayerOnGame {
		PacketHeader header;
		char name[hb::shared::limits::CharNameLen];
		char map_name[14];
		uint8_t padding;
	};

	struct HB_PACKED PacketNotifyPlayerStatus {
		PacketHeader header;
		char name[hb::shared::limits::CharNameLen];
		char map_name[hb::shared::limits::MapNameLen];
		uint16_t x;
		uint16_t y;
	};

	struct HB_PACKED PacketNotifyWhisperMode {
		PacketHeader header;
		char name[hb::shared::limits::CharNameLen];
	};

	struct HB_PACKED PacketNotifyServerChange {
		PacketHeader header;
		char map_name[hb::shared::limits::MapNameLen];
		char log_server_addr[15];
		int32_t log_server_port;
	};

	struct HB_PACKED PacketNotifySetItemCount {
		PacketHeader header;
		uint16_t item_index;
		uint64_t count;
		uint8_t notify;
	};

	struct HB_PACKED PacketNotifyShowMap {
		PacketHeader header;
		uint16_t map_id;
		uint16_t map_type;
	};

	struct HB_PACKED PacketNotifyMagicEffect {
		PacketHeader header;
		uint16_t magic_type;
		uint32_t effect;
		uint32_t owner;
	};

	struct HB_PACKED PacketNotifyMagicStudySuccess {
		PacketHeader header;
		uint8_t magic_id;
		char magic_name[30];
	};

	struct HB_PACKED PacketNotifyMagicStudyFail {
		PacketHeader header;
		uint8_t result;
		uint8_t magic_id;
		char magic_name[30];
		int32_t cost;
		int32_t req_int;
	};

	struct HB_PACKED PacketNotifyRepairItemPrice {
		PacketHeader header;
		uint32_t v1;
		uint32_t v2;
		uint32_t v3;
		uint32_t v4;
		char item_name[hb::shared::limits::ItemNameLen];
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifySellItemPrice {
		PacketHeader header;
		uint32_t v1;
		uint32_t v2;
		uint32_t v3;
		uint32_t v4;
		char item_name[hb::shared::limits::ItemNameLen];
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyQuestContents {
		PacketHeader header;
		int16_t who;
		int16_t quest_type;
		int16_t contribution;
		int16_t target_config_id;
		int16_t target_count;
		int16_t x;
		int16_t y;
		int16_t range;
		int16_t is_completed;
		char target_name[hb::shared::limits::NpcNameLen];
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyPlayerProfile {
		PacketHeader header;
		char text[1];
	};

	struct HB_PACKED PacketNotifyNoticeMsg {
		PacketHeader header;
		char text[1];
	};

	// Floating status text displayed above the player's head (e.g., "* Immune *")
	struct HB_PACKED PacketNotifyStatusText {
		PacketHeader header;
		char text[32];
	};

	struct HB_PACKED PacketNotifyTimeChange {
		PacketHeader header;
		uint8_t sprite_alpha;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyHP {
		PacketHeader header;
		uint32_t hp;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyMP {
		PacketHeader header;
		uint32_t mp;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifySP {
		PacketHeader header;
		uint32_t sp;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyExp {
		PacketHeader header;
		uint32_t exp;
		int32_t rating;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyCharisma {
		PacketHeader header;
		uint32_t charisma;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyHunger {
		PacketHeader header;
		uint8_t hunger;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyItemColorChange {
		PacketHeader header;
		int16_t item_index;
		int16_t item_color;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyItemDepletedEraseItem {
		PacketHeader header;
		uint16_t item_index;
		uint16_t use_result;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyItemDurabilityEnd {
		PacketHeader header;
		int16_t equip_pos;
		int16_t item_index;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyItemReleased {
		PacketHeader header;
		int16_t equip_pos;
		int16_t item_index;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyItemRepaired {
		PacketHeader header;
		uint32_t item_id;
		uint32_t life;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifySkill {
		PacketHeader header;
		uint16_t skill_index;
		uint16_t skill_value;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifySkillTrainSuccess {
		PacketHeader header;
		uint8_t skill_num;
		uint8_t skill_level;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifySkillUsingEnd {
		PacketHeader header;
		uint16_t result;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyLevelUp {
		PacketHeader header;
		int32_t level;
		int32_t str;
		int32_t vit;
		int32_t dex;
		int32_t intel;
		int32_t mag;
		int32_t chr;
		uint8_t attack_delay;
	};

	// The derived combat totals, which the client cannot work out for itself.
	//
	// It re-derives what it can — max HP/MP/SP, carry weight, defence ratio, the
	// rolled modifier lines — but absorption, resistance and the hit terms are
	// accumulated across the equip pass into CClient members and never leave the
	// server, so the character pane could only ever show the GEAR CONTRIBUTION
	// and had to say so on screen. These are the totals themselves.
	//
	// Everything is int16: the caps are data values, not wire guarantees.
	struct HB_PACKED PacketNotifyDerivedStats {
		PacketHeader header;

		// Gear attribute totals, already clamped at the per-attribute aggregate
		// cap server-side. Indexed by hb::shared::item::tier_attribute so both
		// ends fill and read it in a loop — six named fields invited a silently
		// transposed assignment, the same reasoning that shaped
		// CClient::m_add_attribute.
		int16_t add_attribute[hb::shared::item::tier_attribute::charisma + 1];

		// The server's own figure, which the client had been reproducing from
		// the equip formulas rather than being told.
		int16_t defense_ratio;

		// Physical absorption is resolved PER HIT ZONE, so there is no single
		// number to send. A blow rolls dice(1,10000) and lands on the body
		// (49.99%), the legs (25%), the arms (15%) or the head (10.01%), and
		// only that zone's armour absorbs it — each clamped at 80. These are the
		// four zones as the resolution reads them, `legs` already being the
		// leggings+boots sum that zone 2 adds together. The client shows the
		// hit-weighted average, which is the only honest scalar.
		//
		// The shield is not one of the zones: it applies on top, and only when a
		// dice(1,100) roll comes in under the wearer's Shield mastery.
		int16_t absorb_body;
		int16_t absorb_legs;
		int16_t absorb_arms;
		int16_t absorb_head;
		int16_t absorb_shield;

		int16_t magic_absorb;        // m_add_abs_magical_defense
		// skill_mastery[3] + m_add_magic_resistance + m_add_resist_magic — two
		// separate gear terms feed one total and neither is the whole of it.
		int16_t magic_resistance;
		int16_t poison_resistance;   // m_add_poison_resistance

		// The equipment half of the hit roll only. A true hit ratio depends on
		// which weapon is swinging (the mastery index varies by weapon) and is
		// clamped between the server's configured floor and ceiling, so a single
		// "hit ratio" figure would be a fiction; this stays the bonus it is.
		int16_t hit_bonus;

		int16_t absorb_air;
		int16_t absorb_earth;
		int16_t absorb_fire;
		int16_t absorb_water;

		int16_t physical_damage;     // m_add_physical_damage
		int16_t magical_damage;      // m_add_magical_damage
	};

	struct HB_PACKED PacketNotifyQuestReward {
		PacketHeader header;
		int16_t who;
		int16_t flag;
		int32_t amount;
		char reward_name[hb::shared::limits::ItemNameLen];
		int32_t contribution;
	};

	struct HB_PACKED PacketNotifyExchangeItem {
		PacketHeader header;
		int16_t dir;
		int32_t amount;
		uint8_t color;
		int16_t cur_durability;
		int16_t max_durability;
		int16_t performance;
		char item_name[hb::shared::limits::ItemNameLen];
		char char_name[hb::shared::limits::CharNameLen];
		hb::shared::item::item_attribute_data attributes;
		int16_t item_id;
	};

	struct HB_PACKED PacketNotifyRepairAllPricesHeader {
		PacketHeader header;
		int16_t total;
	};

	struct HB_PACKED PacketNotifyRepairAllPricesEntry {
		uint8_t index;
		int16_t price;
	};

	struct HB_PACKED PacketNotifyEnemyKillReward {
		PacketHeader header;
		uint32_t exp;
		uint32_t kill_count;
		char killer_name[hb::shared::limits::CharNameLen];
		int16_t war_contribution;
	};

	struct HB_PACKED PacketNotifyPKcaptured {
		PacketHeader header;
		uint16_t pk_count;
		uint16_t victim_pk_count;
		char victim_name[hb::shared::limits::CharNameLen];
		uint32_t reward_gold;
		uint32_t exp;
	};

	struct HB_PACKED PacketNotifyPKpenalty {
		PacketHeader header;
		uint32_t exp;
		uint32_t str;
		uint32_t vit;
		uint32_t dex;
		uint32_t intel;
		uint32_t mag;
		uint32_t chr;
		uint32_t pk_count;
	};

	struct HB_PACKED PacketNotifyKilled {
		PacketHeader header;
		char attacker_name[hb::shared::limits::NpcNameLen];
	};

	struct HB_PACKED PacketNotifyForceDisconn {
		PacketHeader header;
		uint16_t seconds;
	};

	struct HB_PACKED PacketNotifySimpleShort {
		PacketHeader header;
		int16_t value;
	};

	struct HB_PACKED PacketNotifySimpleInt {
		PacketHeader header;
		int32_t value;
	};

	struct HB_PACKED PacketNotifyEmpty {
		PacketHeader header;
	};

	struct HB_PACKED PacketNotifyNpcTalk {
		PacketHeader header;
		int16_t type;
		int16_t response;
		int16_t amount;
		int16_t contribution;
		int16_t target_config_id;
		int16_t target_count;
		int16_t x;
		int16_t y;
		int16_t range;
		char reward_name[hb::shared::limits::ItemNameLen];
		char target_name[hb::shared::limits::NpcNameLen];
	};

	struct HB_PACKED PacketNotifyMapStatusHeader {
		PacketHeader header;
		char map_name[hb::shared::limits::MapNameLen];
		int16_t index;
		uint8_t total;
	};

	struct HB_PACKED PacketNotifyMapStatusEntry {
		uint8_t type;
		int16_t x;
		int16_t y;
		uint8_t side;
	};

	struct HB_PACKED PacketNotifyMobKillCountHeader {
		PacketHeader header;
		int16_t total;
	};

	struct HB_PACKED PacketNotifyMobKillCountEntry {
		int16_t kill_count;
		int16_t next_count;
		int16_t level;
		char npc_name[hb::shared::limits::NpcNameLen];
	};

	struct HB_PACKED PacketNotifyCannotGiveItem {
		PacketHeader header;
		uint16_t item_index;
		int32_t amount;
		char name[hb::shared::limits::ItemNameLen];
	};

	struct HB_PACKED PacketNotifyGlobalAttackMode {
		PacketHeader header;
		uint8_t mode;
	};

	struct HB_PACKED PacketNotifyWhetherChange {
		PacketHeader header;
		uint8_t status;
	};

	struct HB_PACKED PacketNotifyFishChance {
		PacketHeader header;
		uint16_t chance;
	};

	struct HB_PACKED PacketNotifyEventFishMode {
		PacketHeader header;
		uint16_t price;
		char name[hb::shared::limits::NpcNameLen];
	};

	struct HB_PACKED PacketNotifyCannotRating {
		PacketHeader header;
		uint16_t time_left;
	};

	struct HB_PACKED PacketNotifyCannotRepairItem {
		PacketHeader header;
		uint16_t item_index;
		uint16_t reason;
		char name[hb::shared::limits::ItemNameLen];
	};

	struct HB_PACKED PacketNotifyCannotSellItem {
		PacketHeader header;
		uint16_t item_index;
		uint16_t reason;
		char name[hb::shared::limits::ItemNameLen];
	};

	struct HB_PACKED PacketNotifyDownSkillIndexSet {
		PacketHeader header;
		uint16_t skill_index;
	};

	struct HB_PACKED PacketNotifyAdminInfo {
		PacketHeader header;
		int32_t v1;
		int32_t v2;
		int32_t v3;
		int32_t v4;
		int32_t v5;
		int32_t v6;
		int32_t v7;
		int32_t v8;
	};

	struct HB_PACKED PacketNotifyTotalUsers {
		PacketHeader header;
		uint16_t total;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyDropItemFinCountChanged {
		PacketHeader header;
		uint16_t item_index;
		int32_t amount;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyDropItemFinEraseItem {
		PacketHeader header;
		uint16_t item_index;
		int32_t amount;
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyGiveItemFinCountChanged {
		PacketHeader header;
		uint16_t item_index;
		int32_t amount;
		char name[hb::shared::limits::ItemNameLen];
		uint8_t padding[2];
	};

	struct HB_PACKED PacketNotifyGiveItemFinEraseItem {
		PacketHeader header;
		uint16_t item_index;
		int32_t amount;
		char name[hb::shared::limits::ItemNameLen];
		uint8_t padding[2];
	};

#ifdef TESTER_ONLY
	// TESTER MENU — tester-only packet structs
	struct HB_PACKED TesterItemSearchEntry {
		int16_t item_id;
		int16_t effect_type;  // ItemEffectType — determines valid prefixes
		char name[hb::shared::limits::ItemNameLen];
	};

	struct HB_PACKED PacketNotifyTesterItemSearchResult {
		PacketHeader header;
		int16_t count;
		TesterItemSearchEntry entries[50];
	};

	struct HB_PACKED TesterMapEntry {
		char name[11];
	};

	struct HB_PACKED PacketNotifyTesterMapListResult {
		PacketHeader header;
		int16_t count;
		TesterMapEntry entries[100];
	};

	struct HB_PACKED TesterNpcSearchEntry {
		int16_t npc_id;
		char name[hb::shared::limits::NpcNameLen];
	};

	struct HB_PACKED PacketNotifyTesterNpcSearchResult {
		PacketHeader header;
		int16_t count;
		TesterNpcSearchEntry entries[50];
	};
#endif // TESTER_ONLY
	HB_PACK_END
}
}
