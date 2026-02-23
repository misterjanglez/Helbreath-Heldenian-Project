# Dialog box system modernization (Phase 5: Complete State Encapsulation)

## Phase 5A: Lifecycle Methods + HudPanel Fix
- Added `on_enable()` / `on_disable()` lifecycle overrides to IDialogBox
- DialogBoxManager calls lifecycle methods during enable/disable
- Fixed HudPanel rendering bug where dialog was drawn but not logically enabled

## Phase 5B: Move Dialog-Domain State from Game.h
- 5B.1: Moved `m_guild_op_list[100]` into DialogBox_GuildOperation with `put()`, `shift()`, `reset()` API
- 5B.2: Moved `m_sell_item_list[12]` into DialogBox_SellList with `reset()`, `add_item()`, `find_empty_slot()`, `contains()`, `get_entry()` API
- 5B.3: Moved `m_party_member` + `m_party_member_name_list` into DialogBox_Party with `reset_members()`, `add_member_name()`, `set_name_list()`, `remove_member_name()`, `is_party_member()` API
- 5B.4: Moved `m_dialog_box_exchange_info[8]` into DialogBox_Exchange as `ExchangeSlot m_slots[8]` with `reset_slots()`, `find_empty_slot()` API (95 accesses across 7 files)

## Phase 5C: Consolidate Enable + State-Init Patterns
- 5C.1: Magic `m_circle_view` set via `on_enable(v1)` — collapsed 10 hotkey cases
- 5C.2: NpcActionQuery `enable_with_target()` — consolidated 17 call sites that set mode, item_id, owner, action, target, npc_name

## Phase 5D: Centralize Blocking-Operation Check
- Added `DialogBoxManager::is_blocking_operation_active()` — checks 7 dialog IDs
- Replaced 7-way cascade in InventoryManager with single call

## Phase 5E: Item Lock API on inventory_manager
- Added `lock_item()`, `unlock_item()`, `is_locked()`, `unlock_all()` to inventory_manager
- Replaced all 50+ direct `m_is_item_disabled` accesses across 19 files

## Review Fixes
- Fixed equipped item gender sprites in DialogBox_Character — paper-doll now uses player gender instead of item gender_limit (3 call sites)
- Renamed Party mode enums: `invite_rejected` → `join_requested`, `joined` → `withdrawn` for semantic accuracy
- Fixed NpcActionQuery `m_item_index` default from 0 to -1 — prevents spurious `unlock_item(0)` on first open
- Cached repeated `get_dialog_as<DialogBox_Exchange>` lookups in NetworkMessages_Items.cpp and NetworkMessages_Exchange.cpp

## Version
- Client: 0.2.46

# Dialog box system modernization (Phase 4: Readable Dialog Code)

## Batch 7+8: Layout Constants + Hit-Rect Only (3 dialogs converted, 9 skipped)
- WarningMsg: btn_ok rect; mouse_in() in on_draw + on_click
- Character: btn_quest, btn_party, btn_levelup rects; mouse_in() in on_click
- ChatHistory: scroll_track, scroll_up, scroll_down, scroll_area rects; mouse_in() in handle_scroll_input + on_press
- Skipped: Inventory (no hit-tests), HudPanel (helper already wraps), Map (no hit-tests), GuideMap (no hit-tests), GuildOperation (fully uses ui_layout), Help (uses abstraction helper), SellList (dynamic row Y), LevelUpSetting (dynamic row Y), SysMenu (dynamic layout positions)

## Batch 6: Complex Many-Mode Dialogs (4 dialogs + 8 external files)
- CityHallMenu: enum mode with 11 values (main_menu..hero_item_confirm); 7 menu link rects; mouse_in() in DrawMode0 + on_click_mode0
- ItemUpgrade: enum mode with 11 values (gizon_upgrade..need_stone); link_normal_upgrade, link_majestic_upgrade rects; mouse_in() in DrawMode5 + on_click
- NpcActionQuery: enum mode with 7 values (npc_menu..gail); no rects (draw_highlighted_text helper)
- GuildMenu: enum mode with 23 values (main_menu..fightzone_canceled); no rects (ADJX/ADJY offset patterns)
- Updated external callers in Game.cpp (16 refs), GuildManager.cpp (4), Screen_OnGame.cpp (3), Game.Hotkeys.cpp (1), NetworkMessages_Items.cpp (6), DialogBox_Bank.cpp (1), DialogBox_Manufacture.cpp (1), DialogBox_Slates.cpp (1)

## Batch 5: Medium-Complex (4 dialogs + 1 external file)
- Manufacture: enum mode{alchemy_waiting..crafting_in_progress} (8 values); btn_try_now, btn_back_mfg, btn_manufacture rects; mouse_in() in on_click + draw (non-shaking modes only)
- Slates: enum mode{waiting, casting}; link_cast rect; mouse_in() in on_draw + on_click
- Skill: skip enum (mode -1 dead, 0 only active); area_scroll rect; mouse_in() in on_press
- Magic: no m_mode; 10 circle selector rects + btn_alchemy rect; mouse_in() in on_draw + on_click
- Updated external caller in DialogBox_Inventory.cpp (1 ref: Manufacture mode check)

## Step 0 + Batch 1: Infrastructure + Simple Dialogs (6 dialogs)
- Added `ui_rect` struct and `mouse_in()` helper to IDialogBox base class
- ConfirmExchange: enum mode{question, waiting}; btn_yes, btn_no rects
- Exchange: enum mode{pending, confirmed}; btn_exchange, btn_cancel rects
- Quest: enum mode{details, unavailable}; btn_ok rect
- CrusadeJob: enum mode{select_job, confirm}; 5 rects
- Soldier: enum mode{overview, teleport}; 5 rects
- Fishing: skip enum (single value); btn_try_now rect
- Fixed DialogBoxManager.cpp external Exchange mode reference

## Batch 2: Dialogs with m_mode Never Branched (6 dialogs)
- Resurrect: skip enum; btn_yes, btn_no rects
- ChangeStatsMajestic: skip enum; btn_cancel, btn_confirm rects
- MagicShop: skipped entirely (all dynamic hit-tests)
- Text: skip enum; btn_close, area_scroll rects
- Noticement: skip enum; btn_ok rect
- RepairAll: skip enum; btn_repair, btn_cancel rects

## Batch 3: Medium Complexity (5 dialogs + 2 external files)
- Constructor: enum mode{main, select_building, teleport}; 10 rects (toolbar, building icons, map overlay)
- GuildHallMenu: enum mode{main_menu, teleport, hire_soldier, take_flag, tutelary_angel}; 11 rects (menu links, angel options)
- NpcTalk: enum mode{ok_only, accept_decline, next}; btn_left, btn_right, link_next, area_scroll rects
- Party: enum mode with 12 values (main_menu through confirm_leave); btn_left, btn_right, link_create/leave/members rects
- ItemDrop: skip enum (m_mode set but never branched); btn_yes, btn_no, link_toggle rects
- Updated external callers in Game.cpp (2 refs) and NetworkMessages_Party.cpp (12 refs)

## Batch 4: Medium Complexity (5 dialogs + 5 external files)
- ItemDropAmount: enum mode{input, selected}; no hit-tests to convert
- Commander: enum mode{main, set_tp, use_tp, summon, set_construct}; 12 rects (toolbar, units, factions, map)
- SellOrRepair: enum mode{sell, repair, sell_pending, repair_pending}; btn_confirm, btn_cancel rects
- Bank: enum mode{waiting, list}; area_scroll rect
- Shop: skip enum (m_mode used as item index); 7 rects (buy/cancel buttons, qty buttons, scroll)
- Updated external callers in Game.cpp (6 refs), Game.Hotkeys.cpp (1), Screen_OnGame.cpp (1), DialogBoxManager.cpp (1), NetworkMessages_Items.cpp (1)

## Version
- Client: 0.2.46

# Dialog box system modernization (Phase 3: Eliminate DialogBoxInfo)

## Refactoring — Step 0: Infrastructure Prep
- Removed m_str double-write from DialogBoxManager::enable_dialog_box (was redundant after Phase 2)
- Created `give_item_state` struct in DialogBoxManager.h for GiveItem shared state (replaced Info(DialogBoxId::GiveItem) accesses)
- Added deprecation comment to `IDialogBox::info_of()` for progressive removal
- Updated Game.cpp, Screen_OnGame.cpp, DialogBox_Bank.cpp, DialogBox_Character.cpp, DialogBox_Inventory.cpp

## Refactoring — Batch 1: Self-Contained Dialogs (18 dialogs)
- Replaced anonymous `Info().m_v*` fields with typed, semantically-named members on 18 dialog classes
- Batch 1A: ConfirmExchange, Constructor, CrusadeJob, Exchange, GuildHallMenu, Quest (m_mode, m_npc_type)
- Batch 1B: ItemDrop, ItemDropAmount, LevelUpSetting, Map, MagicShop, Text (m_mode, m_item_index, m_name, m_label, m_max_amount, m_initial_lu_points, m_map_zone, m_map_id, m_page, m_scroll_view)
- Batch 1C: Resurrect, ChangeStatsMajestic, Shop, WarningMsg, Soldier, RepairAll (m_mode, m_view, m_scroll_offset, m_items_loaded, m_npc_id, m_quantity, m_item_index)
- Cross-dialog accesses use `get_dialog_as<T>()` (ConfirmExchange↔Exchange)

## Refactoring — Batch 2A: Low External Coupling (4 dialogs)
- Fishing: m_mode, m_fish_name[32], m_catch_chance, m_fish_count; added string copy to on_enable; removed unused m_v3/m_v4 sprite fields
- Noticement: m_mode, m_countdown_seconds, m_param; external caller uses typed pointer
- NpcTalk: m_mode, m_scroll_view, m_text_line_count, m_dialog_id; external callers in Game.cpp, QuestManager.cpp
- CityHallMenu: m_mode, m_hero_item_id; external callers in Game.cpp
- Updated FishingManager.cpp, NetworkMessages_Admin.cpp, Game.cpp, QuestManager.cpp

## Refactoring — Batch 2B: Low External Coupling (4 dialogs)
- ChatHistory: m_scroll_position; external caller in Game.Hotkeys.cpp
- Skill: m_mode, m_scroll_position, m_is_down_skill_pending; external callers in Game.cpp, NetworkMessages_Skills.cpp
- WarningBattleArea: no Info() usage — nothing to do
- ItemDropConfirm: IS DialogBox_ItemDrop (already migrated Batch 1B); removed dead Game.cpp writes

## Refactoring — Batch 3A: Medium External Coupling (3 dialogs)
- Commander: m_mode, m_selected_faction; cross-dialog access from DialogBox_Constructor.cpp
- Bank: m_mode, m_scroll_offset, m_item_count; external callers in Game.cpp, NetworkMessages_Bank.cpp, NetworkMessages_Items.cpp
- ItemUpgrade: m_mode, m_selected_item_index, m_stone_xelima_count, m_stone_merien_count, m_upgrade_start_time; external callers in NetworkMessages_Items.cpp

## Refactoring — Batch 3B: Medium External Coupling (3 dialogs)
- Shop (SaleMenu): finished self-referential Info() migration (~50 accesses); m_mode, m_scroll_offset, m_quantity now used directly
- SellOrRepair: m_mode, m_item_index, m_price, m_secondary_price, m_item_count; external callers in NetworkMessages_Items.cpp
- Magic: m_circle_view; external callers in Game.cpp (Ctrl+0-9 hotkeys)

## Refactoring — Batch 4A-4B: High External Coupling (4 dialogs)
- GuildMenu: m_mode; external callers in Game.cpp (14), GuildManager.cpp (4), Screen_OnGame.cpp (2), Game.Hotkeys.cpp (1)
- Party: m_mode; external callers in Game.cpp (3), NetworkMessages_Party.cpp (13)
- ItemDropExternal (DialogBox_ItemDropAmount): m_mode, m_drop_x/y, m_item_index, m_amount, m_drop_id_num, m_label[32]; external callers in Game.cpp (21), Screen_OnGame.cpp (26), Game.Hotkeys.cpp (1)
- Slates: m_mode, m_scroll_view, m_slate_names, m_slate_config_types, m_selected_slate, m_slate_page, m_slate_count; external callers in Game.cpp (6)

## Refactoring — Batch 4C: NpcActionQuery (105 total accesses)
- NpcActionQuery: m_mode, m_item_index, m_owner_type, m_action_type, m_object_id, m_target_x, m_target_y, m_npc_name[32]
- Updated Game.cpp (35), Screen_OnGame.cpp (14), plus cross-dialog accesses from Slates, Bank, Manufacture
- Removed dead SaleMenu Info() writes in on_enable (already migrated)

## Refactoring — Batch 4D: Manufacture (258 total accesses)
- Manufacture: m_mode, m_scroll_view, m_slot_1..m_slot_6, m_anim_timer, m_progress, m_anim_frame, m_result_flag, m_result_value, m_recipe_valid
- Updated BuildItemManager.cpp (7), CraftingManager.cpp (2), DialogBox_Inventory.cpp (1), Game.cpp (18)

## Final Cleanup — DialogBoxInfo Eliminated
- Removed `Info()`, `info()`, `info_of()` accessors from IDialogBox base class
- Removed `Info()` delegation methods and `s_dummy_info` from DialogBoxManager
- Removed `#include "DialogBoxInfo.h"` from all files
- Deleted `DialogBoxInfo.h` — struct no longer exists
- Fixed remaining self-referential manager-path accesses in DialogBox_ItemUpgrade.cpp (18 accesses)
- Removed dead writes to Info(i).m_flag/m_view in Game.cpp reset loop
- Fixed cross-dialog Info() accesses in Bank, Exchange, SellList (→ get_dialog_as<T>())
- Removed DialogBoxInfo.h from Client.vcxproj and Client.vcxproj.filters

## Version
- Client: 0.2.45

# Dialog box system modernization (Phase 2: Enable/Disable Ownership)

## Refactoring
- Moved 29 enable case blocks from DialogBoxManager's centralized switch into per-dialog `on_enable()` overrides (~430 lines of switch removed)
- Moved 15 disable case blocks into per-dialog `on_disable()` overrides (~150 lines of switch removed)
- Changed `on_enable()` and `on_disable()` return types to `bool` — returning `false` cancels the enable/disable (used by CrusadeJob, MagicShop, Bank)
- Added `cancels_text_input_on_enable() = false` overrides for CharacterInfo, ChatHistory, Inventory
- DialogBoxManager enable/disable methods are now fully generic (24 lines enable, 10 lines disable)
- Removed unused BuildItemManager.h and ShopManager.h includes from DialogBoxManager.cpp
- Script: `Scripts/phase2_enable_disable_ownership.py`

## Files Modified
- IDialogBox.h: `on_enable`/`on_disable` return `bool`
- DialogBoxManager.cpp: Replaced ~580 lines of switches with generic flow
- 27 DialogBox_*.h files: Added `on_enable`/`on_disable`/`cancels_text_input_on_enable` override declarations
- 27 DialogBox_*.cpp files: Added `on_enable`/`on_disable` implementations
- version.cfg: Client 0.2.44

# Dialog box system modernization (Phase 1)

Replaced fixed-size dialog box arrays with map-based dynamic ownership, implemented z-layer rendering system, removed mouse parameter threading from all dialog virtual methods (dialogs now read input directly), moved rendering loop from Game.cpp into DialogBoxManager, and moved super attack overlay into HudPanel.
