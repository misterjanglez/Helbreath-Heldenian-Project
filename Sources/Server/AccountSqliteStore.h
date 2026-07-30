#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include "Appearance.h"
#include "Item/ItemAttributeData.h"

struct sqlite3;
struct sqlite3_stmt;

struct AccountDbAccountData
{
    char name[11];
    char password_hash[65];
    char password_salt[33];
    char email[52];
    char created_at[32];
    char password_changed_at[32];
    char last_ip[32];
};

struct AccountDbCharacterData
{
    char account_name[11];
    char character_name[11];
    char created_at[32];
    hb::shared::entity::PlayerAppearance appearance;
    int level;
    uint32_t exp;
    char map_name[11];
    int map_x;
    int map_y;
    int hp;
    int mp;
    int sp;
    int str;
    int vit;
    int dex;
    int intl;
    int mag;
    int chr;
    int gender;
    int skin;
    int hair_style;
    int hair_color;
    int underwear;
};

struct AccountDbCharacterSummary
{
    char character_name[11];
    hb::shared::entity::PlayerAppearance appearance;
    uint16_t sex;
    uint16_t skin;
    uint16_t level;
    uint32_t exp;
    char map_name[11];
};

struct AccountDbCharacterState
{
    char account_name[11];
    char character_name[11];
    char profile[256];
    char location[11];
    char map_name[11];
    int map_x;
    int map_y;
    int hp;
    int mp;
    int sp;
    int level;
    int rating;
    int str;
    int intl;
    int vit;
    int dex;
    int mag;
    int chr;
    int luck;
    uint32_t exp;
    int lu_pool;
    int enemy_kill_count;
    int pk_count;
    uint32_t reward_gold;
    int down_skill_index;
    int id_num1;
    int id_num2;
    int id_num3;
    int sex;
    int skin;
    int hair_style;
    int hair_color;
    int underwear;
    int hunger_status;
    int timeleft_rating;
    int timeleft_force_recall;
    int timeleft_firm_stamina;
    int penalty_block_year;
    int penalty_block_month;
    int penalty_block_day;
    int quest_number;
    int quest_id;
    int current_quest_count;
    int quest_reward_type;
    int quest_reward_amount;
    int contribution;
    int war_contribution;
    int quest_completed;
    int special_event_id;
    int super_attack_left;
    int special_ability_time;
    char locked_map_name[11];
    int locked_map_time;
    int crusade_job;
    uint32_t crusade_guid;
    int construct_point;
    int dead_penalty_time;
    int party_id;
    int gizon_item_upgrade_left;
    hb::shared::entity::PlayerAppearance appearance;
};

struct AccountDbItemRow
{
    int slot;
    int item_id;
    // The item's permanent Serial (ADR 0003). 0 for a Counted (stackable) item,
    // which has no identity to persist; restore_item mints one for a
    // non-stackable row that somehow arrives at 0.
    int64_t serial;
    int64_t count;
    int touch_effect_type;
    int touch_effect_value1;
    int touch_effect_value2;
    int touch_effect_value3;
    int item_color;
    int spec_effect_value1;
    int spec_effect_value2;
    int spec_effect_value3;
    int cur_durability;
    int pos_x;
    int pos_y;
    int is_equipped;
    // Loaded/inserted through the 15 flat attribute columns as one unit
    // (HB_ITEM_ATTR_COLUMNS_SQL below).
    hb::shared::item::item_attribute_data attributes;
};

struct AccountDbBankItemRow
{
    int slot;
    int item_id;
    int64_t serial;   // see AccountDbItemRow::serial
    int64_t count;
    int touch_effect_type;
    int touch_effect_value1;
    int touch_effect_value2;
    int touch_effect_value3;
    int item_color;
    int spec_effect_value1;
    int spec_effect_value2;
    int spec_effect_value3;
    int cur_durability;
    // Loaded/inserted through the 15 flat attribute columns as one unit
    // (HB_ITEM_ATTR_COLUMNS_SQL below).
    hb::shared::item::item_attribute_data attributes;
};

struct AccountDbIndexedValue
{
    int index;
    int value;
};

struct AccountDbEquippedItem
{
    int item_id;
    int item_color;
};

class CClient;

// The 15 flat item-attribute columns shared by every item table
// (character_items, character_bank_items, trading-post listing_items /
// offer_items), in item_attribute_data field order. Macros (not constexpr)
// because they splice into adjacent SQL string literals at compile time.
// Helpers bind/read the whole POD as one unit; col advances past all 15.
#define HB_ITEM_ATTR_COLUMNS_SQL \
    " custom_made, tier," \
    " mod1_type, mod1_value, mod1_value2, mod2_type, mod2_value, mod2_value2," \
    " mod3_type, mod3_value, mod3_value2, mod4_type, mod4_value, mod4_value2," \
    " enchant_bonus"
#define HB_ITEM_ATTR_PLACEHOLDERS_SQL "?,?,?,?,?,?,?,?,?,?,?,?,?,?,?"
#define HB_ITEM_ATTR_COLUMNS_DDL \
    " custom_made INTEGER NOT NULL DEFAULT 0," \
    " tier INTEGER NOT NULL DEFAULT 0," \
    " mod1_type INTEGER NOT NULL DEFAULT 0," \
    " mod1_value INTEGER NOT NULL DEFAULT 0," \
    " mod1_value2 INTEGER NOT NULL DEFAULT 0," \
    " mod2_type INTEGER NOT NULL DEFAULT 0," \
    " mod2_value INTEGER NOT NULL DEFAULT 0," \
    " mod2_value2 INTEGER NOT NULL DEFAULT 0," \
    " mod3_type INTEGER NOT NULL DEFAULT 0," \
    " mod3_value INTEGER NOT NULL DEFAULT 0," \
    " mod3_value2 INTEGER NOT NULL DEFAULT 0," \
    " mod4_type INTEGER NOT NULL DEFAULT 0," \
    " mod4_value INTEGER NOT NULL DEFAULT 0," \
    " mod4_value2 INTEGER NOT NULL DEFAULT 0," \
    " enchant_bonus INTEGER NOT NULL DEFAULT 0,"
bool BindItemAttributeColumns(sqlite3_stmt* stmt, int& col, const hb::shared::item::item_attribute_data& attributes);
hb::shared::item::item_attribute_data ReadItemAttributeColumns(sqlite3_stmt* stmt, int& col);

// The one load-path gate for the Tiers spec §3 invariant (tiered =>
// modifier count == tier): false logs "<row_context>: tier X != modifier
// count Y - load rejected" and the caller fails its load, leaving the row
// intact on disk rather than loading — and later re-saving — corrupt
// attribute data. Every ReadItemAttributeColumns caller must apply it.
bool ItemAttributesLoadOk(const hb::shared::item::item_attribute_data& attributes, const std::string& row_context);

// Loud stale-schema gate: a dev DB whose meta.schema_version differs from
// expected_version refuses to load (fresh start — no migrations). A database
// with tables but no readable version row is pre-versioning and also refused.
// Only the item-carrying stores use this; GameConfig/MapInfo stores rebuild or
// self-heal their content in place instead.
enum class sqlite_schema_state
{
    fresh,      // no tables at all - caller must run its DDL
    current,    // schema_version matches - caller may skip its DDL
    mismatch,   // stale or unreadable - refuse to load (already logged)
};
sqlite_schema_state VerifySqliteSchemaVersion(sqlite3* db, const char* db_label, const char* expected_version);

// Open the one Game DB (ADR 0004) and put the v8 schema on it, creating the
// file on a fresh world. Call once at boot, before anything reads an account.
//
// Two refusals live here, and neither has a migration behind it (D6): a
// surviving `accounts/` directory of per-account files, and a game.db stamped
// at any version but 8. Both mean "this world predates v8" and the answer to
// both is a wipe.
bool EnsureGameDatabase();
void CloseGameDatabase();

// The two halves of EnsureGameDatabase, separately callable so `gamedbcheck`
// can prove them against a scratch file instead of the live world.
//
// LegacyAccountLayoutPresent reports the pre-v8 `<dir>/<account>.db` files it
// found (outExample names the first, outCount how many). EnsureGameSchema runs
// the version gate on an already-open connection and lays down the v8 DDL when
// the database is fresh; db_label only names the file in the refusal message.
bool LegacyAccountLayoutPresent(const char* directory, std::string& outExample, int& outCount);
bool EnsureGameSchema(sqlite3* db, const char* db_label);

bool LoadAccountRecord(sqlite3* db, const char* account_name, AccountDbAccountData& outData);
bool UpdateAccountPassword(sqlite3* db, const char* account_name, const char* passwordHash, const char* passwordSalt);
bool ListCharacterSummaries(sqlite3* db, const char* account_name, std::vector<AccountDbCharacterSummary>& outChars);
bool LoadCharacterState(sqlite3* db, const char* character_name, AccountDbCharacterState& outState);
bool LoadCharacterItems(sqlite3* db, const char* character_name, std::vector<AccountDbItemRow>& outItems);
bool LoadCharacterBankItems(sqlite3* db, const char* character_name, std::vector<AccountDbBankItemRow>& outItems);
bool LoadCharacterItemPositions(sqlite3* db, const char* character_name, std::vector<AccountDbIndexedValue>& outPositionsX, std::vector<AccountDbIndexedValue>& outPositionsY);
bool LoadCharacterItemEquips(sqlite3* db, const char* character_name, std::vector<AccountDbIndexedValue>& outEquips);
bool LoadCharacterMagicMastery(sqlite3* db, const char* character_name, std::vector<AccountDbIndexedValue>& outMastery);
bool LoadCharacterSkillMastery(sqlite3* db, const char* character_name, std::vector<AccountDbIndexedValue>& outMastery);
bool LoadCharacterSkillSSN(sqlite3* db, const char* character_name, std::vector<AccountDbIndexedValue>& outValues);
bool LoadEquippedItemAppearances(sqlite3* db, const char* character_name, std::vector<AccountDbEquippedItem>& outItems);
bool InsertCharacterState(sqlite3* db, const AccountDbCharacterState& state);
bool InsertCharacterItems(sqlite3* db, const char* character_name, const std::vector<AccountDbItemRow>& items);
bool InsertCharacterBankItems(sqlite3* db, const char* character_name, const std::vector<AccountDbBankItemRow>& items);
bool InsertCharacterItemPositions(sqlite3* db, const char* character_name, const std::vector<AccountDbIndexedValue>& positionsX, const std::vector<AccountDbIndexedValue>& positionsY);
bool InsertCharacterItemEquips(sqlite3* db, const char* character_name, const std::vector<AccountDbIndexedValue>& equips);
bool InsertCharacterMagicMastery(sqlite3* db, const char* character_name, const std::vector<AccountDbIndexedValue>& mastery);
bool InsertCharacterSkillMastery(sqlite3* db, const char* character_name, const std::vector<AccountDbIndexedValue>& mastery);
bool InsertCharacterSkillSSN(sqlite3* db, const char* character_name, const std::vector<AccountDbIndexedValue>& values);
bool InsertAccountRecord(sqlite3* db, const AccountDbAccountData& data);
bool InsertCharacterRecord(sqlite3* db, const AccountDbCharacterData& data);
bool SaveCharacterSnapshot(sqlite3* db, const CClient* client);
bool DeleteCharacterData(sqlite3* db, const char* character_name);

// Block list. Scoped by the owning account: the table was one file per account
// before v8, so its rows carried no owner and every account's block list was
// implicitly "all the rows in this file".
bool LoadBlockList(sqlite3* db, const char* owner_account_name, std::vector<std::pair<std::string, std::string>>& outBlocks);
bool SaveBlockList(sqlite3* db, const char* owner_account_name, const std::vector<std::pair<std::string, std::string>>& blocks);

// The account that owns a character. One indexed lookup since v8 — it used to
// open and query every file in accounts/ until it found a hit.
bool ResolveCharacterToAccount(const char* character_name, char* outAccountName, size_t accountNameSize);

// Global name checks. Character names are `UNIQUE COLLATE NOCASE` in the
// schema now, so these are a guard rail with a friendly message in front of a
// constraint rather than the only thing enforcing uniqueness.
bool CharacterNameExistsGlobally(const char* character_name);
bool AccountNameExists(const char* account_name);

// Global counts, one query each since v8.
struct account_stats
{
    int accounts;
    int characters;
    std::vector<std::pair<std::string, int>> over_limit; // accounts with >4 characters
};
account_stats CountAccountStats();
