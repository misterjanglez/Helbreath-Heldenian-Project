cmake_minimum_required(VERSION 3.16)

# Include generated version info (provides HB_SERVER_VERSION etc.)
include(${CMAKE_CURRENT_SOURCE_DIR}/../version.cmake OPTIONAL)

project(HelbreathServer
    VERSION ${HB_SERVER_VERSION}
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Server sources (64 .cpp files)
# ---------------------------------------------------------------------------
set(SERVER_SOURCES
    AccountSqliteStore.cpp
    Client.cpp
    CmdBroadcast.cpp
    CmdDropOdds.cpp
    CmdGameDbCheck.cpp
    CmdGiveItem.cpp
    CheckTally.cpp
    CmdHelp.cpp
    CmdItemLogCheck.cpp
    CmdLedgerCheck.cpp
    CmdLifecycleCheck.cpp
    CmdMintCheck.cpp
    CmdSerialCheck.cpp
    CmdTpEscrowCheck.cpp
    CmdReload.cpp
    CmdRollSmoke.cpp
    CmdScatterSmoke.cpp
    CmdSetAdmin.cpp
    CmdSaveAll.cpp
    CmdSetCmdLevel.cpp
    CmdShutdown.cpp
    CmdTestCrash.cpp
    CmdShowChat.cpp
    CombatManager.cpp
    CommonTypes.cpp
    CraftingManager.cpp
    RegenManager.cpp
    DelayEventManager.cpp
    DropModel.cpp
    DynamicObjectManager.cpp
    EntityManager.cpp
    FishingManager.cpp
    Game.cpp
    GameDatabase.cpp
    GmMintSpec.cpp
    GameChatCommand.cpp
    GameCmdBeginApocalypse.cpp
    GameCmdBeginCrusade.cpp
    GameCmdBeginHeldenian.cpp
    GameCmdBlock.cpp
    GameCmdClearNpc.cpp
    GameCmdCome.cpp
    GameCmdCreateItem.cpp
    GameCmdEndApocalypse.cpp
    GameCmdEndCrusade.cpp
    GameCmdEndHeldenian.cpp
    GameCmdGiveItem.cpp
    GameCmdGM.cpp
    GameCmdGoto.cpp
    GameCmdInvis.cpp
    GameCmdRegen.cpp
    GameCmdRep.cpp
    GameCmdSaveAll.cpp
    GameCmdSpawn.cpp
    GameCmdThunder.cpp
    GameCmdUnblock.cpp
    GameCmdWhisper.cpp
    GameConfigSqliteStore.cpp
    ServerConfig.cpp
    GuildManager.cpp
    ItemLedgerStore.cpp
    ItemManager.cpp
    LegacyRollStrategy.cpp
    LoginServer.cpp
    LootManager.cpp
    MagicManager.cpp
    Map.cpp
    MapInfoSqliteStore.cpp
    MiningManager.cpp
    NetworkMsg.cpp
    Npc.cpp
    PartyManager.cpp
    Quest.cpp
    QuestManager.cpp
    ServerCommand.cpp
    ServerConsole.cpp
    ServerLog.cpp
    SkillManager.cpp
    StatusEffectManager.cpp
    TierConfigStore.cpp
    TierConfigValidator.cpp
    TieredRollStrategy.cpp
    Tile.cpp
    TradingPostManager.cpp
    TradingPostStore.cpp
    WarManager.cpp
    Wmain.cpp
)

# ---------------------------------------------------------------------------
# Shared sources (3 .cpp files)
# ---------------------------------------------------------------------------
set(SHARED_SOURCES
    ../Dependencies/Shared/Net/ASIOSocket.cpp
    ../Dependencies/Shared/Net/IOServicePool.cpp
    ../Dependencies/Shared/Log/LogBackend.cpp
    ../Dependencies/Shared/Util/SHA256.cpp
    ../Dependencies/Shared/Util/error_monitor.cpp
    ../Dependencies/Shared/Game/FormulaEngine.cpp
)

# ---------------------------------------------------------------------------
# SQLite amalgamation (compiled as C)
# ---------------------------------------------------------------------------
set(SQLITE_SOURCE
    ../Dependencies/Server/Includes/SQLite/sqlite3.c
)
set_source_files_properties(${SQLITE_SOURCE} PROPERTIES LANGUAGE C)
set_source_files_properties(${SQLITE_SOURCE} PROPERTIES COMPILE_FLAGS "-w")

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(HelbreathServer
    ${SERVER_SOURCES}
    ${SHARED_SOURCES}
    ${SQLITE_SOURCE}
)

# ---------------------------------------------------------------------------
# Include directories (mirrors vcxproj IncludePath)
# ---------------------------------------------------------------------------
target_include_directories(HelbreathServer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../Dependencies/Shared
    ${CMAKE_CURRENT_SOURCE_DIR}/../Dependencies/Shared/ASIO
    ${CMAKE_CURRENT_SOURCE_DIR}/../Dependencies/Shared/Entity
    ${CMAKE_CURRENT_SOURCE_DIR}/../Dependencies/Shared/Game
    ${CMAKE_CURRENT_SOURCE_DIR}/../Dependencies/Shared/Net
    ${CMAKE_CURRENT_SOURCE_DIR}/../Dependencies/Shared/Render
    ${CMAKE_CURRENT_SOURCE_DIR}/../Dependencies/Shared/Util
    ${CMAKE_CURRENT_SOURCE_DIR}/../Dependencies/Shared/Log
    ${CMAKE_CURRENT_SOURCE_DIR}/../Dependencies/Server/Includes
)

# ---------------------------------------------------------------------------
# Preprocessor definitions
# ---------------------------------------------------------------------------
target_compile_definitions(HelbreathServer PRIVATE TESTER_ONLY)

# ---------------------------------------------------------------------------
# Linux link libraries
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)
target_link_libraries(HelbreathServer PRIVATE
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

# ---------------------------------------------------------------------------
# Sentry crash reporting
# ---------------------------------------------------------------------------
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/sentry.cmake)
hb_enable_sentry(HelbreathServer)
