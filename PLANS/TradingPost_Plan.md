# Trading Post — v1 Implementation Plan

Designed 2026-07-21 in a grill session. Vocabulary: see `CONTEXT.md` (Trading Post section). Architecture decision: see `docs/adr/0001-trading-post-physical-escrow.md`. This is the build contract for v1; the feature may later grow into a full auction house (growth path preserved, see Deferred).

## Agreed model (the rules)

1. **One global market.** The Auctioneer NPC (**Vince**) stands in all six warehouse maps — `arewrhus`, `elvwrhus`, `wrhus_1`, `wrhus_1f`, `wrhus_2`, `wrhus_2f` — and every one shows the same board. Cross-nation barter is accepted; Listings record the Seller's nation so a filter is a cheap later add.
2. **Listing** = bundle of 1–4 escrowed items + optional display-only **seeking note** (≤60 chars) + Seller identity + 14-day expiry.
3. Items enter **from inventory only**, dragged into the dialog while standing at Vince. Equipped items are unequipped at escrow time (reuse the release/unequip path).
4. **Offers** = bundles of 1–4 escrowed items. Up to **10 concurrent Offers per Listing**, **one active Offer per character per Listing** (rescind to change). Offer contents are **public**.
5. **Finalize** (Seller only): winning Offer's items → Seller's Warehouse; Listing's items → winner's Warehouse; **all losing Offers auto-refunded** to their offerers' Warehouses; Listing removed. One logical operation.
6. **Rescind** (offerer) and **Delist** (Seller) allowed any time before finalize. Delist refunds all pending Offers.
7. **Every delivery goes to the Warehouse (bank)** — proceeds, refunds, rescinds, delists. Never to inventory, never to the ground.
8. **Participation:** gold participates freely (normal stackable item, `ItemId::Gold`); no item restrictions in v1; Offers from **any character on the Seller's account are rejected**; max **5 active Listings per character**.
9. **Lifecycle:** no fees or taxes. Listings **expire after 14 days** via periodic sweep (auto-delist + refunds + notices). **Character deletion voids** its entries: own escrowed items destroyed (same fate as inventory), counterparties' Offers refunded.
10. **Notifications:** participants online at the moment of an event get a system chat line; offline participants get a **persisted notice delivered as chat on next login**.

Limits summary: **4** items/Listing, **4** items/Offer, **5** Listings/character, **10** Offers/Listing, note ≤ **60** chars, expiry **14 days**. Define in `hb::shared::limits` (constexpr) for v1; promote to gameconfigs if tuning becomes frequent.

## Data: `Binaries/Server/tradingpost.db`

New server-owned store class `trading_post_store`, copying the `GameConfigSqliteStore.cpp` pattern (`EnsureGameConfigDatabase`, `GameConfigSqliteStore.cpp:142`): open at startup, `busy_timeout`, `PRAGMA foreign_keys=ON`, idempotent `CREATE TABLE IF NOT EXISTS` block, `meta(key,value)` schema_version, additive `HasColumn`/`ALTER` migrations. Single connection owned by `CGame`.

Item columns mirror `character_bank_items` (see `AccountSqliteStore.cpp`): `item_id, count, touch_effect_type, touch_effect_value1..3, item_color, spec_effect_value1..3, cur_durability, custom_made, prefix_type, prefix_value, secondary_type, secondary_value, enchant_bonus`. (Originally `cur_lifespan` + packed `attribute`; re-mirrored to the item-instance model when the Development merge landed it — see the merge changelog.)

```sql
CREATE TABLE listings (
	listing_id      INTEGER PRIMARY KEY AUTOINCREMENT,
	seller_name     TEXT NOT NULL,
	seller_account  TEXT NOT NULL,          -- for the same-account offer block
	seller_nation   INTEGER NOT NULL,       -- future filter; recorded from day one
	seeking_note    TEXT NOT NULL DEFAULT '',
	created_at      INTEGER NOT NULL,       -- unix seconds
	expires_at      INTEGER NOT NULL
);
CREATE TABLE listing_items (
	listing_id  INTEGER NOT NULL REFERENCES listings(listing_id) ON DELETE CASCADE,
	slot        INTEGER NOT NULL,           -- 0..3
	-- item columns as above --
	PRIMARY KEY (listing_id, slot)
);
CREATE TABLE offers (
	offer_id        INTEGER PRIMARY KEY AUTOINCREMENT,
	listing_id      INTEGER NOT NULL REFERENCES listings(listing_id) ON DELETE CASCADE,
	offerer_name    TEXT NOT NULL,
	offerer_account TEXT NOT NULL,
	created_at      INTEGER NOT NULL,
	UNIQUE (listing_id, offerer_name)       -- one active Offer per character per Listing
);
CREATE TABLE offer_items (
	offer_id  INTEGER NOT NULL REFERENCES offers(offer_id) ON DELETE CASCADE,
	slot      INTEGER NOT NULL,
	-- item columns as above --
	PRIMARY KEY (offer_id, slot)
);
CREATE TABLE notices (
	notice_id       INTEGER PRIMARY KEY AUTOINCREMENT,
	character_name  TEXT NOT NULL,
	message         TEXT NOT NULL,
	created_at      INTEGER NOT NULL
);
```

## Server work

### Escrow core (dupe safety — the load-bearing part)

- **Escrow-in** (create Listing / place Offer): validate → remove items from in-memory inventory (`release_item_handler` / `set_item_count`, unequip included) → **forced character save** (`local_save_player_data`, `LoginServer.cpp:874`) → insert rows into tradingpost.db in one transaction → `item_log`. Crash between save and insert = loss, never dupe; recoverable from logs.
- **Escrow-out** (`deliver_to_bank(character_name, items)` — the single delivery function): delete escrow rows (commit) → deliver. Online recipient (lookup by name): `set_item_to_bank_item(client_h, CItem*)` (`ItemManager.cpp:2503`) + forced save. Offline: `ResolveCharacterToAccount` (`AccountSqliteStore.h:210`) → `EnsureAccountDatabase` → insert into `character_bank_items` at `max(slot)+1`, respecting the hard `MaxBankItems=1000` bound only (soft cap 200 is client-side; deliveries may exceed it by design). Deserialization template: `Game.cpp:4283` (row → `new CItem` + `init_item_attr` + overlay instance columns).
- Server is single-threaded: races (finalize vs rescind, double finalize) are serialized; the loser of the race gets a "no longer exists" result code. No locks needed — state re-validation at request time, like the Exchange's re-check (`ItemManager.cpp:4240`).
- Client is never trusted with item data: requests carry inventory slot indices + amounts only; the server re-reads `m_item_list` at request time.
- New `ItemLogAction` values: `TpList, TpDelist, TpOffer, TpRescind, TpRefund, TpTradeOut, TpTradeIn`. Log to the existing `trade` channel (`hb::logger::log<log_channel::trade>`).

### Request handlers (all require: online, alive, within range of an Auctioneer NPC — same proximity rule as other NPC interactions; not in exchange mode)

| Handler | Validation highlights |
|---|---|
| board page | page bounds; newest-first |
| listing detail | listing exists |
| create listing | ≤5 active listings for character; 1–4 distinct valid inventory slots; stack amounts ≤ held count; note length/charset |
| place offer | listing exists; offerer account ≠ seller account; <10 offers; no existing offer by character; slots valid |
| rescind offer | offer exists and is the actor's |
| finalize | listing is the actor's; offer exists on it; then the one-logical-operation swap + refunds + notices |
| delist | listing is the actor's; refund all offers; return items |

### Hooks

- **Expiry sweep**: piggyback the existing periodic timer work (autosave cadence is fine); `expires_at < now` → run the delist routine per listing + notices.
- **Login**: flush `notices` rows for the character as system chat lines, then delete them.
- **Character delete**: void — refund all Offers on the character's Listings to their offerers, delete the character's own Listings/Offers/notices, destroy its escrowed items (logged).
- **Finalize/refund notices**: online participants also get the immediate chat line.

## Shared / protocol

New `MsgId` request/response families (dedicated packet structs like the Exchange's, not bare common-commands, since payloads carry slot arrays and the note). Response lists use the fixed-cap + count pattern (`PacketNotifyTesterNpcSearchResult`, `PacketNotify.h:960`).

- Requests: `TpBoardPage{page}`, `TpListingDetail{listing_id}`, `TpCreateListing{note[64]; count; {inv_slot, amount}[4]}`, `TpPlaceOffer{listing_id; count; {inv_slot, amount}[4]}`, `TpRescindOffer{offer_id}`, `TpFinalize{listing_id, offer_id}`, `TpDelist{listing_id}`.
- Responses: `TpBoardPage{page, page_count; rows[PAGE]{listing_id, seller, nation, offer_count, expires_hours, note, count, TpItemBrief[4]}}` (browse rows carry id+count plus the name-affecting instance fields — `item_color, custom_made, prefix_type/value, secondary_type/value, enchant_bonus` — rendered via `item_name_formatter::format(item_id, item_instance_data)`), `TpListingDetail{...full item-instance columns for bundle + offers[10]{offer_id, offerer, count, items[4]}}`, `TpActionResult{action, result_code, value}` with a result-code enum (`TooManyListings, ListingGone, OfferGone, AccountSelfTrade, TooManyOffers, InventoryChanged, NotNearAuctioneer, ...`).
- Board is **pull-only** in v1: refreshed on open, page turn, and after own actions. No push invalidation.
- Notices reuse the existing system-chat message — no new packet.
- **Compatibility version bump** in `Sources/version.cfg` (new messages both directions), plus client and server identity bumps.

## NPC: Vince the Auctioneer

- New `hb::shared::owner` constant `auctioneer` (next free id after 110, `OwnerType.h`); **not** added to `can_receive_items()` — the dialog, not item-drop-on-NPC, is the interaction.
- `npc_configs` row (next free npc_id; name `Vince`), mirrored in `Scripts/setup_gamedata.py`; reuse an existing humanoid model via `resolve_npc_type` (pick one visually distinct from Howard at implementation).
- Six `map_npcs` rows (move_type stop, waypoint near each map's Warehouse Keeper), following the current mapinfo/gameconfigs SQL update workflow.
- Client dispatch: new case in the `switch(object_type)` at `Client/Game.cpp:5033` → open `DialogBox_TradingPost` + request board page 0.

## Client: `DialogBox_TradingPost`

Legacy `IDialogBox` pattern (exemplar: `DialogBox_NpcSpawner.cpp`), new `DialogBoxId`, registered in `DialogBoxManager.cpp`. One dialog, view-switched:

- **Browse** — paged rows (item names via formatter, seller, offer count, note preview), tabs, page arrows.
- **Detail** — bundle grid (tooltips = full stats via `item_name_formatter`), note, seller/nation, expiry; offers list with contextual buttons: `[Finalize]` per offer + `[Delist]` when viewer is the Seller, `[Rescind]` on own offer, `[Place Offer]` otherwise.
- **Create Listing / Create Offer** — shared 4-slot drop grid (`on_item_drop`, drag source = inventory slot via `CursorTarget`, stackables route through `DialogBox_ItemDropAmount`), note input on the listing variant, `[Post]` / `[Offer]` confirm.
- **My Listings / My Offers** — filtered lists jumping to Detail.

Escrowed-item UX: items vanish from inventory on server confirm (server sends the standard erase/update notifications already used by Exchange/Bank flows).

## Build order

1. **Shared**: message IDs, packet structs, limits constants, version bump.
2. **Server**: `trading_post_store` + schema; escrow-in/escrow-out/refund core + item logging.
3. **Server**: the seven handlers + proximity checks; notices + login flush; expiry sweep; character-delete void.
4. **Content**: OwnerType, Vince's `npc_configs` row, six `map_npcs` placements, sprite mapping, client dispatch case.
5. **Client**: `DialogBox_TradingPost` views, drag-drop, network handlers, result-code toasts.
6. **Manual multi-client test pass**: list → offer → outbid → rescind → finalize with winner offline → losing refund → delist with pending offers → expiry (shortened interval) → same-account block → all limits → warehouse near hard cap.

Both targets build on Windows and Linux — keep all new code cross-platform per CLAUDE.md; `std::chrono` for timestamps, no platform APIs.

## Deferred (auction-house growth path — deliberately unblocked by v1 schema)

Nation filter (column exists), search/sort, buyout/asking price columns (additive migration), currency bids, listing fees / trade tax as gold sinks, bundles > 4, notices → full mail, watch lists, board push-invalidation.

## Implementation-time picks (deliberately not decided in the design session)

Vince's exact sprite/model and coordinates per map; board page size (8–10 rows); seeking-note sanitization rule (reuse chat filtering if present, else length + printable charset); whether limits move into gameconfigs.

### Decided during implementation

- **Board page size = 8** (`hb::shared::limits::TpBoardPageRows`, fixed in Phase 1). Board is newest-first (`ORDER BY listing_id DESC`), paged via `LIMIT/OFFSET`.
- **Seeking-note sanitization (Phase 3): bounded length + printable ASCII.** No chat/profanity filter exists in this codebase, so the rule is: reject unless the note is null-terminated within its 64-byte wire buffer, ≤ `TpSeekingNoteMaxChars` (60) characters, and every character is printable ASCII `0x20`–`0x7E` (control chars and high bytes rejected). Empty notes are allowed (the note is optional). Enforced in `trading_post_manager::note_is_valid`; failures reply `InvalidNote`. Growth path: swap this for a shared chat filter if one is later added.
- **Auctioneer proximity (Phase 3): 5-tile box scan.** Handlers require the actor to be within a ±5-tile box of an NPC whose `m_type == hb::shared::owner::auctioneer`, scanned via `CMap::get_owner` around the actor (`trading_post_manager::is_near_auctioneer`). Precise to position, cheap (≤121 tile probes), and blind to client-supplied coordinates. Returns false until Phase 4 places Vince, so every handler replies `NotNearAuctioneer` until then — expected and correct.
- **Limits stay in `hb::shared::limits`** (constexpr) for v1; not promoted to gameconfigs.
- **Vince's identity (Phase 4): `npc_id` 116, `npc_type` 111 (auctioneer), no drop table.** Non-combat stats copied verbatim from the Warehouse Keeper (`npc_id` 58): `action_limit` 2 (stationary — see below), `target_search_range` 0 (non-aggressive), zeroed combat/exp/gold. The runtime `m_type` is 111, which is what `trading_post_manager::is_near_auctioneer` scans for.
- **Vince's placement (Phase 4): tile (67, 41) on all six warehouse maps** (`arewrhus`, `elvwrhus`, `wrhus_1`, `wrhus_1f`, `wrhus_2`, `wrhus_2f` — verified identical collision layouts). This is open floor ~3 tiles SE of the Warehouse Keeper's alcove tile (66, 38) and ~2 tiles NW of the player entry point (69, 43); walkability of every candidate tile was confirmed by parsing each map's `.amd` collision data. A new `map_waypoints` index 11 = (67, 41) was added per map; Vince's `map_npcs.waypoint_list` points at it.
- **`move_type` = stop (0) (Phase 4).** No existing NPC uses stop, but it is safe here: NPC movement is gated by `action_limit`, not `move_type` — `EntityManager` forces `Behavior::stop` and returns early for `action_limit` ∈ {2,3,5} (the keeper's mechanism). With `action_limit` 2, Vince never moves regardless; stop(0) just makes the "stationary" intent explicit and spawns him at `waypoint_list[0]` (index 11).
- **Vince's client model = William, the CityHall Officer (owner type 25) (Phase 4).** Auctioneer (111) ships with no sprite sheet of its own, so it borrows William's humanoid body — visually distinct from Howard (the Warehouse Keeper it stands beside) and a fitting civic-official look. Implemented as a client-only sprite proxy: `hb::shared::owner::sprite_render_type(111) → 25` is applied at the three NPC sprite-index formula sites (`EquipmentIndices::CalcNpc`, `NpcRenderer` special-frame, `Game::CalcEquipmentIndices`), and `m_stFrame[111]` is populated as a copy of `m_stFrame[25]` so the idle/move animation matches. The entity's true type stays 111 everywhere else (identity, proximity, and the Phase-5 click dispatch).
- **Content delivery (Phase 4): the runtime DBs are the source of truth.** `gamedata.db` / `MapInfo.db` are loaded directly at startup; the dated `Binaries/Server/gameconfigs/*.sql` files are Navicat *exports* (snapshots), kept in sync and committed alongside the DBs — they are **not** auto-applied migrations. Vince shipped by editing both DBs and adding the matching `INSERT`s to both `.sql` snapshots. NOTE: `Scripts/setup_gamedata.py` does **not** mirror `npc_configs` (it seeds formula tables and migrates settings → `server_config.json`); there is no automated `npc_configs → .sql` exporter (`SaveNpcConfigs` is defined but unused), so the `.sql` snapshots are maintained by hand / re-export.
- **Phase 4 scope shift: the client click-dispatch case is deferred to Phase 5.** The plan's "Build order" step 4 listed the `switch(object_type)` case at `Client/Game.cpp` among the NPC content, but its body must open `DialogBox_TradingPost` (a Phase-5 artifact needing `DialogBoxId::TradingPost`). Phase 4 therefore places and renders Vince only; **clicking Vince does nothing until Phase 5**, which is expected. Vince's true owner type remains 111 precisely so that Phase-5 case can key on `switch(object_type)` as designed.
