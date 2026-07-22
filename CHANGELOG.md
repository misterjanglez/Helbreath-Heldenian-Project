# Admin chat commands to start/stop war events

### Commands (Server)
- Restored the original GM event triggers as six registered chat commands: `/begincrusade`, `/endcrusade [winner: 0=none 1=aresden 2=elvine]`, `/beginheldenian [type: 1=battlefield 2=castle siege]`, `/endheldenian`, `/beginapocalypse`, `/endapocalypse`. The WarManager engines for all three events were fully ported but unreachable — the original's `/begincrusadetotalwar`-family chat handlers were never carried over, the scheduled starters are gated behind flags hardwired `false`, and the manual entry points had zero call sites, so no war event could ever start.
- Each command is a `GameChatCommand` subclass (`GameCmdBeginCrusade` … `GameCmdEndApocalypse`), default level **Developer** (DB-overridable per command), requires `/gm on`, logs the GM order to the `commands` channel. Begin commands enforce mutual exclusion — one war event at a time, matching the original's checks — and every path reports a NoticeMsg result to the invoking admin. `/begincrusade` calls `local_start_crusade_mode` directly so the automated starter's once-per-day guard can't block a manual start.
- Rewrote `WarManager::manual_start_heldenian_mode` / `manual_end_heldenian_mode` with parsed-argument signatures (`(int heldenian_type)` / `()`). The old unreachable versions were a broken decompile port: `strtok(NULL, …)` with no initial `strtok(buff, …)` (undefined behavior) and nonsense delay math (`hour*24 + minute*60`). Manual start now sets the mode type, records the start clock, and keeps the original's mutual-exclusion guards.

### Versioning
- **Server** minor bump → 0.2.0 — new server-only functionality. No compatibility bump: no wire changes; the client already handles every notify these events emit (`Crusade`, `HeldenianTeleport`, `ApocGateStartMsg`, …).

# Auctioneer (Vince) — female plate-armor player sprite

### Trading Post (Client / NPC render)
- Replaced Vince's Phase-4 "borrow the William NPC sprite" proxy with a real **player-composite** render. Town NPCs (`CalcNpc`) are flat single-sheet monster sprites with no equipment layers, so a custom outfit isn't possible that way — only the player path (`CalcPlayer` + `RenderHelpers::draw_player_layers`) composites a gendered body with individual, colourable equipment layers.
- Vince now renders as a **female body in a full plate set**: `full_helm` (0), `plate_mail` chest (13), `hauberk` arm (21), `plate_leggings` (27), `long_boots` (31), and a generic `cape` (86). Boots + cape are tinted **green** (Items palette index 5); the plate keeps its natural colour.
- **Contained + safe:** the special case lives only in `CNpcRenderer::draw_stop` (Vince is stationary — `action_limit` gates movement, so only the idle pose is ever drawn) and forces frame 0 for a stable, in-range player idle. His true owner type stays **111 (auctioneer)** everywhere else, so click-to-open, hover, name, and proximity are all unchanged — only the pixels differ. Non-idle poses (never reached) still fall back to the Phase-4 William proxy.

# Trading Post dialog — display fixes (Phase 5 follow-up)

### Trading Post (Client / dialog)
- **Item icons now center in their slot.** Pack-atlas item sprites pivot on their own center (the convention the Exchange/Bank already rely on), but `draw_item_icon` was passing the slot's top-left corner as the draw point, so every staged/bundle item hung off the top-left of its square. It now draws at the slot center (`slot_x + slot_size/2`, `slot_y + slot_size/2`) — fixes the Create-Listing/Offer drop grid and the Detail bundle.
- **Seller nation shows its faction, coloured.** The Detail header showed a raw `nation N`; it now renders **Aresden** in red / **Elvine** in blue / **Neutral** (mapping the Seller's side, 1/2/0), and moved the expiry down to the "Listing bundle" line so the faction gets the header's right side.

# Trading Post client dialog — the Auctioneer speaks (Phase 5)

### Trading Post (Client / dialog)
- Made the Trading Post player-reachable — phase 5 of the build. Clicking **Vince** now opens `DialogBox_TradingPost` (new legacy `IDialogBox`, `DialogBoxId::TradingPost` = 56, registered in `DialogBoxManager`). Wired the click-dispatch case deferred from phase 4: `case hb::shared::owner::auctioneer` in the `switch (object_type)` at `Game.cpp` opens the dialog, whose `on_enable` requests board page 0.
- **One view-switched dialog** (dark parchment panel drawn from `draw_rect_filled`/`draw_rect_outline` primitives, sized to the board) with tabs **[Browse] [My Listings] [My Offers] [+ List]**:
  - **Browse / My Listings / My Offers** — paged rows: bundle summary (names via `item_name_formatter` from `TpItemBrief` id/count/attribute), seller, offer count, note preview, expiry; Prev/Next page arrows; click a row → Detail.
  - **Detail** — bundle icons with hover full-stat info (from `TpItemFull`), seeking note, seller/nation, expiry; scrollable Offers list with **contextual** buttons — `[Finalize]` per Offer + `[Delist]` when I am the Seller, `[Rescind]` on my own Offer, `[Place Offer]` otherwise. "Mine" = seller/offerer name equals my character name (case-insensitive).
  - **Create Listing / Create Offer** — shared 4-slot drag-drop grid (`on_item_drop` from inventory; stackables route through `DialogBox_ItemDropAmount` via a new drop-target `1003`), a seeking-note text input (≤ 60 chars) on the Listing variant, `[Post Listing]` / `[Place Offer]` confirm. Staged items are locked in inventory and unlock on remove/cancel/close.
- **Receive handlers** (`NetworkMessages_TradingPost.cpp`, dispatched from `Screen_OnGame::on_game_msg`): `ResponseTpBoardPage` / `ResponseTpListingDetail` feed the open dialog; `ResponseTpActionResult` drives a result **toast** and a pull-only refresh (board re-requested after each own successful action; on Ok, Create-Listing → My Listings, Place-Offer → the Listing's Detail, Finalize/Delist → back to the board, Rescind → refresh Detail).
- **Toast strings** for every `TpResultCode` (`lan_eng.h`, `TP_TOAST_*`), player-facing vocabulary per `CONTEXT.md` (Listing, Offer, Finalize, Rescind, Delist, Warehouse). `WarehouseFull` is worded as a completion, not a failure ("A Warehouse was full — the action completed; check your Warehouse"), matching the server contract where a recipient hitting the 1000 hard cap during finalize/delist still completes the action.
- **Cheap client-side mirrors** of the server's validation to avoid pointless round-trips (empty bundle, > 4 items, note length via the input cap, Offer on your own Listing → the same `AccountSelfTrade` message, Finalize/Delist only shown to the Seller). The server stays authoritative for all of them.
- **Integration check (cross-boundary, verified):** escrow-in already notifies the actor's client — full-item removal sends `Notify::ItemDepletedEraseItem` (+ `ItemReleased` for equipped), partial stacks send `Notify::set_item_count` — so listed/offered items visibly leave the inventory on server confirm. No server-side gap to close.

### Trading Post (My Listings / My Offers server query)
- The v1 board is pull-only and a browse row (`TpBoardRow`) carries only `seller` + `offer_count`, never offerer identity, so a character's own Offers cannot be derived from board data. Added a small **read-only** query rather than a client-side hack: new `MsgId::RequestTpMyBoard` + `PacketRequestTpMyBoard { page; which }` (`TpMyBoardFilter` = MyListings / MyOffers); the response **reuses `PacketResponseTpBoardPage`** (identical row shape).
- Server: `trading_post_store::get_my_listings_page` (`WHERE seller_name = ? COLLATE NOCASE`) and `get_my_offers_page` (Listings whose `listing_id IN (SELECT … FROM offers WHERE offerer_name = ?)`), both built on a refactored `query_board` helper shared with `get_board_page`; `trading_post_manager::handle_my_board` (mirrors `handle_board_page` with the filter) + a shared `send_board_page_packet`; one dispatch case in `Game.cpp`. Read-only — the escrow/dupe-safety core is untouched.

### Build / cross-platform
- New client files `DialogBox_TradingPost.{h,cpp}` and `NetworkMessages_TradingPost.cpp` added to `Client.vcxproj` (+ `.filters`); the Linux `CMakeLists.txt` globs `*.cpp`, so it picks them up automatically. Full `Target All` Debug build green (client + server); server boots clean (Trading Post store opens, no startup regression from the new handler).

### Versioning
- Bumped **compatibility** to 0.3.1 — one genuinely new wire message (`RequestTpMyBoard`), so client and server must match. Since the whole Trading Post protocol is still unreleased (phase 5 is the first client that speaks it), folding this message in now costs nothing later. No client/server **identity** bump yet — those land after the phase-6 multi-client test pass.

# Trading Post NPC — place Vince the Auctioneer (Phase 4)

### Trading Post (Content / NPC placement)
- Placed **Vince the Auctioneer** in all six warehouse maps — phase 4 of the build. Phase 3's handlers were complete but every `Tp*` request replied `NotNearAuctioneer` because no NPC of owner type `auctioneer` (111) existed for `trading_post_manager::is_near_auctioneer` to find; phase 4 makes Vince exist. (Clicking him still does nothing — the click-dispatch case that opens the dialog is phase 5.)
- **`npc_configs` row** (`gamedata.db`): `npc_id` 116, name `Vince`, `npc_type` 111, no drop table; all non-combat stats copied from the Warehouse Keeper (`npc_id` 58), including `action_limit` 2 (stationary) and `target_search_range` 0 (non-aggressive). Verified the chain end-to-end: `npc_configs.npc_type` → `LoadNpcConfigs` → `init_npc_attr` sets `CNpc::m_type` = 111, which is exactly what the proximity scan matches.
- **Six `map_npcs` + six `map_waypoints` rows** (`MapInfo.db`): one Vince per warehouse map (`arewrhus`, `elvwrhus`, `wrhus_1`, `wrhus_1f`, `wrhus_2`, `wrhus_2f`), `move_type` stop, spawned at a new waypoint index 11 = tile (67, 41) — open floor a few tiles from each map's Warehouse Keeper (66, 38) and near the player entry point (69, 43). Tile walkability was confirmed against each map's `.amd` collision data. `move_type` stop is safe despite having no precedent: movement is gated by `action_limit` (2 → forced `Behavior::stop`), not `move_type`.
- **Client rendering:** Vince has no sprite sheet of his own, so owner type 111 renders using the **CityHall Officer (William, type 25)** body — humanoid and visually distinct from the Warehouse Keeper beside him. Added `hb::shared::owner::sprite_render_type()` (auctioneer → William) applied at the three NPC sprite-index formula sites (`EquipmentIndices::CalcNpc`, `NpcRenderer`, `Game::CalcEquipmentIndices`); populated `m_stFrame[111]` as a copy of William's frames so the idle animation matches. His true type stays 111 for identity, proximity, and the phase-5 click dispatch. His name ("Vince") shows via the existing npc-config name sync.
- **Content delivery:** the runtime DBs are the source of truth (loaded directly at startup); the dated `Binaries/Server/gameconfigs/*.sql` files are Navicat export snapshots kept in sync, not auto-applied migrations — so the same `INSERT`s were added to both `gameconfigs-*.sql` and `mapinfo-*.sql`. (`Scripts/setup_gamedata.py` seeds formula tables / migrates settings and does not touch `npc_configs`; there is no automated `npc_configs → .sql` exporter.)
- Verified: full `Target All` Debug build green; server boots clean, NPC config count `116 → 117`, Npcs config hash changed, zero spawn-failure/invalid-config warnings. Client sprite + standing-in-place is an interactive eyeball check.

### Versioning
- No version change — no protocol messages or structs changed (phase 4 is data + client-render only), and the feature is still not player-reachable (the click-to-open-dialog case and the dialog itself are phase 5). Identity bumps wait until the feature ships.

# Trading Post server handlers + hooks (Phase 3)

### Trading Post (Server / request handlers)
- Wired the escrow core from phase 2 to the wire protocol — phase 3 of the build. Added `trading_post_manager` (`TradingPostManager.h/.cpp`, the ItemManager handler style), owned by `CGame` and dispatched from the client message switch, implementing the seven `Tp*` request handlers: board page, listing detail, create listing, place offer, rescind offer, finalize, delist. (No NPC content or client dialog yet — see below on why nothing is player-reachable until phase 4.)
- Every handler re-validates against live server state (never trusts the client): actor online, alive, not mid-Exchange, and standing near an Auctioneer, then the business rules — ≤5 active Listings per character, 1–4 distinct valid inventory slots with amounts ≤ held count, seeking-note length/charset, same-**account** Offer block, <10 Offers per Listing, one active Offer per character per Listing, and Seller-only checks on finalize/delist. Failures reply `PacketResponseTpActionResult` with the matching `TpResultCode`.
- **Finalize/delist/rescind** compose the phase-2 escrow primitives with the loss-never-dupe ordering intact (every escrow row is deleted + committed **before** its items are delivered): rescind = `refund_offer(TpRescind)`; delist refunds all Offers + returns the bundle to the Seller; finalize routes the winning Offer to the Seller's Warehouse, the Listing bundle to the winner's, auto-refunds the losers, and removes the Listing — one logical operation, logged `TpTradeOut`/`TpTradeIn`.
- **Notices:** finalize/refund/expiry notify each participant — an immediate system chat line if they are online, otherwise a persisted `notices` row delivered as chat on their next login (login hook flushes then deletes them).
- **Expiry sweep:** piggybacks the game tick at ~autosave cadence; Listings past `expires_at` run the delist routine (returns + refunds + notices). No-ops cheaply on an empty board.
- **Character-delete void hook** (`LoginServer::delete_character` → `trading_post_store::void_character`): the account DB's cascade can't reach `tradingpost.db`, so deletion explicitly refunds counterparties' Offers on the character's Listings, destroys the character's own escrowed items (logged, never delivered — same fate as its inventory), and deletes its Listings/Offers/notices.
- Added the `hb::shared::owner::auctioneer` constant (id 111, `OwnerType.h`) — pulled forward from phase 4 so handlers can check "actor is near an NPC of that owner type" via a small tile-box scan. Vince is not placed until phase 4, so `is_near_auctioneer` returns false and every handler currently replies `NotNearAuctioneer` — expected and correct for phase 3.
- Registered `TradingPostManager.cpp` in both `Server.vcxproj` (+ `.filters`) and the Linux `CMakeLists.txt`.

### Versioning
- No version change — the protocol is unchanged since phase 1 (no new messages or structs), and the feature is still not player-reachable (no Auctioneer NPC, no client dialog). Identity bumps wait until the feature is player-visible.

# Trading Post server escrow core (Phase 2)

### Trading Post (Server / escrow store)
- Built the server-side escrow store for the Trading Post — phase 2 of the build (no request handlers, hooks, NPC content, or client dialog yet; the core functions have no callers beyond startup wiring). The Trading Post is now the physical owner of record for listed/offered items (see `docs/adr/0001-trading-post-physical-escrow.md`): items leave the character's storage entirely and live in a server-owned `tradingpost.db` until a Trade completes or they are returned.
- Added `trading_post_store` (`TradingPostStore.h/.cpp`) — a single persistent SQLite connection owned by `CGame`, opened at startup (`busy_timeout`, `PRAGMA foreign_keys=ON`, idempotent `CREATE TABLE IF NOT EXISTS`, `meta` schema_version). Five tables: `listings`, `listing_items`, `offers`, `offer_items`, `notices`; item columns mirror `character_bank_items`. `ON DELETE CASCADE` ties items to their listing/offer so a single delete unwinds the whole bundle.
- **Escrow-in** (`create_listing`, `place_offer`): validate the requested inventory slots → remove them from the in-memory inventory (unequip via the release path, partial stacks decremented via `set_item_count`) → **forced character save** → insert the escrow rows in one transaction → log. The remove-and-save always precedes the escrow insert, so a crash in the window loses the items, never dupes them; the loss is logged for GM recovery.
- **Escrow-out** (`deliver_to_bank`): delivers items to a character's Warehouse — online recipients via an in-memory bank add + forced save, offline recipients via a direct insert into their account DB at `max(slot)+1` (bounded only by the hard 1000-item cap; the soft 200 cap is intentionally ignored for deliveries). Refund helpers (`refund_offer`, `refund_all_offers_on_listing`) delete the escrow rows and commit **before** delivering, preserving the loss-never-dupe ordering.
- Added `ItemLogAction` values `TpList`, `TpDelist`, `TpOffer`, `TpRescind`, `TpRefund`, `TpTradeOut`, `TpTradeIn`; every escrow transition (and every delivery failure) is logged to the `trade` channel.
- Registered `TradingPostStore.cpp` in both the MSBuild project (`Server.vcxproj` + `.filters`) and the Linux `CMakeLists.txt`. `tradingpost.db` is created with its schema on first server startup.

### Versioning
- No version change — the compatibility bump landed in phase 1, and identity bumps wait until the feature is player-visible. Phase 2 adds no runtime-visible behavior (the escrow core has no request handlers yet).

# Trading Post protocol layer (Phase 1)

### Trading Post (Shared / protocol)
- Laid down the shared protocol for the upcoming Trading Post — the escrow barter market opened through the Auctioneer NPC (Vince). This is phase 1 of the build (protocol only): no server handlers, store, client dialog, or NPC content yet.
- Added a `Tp*` message family: request IDs for browse-board-page, listing-detail, create-listing, place-offer, rescind-offer, finalize, and delist, plus response IDs for the board page, the listing detail, and a shared action-result. Requests carry inventory slot indices + amounts only — item data is never trusted from the client.
- Added the seven request structs, the board-page and listing-detail response structs (fixed-cap rows/offers + count, like the tester-search results), a compact browse-row item (id/count/attribute, names rendered client-side from the item config) and a full-column detail item (so the client can rebuild a complete tooltip), and the action-result packet with its `TpAction` and `TpResultCode` enums.
- Added the Trading Post limits as shared constants: 4 items per listing, 4 items per offer, 5 listings per character, 10 offers per listing, a 60-character seeking note, a 14-day expiry, and an 8-row board page.

### Versioning
- Bumped compatibility to 0.3.0 (new bidirectional protocol messages; client and server must match). No client or server identity bump — phase 1 adds only protocol definitions and changes no runtime behavior yet.

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

---

# Merged from upstream/Development (ShadowEvil, 2026-02-24 - 2026-03-04)

This merge adopts ShadowEvil's final 35 Development commits (item instance redesign,
item type system redesign, NPC damage/XP rework, config push to client, RegenManager
extraction, guild system full gut). His changelog file was cleared each posting cycle;
entries for the final commits live in the commit messages themselves (NPC damage rework
`7786315`, item type redesign `94e6151`, guild gut `506f7f1` - see `git log`). The last
three in-file cycles are preserved below, newest first.

# Changelog

## [Server 0.1.23 / Client 0.2.51] - 2026-03-03

### Ground Item Attribute Refactor

**Bug fixes:**
- Ground items now display proper names with prefixes and enchant levels (e.g., "Ancient Rapier+3" instead of "Rapier")
- Item obtained event log message now shows the full formatted name
- Ground item tooltip now shows only the item name — no longer displays stat breakdowns (e.g., "Hitting Probability +13")
- Ground item tooltip and sprite now use the correct dye color from the color palette (was showing green instead of the prefix color)
- Fixed `& 0x0F` mask on ground item color index that truncated palette indices above 15

**Refactoring:**
- Added shared `item_instance_data` struct — canonical type for all per-instance item data (17 fields)
- Added `PacketEventGroundItem` packet — carries full item instance data for ground item events, replacing packed `uint32_t` bitmask
- Added `CItem::to_instance_data()` conversion method
- Server: Added `send_ground_item_event()`, replacing 25 manual `send_event_to_near_client_type_b` calls across 9 files
- Server: Simplified `Map::get_item` from 6 decomposed output params to `CItem** remain`
- Server: Consolidated 5 manual field-by-field copy blocks with `copy_attributes_to()` template
- Client: Replaced CTile's 4 separate item fields with single `item_instance_data m_item`
- Client: `MapData::set_item` now accepts `item_instance_data` struct
- Client: Exchange dialog uses `item_instance_data` instead of packed `dw_v1`
- Client: `ItemNameFormatter` — new `format(item_instance_data)` overload; `format(CItem*)` delegates to it
- Removed all packed `uint32_t` attribute code (`pack_attributes_uint32`, `unpack_legacy_attribute`, `pack_exchange_attribute`)

**Files changed:** 28 (1 new, 27 modified)

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
