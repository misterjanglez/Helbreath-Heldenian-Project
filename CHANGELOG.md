## 2026-03-02

- **Refactor:** EntityManager.cpp simplification (7 phases, no logic changes):
  - Deleted duplicate `using namespace` declarations and 3 blocks of commented-out dead code.
  - Standardized ~85 pointer null checks from `== 0`/`!= 0`/`== NULL`/`!= NULL` to `nullptr`.
  - Replaced 17-case construction point switch with `constexpr` lookup table.
  - Replaced 62-entry monster spawn name/attribute switch and 45-entry `total_mob` switch with `constexpr random_spawn_info spawn_table[]`.
  - Added `is_crusade_structure()` helper to centralize crusade structure type checks.
  - Extracted 7 helper functions from the 4 longest functions: `find_spawn_position()`, `setup_entity_appearance()`, `award_kill_experience()`, `apply_crusade_contribution()`, `try_magic_attack()`, `try_ranged_attack()`, `spawn_follower_mobs()`.
  - Cleaned up C89-style declarations in `process_random_spawns()` — removed dead variables (`map_level` with 21 unused assignments, `x`, `j`, `cName_Slave`), moved remaining variables closer to first use.
- **Fix:** Initialize and reset `is_special_event` in `process_random_spawns` (EntityManager.cpp). The variable was uninitialized and never reset between map iterations, causing the special event spawn logic (`total_mob = 12`) to trigger on garbage values or leak across all maps in a tick — resulting in massive unintended spawn volumes.
- **Fix:** `m_total_alive_object` counter leak in `delete_npc_internal` (EntityManager.cpp). Code paths that delete living NPCs directly (summoned NPC cleanup, crop harvest, energy sphere, crusade structures) bypassed `on_entity_killed()`, so `m_total_alive_object` was never decremented — causing it to drift permanently upward over server runtime.
- **Fix:** Restore intentional fall-through from Frost/Nizie (case 63/79) to Beholder (case 53) in `try_ranged_attack()` (EntityManager.cpp). The extraction accidentally added a `break` that prevented Frost/Nizie from dealing ranged physical damage after applying their ice effect.
- **Fix:** GuideMap division-by-zero crash on client (DialogBox_GuideMap.cpp). The guide map dialog was enabled in `Screen_OnGame::on_initialize()` before the server sent map data, so `m_map_size_x/y` were 0 when `draw_full_map()` divided by them. Moved `enable_dialog_box(GuideMap)` to `init_data_response_handler()` after map sizes are set. Also initialized `m_map_size_x/y` to 0 in `CMapData` constructor and added early-return guards in both draw methods as defense-in-depth.
