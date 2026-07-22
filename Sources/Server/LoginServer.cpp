#include "CommonTypes.h"
#include "LoginServer.h"
#include <string>
#include <vector>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include "StringCompat.h"
#include "TimeUtils.h"
using namespace std;

#include "SharedCalculations.h"
#include "AccountSqliteStore.h"
#include "sqlite3.h"
#include "PasswordHash.h"
#include "../../Dependencies/Shared/Packet/SharedPackets.h"
#include "Log.h"
#include "Item/ItemEnums.h"
#include "version_info.h"
#include "Game.h"
#include "TradingPostStore.h"
#include <filesystem>

using namespace hb::shared::net;
using namespace hb::shared::item;
using namespace hb::server::config;
namespace sock = hb::shared::net::socket;
extern char	G_cData50000[50000];
//extern void PutLogList(char* cMsg);
extern char G_cTxt[512];
extern class CGame* G_pGame;

static void LowercaseInPlace(char* buf, size_t len)
{
    for (size_t i = 0; i < len && buf[i] != '\0'; i++)
        buf[i] = static_cast<char>(::tolower(static_cast<unsigned char>(buf[i])));
}

#define WORLDNAMELS   "WS1"
#define WORLDNAMELS2   "WS2"

LoginServer::LoginServer()
{

}

LoginServer::~LoginServer()
{

}

bool IsValidName(char* str)
{
	size_t len = strlen(str);
	for(size_t i = 0; i < len; i++)
	{
		//if (str[i] < 0)	return false;
		if ((str[i] == ',') || (str[i] == '=') || (str[i] == ' ') || (str[i] == '\n') ||
			(str[i] == '\t') || (str[i] == '.') || (str[i] == '\\') || (str[i] == '/') ||
			(str[i] == ':') || (str[i] == '*') || (str[i] == '?') || (str[i] == '<') ||
			(str[i] == '>') || (str[i] == '|') || (str[i] == '"') || (str[i] == '`') ||
			(str[i] == ';') || (str[i] == '=') || (str[i] == '@') || (str[i] == '[') ||
			(str[i] == ']') || (str[i] == '^') || (str[i] == '_') || (str[i] == '\''))
			return false;
	}
	return true;
}
bool AccountDbExists(const char* account_name)
{
	char lower[hb::shared::limits::AccountNameLen] = {};
	std::strncpy(lower, account_name, hb::shared::limits::AccountNameLen - 1);
	LowercaseInPlace(lower, sizeof(lower));
	char dbPath[256] = {};
	std::snprintf(dbPath, sizeof(dbPath), "accounts/%s.db", lower);
	return std::filesystem::exists(dbPath);
}

bool OpenAccountDatabaseIfExists(const char* account_name, sqlite3** outDb)
{
	if (!AccountDbExists(account_name)) {
		return false;
	}
	std::string dbPath;
	return EnsureAccountDatabase(account_name, outDb, dbPath);
}

void LoginServer::activated()
{
	
}

void LoginServer::request_login(int h, char* data)
{
	if (G_pGame->_lclients[h] == 0)
		return;

	char name[hb::shared::limits::AccountNameLen] = {};
	char password[hb::shared::limits::AccountPassLen] = {};
	char world_name[32] = {};

	const auto* req = hb::net::PacketCast<hb::net::LoginRequest>(data, sizeof(hb::net::LoginRequest));
	if (!req) return;

	std::memcpy(name, req->account_name, sizeof(req->account_name));
	LowercaseInPlace(name, sizeof(name));
	std::memcpy(password, req->password, sizeof(req->password));
	std::memcpy(world_name, req->world_name, 30);

	if (req->version_major != hb::version::compatibility::major ||
		req->version_minor != hb::version::compatibility::minor ||
		req->version_patch != hb::version::compatibility::patch)
	{
		hb::logger::warn("Version mismatch on login from {}: client={}.{}.{} server={}.{}.{}",
			G_pGame->_lclients[h]->ip, req->version_major, req->version_minor, req->version_patch,
			hb::version::compatibility::major, hb::version::compatibility::minor, hb::version::compatibility::patch);
		send_login_msg(LogResMsg::VersionMismatch, LogResMsg::VersionMismatch, 0, 0, h);
		return;
	}

	if (string(world_name) != WORLDNAMELS)
		return;

	if (!IsValidName(name) || !IsValidName(password) || !IsValidName(world_name))
		return;

	hb::logger::log("Account Request Login: {}", name);

	std::vector<AccountDbCharacterSummary> chars;
	auto status = AccountLogIn(name, password, chars);
	switch (status)
	{
	case LogIn::Ok:
	{
		// Send balance config BEFORE login response — login response closes the socket
		send_balance_config(h);

		char data[512] = {};
		char* cp2 = data;
		push(cp2, (int)chars.size());
		get_char_list(name, cp2, chars);
		send_login_msg(LogResMsg::Confirm, LogResMsg::Confirm, data, cp2 - data, h);
		break;
	}

	case LogIn::NoAcc:
		send_login_msg(LogResMsg::NotExistingAccount, LogResMsg::NotExistingAccount, 0, 0, h);
		hb::logger::log("Account not found");
		break;

	case LogIn::NoPass:
		send_login_msg(LogResMsg::PasswordMismatch, LogResMsg::PasswordMismatch, 0, 0, h);
		hb::logger::log("Password mismatch");
		break;
	}
}

void LoginServer::get_char_list(string acc, char*& cp2, const std::vector<AccountDbCharacterSummary>& chars)
{
	for (const auto& entry : chars)
	{
		hb::net::PacketLogCharacterEntry pktEntry{};
		std::memcpy(pktEntry.name, entry.character_name, sizeof(pktEntry.name));
		pktEntry.appearance = entry.appearance;
		pktEntry.sex = entry.sex;
		pktEntry.skin = entry.skin;
		pktEntry.level = entry.level;
		pktEntry.exp = entry.exp;
		std::memcpy(pktEntry.map_name, entry.map_name, sizeof(pktEntry.map_name));
		std::memcpy(cp2, &pktEntry, sizeof(pktEntry));
		cp2 += sizeof(pktEntry);
	}
}

LogIn LoginServer::AccountLogIn(string acc, string pass, std::vector<AccountDbCharacterSummary>& chars)
{
	if (acc.size() == 0)
		return LogIn::NoAcc;

	if (pass.size() == 0)
		return LogIn::NoPass;

	sqlite3* db = nullptr;
	if (!OpenAccountDatabaseIfExists(acc.c_str(), &db)) {
		return LogIn::NoAcc;
	}

	AccountDbAccountData account = {};
	if (!LoadAccountRecord(db, acc.c_str(), account)) {
		CloseAccountDatabase(db);
		return LogIn::NoAcc;
	}

	if (!PasswordHash::VerifyPassword(pass.c_str(), account.password_salt, account.password_hash)) {
		CloseAccountDatabase(db);
		return LogIn::NoPass;
	}

	chars.clear();
	if (!ListCharacterSummaries(db, acc.c_str(), chars)) {
		CloseAccountDatabase(db);
		return LogIn::NoAcc;
	}

	// Compute equipment appearance from equipped items for each character
	for (auto& entry : chars) {
		std::vector<AccountDbEquippedItem> equippedItems;
		if (LoadEquippedItemAppearances(db, entry.character_name, equippedItems)) {
			for (const auto& item : equippedItems) {
				if (item.item_id > 0 && item.item_id < MaxItemTypes && G_pGame->m_item_config_list[item.item_id] != nullptr) {
					auto* config = G_pGame->m_item_config_list[item.item_id];
					// Populate item_id + display_id + color/flags for each equipment slot
					uint8_t color = static_cast<uint8_t>(item.item_color);
					switch (config->get_equip_pos()) {
					case EquipPos::Head:
						entry.appearance.helm_item_id = static_cast<int16_t>(item.item_id);
						entry.appearance.helm_display_id = config->m_display_id;
						entry.appearance.helm_color = color;
						break;
					case EquipPos::Body:
						entry.appearance.armor_item_id = static_cast<int16_t>(item.item_id);
						entry.appearance.armor_display_id = config->m_display_id;
						entry.appearance.armor_color = color;
						entry.appearance.hide_armor = (config->m_appearance_value >= 100);
						break;
					case EquipPos::FullBody:
						entry.appearance.armor_item_id = static_cast<int16_t>(item.item_id);
						entry.appearance.armor_display_id = config->m_display_id;
						entry.appearance.mantle_color = 0;
						break;
					case EquipPos::Arms:
						entry.appearance.arm_item_id = static_cast<int16_t>(item.item_id);
						entry.appearance.arm_display_id = config->m_display_id;
						entry.appearance.arm_color = color;
						break;
					case EquipPos::Pants:
						entry.appearance.pants_item_id = static_cast<int16_t>(item.item_id);
						entry.appearance.pants_display_id = config->m_display_id;
						entry.appearance.pants_color = color;
						entry.appearance.is_skirt = (config->m_appearance_value == 1);
						break;
					case EquipPos::Leggings:
						entry.appearance.boots_item_id = static_cast<int16_t>(item.item_id);
						entry.appearance.boots_display_id = config->m_display_id;
						entry.appearance.boots_color = color;
						break;
					case EquipPos::RightHand:
					case EquipPos::TwoHand:
						entry.appearance.weapon_item_id = static_cast<int16_t>(item.item_id);
						entry.appearance.weapon_display_id = config->m_display_id;
						entry.appearance.weapon_color = color;
						break;
					case EquipPos::LeftHand:
						entry.appearance.shield_item_id = static_cast<int16_t>(item.item_id);
						entry.appearance.shield_display_id = config->m_display_id;
						entry.appearance.shield_color = color;
						break;
					case EquipPos::Back:
						entry.appearance.mantle_item_id = static_cast<int16_t>(item.item_id);
						entry.appearance.mantle_display_id = config->m_display_id;
						entry.appearance.mantle_color = color;
						break;
					default:
						break;
					}
				}
			}
		}
	}

	CloseAccountDatabase(db);
	hb::logger::log("Account login");
	return LogIn::Ok;
}

void LoginServer::response_character(int h, char* data)
{
	char name[hb::shared::limits::AccountNameLen] = {};
	char acc[hb::shared::limits::AccountNameLen] = {};
	char password[hb::shared::limits::AccountPassLen] = {};
	char world_name[32] = {};

	char gender, skin, hairstyle, haircolor, under, str, vit, dex, intl, mag, chr;

	const auto* req = hb::net::PacketCast<hb::net::CreateCharacterRequest>(data, sizeof(hb::net::CreateCharacterRequest));
	if (!req) return;

	std::memcpy(name, req->character_name, sizeof(req->character_name));
	std::memcpy(acc, req->account_name, sizeof(req->account_name));
	LowercaseInPlace(acc, sizeof(acc));
	std::memcpy(password, req->password, sizeof(req->password));
	std::memcpy(world_name, req->world_name, 30);
	gender = static_cast<char>(req->gender);
	skin = static_cast<char>(req->skin);
	hairstyle = static_cast<char>(req->hairstyle);
	haircolor = static_cast<char>(req->haircolor);
	under = static_cast<char>(req->underware);
	str = static_cast<char>(req->str);
	vit = static_cast<char>(req->vit);
	dex = static_cast<char>(req->dex);
	intl = static_cast<char>(req->intl);
	mag = static_cast<char>(req->mag);
	chr = static_cast<char>(req->chr);

	if (string(world_name) != WORLDNAMELS)
		return;

	hb::logger::log("Request create new Character: {}", name);

	std::vector<AccountDbCharacterSummary> chars;
	auto status = AccountLogIn(acc, password, chars);
	if (status != LogIn::Ok)
		return;

	if (chars.size() >= hb::shared::limits::MaxCharactersPerAccount)
		return;

	if (!IsValidName(acc) || !IsValidName(password) || !IsValidName(name))
		return;

	// Check if character name already exists globally (across all accounts)
	if (CharacterNameExistsGlobally(name)) {
		hb::logger::log("Character name '{}' already exists globally", name);
		send_login_msg(LogResMsg::NewCharacterFailed, LogResMsg::NewCharacterFailed, 0, 0, h);
		return;
	}

	// Also check within current account (redundant but kept for safety)
	for (const auto& entry : chars) {
		if (hb_strnicmp(entry.character_name, name, hb::shared::limits::CharNameLen - 1) == 0) {
			send_login_msg(LogResMsg::NewCharacterFailed, LogResMsg::NewCharacterFailed, 0, 0, h);
			return;
		}
	}

	sqlite3* db = nullptr;
	std::string dbPath;
	if (!EnsureAccountDatabase(acc, &db, dbPath)) {
		send_login_msg(LogResMsg::NewCharacterFailed, LogResMsg::NewCharacterFailed, 0, 0, h);
		return;
	}

	// Read the actual stored account_name so the FK constraint matches exactly.
	// acc was lowercased (line 210) but the accounts table may store original case.
	AccountDbAccountData storedAccount = {};
	if (!LoadAccountRecord(db, acc, storedAccount)) {
		CloseAccountDatabase(db);
		send_login_msg(LogResMsg::NewCharacterFailed, LogResMsg::NewCharacterFailed, 0, 0, h);
		return;
	}

	AccountDbCharacterState state = {};
	std::snprintf(state.account_name, sizeof(state.account_name), "%s", storedAccount.name);
	std::snprintf(state.character_name, sizeof(state.character_name), "%s", name);
	std::snprintf(state.profile, sizeof(state.profile), "__________");
	std::snprintf(state.location, sizeof(state.location), "NONE");
	std::snprintf(state.guild_name, sizeof(state.guild_name), "NONE");
	state.guild_guid = -1;
	state.guild_rank = -1;
	std::snprintf(state.map_name, sizeof(state.map_name), "default");
	state.map_x = -1;
	state.map_y = -1;
	state.hp = hb::shared::calc::max_hp(G_pGame->m_formula_engine,
		hb::shared::calc::vit{(double)vit}, hb::shared::calc::level{1.0},
		hb::shared::calc::str{(double)str}, hb::shared::calc::angelic_str{0.0});
	state.mp = hb::shared::calc::max_mp(G_pGame->m_formula_engine,
		hb::shared::calc::mag{(double)mag}, hb::shared::calc::angelic_mag{0.0},
		hb::shared::calc::level{1.0}, hb::shared::calc::intel{(double)intl},
		hb::shared::calc::angelic_int{0.0});
	state.sp = hb::shared::calc::max_sp(G_pGame->m_formula_engine,
		hb::shared::calc::str{(double)str}, hb::shared::calc::angelic_str{0.0},
		hb::shared::calc::level{1.0});
	state.level = 1;
	state.rating = 0;
	state.str = str;
	state.intl = intl;
	state.vit = vit;
	state.dex = dex;
	state.mag = mag;
	state.chr = chr;
	state.luck = 10;
	state.exp = 0;
	state.lu_pool = 0;
	state.enemy_kill_count = 0;
	state.pk_count = 0;
	state.reward_gold = 0;
	state.down_skill_index = -1;
	state.id_num1 = 0;
	state.id_num2 = 0;
	state.id_num3 = 0;
	state.sex = gender;
	state.skin = skin;
	state.hair_style = hairstyle;
	state.hair_color = haircolor;
	state.underwear = under;
	state.hunger_status = 100;
	state.timeleft_rating = 0;
	state.timeleft_force_recall = 0;
	state.timeleft_firm_stamina = 0;
	state.penalty_block_year = 0;
	state.penalty_block_month = 0;
	state.penalty_block_day = 0;
	state.quest_number = 0;
	state.quest_id = 0;
	state.current_quest_count = 0;
	state.quest_reward_type = 0;
	state.quest_reward_amount = 0;
	state.contribution = 0;
	state.war_contribution = 0;
	state.quest_completed = 0;
	state.special_event_id = 200081;
	state.super_attack_left = 0;
	state.fightzone_number = 0;
	state.reserve_time = 0;
	state.fightzone_ticket_number = 0;
	state.special_ability_time = SpecialAbilityTimeSec;
	std::snprintf(state.locked_map_name, sizeof(state.locked_map_name), "NONE");
	state.locked_map_time = 0;
	state.crusade_job = 0;
	state.crusade_guid = 0;
	state.construct_point = 0;
	state.dead_penalty_time = 0;
	state.party_id = 0;
	state.gizon_item_upgrade_left = 0;
	state.appearance.clear();
	state.appearance.underwear_type = static_cast<uint8_t>(state.underwear);
	state.appearance.hair_color = static_cast<uint8_t>(state.hair_color);
	state.appearance.hair_style = static_cast<uint8_t>(state.hair_style);
	state.appearance.skin_color = static_cast<uint8_t>(state.skin);

	bool ok = InsertCharacterState(db, state);

	// Starter item IDs from gamedata.db
	constexpr int ITEM_DAGGER = 1;
	constexpr int ITEM_BIG_RED_POTION = 92;    // Health potion
	constexpr int ITEM_BIG_BLUE_POTION = 94;   // Mana potion
	constexpr int ITEM_MAP = 104;
	constexpr int ITEM_RECALL_SCROLL = 114;
	constexpr int ITEM_KNEE_TROUSERS_M = 460;  // Shorts for males
	constexpr int ITEM_BODICE_W = 473;         // Bodice for females

	std::vector<AccountDbItemRow> items;
	auto addItem = [&](int item_id, int item_color) {
		AccountDbItemRow item = {};
		item.slot = static_cast<int>(items.size());
		item.item_id = item_id;
		item.count = 1;
		item.touch_effect_type = 0;
		item.touch_effect_value1 = 0;
		item.touch_effect_value2 = 0;
		item.touch_effect_value3 = 0;
		item.item_color = item_color;
		item.spec_effect_value1 = 0;
		item.spec_effect_value2 = 0;
		item.spec_effect_value3 = 0;
		item.cur_life_span = 300;
		item.attribute = 0;
		item.pos_x = 40;
		item.pos_y = 30;
		item.is_equipped = 0;
		items.push_back(item);
	};

	addItem(ITEM_DAGGER, 0);
	addItem(ITEM_RECALL_SCROLL, 0);
	addItem(ITEM_BIG_RED_POTION, 0);
	addItem(ITEM_BIG_BLUE_POTION, 0);
	addItem(ITEM_MAP, 0);

	// Gender-specific clothing: males get shorts, females get bodice
	if (gender == 1) {
		addItem(ITEM_KNEE_TROUSERS_M, 0);
	} else {
		addItem(ITEM_BODICE_W, 0);
	}

	const char* equipStatus = "00000110000000000000000000000000000000000000000000";
	const size_t equipLen = std::strlen(equipStatus);
	for (auto& item : items) {
		if (item.slot < static_cast<int>(equipLen) && equipStatus[item.slot] == '1') {
			item.is_equipped = 1;
		}
	}

	std::vector<AccountDbIndexedValue> positionsX;
	std::vector<AccountDbIndexedValue> positionsY;
	std::vector<AccountDbIndexedValue> equips;
	for(int i = 0; i < hb::shared::limits::MaxItems; i++) {
		AccountDbIndexedValue pos_x = {};
		AccountDbIndexedValue pos_y = {};
		AccountDbIndexedValue equip = {};
		pos_x.index = i;
		pos_y.index = i;
		equip.index = i;
		pos_x.value = 40;
		pos_y.value = 30;
		equip.value = (i < static_cast<int>(equipLen) && equipStatus[i] == '1') ? 1 : 0;
		positionsX.push_back(pos_x);
		positionsY.push_back(pos_y);
		equips.push_back(equip);
	}

	std::vector<AccountDbIndexedValue> magicMastery;
	for(int i = 0; i < hb::shared::limits::MaxMagicType; i++) {
		AccountDbIndexedValue entry = {};
		entry.index = i;
		entry.value = 0;
		magicMastery.push_back(entry);
	}

	std::vector<AccountDbIndexedValue> skillMastery;
	std::vector<AccountDbIndexedValue> skillSsn;
	const char* skillSeed = "0 0 0 3 20 24 0 24 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0";
	char skillBuf[512] = {};
	std::snprintf(skillBuf, sizeof(skillBuf), "%s", skillSeed);
	char* token = std::strtok(skillBuf, " " );
	int skillIndex = 0;
	while (token != nullptr && skillIndex < hb::shared::limits::MaxSkillType) {
		AccountDbIndexedValue entry = {};
		entry.index = skillIndex;
		entry.value = std::atoi(token);
		skillMastery.push_back(entry);
		skillIndex++;
		token = std::strtok(nullptr, " " );
	}
	for (; skillIndex < hb::shared::limits::MaxSkillType; skillIndex++) {
		AccountDbIndexedValue entry = {};
		entry.index = skillIndex;
		entry.value = 0;
		skillMastery.push_back(entry);
	}
	for(int i = 0; i < hb::shared::limits::MaxSkillType; i++) {
		AccountDbIndexedValue entry = {};
		entry.index = i;
		entry.value = 0;
		skillSsn.push_back(entry);
	}

	if (ok) {
		ok &= InsertCharacterItems(db, name, items);
		ok &= InsertCharacterItemPositions(db, name, positionsX, positionsY);
		ok &= InsertCharacterItemEquips(db, name, equips);
		ok &= InsertCharacterMagicMastery(db, name, magicMastery);
		ok &= InsertCharacterSkillMastery(db, name, skillMastery);
		ok &= InsertCharacterSkillSSN(db, name, skillSsn);
	}

	CloseAccountDatabase(db);

	if (!ok) {
		send_login_msg(LogResMsg::NewCharacterFailed, LogResMsg::NewCharacterFailed, 0, 0, h);
		return;
	}

	AccountDbCharacterSummary summary = {};
	std::snprintf(summary.character_name, sizeof(summary.character_name), "%s", name);
	summary.appearance = state.appearance;
	summary.sex = static_cast<uint16_t>(state.sex);
	summary.skin = static_cast<uint16_t>(state.skin);
	summary.level = static_cast<uint16_t>(state.level);
	summary.exp = state.exp;
	std::snprintf(summary.map_name, sizeof(summary.map_name), "%s", state.map_name);
	chars.push_back(summary);

	char resp_data[512] = {};
	char* cp2 = resp_data;
	push(cp2, name, hb::shared::limits::CharNameLen);
	push(cp2, (int)chars.size());
	get_char_list(acc, cp2, chars);
	send_login_msg(LogResMsg::NewCharacterCreated, LogResMsg::NewCharacterCreated, resp_data, cp2 - resp_data, h);
}

void LoginServer::delete_character(int h, char* data)
{
	char name[hb::shared::limits::AccountNameLen] = {};
	char acc[hb::shared::limits::AccountNameLen] = {};
	char password[hb::shared::limits::AccountPassLen] = {};
	char world_name[32] = {};

	const auto* req = hb::net::PacketCast<hb::net::DeleteCharacterRequest>(data, sizeof(hb::net::DeleteCharacterRequest));
	if (!req) return;

	std::memcpy(name, req->character_name, sizeof(req->character_name));
	std::memcpy(acc, req->account_name, sizeof(req->account_name));
	LowercaseInPlace(acc, sizeof(acc));
	std::memcpy(password, req->password, sizeof(req->password));
	std::memcpy(world_name, req->world_name, 30);

	hb::logger::log("Request delete Character: {}", name);

	std::vector<AccountDbCharacterSummary> chars;
	auto status = AccountLogIn(acc, password, chars);
	if (status != LogIn::Ok)
		return;

	if (chars.size() == 0)
		return;

	sqlite3* db = nullptr;
	if (!OpenAccountDatabaseIfExists(acc, &db)) {
		return;
	}

	if (!DeleteCharacterData(db, name)) {
		CloseAccountDatabase(db);
		return;
	}

	CloseAccountDatabase(db);

	// Trading Post void: the account DB's ON DELETE CASCADE cannot reach
	// tradingpost.db, so explicitly refund counterparties' Offers on this
	// character's Listings, destroy its own escrowed items, and delete its
	// Listings/Offers/notices (see docs/adr/0001-trading-post-physical-escrow.md).
	if (G_pGame != nullptr && G_pGame->m_trading_post_store != nullptr) {
		G_pGame->m_trading_post_store->void_character(name);
	}

	for (auto it = chars.begin(); it != chars.end();) {
		if (hb_strnicmp(it->character_name, name, hb::shared::limits::CharNameLen - 1) == 0) {
			it = chars.erase(it);
			continue;
		}
		++it;
	}

	char resp_data[512] = {};
	char* cp2 = resp_data;
	push(cp2, (int)chars.size());
	get_char_list(acc, cp2, chars);
	send_login_msg(LogResMsg::CharacterDeleted, LogResMsg::CharacterDeleted, resp_data, cp2 - resp_data, h);
}

void LoginServer::change_password(int h, char* data)
{
	char acc[hb::shared::limits::AccountNameLen] = {};
	char password[hb::shared::limits::AccountPassLen] = {};
	char new_pw[hb::shared::limits::AccountPassLen] = {};
	char new_pw_conf[hb::shared::limits::AccountPassLen] = {};

	const auto* req = hb::net::PacketCast<hb::net::ChangePasswordRequest>(data, sizeof(hb::net::ChangePasswordRequest));
	if (!req) return;

	std::memcpy(acc, req->account_name, sizeof(req->account_name));
	LowercaseInPlace(acc, sizeof(acc));
	std::memcpy(password, req->password, sizeof(req->password));
	std::memcpy(new_pw, req->new_password, sizeof(req->new_password));
	std::memcpy(new_pw_conf, req->new_password_confirm, sizeof(req->new_password_confirm));

	hb::logger::log("Request change password: {}", acc);

	std::vector<AccountDbCharacterSummary> chars;
	auto status = AccountLogIn(acc, password, chars);
	if (status != LogIn::Ok) {
		send_login_msg(LogResMsg::PasswordChangeFail, LogResMsg::PasswordChangeFail, 0, 0, h);
		return;
	}

	if (string(new_pw) != new_pw_conf) {
		send_login_msg(LogResMsg::PasswordChangeFail, LogResMsg::PasswordChangeFail, 0, 0, h);
		return;
	}

	sqlite3* db = nullptr;
	if (!OpenAccountDatabaseIfExists(acc, &db)) {
		send_login_msg(LogResMsg::PasswordChangeFail, LogResMsg::PasswordChangeFail, 0, 0, h);
		return;
	}

	char newSalt[PasswordHash::SaltHexLen] = {};
	char newHash[PasswordHash::HashHexLen] = {};
	if (!PasswordHash::GenerateSalt(newSalt, sizeof(newSalt)) ||
		!PasswordHash::HashPassword(new_pw, newSalt, newHash, sizeof(newHash))) {
		send_login_msg(LogResMsg::PasswordChangeFail, LogResMsg::PasswordChangeFail, 0, 0, h);
		CloseAccountDatabase(db);
		return;
	}

	if (UpdateAccountPassword(db, acc, newHash, newSalt)) {
		send_login_msg(LogResMsg::PasswordChangeSuccess, LogResMsg::PasswordChangeSuccess, 0, 0, h);
	}
	else {
		send_login_msg(LogResMsg::PasswordChangeFail, LogResMsg::PasswordChangeFail, 0, 0, h);
	}

	CloseAccountDatabase(db);
}

void LoginServer::create_new_account(int h, char* data)
{
	char name[hb::shared::limits::AccountNameLen] = {};
	char password[hb::shared::limits::AccountPassLen] = {};
	char email_addr[hb::shared::limits::AccountEmailLen] = {};

	if (G_pGame->_lclients[h] == 0)
		return;

	const auto* req = hb::net::PacketCast<hb::net::CreateAccountRequest>(data, sizeof(hb::net::CreateAccountRequest));
	if (!req) return;

	std::strncpy(name, req->account_name, sizeof(req->account_name) - 1);
	LowercaseInPlace(name, sizeof(name));
	std::strncpy(password, req->password, sizeof(req->password) - 1);
	std::strncpy(email_addr, req->email, sizeof(req->email) - 1);

	if ((strlen(name) == 0) || (strlen(password) == 0) ||
		(strlen(email_addr) == 0))
		return;

	hb::logger::log("Request create new Account: {}", name);

	if (!IsValidName(name) || !IsValidName(password))
		return;

	if (AccountDbExists(name)) {
		hb::logger::log("Account creation failed: '{}' already exists", name);
		send_login_msg(LogResMsg::NewAccountFailed, LogResMsg::NewAccountFailed, 0, 0, h);
		return;
	}

	sqlite3* db = nullptr;
	std::string dbPath;
	if (!EnsureAccountDatabase(name, &db, dbPath)) {
		send_login_msg(LogResMsg::NewAccountFailed, LogResMsg::NewAccountFailed, 0, 0, h);
		return;
	}

	AccountDbAccountData acct_data = {};
	std::snprintf(acct_data.name, sizeof(acct_data.name), "%s", name);

	char salt[PasswordHash::SaltHexLen] = {};
	char hash[PasswordHash::HashHexLen] = {};
	if (!PasswordHash::GenerateSalt(salt, sizeof(salt)) ||
		!PasswordHash::HashPassword(password, salt, hash, sizeof(hash))) {
		CloseAccountDatabase(db);
		send_login_msg(LogResMsg::NewAccountFailed, LogResMsg::NewAccountFailed, 0, 0, h);
		return;
	}
	std::snprintf(acct_data.password_hash, sizeof(acct_data.password_hash), "%s", hash);
	std::snprintf(acct_data.password_salt, sizeof(acct_data.password_salt), "%s", salt);
	std::snprintf(acct_data.email, sizeof(acct_data.email), "%s", email_addr);

	hb::time::local_time sysTime{};
	sysTime = hb::time::local_time::now();
	hb::time::format_timestamp(sysTime, acct_data.created_at, sizeof(acct_data.created_at));
	hb::time::format_timestamp(sysTime, acct_data.password_changed_at, sizeof(acct_data.password_changed_at));
	std::snprintf(acct_data.last_ip, sizeof(acct_data.last_ip), "%s", G_pGame->_lclients[h]->ip);

	if (!InsertAccountRecord(db, acct_data)) {
		CloseAccountDatabase(db);
		send_login_msg(LogResMsg::NewAccountFailed, LogResMsg::NewAccountFailed, 0, 0, h);
		return;
	}

	CloseAccountDatabase(db);
	send_login_msg(LogResMsg::NewAccountCreated, LogResMsg::NewAccountCreated, 0, 0, h);
}

void LoginServer::send_login_msg(uint32_t msg_id, uint16_t msg_type, char* data, size_t sz, int h)
{

	int ret;
	char* cp;
	int index = h;

	if (!G_pGame->_lclients[h])
		return;

	std::memset(G_cData50000, 0, sizeof(G_cData50000));

	auto* header = reinterpret_cast<hb::net::PacketHeader*>(G_cData50000);
	header->msg_id = msg_id;
	header->msg_type = msg_type;

	cp = reinterpret_cast<char*>(G_cData50000) + sizeof(hb::net::PacketHeader);

	memcpy((char*)cp, data, sz);

	//if is registered
	if (true)
	{
		ret = G_pGame->_lclients[index]->sock->send_msg(G_cData50000, sz + 6);

		switch (ret)
		{
		case sock::Event::QueueFull:
		case sock::Event::SocketError:
		case sock::Event::CriticalError:
		case sock::Event::SocketClosed:
			hb::logger::log("Login Connection Lost on Send ({})", index);
			delete G_pGame->_lclients[index];
			G_pGame->_lclients[index] = 0;
			return;
		}
	}
}

void LoginServer::send_balance_config(int h)
{
	if (!G_pGame->_lclients[h]) return;

	auto serialized = G_pGame->m_formula_engine.serialize();
	if (serialized.empty()) return;

	// Build packet: header + serialized formula data
	std::vector<char> buf(sizeof(hb::net::PacketHeader) + serialized.size(), 0);
	auto* header = reinterpret_cast<hb::net::PacketHeader*>(buf.data());
	header->msg_id = MsgId::BalanceConfigContents;
	header->msg_type = MsgType::Confirm;
	std::memcpy(buf.data() + sizeof(hb::net::PacketHeader), serialized.data(), serialized.size());

	G_pGame->_lclients[h]->sock->send_msg(buf.data(), static_cast<int>(buf.size()));
}

void LoginServer::request_enter_game(int h, char* data)
{
	//	PutLogList("request_enter_game()");

	char name[hb::shared::limits::CharNameLen] = {};
	char map_name[11] = {};
	char acc[hb::shared::limits::AccountNameLen] = {};
	char pass[hb::shared::limits::AccountPassLen] = {};
	int lvl;
	char ws_name[31] = {};

	const auto* req = hb::net::PacketCast<hb::net::EnterGameRequest>(data, sizeof(hb::net::EnterGameRequest));
	if (!req) return;

	if (req->version_major != hb::version::compatibility::major ||
		req->version_minor != hb::version::compatibility::minor ||
		req->version_patch != hb::version::compatibility::patch)
	{
		hb::logger::warn("Version mismatch on enter-game from {}: client={}.{}.{} server={}.{}.{}",
			G_pGame->_lclients[h]->ip, req->version_major, req->version_minor, req->version_patch,
			hb::version::compatibility::major, hb::version::compatibility::minor, hb::version::compatibility::patch);
		send_login_msg(LogResMsg::VersionMismatch, LogResMsg::VersionMismatch, 0, 0, h);
		return;
	}

	std::memcpy(name, req->character_name, sizeof(req->character_name));
	std::memcpy(map_name, req->map_name, sizeof(req->map_name));
	std::memcpy(acc, req->account_name, sizeof(req->account_name));
	LowercaseInPlace(acc, sizeof(acc));
	std::memcpy(pass, req->password, sizeof(req->password));
	lvl = req->level;
	std::memcpy(ws_name, req->world_name, sizeof(req->world_name));

	char resp_data[112] = {};
	if (string(ws_name) != WORLDNAMELS)
	{
		//SendEventToWLS(ENTERGAMERESTYPE_REJECT, ENTERGAMERESTYPE_REJECT, 0, 0, h);
		return;
	}

	hb::logger::log("Request enter Game: {}", name);

	std::vector<AccountDbCharacterSummary> chars;
	auto status = AccountLogIn(acc, pass, chars);
	if (status != LogIn::Ok)
		return;

	for(int i = 0; i < MaxClients; i++)
	{
		if (!G_pGame->m_client_list[i])
			continue;

		if (string(G_pGame->m_client_list[i]->m_account_name) == acc)
		{
			G_pGame->delete_client(i, true, true);
			break;
		}
	}

	hb::net::EnterGameResponseData enterResp{};
	std::memcpy(enterResp.server_ip, G_pGame->m_game_listen_ip, sizeof(enterResp.server_ip));
	enterResp.server_port = static_cast<uint16_t>(G_pGame->m_game_listen_port);
	std::snprintf(enterResp.server_name, sizeof(enterResp.server_name), "%s", WORLDNAMELS2);

	std::memcpy(resp_data, &enterResp, sizeof(enterResp));
	send_login_msg(EnterGameRes::Confirm, EnterGameRes::Confirm, resp_data, sizeof(enterResp), h);
}

void LoginServer::local_save_player_data(int h)
{
	if (G_pGame->m_client_list[h] == 0) return;

	sqlite3* db = nullptr;
	std::string dbPath;
	if (EnsureAccountDatabase(G_pGame->m_client_list[h]->m_account_name, &db, dbPath)) {
		if (!SaveCharacterSnapshot(db, G_pGame->m_client_list[h])) {
			char logMsg[256] = {};
			hb::logger::error("SaveCharacterSnapshot failed: Account({}) Char({}) Error({})", G_pGame->m_client_list[h]->m_account_name, G_pGame->m_client_list[h]->m_char_name, sqlite3_errmsg(db));
		}
		if (G_pGame->m_client_list[h]->m_block_list_dirty) {
			if (SaveBlockList(db, G_pGame->m_client_list[h]->m_blocked_accounts_list)) {
				G_pGame->m_client_list[h]->m_block_list_dirty = false;
			}
		}
		CloseAccountDatabase(db);
	} else {
		char logMsg[256] = {};
		hb::logger::error("EnsureAccountDatabase failed: Account({})", G_pGame->m_client_list[h]->m_account_name);
	}
}
