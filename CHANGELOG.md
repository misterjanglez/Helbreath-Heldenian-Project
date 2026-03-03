## 2026-03-03

### Items

- **New:** Ground item stack count display — Shift-hover tooltip on ground items now shows stack count (e.g., "500 Gold", "100 Arrow"). Server transmits item count via the previously-unused `v2` field in ItemDrop/SetItem event packets and a new `count` field in `PacketMapDataItem`. Client stores count per-tile and prepends it to the tooltip name for stacks > 1.

### Items (older)

- **Refactor:** Weight system unified — shared `calc_item_stack_weight()` and `weight_to_stones()` functions replace duplicated weight math across server (`ItemManager`), client inventory, character dialog, tooltip, and shop. Removed gold-specific `÷20` weight divisor. Increased weight precision: `weight_units_per_stone` 100→1000 (all DB weights ×10, gold stays at 1 = 0.001 stones). Weight displays 2 decimal places everywhere (was integer in character dialog, 1 decimal in tooltip/shop). Zero-weight items show no weight line instead of being floored to 1.
- **New:** Tooltip reorder — standardized section order: Name → Classification → Damage/Defence (with inline modifiers) → Standalone bonuses → Consumable info → Requirements → Weight → Durability → Stack count.
- **New:** Inline prefix modifiers — Sharp/Ancient show `(+N)` green on damage line, Strong shows `(+N%)` green on defence line, Light shows `(-N%)` green on weight line.
- **New:** Dye-colored item names — prefixed items with a dye color show the item name in the dye color in tooltips.
- **New:** HP/MP/SP potion restore ranges — potions now show "Restores HP/MP/SP: min-max" in both tooltips and shop details (previously only food items showed hunger restore).
- **New:** Zemstone usage display — Zemstone of Sacrifice (and any AlterItemDrop item) now shows "Usages: X/Y" in tooltip, tracking remaining death-protection charges.
- **Fix:** Arrow/Poison Arrow weight display — shop showed "0 Stone" due to integer division truncation. Changed to float display (`0.1 Stone`). Added weight line to item tooltips for all items, including total weight for stacks.
- **Fix:** Poison Arrow now applies poison on hit. Previously was functionally identical to a regular Arrow — no poison code existed. Added poison trigger (level 20) in `CombatManager.cpp` after arrow consumption.
- **New:** Hunger-restoring item descriptions — 28 food items now show "Restores Hunger: min-max" in both item tooltips and shop details.
- **Remove:** Magic Diamond (1081), Magic Ruby (1082), Magic Emerald (1083), Magic Sapphire (1084) removed from item database.
- **Remove:** Gold Sack items (740-744) removed from item database.
- **Remove:** Ball items (651-655) removed from item database. Cleaned up lottery handler, removed 3 dead `command_*_ball()` functions.
- **Remove:** GM Shield (623) removed from item database and related switch cases.
- **Fix:** Hero items (400-428) can no longer be dyed — set `is_dyeable=0` in database.

### UI / Input

- **Fix:** Mouse clicks now register reliably — quick/single clicks were missed because input polling only checked `is_mouse_button_down()` (level-triggered). Added `is_mouse_button_pressed()` (edge-triggered) OR to catch the press frame.
- **Fix:** Right-click dialog close no longer falls through to the game world. The 300ms debounce guard in `DialogBoxManager::handle_right_click()` now returns `true` (consumed) instead of `false`.
- **Fix:** Window centering is now monitor-aware. Resolution changes, fullscreen toggles, and borderless switches keep the window on its current monitor instead of jumping to the primary display. Uses `MonitorFromWindow` + `GetMonitorInfo` on Windows with `get_desktop_size()` fallback on Linux.

### Spells

- **Fix:** Spell targeting now clamps to nearest valid tile instead of cancelling the cast when the target is outside range. Replaced early-return with `std::clamp` in `MagicManager::player_magic_handler()`.
- **Fix:** Possession spell now broadcasts tile update to nearby clients after picking up an item. Previously the item was removed server-side but remained visible on other clients' screens.
- **Refactor:** Summon Creature spell is now data-driven. Created `summon_thresholds` table in `gamedata.db` with 5 mastery tiers (0/20/40/60/80%) and 11 creatures. Replaced hardcoded creature names (7 of 10 had naming mismatches with DB, causing silent failures) with weighted random selection by `npc_id`. All 3 failure paths now send player-visible messages instead of failing silently.

### Combat

- **Fix:** Direction-Bow (itemid:874) now deals damage and depletes arrows. Two `calculate_attack_effect` calls were commented out in `Game.cpp::client_motion_attack_handler()`.
