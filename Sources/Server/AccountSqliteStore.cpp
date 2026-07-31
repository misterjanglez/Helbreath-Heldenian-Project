#include "AccountSqliteStore.h"

#include <filesystem>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <format>
#include <string>

#include "Client.h"
#include "GameDatabase.h"
#include "NetConstants.h"
#include "sqlite3.h"
#include "Log.h"
#include "TimeUtils.h"

// Single source of truth for the Game DB schema version: spliced into the DDL
// stamp and passed to the VerifySqliteSchemaVersion gate. Bump on any schema
// change (fresh start - stale dev DBs are refused, never migrated).
//
// v8 was one epoch combining two breaks (ADR 0004 + ADR 0003, plan P1.2): the
// per-account files collapsed into this one database, and every item row grew a
// Serial. They landed together so the persistence layer churned once.
//
// v9 finishes the job ADR 0004 started (#82, plan P3.4): the Trading Post's five
// escrow tables move in from tradingpost.db, because a custody move that spans
// two files cannot be one transaction under WAL no matter how it is ordered.
#define ACCOUNT_DB_SCHEMA_VERSION "9"

// The single database file. Sits beside gamedata.db / mapinfo.db / itemledger.db
// in the server's working directory; lowercase because the Linux filesystem is
// case-sensitive and the deployment copies this name verbatim.
#define GAME_DB_FILENAME "game.db"

// The pre-v8 layout, kept only so the gate can recognise and refuse it.
#define LEGACY_ACCOUNTS_DIR "accounts"

// The pre-v9 escrow file, same purpose. A world that still has one holds
// listings this database knows nothing about, and there is no migration (D6) —
// so booting next to it would silently strand every escrowed item.
#define LEGACY_TRADING_POST_DB "tradingpost.db"

using hb::server::game_db;
using hb::server::stmt_scope;

namespace
{
    static void LowercaseInPlace(char* buf, size_t len)
    {
        for (size_t i = 0; i < len && buf[i] != '\0'; i++)
            buf[i] = static_cast<char>(::tolower(static_cast<unsigned char>(buf[i])));
    }
    bool ExecSql(sqlite3* db, const char* sql)
    {
        char* err = nullptr;
        int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            char logMsg[512] = {};
            hb::logger::error("SQLite exec failed: {}", err ? err : "unknown");
            sqlite3_free(err);
            return false;
        }
        return true;
    }

    bool PrepareAndBindText(sqlite3_stmt* stmt, int idx, const char* value)
    {
        return sqlite3_bind_text(stmt, idx, value, -1, SQLITE_TRANSIENT) == SQLITE_OK;
    }

    void CopyColumnText(sqlite3_stmt* stmt, int col, char* dest, size_t destSize)
    {
        const unsigned char* text = sqlite3_column_text(stmt, col);
        if (text == nullptr) {
            if (destSize > 0) {
                dest[0] = 0;
            }
            return;
        }
        std::snprintf(dest, destSize, "%s", reinterpret_cast<const char*>(text));
    }

} // end anonymous namespace

bool BindItemAttributeColumns(sqlite3_stmt* stmt, int& col, const hb::shared::item::item_attribute_data& attributes)
{
    bool ok = true;
    ok &= (sqlite3_bind_int(stmt, col++, attributes.custom_made) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, attributes.tier) == SQLITE_OK);
    for (const auto& mod : attributes.modifiers) {
        ok &= (sqlite3_bind_int(stmt, col++, mod.type) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, mod.value) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, mod.value2) == SQLITE_OK);
    }
    ok &= (sqlite3_bind_int(stmt, col++, attributes.enchant_bonus) == SQLITE_OK);
    return ok;
}

hb::shared::item::item_attribute_data ReadItemAttributeColumns(sqlite3_stmt* stmt, int& col)
{
    hb::shared::item::item_attribute_data attributes;
    attributes.custom_made = static_cast<uint8_t>(sqlite3_column_int(stmt, col++));
    attributes.tier = static_cast<uint8_t>(sqlite3_column_int(stmt, col++));
    for (auto& mod : attributes.modifiers) {
        mod.type = static_cast<uint8_t>(sqlite3_column_int(stmt, col++));
        mod.value = static_cast<uint8_t>(sqlite3_column_int(stmt, col++));
        mod.value2 = static_cast<uint8_t>(sqlite3_column_int(stmt, col++));
    }
    attributes.enchant_bonus = static_cast<uint8_t>(sqlite3_column_int(stmt, col++));
    return attributes;
}

bool ItemAttributesLoadOk(const hb::shared::item::item_attribute_data& attributes, const std::string& row_context)
{
    if (attributes.tier_invariant_ok()) {
        return true;
    }
    hb::logger::error("{}: tier {} != modifier count {} - load rejected",
        row_context, (int)attributes.tier, (int)attributes.modifier_count());
    return false;
}

sqlite_schema_state VerifySqliteSchemaVersion(sqlite3* db, const char* db_label, const char* expected_version)
{
    // Prepared and finalized by hand rather than through stmt_scope: this runs
    // against any store's connection (the ledger and Trading Post both call it),
    // including one that is mid-open and not yet the cache's owner.
    //
    // A database with no tables at all is fresh: the caller's DDL will build
    // it at the current version.
    sqlite3_stmt* probe = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT count(*) FROM sqlite_master WHERE type='table';", -1, &probe, nullptr) != SQLITE_OK) {
        hb::logger::error("SQLite [{}]: cannot inspect schema: {}", db_label, sqlite3_errmsg(db));
        return sqlite_schema_state::mismatch;
    }
    int tableCount = 0;
    if (sqlite3_step(probe) == SQLITE_ROW) {
        tableCount = sqlite3_column_int(probe, 0);
    }
    sqlite3_finalize(probe);
    if (tableCount == 0) {
        return sqlite_schema_state::fresh;
    }

    char found[32] = {};
    if (sqlite3_prepare_v2(db, "SELECT value FROM meta WHERE key='schema_version';", -1, &probe, nullptr) == SQLITE_OK) {
        if (sqlite3_step(probe) == SQLITE_ROW) {
            CopyColumnText(probe, 0, found, sizeof(found));
        }
        sqlite3_finalize(probe);
    }

    if (std::strcmp(found, expected_version) == 0) {
        return sqlite_schema_state::current;
    }
    hb::logger::error("SQLite [{}]: schema version '{}' does not match required '{}' - "
        "stale dev database refused (fresh start, no migration); delete it to recreate",
        db_label, found[0] != 0 ? found : "none", expected_version);
    return sqlite_schema_state::mismatch;
}

// True when the pre-v8 per-account layout is still on disk. `accounts/` full of
// `<name>.db` files is a world from before ADR 0004, and there is no migration
// (D6) — so the only correct thing to do is stop and say so, rather than boot a
// fresh empty game.db next to it and let an operator discover the wipe by
// finding every character gone.
bool LegacyAccountLayoutPresent(const char* directory, std::string& outExample, int& outCount)
{
    std::error_code ec;
    outExample.clear();
    outCount = 0;
    if (directory == nullptr) {
        return false;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".db") {
            continue;
        }
        if (outCount == 0) {
            outExample = entry.path().filename().string();
        }
        outCount++;
    }
    return outCount > 0;
}

bool LegacyTradingPostFilePresent(const char* path)
{
    if (path == nullptr) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec);
}

bool EnsureGameDatabase()
{
    std::string example;
    int legacyCount = 0;
    if (LegacyAccountLayoutPresent(LEGACY_ACCOUNTS_DIR, example, legacyCount)) {
        hb::logger::error("Pre-v8 account layout found: {} per-account database(s) in '{}/' "
            "(e.g. {}). Persistence v8 consolidates these into a single '{}' and ships no "
            "migration - the world is wiped. Move or delete '{}/' to start a fresh v8 world.",
            legacyCount, LEGACY_ACCOUNTS_DIR, example, GAME_DB_FILENAME, LEGACY_ACCOUNTS_DIR);
        return false;
    }

    // Same refusal, one epoch later. The escrow tables are in game.db from v9,
    // so a surviving tradingpost.db is a board full of items this server will
    // never look at again - and every one of them was physically removed from a
    // character (ADR 0001), so booting past it strands them silently.
    if (LegacyTradingPostFilePresent(LEGACY_TRADING_POST_DB)) {
        hb::logger::error("Pre-v9 Trading Post store found: '{}'. Persistence v9 moves the escrow "
            "tables into '{}' so a custody move is one transaction, and ships no migration - the "
            "world is wiped. Move or delete '{}' to start a fresh v9 world.",
            LEGACY_TRADING_POST_DB, GAME_DB_FILENAME, LEGACY_TRADING_POST_DB);
        return false;
    }

    if (!game_db().open(GAME_DB_FILENAME)) {
        return false;
    }

    if (!EnsureGameSchema(game_db().handle(), GAME_DB_FILENAME)) {
        CloseGameDatabase();
        return false;
    }
    return true;
}

bool EnsureGameSchema(sqlite3* db, const char* db_label)
{
    if (db == nullptr) {
        return false;
    }

    const sqlite_schema_state schemaState =
        VerifySqliteSchemaVersion(db, db_label, ACCOUNT_DB_SCHEMA_VERSION);
    if (schemaState == sqlite_schema_state::mismatch) {
        return false;
    }
    if (schemaState == sqlite_schema_state::current) {
        return true;
    }

    // Fresh database: build the full schema at the current version.
    //
    // Every name column is COLLATE NOCASE. Before v8 the case-insensitivity was
    // in the queries (`WHERE x = ? COLLATE NOCASE`) and uniqueness was a
    // directory scan; putting the collation on the column instead is what makes
    // the UNIQUE constraint mean what the scan used to mean, and it is also what
    // lets those same queries use an index now that one table holds every
    // account's rows rather than one account's.
    const char* schemaSql =
        "BEGIN;"
        "CREATE TABLE IF NOT EXISTS meta ("
        " key TEXT PRIMARY KEY,"
        " value TEXT NOT NULL"
        ");"
        "INSERT INTO meta(key, value) VALUES('schema_version','" ACCOUNT_DB_SCHEMA_VERSION "');"
        "CREATE TABLE IF NOT EXISTS accounts ("
        " account_name TEXT PRIMARY KEY COLLATE NOCASE,"
        " password_hash TEXT NOT NULL,"
        " password_salt TEXT NOT NULL,"
        " email TEXT NOT NULL,"
        " created_at TEXT NOT NULL,"
        " password_changed_at TEXT NOT NULL,"
        " last_ip TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS characters ("
        " character_name TEXT PRIMARY KEY COLLATE NOCASE,"
        " account_name TEXT NOT NULL COLLATE NOCASE,"
        " created_at TEXT NOT NULL,"
        " underwear_type INTEGER NOT NULL DEFAULT 0,"
        " hair_color INTEGER NOT NULL DEFAULT 0,"
        " hair_style INTEGER NOT NULL DEFAULT 0,"
        " skin_color INTEGER NOT NULL DEFAULT 0,"
        " level INTEGER NOT NULL,"
        " exp INTEGER NOT NULL,"
        " map_name TEXT NOT NULL,"
        " map_x INTEGER NOT NULL,"
        " map_y INTEGER NOT NULL,"
        " hp INTEGER NOT NULL,"
        " mp INTEGER NOT NULL,"
        " sp INTEGER NOT NULL,"
        " str INTEGER NOT NULL,"
        " vit INTEGER NOT NULL,"
        " dex INTEGER NOT NULL,"
        " intl INTEGER NOT NULL,"
        " mag INTEGER NOT NULL,"
        " chr INTEGER NOT NULL,"
        " gender INTEGER NOT NULL,"
        " skin INTEGER NOT NULL,"
        " hairstyle INTEGER NOT NULL,"
        " haircolor INTEGER NOT NULL,"
        " underwear INTEGER NOT NULL,"
        " profile TEXT NOT NULL DEFAULT '',"
        " location TEXT NOT NULL DEFAULT '',"
        " rating INTEGER NOT NULL DEFAULT 0,"
        " luck INTEGER NOT NULL DEFAULT 0,"
        " lu_pool INTEGER NOT NULL DEFAULT 0,"
        " enemy_kill_count INTEGER NOT NULL DEFAULT 0,"
        " pk_count INTEGER NOT NULL DEFAULT 0,"
        " reward_gold INTEGER NOT NULL DEFAULT 0,"
        " downskill_index INTEGER NOT NULL DEFAULT -1,"
        " id_num1 INTEGER NOT NULL DEFAULT 0,"
        " id_num2 INTEGER NOT NULL DEFAULT 0,"
        " id_num3 INTEGER NOT NULL DEFAULT 0,"
        " hunger_status INTEGER NOT NULL DEFAULT 100,"
        " timeleft_rating INTEGER NOT NULL DEFAULT 0,"
        " timeleft_force_recall INTEGER NOT NULL DEFAULT 0,"
        " timeleft_firm_staminar INTEGER NOT NULL DEFAULT 0,"
        " penalty_block_year INTEGER NOT NULL DEFAULT 0,"
        " penalty_block_month INTEGER NOT NULL DEFAULT 0,"
        " penalty_block_day INTEGER NOT NULL DEFAULT 0,"
        " quest_number INTEGER NOT NULL DEFAULT 0,"
        " quest_id INTEGER NOT NULL DEFAULT 0,"
        " current_quest_count INTEGER NOT NULL DEFAULT 0,"
        " quest_reward_type INTEGER NOT NULL DEFAULT 0,"
        " quest_reward_amount INTEGER NOT NULL DEFAULT 0,"
        " contribution INTEGER NOT NULL DEFAULT 0,"
        " war_contribution INTEGER NOT NULL DEFAULT 0,"
        " quest_completed INTEGER NOT NULL DEFAULT 0,"
        " special_event_id INTEGER NOT NULL DEFAULT 0,"
        " super_attack_left INTEGER NOT NULL DEFAULT 0,"
        " special_ability_time INTEGER NOT NULL DEFAULT 0,"
        " locked_map_name TEXT NOT NULL DEFAULT '',"
        " locked_map_time INTEGER NOT NULL DEFAULT 0,"
        " crusade_job INTEGER NOT NULL DEFAULT 0,"
        " crusade_guid INTEGER NOT NULL DEFAULT 0,"
        " construct_point INTEGER NOT NULL DEFAULT 0,"
        " dead_penalty_time INTEGER NOT NULL DEFAULT 0,"
        " party_id INTEGER NOT NULL DEFAULT 0,"
        " gizon_item_upgrade_left INTEGER NOT NULL DEFAULT 0,"
        " FOREIGN KEY(account_name) REFERENCES accounts(account_name) ON DELETE CASCADE"
        ");"
        "CREATE TABLE IF NOT EXISTS character_items ("
        " character_name TEXT NOT NULL COLLATE NOCASE,"
        " slot INTEGER NOT NULL,"
        " item_id INTEGER NOT NULL,"
        // The Serial the item was minted with (ADR 0003). 0 means Counted:
        // stackables have no identity to carry, and merge/split would dissolve
        // it anyway. NOT NULL with a default so a row written by an older tool
        // reads as Counted rather than as a null the load path has to guess at.
        " serial INTEGER NOT NULL DEFAULT 0,"
        " count INTEGER NOT NULL,"
        " touch_effect_type INTEGER NOT NULL,"
        " touch_effect_value1 INTEGER NOT NULL,"
        " touch_effect_value2 INTEGER NOT NULL,"
        " touch_effect_value3 INTEGER NOT NULL,"
        " item_color INTEGER NOT NULL,"
        " spec_effect_value1 INTEGER NOT NULL,"
        " spec_effect_value2 INTEGER NOT NULL,"
        " spec_effect_value3 INTEGER NOT NULL,"
        " cur_durability INTEGER NOT NULL,"
        HB_ITEM_ATTR_COLUMNS_DDL
        " pos_x INTEGER NOT NULL,"
        " pos_y INTEGER NOT NULL,"
        " is_equipped INTEGER NOT NULL,"
        " PRIMARY KEY(character_name, slot),"
        " FOREIGN KEY(character_name) REFERENCES characters(character_name) ON DELETE CASCADE"
        ");"
        "CREATE TABLE IF NOT EXISTS character_bank_items ("
        " character_name TEXT NOT NULL COLLATE NOCASE,"
        " slot INTEGER NOT NULL,"
        " item_id INTEGER NOT NULL,"
        " serial INTEGER NOT NULL DEFAULT 0,"
        " count INTEGER NOT NULL,"
        " touch_effect_type INTEGER NOT NULL,"
        " touch_effect_value1 INTEGER NOT NULL,"
        " touch_effect_value2 INTEGER NOT NULL,"
        " touch_effect_value3 INTEGER NOT NULL,"
        " item_color INTEGER NOT NULL,"
        " spec_effect_value1 INTEGER NOT NULL,"
        " spec_effect_value2 INTEGER NOT NULL,"
        " spec_effect_value3 INTEGER NOT NULL,"
        " cur_durability INTEGER NOT NULL,"
        HB_ITEM_ATTR_COLUMNS_DDL
        " PRIMARY KEY(character_name, slot),"
        " FOREIGN KEY(character_name) REFERENCES characters(character_name) ON DELETE CASCADE"
        ");"
        "CREATE TABLE IF NOT EXISTS character_item_positions ("
        " character_name TEXT NOT NULL COLLATE NOCASE,"
        " slot INTEGER NOT NULL,"
        " pos_x INTEGER NOT NULL,"
        " pos_y INTEGER NOT NULL,"
        " PRIMARY KEY(character_name, slot),"
        " FOREIGN KEY(character_name) REFERENCES characters(character_name) ON DELETE CASCADE"
        ");"
        "CREATE TABLE IF NOT EXISTS character_item_equips ("
        " character_name TEXT NOT NULL COLLATE NOCASE,"
        " slot INTEGER NOT NULL,"
        " is_equipped INTEGER NOT NULL,"
        " PRIMARY KEY(character_name, slot),"
        " FOREIGN KEY(character_name) REFERENCES characters(character_name) ON DELETE CASCADE"
        ");"
        "CREATE TABLE IF NOT EXISTS character_magic_mastery ("
        " character_name TEXT NOT NULL COLLATE NOCASE,"
        " magic_index INTEGER NOT NULL,"
        " mastery_value INTEGER NOT NULL,"
        " PRIMARY KEY(character_name, magic_index),"
        " FOREIGN KEY(character_name) REFERENCES characters(character_name) ON DELETE CASCADE"
        ");"
        "CREATE TABLE IF NOT EXISTS character_skill_mastery ("
        " character_name TEXT NOT NULL COLLATE NOCASE,"
        " skill_index INTEGER NOT NULL,"
        " mastery_value INTEGER NOT NULL,"
        " PRIMARY KEY(character_name, skill_index),"
        " FOREIGN KEY(character_name) REFERENCES characters(character_name) ON DELETE CASCADE"
        ");"
        "CREATE TABLE IF NOT EXISTS character_skill_ssn ("
        " character_name TEXT NOT NULL COLLATE NOCASE,"
        " skill_index INTEGER NOT NULL,"
        " ssn_value INTEGER NOT NULL,"
        " PRIMARY KEY(character_name, skill_index),"
        " FOREIGN KEY(character_name) REFERENCES characters(character_name) ON DELETE CASCADE"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_characters_account ON characters(account_name);"
        // Reconciliation (#83) joins the ledger's current holder against these
        // two tables by Serial. Built now because adding an index later is a
        // schema bump, and a schema bump is a world wipe.
        "CREATE INDEX IF NOT EXISTS idx_character_items_serial ON character_items(serial);"
        "CREATE INDEX IF NOT EXISTS idx_character_bank_items_serial ON character_bank_items(serial);"
        // owner_account_name is new in v8. The table used to live in the owner's
        // own file, so "whose block list is this" was the filename; in one
        // database that has to be a column or every account inherits everyone's.
        "CREATE TABLE IF NOT EXISTS block_list ("
        " owner_account_name TEXT NOT NULL COLLATE NOCASE,"
        " blocked_account_name TEXT NOT NULL COLLATE NOCASE,"
        " blocked_character_name TEXT NOT NULL,"
        " PRIMARY KEY(owner_account_name, blocked_account_name),"
        " FOREIGN KEY(owner_account_name) REFERENCES accounts(account_name) ON DELETE CASCADE"
        ");"

        // --- Trading Post escrow (v9; was its own tradingpost.db) -------------
        //
        // These five tables are here, next to the character tables, for the one
        // reason ADR 0004 consolidated the account files in the first place: a
        // SQLite transaction spanning two files is not atomic under WAL. Escrow
        // is a custody move between an account's inventory and the board, so
        // while the board lived in its own file every listing, offer, refund and
        // finalized Trade was two commits with a crash window between them —
        // which ADR 0001 could only mitigate by ordering every step to fail as
        // loss rather than duplication. In one file the pair is one transaction
        // and the window is gone (#82, plan P3.4).
        //
        // The queries stay in TradingPostStore.cpp. The DDL does not, because
        // game.db has ONE schema version: a change to an escrow column is a
        // change to this database, and splitting the definition across two files
        // would let one of them be bumped without the other.
        //
        // ON DELETE CASCADE relies on foreign_keys=ON, which game_database::open
        // sets: deleting a listings row removes its listing_items, offers and
        // (transitively) offer_items; deleting an offers row removes its
        // offer_items. Item columns mirror character_bank_items above, so the
        // same record moves losslessly between a live CItem, escrow, and a
        // Warehouse. `serial` is the escrowed item's identity while no CItem
        // exists to carry it (ADR 0003); 0 for a Counted (stackable) item.
        "CREATE TABLE IF NOT EXISTS listings ("
        " listing_id      INTEGER PRIMARY KEY AUTOINCREMENT,"
        " seller_name     TEXT NOT NULL COLLATE NOCASE,"
        " seller_account  TEXT NOT NULL COLLATE NOCASE,"
        " seller_nation   INTEGER NOT NULL,"
        " seeking_note    TEXT NOT NULL DEFAULT '',"
        " created_at      INTEGER NOT NULL,"
        " expires_at      INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS listing_items ("
        " listing_id  INTEGER NOT NULL REFERENCES listings(listing_id) ON DELETE CASCADE,"
        " slot        INTEGER NOT NULL,"
        " item_id INTEGER NOT NULL,"
        " serial INTEGER NOT NULL DEFAULT 0,"
        " count INTEGER NOT NULL,"
        " touch_effect_type INTEGER NOT NULL,"
        " touch_effect_value1 INTEGER NOT NULL,"
        " touch_effect_value2 INTEGER NOT NULL,"
        " touch_effect_value3 INTEGER NOT NULL,"
        " item_color INTEGER NOT NULL,"
        " spec_effect_value1 INTEGER NOT NULL,"
        " spec_effect_value2 INTEGER NOT NULL,"
        " spec_effect_value3 INTEGER NOT NULL,"
        " cur_durability INTEGER NOT NULL,"
        HB_ITEM_ATTR_COLUMNS_DDL
        " PRIMARY KEY (listing_id, slot)"
        ");"
        "CREATE TABLE IF NOT EXISTS offers ("
        " offer_id        INTEGER PRIMARY KEY AUTOINCREMENT,"
        " listing_id      INTEGER NOT NULL REFERENCES listings(listing_id) ON DELETE CASCADE,"
        " offerer_name    TEXT NOT NULL COLLATE NOCASE,"
        " offerer_account TEXT NOT NULL COLLATE NOCASE,"
        " created_at      INTEGER NOT NULL,"
        " UNIQUE (listing_id, offerer_name)"
        ");"
        "CREATE TABLE IF NOT EXISTS offer_items ("
        " offer_id  INTEGER NOT NULL REFERENCES offers(offer_id) ON DELETE CASCADE,"
        " slot      INTEGER NOT NULL,"
        " item_id INTEGER NOT NULL,"
        " serial INTEGER NOT NULL DEFAULT 0,"
        " count INTEGER NOT NULL,"
        " touch_effect_type INTEGER NOT NULL,"
        " touch_effect_value1 INTEGER NOT NULL,"
        " touch_effect_value2 INTEGER NOT NULL,"
        " touch_effect_value3 INTEGER NOT NULL,"
        " item_color INTEGER NOT NULL,"
        " spec_effect_value1 INTEGER NOT NULL,"
        " spec_effect_value2 INTEGER NOT NULL,"
        " spec_effect_value3 INTEGER NOT NULL,"
        " cur_durability INTEGER NOT NULL,"
        HB_ITEM_ATTR_COLUMNS_DDL
        " PRIMARY KEY (offer_id, slot)"
        ");"
        "CREATE TABLE IF NOT EXISTS notices ("
        " notice_id       INTEGER PRIMARY KEY AUTOINCREMENT,"
        " character_name  TEXT NOT NULL COLLATE NOCASE,"
        " message         TEXT NOT NULL,"
        " created_at      INTEGER NOT NULL"
        ");"
        // Reconciliation (#83) has to find an item that is in escrow rather than
        // in an inventory, or it reports every listed item as missing.
        "CREATE INDEX IF NOT EXISTS idx_listing_items_serial ON listing_items(serial);"
        "CREATE INDEX IF NOT EXISTS idx_offer_items_serial ON offer_items(serial);"
        "COMMIT;";

    if (!ExecSql(db, schemaSql)) {
        return false;
    }

    hb::logger::log("Game DB schema v" ACCOUNT_DB_SCHEMA_VERSION " created ({})", db_label);
    return true;
}

void CloseGameDatabase()
{
    game_db().close();
}

bool LoadAccountRecord(sqlite3* db, const char* account_name, AccountDbAccountData& outData)
{
    if (db == nullptr || account_name == nullptr) {
        return false;
    }

    const char* sql =
        "SELECT account_name, password_hash, password_salt, email, created_at, password_changed_at, last_ip "
        "FROM accounts WHERE account_name = ? COLLATE NOCASE;";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    PrepareAndBindText(stmt, 1, account_name);
    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::memset(&outData, 0, sizeof(outData));
        CopyColumnText(stmt, 0, outData.name, sizeof(outData.name));
        CopyColumnText(stmt, 1, outData.password_hash, sizeof(outData.password_hash));
        CopyColumnText(stmt, 2, outData.password_salt, sizeof(outData.password_salt));
        CopyColumnText(stmt, 3, outData.email, sizeof(outData.email));
        CopyColumnText(stmt, 4, outData.created_at, sizeof(outData.created_at));
        CopyColumnText(stmt, 5, outData.password_changed_at, sizeof(outData.password_changed_at));
        CopyColumnText(stmt, 6, outData.last_ip, sizeof(outData.last_ip));
        ok = true;
    }

    return ok;
}

bool UpdateAccountPassword(sqlite3* db, const char* account_name, const char* passwordHash, const char* passwordSalt)
{
    if (db == nullptr || account_name == nullptr || passwordHash == nullptr || passwordSalt == nullptr) {
        return false;
    }

    hb::time::local_time sysTime{};
    sysTime = hb::time::local_time::now();
    char timestamp[32] = {};
    hb::time::format_timestamp(sysTime, timestamp, sizeof(timestamp));

    const char* sql =
        "UPDATE accounts SET password_hash = ?, password_salt = ?, password_changed_at = ? WHERE account_name = ? COLLATE NOCASE;";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    bool ok = true;
    ok &= PrepareAndBindText(stmt, 1, passwordHash);
    ok &= PrepareAndBindText(stmt, 2, passwordSalt);
    ok &= PrepareAndBindText(stmt, 3, timestamp);
    ok &= PrepareAndBindText(stmt, 4, account_name);

    if (ok) {
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }

    return ok;
}

bool ListCharacterSummaries(sqlite3* db, const char* account_name, std::vector<AccountDbCharacterSummary>& outChars)
{
    if (db == nullptr || account_name == nullptr) {
        return false;
    }

    const char* sql =
        "SELECT character_name, underwear_type, hair_color, hair_style, skin_color, gender, skin, level, exp, map_name "
        "FROM characters WHERE account_name = ? COLLATE NOCASE ORDER BY character_name;";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    PrepareAndBindText(stmt, 1, account_name);
    outChars.clear();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AccountDbCharacterSummary row = {};
        CopyColumnText(stmt, 0, row.character_name, sizeof(row.character_name));
        row.appearance.underwear_type = static_cast<uint8_t>(sqlite3_column_int(stmt, 1));
        row.appearance.hair_color = static_cast<uint8_t>(sqlite3_column_int(stmt, 2));
        row.appearance.hair_style = static_cast<uint8_t>(sqlite3_column_int(stmt, 3));
        row.appearance.skin_color = static_cast<uint8_t>(sqlite3_column_int(stmt, 4));
        row.sex = static_cast<uint16_t>(sqlite3_column_int(stmt, 5));
        row.skin = static_cast<uint16_t>(sqlite3_column_int(stmt, 6));
        row.level = static_cast<uint16_t>(sqlite3_column_int(stmt, 7));
        row.exp = static_cast<uint32_t>(sqlite3_column_int(stmt, 8));
        CopyColumnText(stmt, 9, row.map_name, sizeof(row.map_name));
        outChars.push_back(row);
    }

    return true;
}

bool LoadCharacterState(sqlite3* db, const char* character_name, AccountDbCharacterState& outState)
{
    if (db == nullptr || character_name == nullptr) {
        return false;
    }

    const char* sql =
        "SELECT account_name, character_name, profile, location, "
        "map_name, map_x, map_y, hp, mp, sp, level, rating, str, intl, vit, dex, mag, chr, luck, exp, "
        "lu_pool, enemy_kill_count, pk_count, reward_gold, downskill_index, id_num1, id_num2, id_num3, "
        "gender, skin, hairstyle, haircolor, underwear, hunger_status, timeleft_rating, "
        "timeleft_force_recall, timeleft_firm_staminar, penalty_block_year, "
        "penalty_block_month, penalty_block_day, quest_number, quest_id, current_quest_count, "
        "quest_reward_type, quest_reward_amount, contribution, war_contribution, quest_completed, "
        "special_event_id, super_attack_left, "
        "special_ability_time, locked_map_name, locked_map_time, crusade_job, crusade_guid, "
        "construct_point, dead_penalty_time, party_id, gizon_item_upgrade_left, "
        "underwear_type, hair_color, hair_style, skin_color "
        "FROM characters WHERE character_name = ? COLLATE NOCASE;";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    PrepareAndBindText(stmt, 1, character_name);
    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::memset(&outState, 0, sizeof(outState));
        int col = 0;
        CopyColumnText(stmt, col++, outState.account_name, sizeof(outState.account_name));
        CopyColumnText(stmt, col++, outState.character_name, sizeof(outState.character_name));
        CopyColumnText(stmt, col++, outState.profile, sizeof(outState.profile));
        CopyColumnText(stmt, col++, outState.location, sizeof(outState.location));
        CopyColumnText(stmt, col++, outState.map_name, sizeof(outState.map_name));
        outState.map_x = sqlite3_column_int(stmt, col++);
        outState.map_y = sqlite3_column_int(stmt, col++);
        outState.hp = sqlite3_column_int(stmt, col++);
        outState.mp = sqlite3_column_int(stmt, col++);
        outState.sp = sqlite3_column_int(stmt, col++);
        outState.level = sqlite3_column_int(stmt, col++);
        outState.rating = sqlite3_column_int(stmt, col++);
        outState.str = sqlite3_column_int(stmt, col++);
        outState.intl = sqlite3_column_int(stmt, col++);
        outState.vit = sqlite3_column_int(stmt, col++);
        outState.dex = sqlite3_column_int(stmt, col++);
        outState.mag = sqlite3_column_int(stmt, col++);
        outState.chr = sqlite3_column_int(stmt, col++);
        outState.luck = sqlite3_column_int(stmt, col++);
        outState.exp = static_cast<uint32_t>(sqlite3_column_int(stmt, col++));
        outState.lu_pool = sqlite3_column_int(stmt, col++);
        outState.enemy_kill_count = sqlite3_column_int(stmt, col++);
        outState.pk_count = sqlite3_column_int(stmt, col++);
        outState.reward_gold = static_cast<uint32_t>(sqlite3_column_int(stmt, col++));
        outState.down_skill_index = sqlite3_column_int(stmt, col++);
        outState.id_num1 = sqlite3_column_int(stmt, col++);
        outState.id_num2 = sqlite3_column_int(stmt, col++);
        outState.id_num3 = sqlite3_column_int(stmt, col++);
        outState.sex = sqlite3_column_int(stmt, col++);
        outState.skin = sqlite3_column_int(stmt, col++);
        outState.hair_style = sqlite3_column_int(stmt, col++);
        outState.hair_color = sqlite3_column_int(stmt, col++);
        outState.underwear = sqlite3_column_int(stmt, col++);
        outState.hunger_status = sqlite3_column_int(stmt, col++);
        outState.timeleft_rating = sqlite3_column_int(stmt, col++);
        outState.timeleft_force_recall = sqlite3_column_int(stmt, col++);
        outState.timeleft_firm_stamina = sqlite3_column_int(stmt, col++);
        outState.penalty_block_year = sqlite3_column_int(stmt, col++);
        outState.penalty_block_month = sqlite3_column_int(stmt, col++);
        outState.penalty_block_day = sqlite3_column_int(stmt, col++);
        outState.quest_number = sqlite3_column_int(stmt, col++);
        outState.quest_id = sqlite3_column_int(stmt, col++);
        outState.current_quest_count = sqlite3_column_int(stmt, col++);
        outState.quest_reward_type = sqlite3_column_int(stmt, col++);
        outState.quest_reward_amount = sqlite3_column_int(stmt, col++);
        outState.contribution = sqlite3_column_int(stmt, col++);
        outState.war_contribution = sqlite3_column_int(stmt, col++);
        outState.quest_completed = sqlite3_column_int(stmt, col++);
        outState.special_event_id = sqlite3_column_int(stmt, col++);
        outState.super_attack_left = sqlite3_column_int(stmt, col++);
        outState.special_ability_time = sqlite3_column_int(stmt, col++);
        CopyColumnText(stmt, col++, outState.locked_map_name, sizeof(outState.locked_map_name));
        outState.locked_map_time = sqlite3_column_int(stmt, col++);
        outState.crusade_job = sqlite3_column_int(stmt, col++);
        outState.crusade_guid = static_cast<uint32_t>(sqlite3_column_int(stmt, col++));
        outState.construct_point = sqlite3_column_int(stmt, col++);
        outState.dead_penalty_time = sqlite3_column_int(stmt, col++);
        outState.party_id = sqlite3_column_int(stmt, col++);
        outState.gizon_item_upgrade_left = sqlite3_column_int(stmt, col++);
        outState.appearance.underwear_type = static_cast<uint8_t>(sqlite3_column_int(stmt, col++));
        outState.appearance.hair_color = static_cast<uint8_t>(sqlite3_column_int(stmt, col++));
        outState.appearance.hair_style = static_cast<uint8_t>(sqlite3_column_int(stmt, col++));
        outState.appearance.skin_color = static_cast<uint8_t>(sqlite3_column_int(stmt, col++));
        ok = true;
    }

    return ok;
}

bool LoadCharacterItems(sqlite3* db, const char* character_name, std::vector<AccountDbItemRow>& outItems)
{
    if (db == nullptr || character_name == nullptr) {
        return false;
    }

    const char* sql =
        "SELECT slot, item_id, serial, count, touch_effect_type, touch_effect_value1, touch_effect_value2, "
        "touch_effect_value3, item_color, spec_effect_value1, spec_effect_value2, spec_effect_value3, "
        "cur_durability," HB_ITEM_ATTR_COLUMNS_SQL ", pos_x, pos_y, is_equipped "
        "FROM character_items WHERE character_name = ? COLLATE NOCASE ORDER BY slot;";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    PrepareAndBindText(stmt, 1, character_name);
    outItems.clear();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AccountDbItemRow row = {};
        int col = 0;
        row.slot = sqlite3_column_int(stmt, col++);
        row.item_id = sqlite3_column_int(stmt, col++);
        row.serial = sqlite3_column_int64(stmt, col++);
        row.count = sqlite3_column_int64(stmt, col++);
        row.touch_effect_type = sqlite3_column_int(stmt, col++);
        row.touch_effect_value1 = sqlite3_column_int(stmt, col++);
        row.touch_effect_value2 = sqlite3_column_int(stmt, col++);
        row.touch_effect_value3 = sqlite3_column_int(stmt, col++);
        row.item_color = sqlite3_column_int(stmt, col++);
        row.spec_effect_value1 = sqlite3_column_int(stmt, col++);
        row.spec_effect_value2 = sqlite3_column_int(stmt, col++);
        row.spec_effect_value3 = sqlite3_column_int(stmt, col++);
        row.cur_durability = sqlite3_column_int(stmt, col++);
        row.attributes = ReadItemAttributeColumns(stmt, col);
        row.pos_x = sqlite3_column_int(stmt, col++);
        row.pos_y = sqlite3_column_int(stmt, col++);
        row.is_equipped = sqlite3_column_int(stmt, col++);

        if (!ItemAttributesLoadOk(row.attributes,
            std::format("character_items '{}' slot {} item {}", character_name, row.slot, row.item_id))) {
            return false;
        }
        outItems.push_back(row);
    }

    return true;
}

bool LoadCharacterBankItems(sqlite3* db, const char* character_name, std::vector<AccountDbBankItemRow>& outItems)
{
    if (db == nullptr || character_name == nullptr) {
        return false;
    }

    const char* sql =
        "SELECT slot, item_id, serial, count, touch_effect_type, touch_effect_value1, touch_effect_value2, "
        "touch_effect_value3, item_color, spec_effect_value1, spec_effect_value2, spec_effect_value3, "
        "cur_durability," HB_ITEM_ATTR_COLUMNS_SQL " "
        "FROM character_bank_items WHERE character_name = ? COLLATE NOCASE ORDER BY slot;";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    PrepareAndBindText(stmt, 1, character_name);
    outItems.clear();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AccountDbBankItemRow row = {};
        int col = 0;
        row.slot = sqlite3_column_int(stmt, col++);
        row.item_id = sqlite3_column_int(stmt, col++);
        row.serial = sqlite3_column_int64(stmt, col++);
        row.count = sqlite3_column_int64(stmt, col++);
        row.touch_effect_type = sqlite3_column_int(stmt, col++);
        row.touch_effect_value1 = sqlite3_column_int(stmt, col++);
        row.touch_effect_value2 = sqlite3_column_int(stmt, col++);
        row.touch_effect_value3 = sqlite3_column_int(stmt, col++);
        row.item_color = sqlite3_column_int(stmt, col++);
        row.spec_effect_value1 = sqlite3_column_int(stmt, col++);
        row.spec_effect_value2 = sqlite3_column_int(stmt, col++);
        row.spec_effect_value3 = sqlite3_column_int(stmt, col++);
        row.cur_durability = sqlite3_column_int(stmt, col++);
        row.attributes = ReadItemAttributeColumns(stmt, col);

        if (!ItemAttributesLoadOk(row.attributes,
            std::format("character_bank_items '{}' slot {} item {}", character_name, row.slot, row.item_id))) {
            return false;
        }
        outItems.push_back(row);
    }

    return true;
}

bool LoadCharacterItemPositions(sqlite3* db, const char* character_name, std::vector<AccountDbIndexedValue>& outPositionsX, std::vector<AccountDbIndexedValue>& outPositionsY)
{
    if (db == nullptr || character_name == nullptr) {
        return false;
    }

    const char* sql =
        "SELECT slot, pos_x, pos_y FROM character_item_positions WHERE character_name = ? COLLATE NOCASE ORDER BY slot;";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    PrepareAndBindText(stmt, 1, character_name);
    outPositionsX.clear();
    outPositionsY.clear();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AccountDbIndexedValue pos_x = {};
        AccountDbIndexedValue pos_y = {};
        pos_x.index = sqlite3_column_int(stmt, 0);
        pos_x.value = sqlite3_column_int(stmt, 1);
        pos_y.index = pos_x.index;
        pos_y.value = sqlite3_column_int(stmt, 2);
        outPositionsX.push_back(pos_x);
        outPositionsY.push_back(pos_y);
    }

    return true;
}

bool LoadCharacterItemEquips(sqlite3* db, const char* character_name, std::vector<AccountDbIndexedValue>& outEquips)
{
    if (db == nullptr || character_name == nullptr) {
        return false;
    }

    const char* sql =
        "SELECT slot, is_equipped FROM character_item_equips WHERE character_name = ? COLLATE NOCASE ORDER BY slot;";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    PrepareAndBindText(stmt, 1, character_name);
    outEquips.clear();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AccountDbIndexedValue equip = {};
        equip.index = sqlite3_column_int(stmt, 0);
        equip.value = sqlite3_column_int(stmt, 1);
        outEquips.push_back(equip);
    }

    return true;
}

bool LoadCharacterMagicMastery(sqlite3* db, const char* character_name, std::vector<AccountDbIndexedValue>& outMastery)
{
    if (db == nullptr || character_name == nullptr) {
        return false;
    }

    const char* sql =
        "SELECT magic_index, mastery_value FROM character_magic_mastery WHERE character_name = ? COLLATE NOCASE ORDER BY magic_index;";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    PrepareAndBindText(stmt, 1, character_name);
    outMastery.clear();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AccountDbIndexedValue row = {};
        row.index = sqlite3_column_int(stmt, 0);
        row.value = sqlite3_column_int(stmt, 1);
        outMastery.push_back(row);
    }

    return true;
}

bool LoadCharacterSkillMastery(sqlite3* db, const char* character_name, std::vector<AccountDbIndexedValue>& outMastery)
{
    if (db == nullptr || character_name == nullptr) {
        return false;
    }

    const char* sql =
        "SELECT skill_index, mastery_value FROM character_skill_mastery WHERE character_name = ? COLLATE NOCASE ORDER BY skill_index;";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    PrepareAndBindText(stmt, 1, character_name);
    outMastery.clear();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AccountDbIndexedValue row = {};
        row.index = sqlite3_column_int(stmt, 0);
        row.value = sqlite3_column_int(stmt, 1);
        outMastery.push_back(row);
    }

    return true;
}

bool LoadCharacterSkillSSN(sqlite3* db, const char* character_name, std::vector<AccountDbIndexedValue>& outValues)
{
    if (db == nullptr || character_name == nullptr) {
        return false;
    }

    const char* sql =
        "SELECT skill_index, ssn_value FROM character_skill_ssn WHERE character_name = ? COLLATE NOCASE ORDER BY skill_index;";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    PrepareAndBindText(stmt, 1, character_name);
    outValues.clear();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AccountDbIndexedValue row = {};
        row.index = sqlite3_column_int(stmt, 0);
        row.value = sqlite3_column_int(stmt, 1);
        outValues.push_back(row);
    }

    return true;
}

bool LoadEquippedItemAppearances(sqlite3* db, const char* character_name, std::vector<AccountDbEquippedItem>& outItems)
{
    if (db == nullptr || character_name == nullptr) {
        return false;
    }

    const char* sql = "SELECT item_id, item_color FROM character_items WHERE character_name = ? COLLATE NOCASE AND is_equipped = 1;";
    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    PrepareAndBindText(stmt, 1, character_name);
    outItems.clear();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AccountDbEquippedItem item = {};
        item.item_id = sqlite3_column_int(stmt, 0);
        item.item_color = sqlite3_column_int(stmt, 1);
        outItems.push_back(item);
    }

    return true;
}

bool InsertCharacterState(sqlite3* db, const AccountDbCharacterState& state)
{
    if (db == nullptr) {
        return false;
    }

    hb::time::local_time sysTime{};
    sysTime = hb::time::local_time::now();
    char timestamp[32] = {};
    hb::time::format_timestamp(sysTime, timestamp, sizeof(timestamp));

    const char* sql =
        "INSERT INTO characters("
        " account_name, character_name, created_at, profile, location, "
        " map_name, map_x, map_y, hp, mp, sp, level, rating, str, intl, vit, dex, mag, chr, luck, exp, "
        " lu_pool, enemy_kill_count, pk_count, reward_gold, downskill_index, id_num1, id_num2, id_num3, "
        " gender, skin, hairstyle, haircolor, underwear, hunger_status, timeleft_rating, "
        " timeleft_force_recall, timeleft_firm_staminar, penalty_block_year, "
        " penalty_block_month, penalty_block_day, quest_number, quest_id, current_quest_count, "
        " quest_reward_type, quest_reward_amount, contribution, war_contribution, quest_completed, "
        " special_event_id, super_attack_left, "
        " special_ability_time, locked_map_name, locked_map_time, crusade_job, crusade_guid, "
        " construct_point, dead_penalty_time, party_id, gizon_item_upgrade_left,"
        " underwear_type, hair_color, hair_style, skin_color"
        ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    int col = 1;
    bool ok = true;
    ok &= PrepareAndBindText(stmt, col++, state.account_name);
    ok &= PrepareAndBindText(stmt, col++, state.character_name);
    ok &= PrepareAndBindText(stmt, col++, timestamp);
    ok &= PrepareAndBindText(stmt, col++, state.profile);
    ok &= PrepareAndBindText(stmt, col++, state.location);
    ok &= PrepareAndBindText(stmt, col++, state.map_name);
    ok &= (sqlite3_bind_int(stmt, col++, state.map_x) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.map_y) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.hp) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.mp) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.sp) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.level) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.rating) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.str) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.intl) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.vit) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.dex) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.mag) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.chr) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.luck) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, static_cast<int>(state.exp)) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.lu_pool) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.enemy_kill_count) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.pk_count) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, static_cast<int>(state.reward_gold)) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.down_skill_index) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.id_num1) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.id_num2) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.id_num3) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.sex) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.skin) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.hair_style) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.hair_color) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.underwear) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.hunger_status) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.timeleft_rating) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.timeleft_force_recall) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.timeleft_firm_stamina) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.penalty_block_year) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.penalty_block_month) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.penalty_block_day) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.quest_number) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.quest_id) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.current_quest_count) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.quest_reward_type) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.quest_reward_amount) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.contribution) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.war_contribution) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.quest_completed) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.special_event_id) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.super_attack_left) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.special_ability_time) == SQLITE_OK);
    ok &= PrepareAndBindText(stmt, col++, state.locked_map_name);
    ok &= (sqlite3_bind_int(stmt, col++, state.locked_map_time) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.crusade_job) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, static_cast<int>(state.crusade_guid)) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.construct_point) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.dead_penalty_time) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.party_id) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.gizon_item_upgrade_left) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.appearance.underwear_type) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.appearance.hair_color) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.appearance.hair_style) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, col++, state.appearance.skin_color) == SQLITE_OK);

    if (ok) {
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }

    return ok;
}

bool InsertCharacterItems(sqlite3* db, const char* character_name, const std::vector<AccountDbItemRow>& items)
{
    if (db == nullptr || character_name == nullptr) {
        return false;
    }

    const char* sql =
        "INSERT INTO character_items("
        " character_name, slot, item_id, serial, count, touch_effect_type, touch_effect_value1, touch_effect_value2,"
        " touch_effect_value3, item_color, spec_effect_value1, spec_effect_value2, spec_effect_value3,"
        " cur_durability," HB_ITEM_ATTR_COLUMNS_SQL ", pos_x, pos_y, is_equipped"
        ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?," HB_ITEM_ATTR_PLACEHOLDERS_SQL ",?,?,?);";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    for (const auto& item : items) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        int col = 1;
        bool ok = true;
        ok &= PrepareAndBindText(stmt, col++, character_name);
        ok &= (sqlite3_bind_int(stmt, col++, item.slot) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.item_id) == SQLITE_OK);
        ok &= (sqlite3_bind_int64(stmt, col++, item.serial) == SQLITE_OK);
        ok &= (sqlite3_bind_int64(stmt, col++, item.count) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.touch_effect_type) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.touch_effect_value1) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.touch_effect_value2) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.touch_effect_value3) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.item_color) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.spec_effect_value1) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.spec_effect_value2) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.spec_effect_value3) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.cur_durability) == SQLITE_OK);
        ok &= BindItemAttributeColumns(stmt, col, item.attributes);
        ok &= (sqlite3_bind_int(stmt, col++, item.pos_x) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.pos_y) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.is_equipped) == SQLITE_OK);
        if (!ok || sqlite3_step(stmt) != SQLITE_DONE) {
            return false;
        }
    }

    return true;
}

bool InsertCharacterBankItems(sqlite3* db, const char* character_name, const std::vector<AccountDbBankItemRow>& items)
{
    if (db == nullptr || character_name == nullptr) {
        return false;
    }

    const char* sql =
        "INSERT INTO character_bank_items("
        " character_name, slot, item_id, serial, count, touch_effect_type, touch_effect_value1, touch_effect_value2,"
        " touch_effect_value3, item_color, spec_effect_value1, spec_effect_value2, spec_effect_value3,"
        " cur_durability," HB_ITEM_ATTR_COLUMNS_SQL
        ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?," HB_ITEM_ATTR_PLACEHOLDERS_SQL ");";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    for (const auto& item : items) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        int col = 1;
        bool ok = true;
        ok &= PrepareAndBindText(stmt, col++, character_name);
        ok &= (sqlite3_bind_int(stmt, col++, item.slot) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.item_id) == SQLITE_OK);
        ok &= (sqlite3_bind_int64(stmt, col++, item.serial) == SQLITE_OK);
        ok &= (sqlite3_bind_int64(stmt, col++, item.count) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.touch_effect_type) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.touch_effect_value1) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.touch_effect_value2) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.touch_effect_value3) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.item_color) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.spec_effect_value1) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.spec_effect_value2) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.spec_effect_value3) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, col++, item.cur_durability) == SQLITE_OK);
        ok &= BindItemAttributeColumns(stmt, col, item.attributes);
        if (!ok || sqlite3_step(stmt) != SQLITE_DONE) {
            return false;
        }
    }

    return true;
}

bool InsertCharacterItemPositions(sqlite3* db, const char* character_name, const std::vector<AccountDbIndexedValue>& positionsX, const std::vector<AccountDbIndexedValue>& positionsY)
{
    if (db == nullptr || character_name == nullptr || positionsX.size() != positionsY.size()) {
        return false;
    }

    const char* sql =
        "INSERT INTO character_item_positions(character_name, slot, pos_x, pos_y) VALUES(?,?,?,?);";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    for (size_t i = 0; i < positionsX.size(); i++) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        bool ok = true;
        ok &= PrepareAndBindText(stmt, 1, character_name);
        ok &= (sqlite3_bind_int(stmt, 2, positionsX[i].index) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, 3, positionsX[i].value) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, 4, positionsY[i].value) == SQLITE_OK);
        if (!ok || sqlite3_step(stmt) != SQLITE_DONE) {
            return false;
        }
    }

    return true;
}

bool InsertCharacterItemEquips(sqlite3* db, const char* character_name, const std::vector<AccountDbIndexedValue>& equips)
{
    if (db == nullptr || character_name == nullptr) {
        return false;
    }

    const char* sql =
        "INSERT INTO character_item_equips(character_name, slot, is_equipped) VALUES(?,?,?);";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    for (const auto& equip : equips) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        bool ok = true;
        ok &= PrepareAndBindText(stmt, 1, character_name);
        ok &= (sqlite3_bind_int(stmt, 2, equip.index) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, 3, equip.value) == SQLITE_OK);
        if (!ok || sqlite3_step(stmt) != SQLITE_DONE) {
            return false;
        }
    }

    return true;
}

bool InsertCharacterMagicMastery(sqlite3* db, const char* character_name, const std::vector<AccountDbIndexedValue>& mastery)
{
    if (db == nullptr || character_name == nullptr) {
        return false;
    }

    const char* sql =
        "INSERT INTO character_magic_mastery(character_name, magic_index, mastery_value) VALUES(?,?,?);";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    for (const auto& entry : mastery) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        bool ok = true;
        ok &= PrepareAndBindText(stmt, 1, character_name);
        ok &= (sqlite3_bind_int(stmt, 2, entry.index) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, 3, entry.value) == SQLITE_OK);
        if (!ok || sqlite3_step(stmt) != SQLITE_DONE) {
            return false;
        }
    }

    return true;
}

bool InsertCharacterSkillMastery(sqlite3* db, const char* character_name, const std::vector<AccountDbIndexedValue>& mastery)
{
    if (db == nullptr || character_name == nullptr) {
        return false;
    }

    const char* sql =
        "INSERT INTO character_skill_mastery(character_name, skill_index, mastery_value) VALUES(?,?,?);";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    for (const auto& entry : mastery) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        bool ok = true;
        ok &= PrepareAndBindText(stmt, 1, character_name);
        ok &= (sqlite3_bind_int(stmt, 2, entry.index) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, 3, entry.value) == SQLITE_OK);
        if (!ok || sqlite3_step(stmt) != SQLITE_DONE) {
            return false;
        }
    }

    return true;
}

bool InsertCharacterSkillSSN(sqlite3* db, const char* character_name, const std::vector<AccountDbIndexedValue>& values)
{
    if (db == nullptr || character_name == nullptr) {
        return false;
    }

    const char* sql =
        "INSERT INTO character_skill_ssn(character_name, skill_index, ssn_value) VALUES(?,?,?);";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    for (const auto& entry : values) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        bool ok = true;
        ok &= PrepareAndBindText(stmt, 1, character_name);
        ok &= (sqlite3_bind_int(stmt, 2, entry.index) == SQLITE_OK);
        ok &= (sqlite3_bind_int(stmt, 3, entry.value) == SQLITE_OK);
        if (!ok || sqlite3_step(stmt) != SQLITE_DONE) {
            return false;
        }
    }

    return true;
}

bool InsertAccountRecord(sqlite3* db, const AccountDbAccountData& data)
{
    if (db == nullptr) {
        return false;
    }

    const char* sql =
        "INSERT INTO accounts("
        " account_name, password_hash, password_salt, email, created_at, password_changed_at, last_ip"
        ") VALUES(?,?,?,?,?,?,?);";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        char logMsg[512] = {};
        hb::logger::error("SQLite: account insert prepare failed: {}", sqlite3_errmsg(db));
        return false;
    }

    bool ok =
        PrepareAndBindText(stmt, 1, data.name) &&
        PrepareAndBindText(stmt, 2, data.password_hash) &&
        PrepareAndBindText(stmt, 3, data.password_salt) &&
        PrepareAndBindText(stmt, 4, data.email) &&
        PrepareAndBindText(stmt, 5, data.created_at) &&
        PrepareAndBindText(stmt, 6, data.password_changed_at) &&
        PrepareAndBindText(stmt, 7, data.last_ip);

    if (ok) {
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }

    if (!ok) {
        char logMsg[512] = {};
        hb::logger::error("SQLite: account insert failed: {}", sqlite3_errmsg(db));
    }

    return ok;
}

bool InsertCharacterRecord(sqlite3* db, const AccountDbCharacterData& data)
{
    if (db == nullptr) {
        return false;
    }

    const char* sql =
        "INSERT INTO characters("
        " character_name, account_name, created_at,"
        " underwear_type, hair_color, hair_style, skin_color,"
        " level, exp, map_name, map_x, map_y, hp, mp, sp, str, vit, dex, intl, mag, chr,"
        " gender, skin, hairstyle, haircolor, underwear"
        ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";

    stmt_scope stmt(db, sql);
    if (!stmt) {
        char logMsg[512] = {};
        hb::logger::error("SQLite: character insert prepare failed: {}", sqlite3_errmsg(db));
        return false;
    }

    int idx = 1;
    bool ok = true;
    ok &= PrepareAndBindText(stmt, idx++, data.character_name);
    ok &= PrepareAndBindText(stmt, idx++, data.account_name);
    ok &= PrepareAndBindText(stmt, idx++, data.created_at);
    ok &= (sqlite3_bind_int(stmt, idx++, data.appearance.underwear_type) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, data.appearance.hair_color) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, data.appearance.hair_style) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, data.appearance.skin_color) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, data.level) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, static_cast<int>(data.exp)) == SQLITE_OK);
    ok &= PrepareAndBindText(stmt, idx++, data.map_name);
    ok &= (sqlite3_bind_int(stmt, idx++, data.map_x) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, data.map_y) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, data.hp) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, data.mp) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, data.sp) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, data.str) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, data.vit) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, data.dex) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, data.intl) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, data.mag) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, data.chr) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, data.gender) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, data.skin) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, data.hair_style) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, data.hair_color) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, data.underwear) == SQLITE_OK);

    if (ok) {
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }

    if (!ok) {
        char logMsg[512] = {};
        hb::logger::error("SQLite: character insert failed: {}", sqlite3_errmsg(db));
    }

    return ok;
}

bool DeleteCharacterData(sqlite3* db, const char* character_name)
{
    if (db == nullptr || character_name == nullptr) {
        return false;
    }

    const char* sql = "DELETE FROM characters WHERE character_name = ? COLLATE NOCASE;";
    stmt_scope stmt(db, sql);
    if (!stmt) {
        return false;
    }

    PrepareAndBindText(stmt, 1, character_name);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    return ok;
}

bool SaveCharacterSnapshot(sqlite3* db, const CClient* client)
{
    if (db == nullptr || client == nullptr) {
        hb::logger::error("SQLite: save snapshot failed, null db or client");
        return false;
    }

    // Capture the real SQLite error while it is still the last one. The rollback
    // itself is the transaction scope's job now — every `return false` below
    // undoes this character's work on the way out, and when a mass-save sweep is
    // wrapping us that undo is a SAVEPOINT rewind, so one character failing to
    // save no longer takes the whole sweep down with it.
    auto FailStage = [&](const char* stage) {
        hb::logger::error("SQLite: save snapshot failed at [{}]: {}", stage, sqlite3_errmsg(db));
    };

    hb::server::txn_scope txn(db);
    if (!txn.active()) {
        hb::logger::error("SQLite: save snapshot BEGIN failed");
        return false;
    }

    hb::time::local_time sysTime{};
    sysTime = hb::time::local_time::now();
    char timestamp[32] = {};
    hb::time::format_timestamp(sysTime, timestamp, sizeof(timestamp));

    const char* upsertSql =
        "INSERT OR REPLACE INTO characters("
        " account_name, character_name, created_at, profile, location,"
        " map_name, map_x, map_y, hp, mp, sp, level, rating, str, intl, vit, dex, mag, chr, luck, exp,"
        " lu_pool, enemy_kill_count, pk_count, reward_gold, downskill_index, id_num1, id_num2, id_num3,"
        " gender, skin, hairstyle, haircolor, underwear, hunger_status, timeleft_rating,"
        " timeleft_force_recall, timeleft_firm_staminar, penalty_block_year,"
        " penalty_block_month, penalty_block_day, quest_number, quest_id, current_quest_count,"
        " quest_reward_type, quest_reward_amount, contribution, war_contribution, quest_completed,"
        " special_event_id, super_attack_left,"
        " special_ability_time, locked_map_name, locked_map_time, crusade_job, crusade_guid,"
        " construct_point, dead_penalty_time, party_id, gizon_item_upgrade_left,"
        " underwear_type, hair_color, hair_style, skin_color"
        ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";

    stmt_scope upsert(db, upsertSql);
    if (!upsert) {
        FailStage("characters upsert prepare");
        return false;
    }
    sqlite3_stmt* stmt = upsert;

    int idx = 1;
    bool ok = true;
    ok &= PrepareAndBindText(stmt, idx++, client->m_account_name);
    ok &= PrepareAndBindText(stmt, idx++, client->m_char_name);
    ok &= PrepareAndBindText(stmt, idx++, timestamp);
    ok &= PrepareAndBindText(stmt, idx++, client->m_profile);
    ok &= PrepareAndBindText(stmt, idx++, client->m_location);
    ok &= PrepareAndBindText(stmt, idx++, client->m_map_name);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_x) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_y) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_hp) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_mp) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_sp) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_level) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_rating) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_str) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_int) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_vit) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_dex) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_mag) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_charisma) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_luck) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, static_cast<int>(client->m_exp)) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_levelup_pool) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_enemy_kill_count) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_player_kill_count) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, static_cast<int>(client->m_reward_gold)) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_down_skill_index) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_char_id_num1) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_char_id_num2) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_char_id_num3) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_sex) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_skin) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_hair_style) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_hair_color) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_underwear) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_hunger_status) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_time_left_rating) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_time_left_force_recall) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_time_left_firm_stamina) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_penalty_block_year) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_penalty_block_month) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_penalty_block_day) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_quest) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_quest_id) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_cur_quest_count) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_quest_reward_type) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_quest_reward_amount) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_contribution) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_war_contribution) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_is_quest_completed ? 1 : 0) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_special_event_id) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_super_attack_left) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_special_ability_time) == SQLITE_OK);
    ok &= PrepareAndBindText(stmt, idx++, client->m_locked_map_name);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_locked_map_time) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_crusade_duty) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, static_cast<int>(client->m_crusade_guid)) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_construction_point) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_dead_penalty_time) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_party_id) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_gizon_item_upgrade_left) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_appearance.underwear_type) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_appearance.hair_color) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_appearance.hair_style) == SQLITE_OK);
    ok &= (sqlite3_bind_int(stmt, idx++, client->m_appearance.skin_color) == SQLITE_OK);

    if (!ok) {
        hb::logger::error("SQLite: save snapshot upsert bind failed at idx {}: {}", idx - 1, sqlite3_errmsg(db));
        return false;
    }

    int stepRc = sqlite3_step(stmt);
    if (stepRc != SQLITE_DONE) {
        hb::logger::error("SQLite: save snapshot upsert step failed (rc={}): {}", stepRc, sqlite3_errmsg(db));
        return false;
    }

    const char* deleteItemsSql = "DELETE FROM character_items WHERE character_name = ? COLLATE NOCASE;";
    const char* deleteBankSql = "DELETE FROM character_bank_items WHERE character_name = ? COLLATE NOCASE;";
    const char* deletePosSql = "DELETE FROM character_item_positions WHERE character_name = ? COLLATE NOCASE;";
    const char* deleteEquipSql = "DELETE FROM character_item_equips WHERE character_name = ? COLLATE NOCASE;";
    const char* deleteMagicSql = "DELETE FROM character_magic_mastery WHERE character_name = ? COLLATE NOCASE;";
    const char* deleteSkillSql = "DELETE FROM character_skill_mastery WHERE character_name = ? COLLATE NOCASE;";
    const char* deleteSsnSql = "DELETE FROM character_skill_ssn WHERE character_name = ? COLLATE NOCASE;";

    const char* deleteStatements[] = {
        deleteItemsSql, deleteBankSql, deletePosSql, deleteEquipSql, deleteMagicSql, deleteSkillSql, deleteSsnSql
    };

    for (const char* deleteSql : deleteStatements) {
        stmt_scope purge(db, deleteSql);
        if (!purge) {
            hb::logger::error("SQLite: save snapshot delete prepare failed: {} | SQL: {}", sqlite3_errmsg(db), deleteSql);
            return false;
        }
        PrepareAndBindText(purge, 1, client->m_char_name);
        int rc = sqlite3_step(purge);
        if (rc != SQLITE_DONE) {
            hb::logger::error("SQLite: save snapshot delete step failed (rc={}): {} | SQL: {}", rc, sqlite3_errmsg(db), deleteSql);
            return false;
        }
    }

    const char* insertItemSql =
        "INSERT INTO character_items("
        " character_name, slot, item_id, serial, count, touch_effect_type, touch_effect_value1, touch_effect_value2,"
        " touch_effect_value3, item_color, spec_effect_value1, spec_effect_value2, spec_effect_value3,"
        " cur_durability," HB_ITEM_ATTR_COLUMNS_SQL ", pos_x, pos_y, is_equipped"
        ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?," HB_ITEM_ATTR_PLACEHOLDERS_SQL ",?,?,?);";

    {
        stmt_scope insertItem(db, insertItemSql);
        if (!insertItem) {
            FailStage("character_items prepare");
            return false;
        }
        stmt = insertItem;

        for(int i = 0; i < hb::shared::limits::MaxItems; i++) {
            if (client->m_item_list[i] == nullptr) {
                continue;
            }
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
            int col = 1;
            ok = true;
            ok &= PrepareAndBindText(stmt, col++, client->m_char_name);
            ok &= (sqlite3_bind_int(stmt, col++, i) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_list[i]->m_id_num) == SQLITE_OK);
            // The Serial as minted (ADR 0003). Written unconditionally: 0 is the
            // honest value for a Counted item, and writing it keeps "what is in the
            // row" and "what is on the CItem" the same question.
            ok &= (sqlite3_bind_int64(stmt, col++, client->m_item_list[i]->m_serial) == SQLITE_OK);
            ok &= (sqlite3_bind_int64(stmt, col++, static_cast<int64_t>(client->m_item_list[i]->m_instance.count)) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_list[i]->m_instance.touch_effect_type) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_list[i]->m_instance.touch_effect_value1) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_list[i]->m_instance.touch_effect_value2) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_list[i]->m_instance.touch_effect_value3) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_list[i]->m_instance.item_color) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_list[i]->m_instance.special_effect_value1) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_list[i]->m_instance.special_effect_value2) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_list[i]->m_instance.special_effect_value3) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_list[i]->m_instance.cur_durability) == SQLITE_OK);
            ok &= BindItemAttributeColumns(stmt, col, client->m_item_list[i]->get_attributes());
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_pos_list[i].x) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_pos_list[i].y) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_is_item_equipped[i] ? 1 : 0) == SQLITE_OK);

            if (ok) {
                int rc = sqlite3_step(stmt);
                ok = rc == SQLITE_DONE;
                if (!ok) {
                    hb::logger::error("SQLite: save snapshot item[{}] step failed (rc={}): {}", i, rc, sqlite3_errmsg(db));
                }
            } else {
                hb::logger::error("SQLite: save snapshot item[{}] bind failed", i);
            }
            if (!ok) {
                return false;
            }
        }
    }

    const char* insertBankSql =
        "INSERT INTO character_bank_items("
        " character_name, slot, item_id, serial, count, touch_effect_type, touch_effect_value1, touch_effect_value2,"
        " touch_effect_value3, item_color, spec_effect_value1, spec_effect_value2, spec_effect_value3,"
        " cur_durability," HB_ITEM_ATTR_COLUMNS_SQL
        ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?," HB_ITEM_ATTR_PLACEHOLDERS_SQL ");";

    {
        stmt_scope insertBank(db, insertBankSql);
        if (!insertBank) {
            FailStage("bank_items prepare");
            return false;
        }
        stmt = insertBank;

        for(int i = 0; i < hb::shared::limits::MaxBankItems; i++) {
            if (client->m_item_in_bank_list[i] == nullptr) {
                continue;
            }
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
            int col = 1;
            ok = true;
            ok &= PrepareAndBindText(stmt, col++, client->m_char_name);
            ok &= (sqlite3_bind_int(stmt, col++, i) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_in_bank_list[i]->m_id_num) == SQLITE_OK);
            ok &= (sqlite3_bind_int64(stmt, col++, client->m_item_in_bank_list[i]->m_serial) == SQLITE_OK);
            ok &= (sqlite3_bind_int64(stmt, col++, static_cast<int64_t>(client->m_item_in_bank_list[i]->m_instance.count)) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_in_bank_list[i]->m_instance.touch_effect_type) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_in_bank_list[i]->m_instance.touch_effect_value1) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_in_bank_list[i]->m_instance.touch_effect_value2) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_in_bank_list[i]->m_instance.touch_effect_value3) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_in_bank_list[i]->m_instance.item_color) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_in_bank_list[i]->m_instance.special_effect_value1) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_in_bank_list[i]->m_instance.special_effect_value2) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_in_bank_list[i]->m_instance.special_effect_value3) == SQLITE_OK);
            ok &= (sqlite3_bind_int(stmt, col++, client->m_item_in_bank_list[i]->m_instance.cur_durability) == SQLITE_OK);
            ok &= BindItemAttributeColumns(stmt, col, client->m_item_in_bank_list[i]->get_attributes());

            if (ok) {
                ok = sqlite3_step(stmt) == SQLITE_DONE;
            }
            if (!ok) {
                FailStage("bank_items step/bind");
                return false;
            }
        }
    }

    // The four per-slot index tables. They differ only in their SQL and where
    // the value comes from, so they are one loop over a small table rather than
    // four copies of the same twenty lines — which is also how the fifth one
    // (positions, the odd shape with two values) stays visibly the exception.
    struct indexed_table
    {
        const char* stage;
        const char* sql;
        int count;
        int (*value_at)(const CClient* client, int index);
    };

    const indexed_table indexed_tables[] = {
        { "item_equips",
          "INSERT INTO character_item_equips(character_name, slot, is_equipped) VALUES(?,?,?);",
          hb::shared::limits::MaxItems,
          [](const CClient* c, int i) { return c->m_is_item_equipped[i] ? 1 : 0; } },
        { "magic_mastery",
          "INSERT INTO character_magic_mastery(character_name, magic_index, mastery_value) VALUES(?,?,?);",
          hb::shared::limits::MaxMagicType,
          [](const CClient* c, int i) { return (int)c->m_magic_mastery[i]; } },
        { "skill_mastery",
          "INSERT INTO character_skill_mastery(character_name, skill_index, mastery_value) VALUES(?,?,?);",
          hb::shared::limits::MaxSkillType,
          [](const CClient* c, int i) { return (int)c->m_skill_mastery[i]; } },
        { "skill_ssn",
          "INSERT INTO character_skill_ssn(character_name, skill_index, ssn_value) VALUES(?,?,?);",
          hb::shared::limits::MaxSkillType,
          [](const CClient* c, int i) { return (int)c->m_skill_progress[i]; } },
    };

    {
        const char* insertPosSql =
            "INSERT INTO character_item_positions(character_name, slot, pos_x, pos_y)"
            " VALUES(?,?,?,?);";
        stmt_scope pos(db, insertPosSql);
        if (!pos) {
            FailStage("item_positions prepare");
            return false;
        }
        for(int i = 0; i < hb::shared::limits::MaxItems; i++) {
            sqlite3_reset(pos);
            sqlite3_clear_bindings(pos);
            ok = true;
            ok &= PrepareAndBindText(pos, 1, client->m_char_name);
            ok &= (sqlite3_bind_int(pos, 2, i) == SQLITE_OK);
            ok &= (sqlite3_bind_int(pos, 3, client->m_item_pos_list[i].x) == SQLITE_OK);
            ok &= (sqlite3_bind_int(pos, 4, client->m_item_pos_list[i].y) == SQLITE_OK);
            if (ok) {
                ok = sqlite3_step(pos) == SQLITE_DONE;
            }
            if (!ok) {
                FailStage("item_positions step/bind");
                return false;
            }
        }
    }

    for (const auto& table : indexed_tables) {
        stmt_scope rows(db, table.sql);
        if (!rows) {
            FailStage(table.stage);
            return false;
        }
        for (int i = 0; i < table.count; i++) {
            sqlite3_reset(rows);
            sqlite3_clear_bindings(rows);
            ok = true;
            ok &= PrepareAndBindText(rows, 1, client->m_char_name);
            ok &= (sqlite3_bind_int(rows, 2, i) == SQLITE_OK);
            ok &= (sqlite3_bind_int(rows, 3, table.value_at(client, i)) == SQLITE_OK);
            if (ok) {
                ok = sqlite3_step(rows) == SQLITE_DONE;
            }
            if (!ok) {
                FailStage(table.stage);
                return false;
            }
        }
    }

    if (!txn.commit()) {
        FailStage("COMMIT");
        return false;
    }
    return true;
}

// The three lookups below, plus CountAccountStats, are what ADR 0004 meant by
// "directory-walk helpers become single queries". Each of them used to open
// every file in accounts/ and query it in turn — O(accounts) file opens to
// answer a question an index answers in one seek, on the login path.
bool CharacterNameExistsGlobally(const char* character_name)
{
    sqlite3* db = hb::server::game_db_handle();
    if (db == nullptr || character_name == nullptr || character_name[0] == '\0') {
        return false;
    }

    stmt_scope stmt(db, "SELECT 1 FROM characters WHERE character_name = ? COLLATE NOCASE LIMIT 1;");
    if (!stmt) {
        return false;
    }

    PrepareAndBindText(stmt, 1, character_name);
    return sqlite3_step(stmt) == SQLITE_ROW;
}

bool AccountNameExists(const char* account_name)
{
    sqlite3* db = hb::server::game_db_handle();
    if (db == nullptr || account_name == nullptr || account_name[0] == '\0') {
        return false;
    }

    stmt_scope stmt(db, "SELECT 1 FROM accounts WHERE account_name = ? COLLATE NOCASE LIMIT 1;");
    if (!stmt) {
        return false;
    }

    PrepareAndBindText(stmt, 1, account_name);
    return sqlite3_step(stmt) == SQLITE_ROW;
}

bool LoadBlockList(sqlite3* db, const char* owner_account_name, std::vector<std::pair<std::string, std::string>>& outBlocks)
{
    outBlocks.clear();
    if (db == nullptr || owner_account_name == nullptr) {
        return false;
    }

    stmt_scope stmt(db, "SELECT blocked_account_name, blocked_character_name FROM block_list"
        " WHERE owner_account_name = ? COLLATE NOCASE;");
    if (!stmt)
        return false;

    PrepareAndBindText(stmt, 1, owner_account_name);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char* account_name = (const char*)sqlite3_column_text(stmt, 0);
        const char* charName = (const char*)sqlite3_column_text(stmt, 1);
        if (account_name && charName)
            outBlocks.push_back(std::make_pair(std::string(account_name), std::string(charName)));
    }

    return true;
}

bool SaveBlockList(sqlite3* db, const char* owner_account_name, const std::vector<std::pair<std::string, std::string>>& blocks)
{
    if (db == nullptr || owner_account_name == nullptr) {
        return false;
    }

    // Delete-then-insert, scoped to this owner. Before v8 the DELETE had no
    // WHERE because the file held one account's rows; unscoped in one database
    // it would clear every player's block list on any player's save.
    {
        stmt_scope purge(db, "DELETE FROM block_list WHERE owner_account_name = ? COLLATE NOCASE;");
        if (!purge)
            return false;
        PrepareAndBindText(purge, 1, owner_account_name);
        if (sqlite3_step(purge) != SQLITE_DONE)
            return false;
    }

    stmt_scope stmt(db, "INSERT INTO block_list (owner_account_name, blocked_account_name,"
        " blocked_character_name) VALUES (?, ?, ?);");
    if (!stmt)
        return false;

    for (const auto& entry : blocks)
    {
        sqlite3_reset(stmt);
        PrepareAndBindText(stmt, 1, owner_account_name);
        sqlite3_bind_text(stmt, 2, entry.first.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, entry.second.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            return false;
        }
    }

    return true;
}

bool ResolveCharacterToAccount(const char* character_name, char* outAccountName, size_t accountNameSize)
{
    return ResolveCharacterToAccount(hb::server::game_db_handle(), character_name, outAccountName, accountNameSize);
}

bool ResolveCharacterToAccount(sqlite3* db, const char* character_name, char* outAccountName, size_t accountNameSize)
{
    if (db == nullptr || character_name == nullptr || outAccountName == nullptr || accountNameSize == 0)
        return false;

    stmt_scope stmt(db, "SELECT account_name FROM characters WHERE character_name = ? COLLATE NOCASE LIMIT 1;");
    if (!stmt)
        return false;

    PrepareAndBindText(stmt, 1, character_name);
    if (sqlite3_step(stmt) != SQLITE_ROW)
        return false;

    const char* acctName = (const char*)sqlite3_column_text(stmt, 0);
    if (acctName == nullptr)
        return false;

    std::strncpy(outAccountName, acctName, accountNameSize - 1);
    outAccountName[accountNameSize - 1] = '\0';
    return true;
}

account_stats CountAccountStats()
{
    account_stats stats{0, 0};
    sqlite3* db = hb::server::game_db_handle();
    if (db == nullptr) {
        return stats;
    }

    {
        stmt_scope stmt(db, "SELECT COUNT(*) FROM accounts;");
        if (stmt && sqlite3_step(stmt) == SQLITE_ROW) {
            stats.accounts = sqlite3_column_int(stmt, 0);
        }
    }
    {
        stmt_scope stmt(db, "SELECT COUNT(*) FROM characters;");
        if (stmt && sqlite3_step(stmt) == SQLITE_ROW) {
            stats.characters = sqlite3_column_int(stmt, 0);
        }
    }

    // Over-limit accounts, grouped rather than counted per file. This also
    // reports the account's stored name instead of the lowercased filename the
    // per-file walk had to use.
    {
        stmt_scope stmt(db, "SELECT account_name, COUNT(*) AS n FROM characters"
            " GROUP BY account_name HAVING n > ? ORDER BY account_name;");
        if (stmt) {
            sqlite3_bind_int(stmt, 1, hb::shared::limits::MaxCharactersPerAccount);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* name = (const char*)sqlite3_column_text(stmt, 0);
                stats.over_limit.emplace_back(name ? name : "", sqlite3_column_int(stmt, 1));
            }
        }
    }

    return stats;
}
