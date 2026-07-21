# Cap the delayed second drop at 20 seconds

### Drop system
- The bosses' delayed tier-2 drop now appears ~20s after death instead of a full minute. The second drop was tied to corpse decay (`regen_time`), which is 60s for the five bosses — too long to wait for loot. `npc_behavior_dead` now places the pending drop at `min(regen_time, 20s)`, **decoupled from corpse removal**: the corpse still lingers for its full `regen_time` and respawn timing (which is driven by corpse removal freeing the spot-mob slot) is unchanged. Ordinary mobs already decay in 3–10s, under the cap, so they are unaffected.
- Added `SecondDropMaxDelayMs = 20000` in `EntityManager.cpp`.

### Versioning
- Bumped server version to 0.1.23 (server-only change; no protocol or client impact).

# Frozen players no longer disconnected by the swing-speed anti-hack

### Bug Fixes
- Fixed an intermittent client disconnect (seen as `Game socket drain_to_queue failed: -1`) that happened when a player was frozen — e.g. by a Frost or Nizie — and swung in quick succession during the ~5s freeze. Root cause was server-side: the swing-speed anti-hack checks inflated the *minimum* legal time between swings while `frozen` (`effective_swing += frames * (bft >> 2)`), but the client never slows attacks when frozen (only movement). So swinging at the normal rate fell under the inflated threshold, tripped the "Swing hack" / "Batch swing hack" detector, and the server called `delete_client` on that one player. Removed the `frozen` term from **both** checks — `CombatManager::check_client_attack_frequency` and the 7-attack batch check in `Game.cpp`. Haste still lowers the threshold; real speed hacks are still caught.
- This matches the original game source (`HGServer`): the original player swing check is a flat 3500ms / 7-attacks rule with **no** status terms, and the original never slows a frozen *player's* attacks — the 50% action-time slow is **NPC-only** (`NpcProcess`). Frozen remains movement-only for players.

### Client
- Corrected the freeze notice text (`NOTIFYMSG_MAGICEFFECT_ON13`) from "Your movement decreases to 50" to "Your movement decreases by 25%", so the message matches the client's actual frozen movement slowdown (`EntityMotion` ×1.25). Attacks are unaffected while frozen, consistent with the original.

### Versioning
- Bumped server version to 0.1.22 and client version to 0.2.48 (bug fix touching client + server; no protocol change — compatibility stays 0.2.11).

### Networking (diagnostics / hardening)
- `drain_to_queue` now logs **which** failure produced a `-1` — `SocketClosed` (peer/server dropped the connection), `SocketError` (with `WSAErr`), or `MsgSizeTooLarge` (stream desync) — instead of collapsing all three into an opaque code. The client's `Poll` close/error cases log too. (This is what pinpointed the disconnect above as a server-initiated close rather than a networking fault.)
- `on_read` dumps the raw 3-byte frame header when rejecting an oversized frame, and guards the `size < 3` unsigned underflow that previously wrapped a garbage frame size to ~4 billion. `get_rcv_data_pointer` guards the same underflow so a desynced frame can never make `*msg_size` wrap to ~`SIZE_MAX` (which a caller would try to copy and crash on).

# Delayed second drops and 5×5 boss loot scatter

### Drop system
- The tier-2 ("second") drop is now **delayed** for every NPC instead of landing the instant the mob dies. It is still *rolled* at death (while the killer is valid), but the result is queued on the corpse and *placed* when the corpse decays — reached through the existing `npc_behavior_dead` regen-timer gate. This faithfully reproduces the original game's two-stage split: gold/tier-1 gear drop immediately on the death tile, the tier-2 bonus appears a little later. Tier-1 and gold drops are unchanged (still instant). Delay length is each NPC's own `regen_time` (regular mobs ~3–10s; the five bosses are 60s).
- **Boss loot scatter (faithful 5×5):** Wyvern, Fire-Wyvern, and Abaddon now scatter their delayed tier-2 loot across a 5×5 center-out spiral around the corpse, ported from the original `ITEMSPREAD_FIEXD_COORD` table. Each boss rolls its tier-2 table `scatter_count` times (Wyvern/Fire-Wyvern 15, Abaddon 20); the first roll keeps the one guaranteed item (skips the "nothing" slot), the rest are bonus attempts that mostly miss, so a handful of real items spread out per kill. Hellclaw and Tiger Worm do **not** scatter (matching the original) — their guaranteed second item just drops, delayed, on the exact tile.
- Scattered items only land on walkable, in-bounds tiles (`get_is_move_allowed_tile`); blocked spiral cells are skipped so loot never ends up inside a wall.

### Internals
- `spawn_npc_drop_item` gained optional `dx, dy` tile-offset parameters plus the walkability guard for offset placement; the exact corpse tile (0,0) is still placed unconditionally.
- Added `queue_pending_drop` / `spawn_pending_drops` on `CEntityManager`, and a `PendingDrop m_pending_drops[25]` / `m_pending_drop_count` buffer on `CNpc` (sized to the scatter spiral). `npc_behavior_dead` places the pending drops just before `delete_entity`.
- Added the `NpcScatterCoord[25][2]` spiral offset table to `EntityManager.cpp`.

### Data / Schema
- Added a `scatter_count INTEGER NOT NULL DEFAULT 0` column to `drop_tables` (schema-create in `EnsureGameConfigDatabase`, a self-healing `ALTER TABLE ... ADD COLUMN` migration in `LoadDropTables`, the live `Binaries/Server/gamedata.db`, and the `gameconfigs-02162026.sql` seed). Set to 15 for tables 20090/20091 (Wyvern/Fire-Wyvern) and 20 for 20092 (Abaddon); the other bosses stay 0.
- Bumped server version to 0.1.21 (server-only change; no protocol or client impact).

# Guaranteed tier-2 (second) drops for boss NPCs

### Bug Fixes
- Restored the 100% second-drop behavior for the guaranteed-drop bosses — **Helclaw, Tiger Worm, Wyvern, Fire Wyvern, and Abaddon**. Their tier-2 ("second") drop was being gated by the same flat ~5% `BASE_SECONDARY_DROP_CHANCE` as every other NPC, so a second item actually landed only ~1% of the time. These bosses now always roll their tier-2 table, and the table's `item_id=0` "nothing" slot is skipped for them, so a real second item drops on every kill. The remaining tier-2 weights still decide *which* item, so rare drops stay rare relative to common ones. All other NPCs are unchanged — their tier-1 and tier-2 chances still use the normal rating-modified gates.

### Drop system
- Added a per-drop-table `guaranteed_secondary` flag. `npc_dead_item_generator` bypasses the secondary chance gate when it is set, and `roll_drop_table_item` gained an `exclude_empty` mode that rolls only among real (non-`item_id=0`) entries so a guaranteed table can never roll "nothing".

### Data / Schema
- Added a `guaranteed_secondary INTEGER NOT NULL DEFAULT 0` column to `drop_tables` (schema-create in `EnsureGameConfigDatabase`, a self-healing `ALTER TABLE ... ADD COLUMN` migration in `LoadDropTables` for older DBs, the live `Binaries/Server/gamedata.db`, and the `gameconfigs-02162026.sql` seed). Flagged drop tables 20049, 20050, 20090, 20091, 20092.
- Bumped server version to 0.1.20 (server-only change; no protocol or client impact).

# Tester menu: Spawn NPC

### Features
- Added a "Spawn NPC" option to the tester menu, following the same pattern as "Create item". Opens a new `DialogBox_NpcSpawner` with a live server-side NPC name search, a results list (shown as `[id] name`), an amount stepper (1–50, `-10/-1/+1/+10`), and a Spawn button. Clicking a result selects it; Spawn drops the chosen NPC × amount at the player's location on the current map. Available to all players in tester builds (no admin check), mirroring the rest of the tester menu.

### Server
- Completed `TesterAction` case 10 (Spawn NPC) in `Game.cpp`: validates the NPC config id, then spawns `amount` (clamped 1–50) copies via `create_new_npc(...)` at the requesting player's tile — matching the `/spawn` GM command (`is_summoned=false` so they give EXP/drops, `bypass_mob_limit=true`). Sends a notice and logs to the `commands` channel.
- Added a `TesterNpcSearch` command handler that filters `m_npc_config_list` by case-insensitive name substring (empty = first 50) and returns a `PacketNotifyTesterNpcSearchResult` — a direct analog of the existing `TesterItemSearch` handler.

### Protocol
- Added tester-only messages `CommonType::TesterNpcSearch` and `Notify::TesterNpcSearchResult`, plus `TesterNpcSearchEntry` / `PacketNotifyTesterNpcSearchResult` structs (all under `TESTER_ONLY`). Bumped compatibility to 0.2.11, server to 0.1.19, client to 0.2.47.

# Activate the icebound map

### Bug Fixes
- Fixed an integer divide-by-zero crash in `DialogBox_GuideMap` (`draw_full_map`) that could occur on the first frame(s) after entering a map — most visibly when logging directly onto icebound. The init packet sets `m_map_index` (used to gate the mini-map draw) before `open_map_data_file` runs, and both `m_map_index` and `m_map_data->m_map_size_x/y` start uninitialized, so a render between those two steps divided player coordinates by a zero map size. Added a guard in `on_draw` that skips all mini-map plotting until the map data is actually loaded (the world render already tolerates this because it walks the tile array by pivot, not by dimensions).

### Data
- Added `icebound` to the `active_maps` table so the server actually loads it at startup. The map file (`mapdata/icebound.amd`) and all of its config (`MapInfo.db` display name, spawn generators, teleports, waypoints, no-attack area) already existed and it was referenced throughout the server (snow flag, apocalypse gate, crusade-summon check), but it was never registered as active, so `CGame::LoadMapConfig`'s allocation loop skipped it entirely. Inserted `(78, 'icebound', 1)` into both the seed `Binaries/Server/gameconfigs/gameconfigs-02162026.sql` and the live `Binaries/Server/gamedata.db`.

# Fix "bag is full" — max_load formula scale regression

### Bug Fixes
- Restored the `max_load` carry-weight formula to its original scale (`(str + angelic_str) * 500 + level * 500`). The formula-engine migration had shrunk it 100x to `str * 5 + angelic_str * 5 + level * 5`, but item weights are stored in hundredths and the server compares raw weight against `max_load`. Every character was effectively over-encumbered (a fresh level-1 character had max_load 75 while its starting dagger alone weighs 200), so all inventory additions failed with "Your bag is full."
- Fixed the character screen weight display in `DialogBox_Character.cpp` to divide `max_load` by 100 as well, matching the current-weight display scale (now reads e.g. `5/145` instead of `5/14500`).

### Data
- Updated the seeded `max_load` expression in `Scripts/setup_gamedata.py` and the live `Binaries/Server/gamedata.db` formulas table.

# Toolchain and dev-environment portability fixes

Made the Windows build script resolve Python from PATH (with py-launcher fallback) and locate MSBuild via vswhere, replacing hardcoded install paths so the build works across machines and Visual Studio versions.

Fixed the backup manager and code-search tools to find the Sources folder relative to themselves instead of a hardcoded old-machine path, restoring backup status/commit and search output. Also set the Server project to launch from the Binaries/Server runtime folder in the debugger so it finds its config and databases.
