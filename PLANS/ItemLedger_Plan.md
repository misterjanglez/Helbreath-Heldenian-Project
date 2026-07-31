# Item Provenance Ledger — design contract

**Status: LOCKED — ratified by owner 2026-07-29.** Decision records:
`docs/adr/0003-item-provenance-ledger.md` (Serials + ledger) and
`docs/adr/0004-single-game-db.md` (account-store consolidation). Vocabulary merged into
`CONTEXT.md` (Provenance Ledger section). Implementation epic: **#74** (tickets #75–#86); tickets
sequence the build — they do not reopen decisions.

Origin: storage-architecture discussion, 2026-07-29 (SQLite assessment → Loot 2.0 lifecycle-tracking
requirement). Companion efforts: Item Tiers v1 (`PLANS/ItemTiers_Plan.md`, epic #36, ADR 0002) and
the Trading Post (`PLANS/TradingPost_Plan.md`, ADR 0001).

## What this is

Every identity-bearing item on the server gets a permanent **Serial** at creation, and every custody
or state transition it ever undergoes — NPC drop, pickup, Exchange, Warehouse deposit/retrieve, shop
buy/sell, upgrade, Trading Post escrow, ground despawn, destruction — is appended to a **Provenance
Ledger**: a dedicated SQLite database recording the item's full biography.

What it buys:

- **Disputes** — "I lent it to him" is settled by the custody chain, timestamped.
- **Dupe forensics** — without Serials, dupe detection is statistical inference; with Serials it is
  a JOIN. Illegal state transitions (picked up twice with no intervening drop, one Serial in two
  inventories) are mechanical anomalies, not judgment calls.
- **Loot 2.0 tuning telemetry** — actual (not configured) drop rates by type/source, entering-
  population rate (first pickup), and despawn-without-pickup attrition per item type: a direct
  signal for which drops players don't value.

## Decisions to ratify

- **D1 — Serial identity.** Every Instanced item gets a server-global monotonic `int64` Serial,
  minted once at creation, never reused, **never sent to the client**. Serials are server + DB
  facts only, so a tampered client cannot even observe the tracking, and the Item Tiers one-bump
  law is untouched — this effort makes **zero wire changes**.
- **D2 — Two tracking tiers.** An item type is **Instanced** iff it is non-stackable (gear, named
  uniques, angels, Dark Items, wands/shields — everything disputes and dupes are about). Stackables
  (gold, potions, ammo, ores) are **Counted**: aggregate daily flow counters by type and
  origin/sink, no per-instance rows. Stack merge/split dissolves identity, so this boundary is
  principled, not just a volume optimization.
- **D3 — Passive audit trail, not system of record.** The in-RAM world stays authoritative and
  snapshot saves stay how state persists. The ledger is derived truth for forensics. The
  redundancy is the feature: **reconciliation between snapshots and ledger is the dupe detector.**
  (Explicitly rejected: retrofitting event sourcing — a rewrite for negative value.)
- **D4 — Storage shape.** One global `itemledger.db` (deliberately NOT per-account — biographies
  span accounts by definition), WAL + `synchronous=NORMAL` from birth, persistent connection,
  in-memory event buffer flushed in one transaction on the existing save cadence. WAL also lets
  external tools query the live file while the server writes.
- **D5 — `item_log()` grows a second sink.** The existing text channels stay (ops tailing); the
  same call sites additionally append structured ledger events. The `ItemLogAction` taxonomy is
  already placed at most of the right transitions — this effort completes coverage and upgrades
  the sink, it does not re-instrument from scratch.
- **D6 — Sequencing: the gate is go-live, not a ticket.** The project is in Alpha; test worlds
  wipe freely. Identity (Phase 1) must land **before real players exist** — after that, item
  history becomes permanent world fact and this change is never again this cheap. Consequence of
  wipe-freedom: **no migration or backfill code ever ships.** Pre-ledger DBs are refused by the
  schema gate (same posture as the 1-E DDL swap) and the world is wiped.
- **D7 — One Game DB.** The per-account `accounts/*.db` layout is replaced by a single `game.db`
  (`account_name` columns, `UNIQUE` character names). Decisive reason: SQLite transactions across
  ATTACHed files are **not atomic in WAL mode**, so per-account files make multi-account
  operations — an Exchange between two players, Trading Post custody moves — permanently
  non-atomic: a crash between the two commits is an item dupe or loss, the exact exploit class
  this effort exists to close. One DB makes atomicity possible (the trade-commit path then wraps
  both parties' saves in one transaction — Phase 3). Also gained: structural character-name
  uniqueness (today only per-file), single-JOIN Reconciliation, one persistent connection,
  point-in-time whole-world backups. Per-account restore/delete becomes a small admin script.
  Scope: account files only — `gamedata.db`, `MapInfo.db`, and `itemledger.db` stay separate.

## Schema (itemledger.db)

```sql
CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
-- keys: schema_version, serial_high_water

CREATE TABLE item_instances (
	serial        INTEGER PRIMARY KEY,          -- minted int64, monotonic
	item_id       INTEGER NOT NULL,             -- gamedata item type
	created_at    INTEGER NOT NULL,             -- unix seconds
	origin_type   INTEGER NOT NULL,             -- npc_drop, craft, shop_buy, gm_mint, quest, fishing, mining, dark_claim...
	origin_detail TEXT,                         -- npc name, crafter, GM name...
	map TEXT, x INTEGER, y INTEGER,
	tier INTEGER NOT NULL DEFAULT 0             -- Item Tiers byte at creation (0 = Untiered)
);

CREATE TABLE item_events (
	event_id     INTEGER PRIMARY KEY,
	serial       INTEGER NOT NULL,
	at           INTEGER NOT NULL,
	event_type   INTEGER NOT NULL,              -- superset of ItemLogAction + despawn/destroy/escrow events
	actor_account TEXT, actor_char TEXT,        -- nullable (despawn has no actor)
	counterparty_char TEXT,                     -- Exchange/Give recipient, Trading Post buyer
	map TEXT, x INTEGER, y INTEGER,
	detail TEXT                                 -- event-specific JSON (upgrade level, sale price...)
);
CREATE INDEX idx_events_serial ON item_events(serial, at);
CREATE INDEX idx_events_actor  ON item_events(actor_char, at);
CREATE INDEX idx_events_type   ON item_events(event_type, at);

CREATE TABLE item_flows (                      -- Counted tier: stackable aggregates
	day INTEGER NOT NULL,                       -- yyyymmdd
	item_id INTEGER NOT NULL,
	flow_type INTEGER NOT NULL,                 -- dropped, picked_up, despawned, consumed, shop_bought, shop_sold...
	qty INTEGER NOT NULL,
	PRIMARY KEY (day, item_id, flow_type)
);

-- v2 (P4.3): the denominator. Drop rarity is authored per KILL, so no observed
-- rate exists until kills are counted, and nothing else in the server counts
-- them. Not about an item at all, which is why it sits apart from the tables
-- above.
CREATE TABLE npc_kills (
	day INTEGER NOT NULL,                       -- yyyymmdd, local, as item_flows
	npc_id INTEGER NOT NULL,                    -- npc_config id (CNpc::m_npc_config_id)
	npc_name TEXT NOT NULL,                     -- the join key: a birth row names its monster
	kills INTEGER NOT NULL,
	rep_factor_sum REAL NOT NULL,               -- summed killer reputation; prices gear and unique rows
	PRIMARY KEY (day, npc_id)
);
CREATE INDEX idx_kills_name ON npc_kills(npc_name);
```

Serial allocator: high-water mark in `meta`, loaded at boot, minted in RAM, persisted with each
flush. Crash recovery resumes at `max(meta high-water, MAX(item_instances.serial)) + 1`. Batched
flush means a crash can lose the final seconds of events — accepted (character snapshots share the
same window); boot writes a gap marker event so forensic queries can see crash boundaries.

## Coverage law

**An audit log with gaps is worse than none — every item-mutation path must emit.** Two mechanisms
enforce this:

1. **Creation only via factory.** All 45 `new CItem` sites (16 files; 20 already inside
   `ItemManager`) funnel through an `item_manager` factory that mints the Serial and emits the
   creation event with origin. After the refactor, a raw `new CItem` in game code is a review
   defect.
2. **Coverage audit gate** (P3 exit): enumerate every transition and verify each emits.

### Transition inventory (P3.3 audit result, 2026-07-31 — #81)

The enumeration the coverage audit produced, and the evidence for each row. This
is the trust gate's output: Phase 4's tooling is only as good as this table.

**Two tiers, one numbering.** `item_flows.flow_type` stores the *same* number
`item_events.event_type` does — no second taxonomy was minted. An Instanced arrow
sold writes `item_events(event_type=8)`; five Counted arrows sold write
`item_flows(flow_type=8, qty=5)`. Ratified by the owner 2026-07-31: a second
numbering would have been a second set of permanent world-fact values to keep
from drifting against the first, and every flow this plan asked for (dropped,
picked_up, despawned, consumed, shop_bought, shop_sold) already had a number.

**The one chokepoint.** `ItemManager::begin_ledger_event` is the single door every
emitter passes through — both `item_log()` overloads, `destroy_item`,
`despawn_item`. It answers false for an item with no Serial, and since #81 it
books an aggregate flow before it does. That is why the Counted tier cost zero new
call sites.

| Transition | Number | Instanced | Counted | Emitted at |
|---|---|---|---|---|
| Creation | `created` 100 | `item_instances` row + event | see note ¹ | `stamp_provenance` |
| Give | 1 | ✓ | ✓ | `give_item_handler` — **both** branches since #81 |
| Drop | 2 | ✓ | ✓ | `drop_item_handler`, give-fallbacks, `MagicManager` |
| Pick up | 3 | ✓ | ✓ | `client_motion_get_item_handler`, `MagicManager` |
| Deplete | 4 | ✓ | ✓ | `item_deplete_handler` (21 sites funnel here, #79) |
| NPC drop | 5 | ✓ | ✓ | `EntityManager::npc_dead_item_generator` |
| Shop buy | 7 | ✓ | ✓ | `request_purchase_item_handler` |
| Shop sell | 8 | ✓ | ✓ ² | `req_sell_item_confirm_handler` |
| Warehouse retrieve | 9 | ✓ | ✓ | `request_retrieve_item_handler` — **new in #81**, both branches |
| Warehouse deposit | 10 | ✓ | ✓ | `set_item_to_bank_item` — **new in #81** |
| Exchange | 11 | ✓ | ✓ | `confirm_exchange_item` (snapshot carries the traded count) |
| Upgrade fail/success | 29/30 | ✓ | n/a | the upgrade handlers |
| Use | 32 | ✓ | ✓ | `use_item_handler` |
| Trading Post ×7 | 33–39 | ✓ | ✓ ³ | `deliver_to_bank` (out), per-call-site (in) — #80 |
| GM mint | 40 | ✓ | ✓ | `mint_gm_items`, `GameCmdCreateItem`, `GameCmdGiveItem` |
| Ground despawn | `despawned` 101 | ✓ | ✓ | `despawn_item` (expired / tile_overflow / world_shutdown) |
| Destruction | `destroyed` 102 | ✓ | ✓ ⁴ | `destroy_item` |
| Run boundary | `boundary` 103 | n/a | n/a | `item_ledger_store::open` |

¹ **Counted creation is recorded per-venue, not at the mint funnel.** `stamp_provenance`
cannot book it: every creation venue sets `m_instance.count` *after* `create_item`
returns, so a flow booked inside the funnel would always be a stack of zero. Shop
sale proceeds — real money-supply inflow — book `created` at the venue
(`req_sell_item_confirm_handler`). Other Counted mints are recorded by whichever
transition delivers them (an NPC gold drop by `NewGenDrop`, a reward by its own
action), which is the honest shape: a Counted item has no identity, so it has no
birth — only flows.

² **The quantity is the amount that moved, not the stack's count.** A shop sale
hands `item_log` the whole inventory slot and takes `num` out of it, so
`item_log`'s trailing `qty` parameter overrides the item's own count. Selling 5
arrows from a stack of 100 books 5. Everywhere else the item handed to the emitter
*is* the portion that moved, which is why the default reads its count.

³ A Trading Post bundle's Counted members book flows through the same escrow
recorders; the Instanced members carry Serials and book events.

⁴ **Except `destroy_reason::merged`, which books nothing** — a merge frees a husk
whose contents live on in the stack it joined, so nothing left the world. Every
gold pickup takes that path, so counting it would double-count the currency supply.

**Actions with no caller** (a text-sink switch arm and nothing reaching it):
`SkillLearn` 12, `Make` 13, `SummonMonster` 14, `Poisoned` 15, `MagicLearn` 16,
`Repair` 17. None is a custody move: the first five consume an item, and that exit
is already recorded by `destroy_item` through `item_deplete_handler`; `Repair` is a
durability change on an item that never changes hands. Left unwired deliberately —
adding an event that duplicates an exit already recorded would break "exactly one"
as surely as recording none.

**Known gap, deliberate:** gold *spent* is not booked. Purchases, repairs and
magic-shop payments reduce the stack through `set_item_count_by_id` directly, which
carries no transition, so the currency loop is closed on the inflow side only. Not
closed here because it wants the same deliberate pass this ticket gave `flow_type`
— the sinks are a handful of sites but each needs the right number, and `Repair`
being one of them means picking a meaning for a value that has never been stored.

**Mechanical enforcement.** Three scripts, all green as of this audit:
`Scripts/check_item_factory.py` (nothing is born outside the factory — 7 sanctioned),
`Scripts/check_item_destroy.py` (nothing dies outside the funnel — 29 classified),
and `Scripts/check_item_merge.py` (**new in #81** — no husk is abandoned by a stack
merge; 22 sites). The prover is `coveragecheck` (24/24).

Known gaps in today's `item_log()` (verified in code, 2026-07-29 — **all closed by
#79/#80/#81; kept as the record of what the audit was aimed at**):

- `NewGenDrop` logs no location (`ItemManager.cpp:5193` passes `"", 0, 0`) and is filtered by
  `check_good_item()` — the ledger records **all** Instanced creations, unfiltered.
- Ground despawn emits nothing. Ground items live in map tiles (`Map::set_item`/`get_item`,
  `Map.h:89`); the timed cleanup path must be located and instrumented (P3 ticket).
- Destruction paths need a sweep: Deplete exists, but upgrade-destruction, sell-to-shop (exit from
  population), quest turn-ins, and any silent deletes need explicit events.
- Trading Post escrow (custody in/out, sale to buyer) — coordinate with that workstream; escrow
  should emit from day one rather than be retrofitted (ADR 0001's physical escrow is a custody
  transfer like any other).
- GM minting (`GmMint`) already logs; the factory gives those items Serials + `gm_mint` origin,
  which also serves epic #36's #56 (GM creator legality logging) for free.

## Persistence changes (Game DB — one schema epoch)

Persistence v8 is a single combined break (D6 + D7): the consolidated layout and the Serial
columns land together, so the persistence layer churns once.

- `accounts/*.db` → one `game.db`: all tables gain/keep `account_name` scoping; `character_name`
  becomes globally `UNIQUE` (replacing scan-based name checks); directory-walk helpers
  (`CountAccountStats`, name-availability scans) become single queries.
- The new store is born with the hygiene posture: WAL + `synchronous=NORMAL`, one persistent
  connection with cached prepared statements, and mass-save sweeps (`Game.cpp:4165`, shutdown
  paths) batched into one transaction.
- `character_items` / bank tables gain a `serial` column; save/load carries it. Ground-item state
  and Trading Post listings carry the Serial through their persistence.
- `ACCOUNT_DB_SCHEMA_VERSION` bumps 7 → 8 behind the existing verify gate; the gate also refuses
  the old per-account layout. **No migration or backfill code ships — old worlds are wiped (D6).**

## Constraints

- **No Compatibility bump** — Serials never touch the wire. Server identity bumps only, per
  `VERSION_STANDARDS.md`.
- Cross-platform per `CLAUDE.md` (Linux CMake target gains the new ledger store files;
  `itemledger.db` filename lowercase-safe).
- New code follows `CODING_STANDARDS.md` (`hb::server` snake_case; the ledger store mirrors the
  existing `*SqliteStore` pattern).
- Every knob (flush cadence, Counted/Instanced overrides) lands in data, not code, matching the
  tiers ground rule.

## Phases → implementation epic

Blocking edges are genuine gates only; work the frontier (house rule from epic #36).

**P1 — Identity + Game DB foundation** (gate: before go-live, per D6)
- P1.1 Serial allocator + `m_serial` on `CItem` + creation factory replacing all 45 `new CItem`
  sites (prefactor in the spirit of #38). Durable high-water home arrives with P2.1; until then
  dev worlds tolerate allocator reset via wipe-freedom.
- P1.2 Persistence v8: one `game.db` (consolidation per D7, born with WAL / persistent connection /
  batched sweeps) + Serial columns; schema gate → 8; ground items and Trading Post listings carry
  Serials. *Blocked by P1.1.*

**P2 — Ledger infrastructure**
- P2.1 `ItemLedgerStore` (WAL from birth): schema above, buffered writer, batched flush on the save
  cadence, boot recovery + gap markers, durable Serial high-water in `meta`.
- P2.2 `item_log()` dual sink: existing call sites additionally emit ledger events; creation events
  wired from the factory. *Blocked by P1.1, P2.1.*

**P3 — Coverage + atomicity**
- P3.1 Lifecycle gaps: NPC-drop location, ground drop/pickup, ground despawn (locate timed cleanup),
  destruction sweep (upgrade-break, sell, quest turn-in), shop buy = mint. *Blocked by P2.2.*
- P3.2 Trading Post escrow events (coordinate with that workstream's phase status). *Blocked by
  P2.2.*
- P3.3 Coverage audit — enumerate every mutation path, verify each emits. **Trust gate for P4.**
  *Blocked by P3.1, P3.2.* **DONE 2026-07-31 (#81)** — inventory recorded above; the
  Counted tier wired (`flow_type` = the event number, no second taxonomy); Warehouse
  deposit/retrieve and the split-stack Give emitted for the first time; a 16-site husk
  leak closed and made mechanical by `check_item_merge.py`; `coveragecheck` 24/24.
- P3.4 Atomic multi-account commits: Exchange saves both parties in one `game.db` transaction;
  Trading Post custody moves made atomic (implementation-time pick: listings table moves into
  `game.db`, coordinated with that workstream). *Blocked by P1.2.* **DONE 2026-07-31 (#82)** —
  schema epoch **v9**; the pick was taken, all five escrow tables moved. `save_players_atomic`
  is the primitive; Exchange and **Give** both use it (Give was the same shape and the same
  window, and the plan's "no multi-account operation spans two commits" covers it). Custody
  moves run inside a `custody_scope` — one transaction plus an in-memory undo stack plus
  deferred ledger emission — so an escrow-in that cannot commit now returns the items instead
  of taking ADR 0001's designed loss, and a finalized Trade is ONE commit instead of five.
  `atomiccheck` 36/36, five negative controls.

**P4 — Payoff tooling** *(all blocked by P3.3)*
- P4.1 Reconciliation job: scan all inventories/Warehouse vs ledger current-holder; anomaly report
  (offline tool or admin command). **DONE 2026-07-31 (#83)** — `reconcile` (the tool) and
  `reconcilecheck` (31/31, hash `20661646` byte-identical on both platforms, five negative
  controls). The comparison lives in `ItemReconciliation.{h,cpp}` on two raw `sqlite3*` handles
  so the prover can drive it against planted anomalies; the tool opens both files **read-only**,
  which is what lets it be pointed at a live server. The derivation is the **last locating
  event** per Serial — see the table below; events that say nothing about custody (`Use`, both
  upgrade outcomes, the birth row, the run boundary, the six caller-less actions) are excluded in
  SQL rather than allowed to mask the last real move.

**Holder derivation (P4.1)** — what each event says about where the item ended up:

| Resulting place | Events |
|---|---|
| the actor's inventory | `get` 3, `Buy` 7, `Retrieve` 9, `GmMint` 40 |
| the counterparty's inventory | `Give` 1, `Exchange` 11 |
| the actor's Warehouse | `Deposit` 10, and every escrow-out: `TpDelist` 34, `TpRescind` 36, `TpRefund` 37, `TpTradeOut` 38, `TpTradeIn` 39 |
| the board's custody | `TpList` 33, `TpOffer` 35 |
| the ground | `Drop` 2, `NewGenDrop` 5 |
| out of the world | `Deplete` 4, `Sell` 8, `despawned` 101, `destroyed` 102 |

**Nine anomaly classes**, four of them critical (a healthy world cannot produce them): `held_twice`
(one Serial, two holders — the dupe), `held_but_gone`, `held_but_ground`, `unknown_serial`. The
other five are states a *live* run makes honestly, because a logged-in character's inventory is in
memory until it saves: `holder_mismatch`, `missing_holder`, `held_unrecorded`, `serial_missing`
(an Instanced row carrying no Serial), `serial_on_counted`. Three no-holder states are counted
rather than accused — on the ground, out of the world, never placed — plus `indeterminate` for a
locating event that named nobody.

**What the first run found (2026-07-31), and what it means:** `mint_gm_items` writes N birth rows
and **one** `GmMint` event for a batch, so N−1 GM-minted copies have no event that says who holds
them (`held_unrecorded`). That is a genuine ledger coverage gap, found by the detector rather than
by review — **filed as #104**, deliberately not fixed inside this ticket. The dev worlds' large
`never_placed` counts are the earlier provers' own minted Serials and are expected.
- P4.2 Biography + state-machine validator: per-Serial history query and legal-transition replay
  (offline CLI first; GM `/itemhistory` later). **DONE 2026-07-31 (#84)** — `itemhistory <serial>`
  (the Biography), `ledgervalidate` (the replay) and `biographycheck` (39/39, hash `8fe8ef76`
  byte-identical on both platforms, five negative controls). The machine lives in
  `ItemBiography.{h,cpp}` on **one** raw `sqlite3*` — legality is a property of the ledger alone,
  so unlike P4.1 there is no world DB to compare against — and the same machine drives both
  readers, so a Biography's place column cannot disagree with the validator's verdict. The GM
  `/itemhistory` chat command stays deferred; this is the console/offline half.

**The transition machine (P4.2)** — what each event presupposes, and where it leaves the item:

| Event | required place | resulting place |
|---|---|---|
| `created` 100 | unborn | unplaced |
| `NewGenDrop` 5 | unplaced | ground |
| `Buy` 7, `GmMint` 40 | unplaced | inventory |
| `Drop` 2 | inventory | ground |
| `get` 3 | ground | inventory |
| `Deposit` 10 | inventory | warehouse |
| `Retrieve` 9 | warehouse | inventory |
| `Give` 1, `Exchange` 11 | inventory | inventory (hand-off) |
| `TpList` 33, `TpOffer` 35 | inventory | escrow |
| `TpDelist` 34, `TpRescind` 36, `TpRefund` 37, `TpTradeOut` 38, `TpTradeIn` 39 | escrow | warehouse |
| `Sell` 8, `Deplete` 4 | inventory | **unchanged** |
| `despawned` 101 | ground | gone |
| `destroyed` 102 | anywhere live | gone |
| everything else (`Use`, `Repair`, both upgrade outcomes, the caller-less six) | anywhere live | unchanged |

Two rules carry the whole thing:

- **Unplaced is a wildcard source.** The minting funnel cannot know where a venue puts an item, so
  a crafted sword reads `created` and then nothing until somebody drops or trades it. Demanding a
  placement event would accuse every craft, quest and fishing item in the database. From `unborn`
  or `unplaced` every transition is legal; the machine starts holding the ledger to its word at the
  first event that says where the item is.
- **`Sell` and `Deplete` are not exits**, which is a deliberate divergence from P4.1's locating
  table above. The real emission order is `Sell` → `Deplete` → `destroyed`
  (`ItemManager::item_deplete_handler`), so making either terminal would report every sale in the
  database as an event after the item's death. The two tables answer different questions:
  reconciliation asks who holds the item and hedges against a lost destruction row; this asks
  whether a transition was possible.

**Five violation classes**, four critical (a healthy ledger cannot produce them): `event_after_exit`
(which is also how "destroyed once" is enforced — a second exit is an event after the first),
`acquired_while_held` (the picked-up-twice class: taken into custody while already in custody with
nobody having let go), `double_creation`, `orphan_event` (counted once per Serial, and it suppresses
that Serial's other findings — one cause, one finding). The fifth, `released_when_absent`, is a
warning because it is the shape a lost flush makes from the other direction.

**The crash excuse.** Batched flushing means a crash loses the tail of a window, and losing one
`Drop` makes the following `get` read as the strongest finding the tool has. A violation with a
crash boundary's `event_id` between the Serial's previous event and the offending one is counted as
`gap_excused` rather than accused. It applies only to the three transition-dependent classes — a
birth row and its `created` event ride one flush transaction, so no hole can produce a Serial born
twice or an event about a Serial never born.

**What the first run found (2026-07-31), and what it means:** 27 `double_creation` and 46
`orphan_event`, every one of them inside two short windows on 2026-07-30 (15:40–15:44 and
20:01–20:18) and none since, across 1087 later events. Both are the already-fixed allocator
incident — `CGame::on_timer` rebuilding `ItemManager` and zeroing the Serial allocator, fixed in
`165c656a` — reconstructed from the ledger alone: the duplicate `created` pairs name two different
maps minutes apart, and the orphans are the events whose `item_instances` row the UNIQUE constraint
refused. Nothing to fix; the dev world wipes to v9 at P5.1. Recorded here because it is the
validator's own evidence that it detects a real dupe-shaped fault, not a synthetic one.
- P4.3 Analytics starter pack: drop-rate, entering-population, despawn-attrition SQL (DuckDB
  optional for heavy aggregation — reads SQLite directly, no service install). **DONE.**

**The denominator (P4.3), and why it is a schema change.** Drop rarity is authored as a chance
*per kill* (ADR 0005), so an observed count of drops divides by nothing until kills are counted —
and nothing in the server counted them (`CClient::m_enemy_kill_count` is per character and per
nothing else). The alternative was to estimate kills from the ledger itself, as total observed
drops over total authored expectation; that was considered and rejected by the owner, because the
estimate absorbs any error that scales a whole table equally — a doubled multiplier, a wrong loot
grade — which is precisely the fault class this tooling exists to catch. So **ledger schema v2 adds
`npc_kills(day, npc_id, npc_name, kills, rep_factor_sum)`**, written from
`CEntityManager::npc_dead_item_generator` after its own guards, so the population counted is
exactly the population `dropodds` prices.

Two columns there are not obvious:

- **`rep_factor_sum`**, not a rating bucket. The reputation layer is per-killer and multiplies gear
  and unique rows *linearly* (#88), so the summed factor is a sufficient statistic: the expected
  number of drops of such a row is `rep_factor_sum × authored_ppb_at_rating_0`, exactly, with no
  per-rating breakdown to store or join against. Ordinary rows divide by `kills`.
- **`npc_name`**, beside the id. A drop's birth row records its monster by NAME (`origin_detail`,
  which is what a Biography shows a human) while a kill is keyed by config id, and the ledger holds
  no name-to-id table because it must stay readable on its own, never against a copy of
  `gamedata.db`. Without the name the SQL half could not join at all. Where one name covers several
  configs (14 Catapults, 3 Guards) both sides pool identically.

**The predicted side is consumed, never rebuilt.** `dropodds` gained a `rows` form emitting
`DROPODDS MODE | SPLIT | NPC | SLOT | ROW` and nothing else. `resolved_slot` moved out of the
report into `DropModel.h` beside `resolve_drop_chances`, so the human listing, the machine capture
and the prover all read ONE derivation of a row's per-kill expectation — the arithmetic #89 made
non-obvious. Both earlier `dropodds` captures used as regression evidence stayed byte-identical.

**Scope: Instanced items only, and the reason is not squeamishness.** A stackable item has no birth
row and so no source monster; `item_flows` aggregates it by day and item as a **quantity, not an
occurrence count** — a gold drop books the number of coins, not the fact that a drop happened — so
a monster column alone would still yield no "1 in N". Measured against today's data that leaves
**2247 of 2328 (item, monster) pairs covered**, every tier-rolling gear row among them; the
excluded 81 are Gold plus 30 stackable body parts.

**What the ticket assumed and this cycle corrected: #63 does not move a drop rate.** Every
per-grade generosity multiplier is 1.00, and ADR 0002 deliberately puts grade differentiation in
quality and roster rather than frequency — so a monster rolling the wrong loot grade drops
everything at exactly the authored rate. Nothing in the rate columns moves. What moves is the
**tier** the item is born with, which the ledger records on the birth row. The audit therefore has
a second half, an observed-vs-authored **tier mix per loot grade**, and that is the #63 detector;
the rate diff could never have been.

**Confidence scales with volume, and the pack says so per row.** For a row of chance `p`, seeing an
error of factor `r` at `z` sigma needs `kills ≥ z² · r / (p · (r−1)²)` — for a doubled rate at 3
sigma, `18/p`: about 1,280 kills for a 1-in-71 gear row, about 34,000 for a 1-in-1,878 boss
Legendary. Rows below that threshold are reported **THIN**, which is neither a pass nor a failure.
A 3-sigma gate also misfires about once in 370 rows, so a full run flags roughly three rows in a
perfectly healthy world; the report prints that expectation beside the summary rather than leaving
a reader to infer it.

Artifacts: `Scripts/analytics/*.sql` (overview, drop rates, entering population, attrition, kill
volume) plus `Scripts/drop_audit.py` for the joined view. Provers: `analyticscheck` for the store
contract and the join, `drop_audit.py --selftest` for the audit's own arithmetic.

**P5 — Ship**
- P5.1 Deploy batch: Server identity bump, changelog roll-up, Debian deploy (systemd stop → copy →
  DB sync per deployment runbook), world wipe to the v8 schema. *Blocked by P3.3, P3.4.*

## Ratified answers (owner, 2026-07-29)

- **Q1 — Sequencing:** before go-live. Owner reframe adopted as D6: the true can't-go-back point
  is real players, not ticket #61 (which is already effectively deployed to the Alpha server).
  Alpha worlds wipe freely; no backfill code ever ships.
- **Q2 — Instanced boundary:** all non-stackables, Commons included (D2 as drafted).
- **Q3 — Retention:** events kept forever (revisit only if size ever argues otherwise).
- **Q4 — Consolidation:** **in scope** — ratified as D7 after wipe-freedom removed the migration
  risk that had argued for deferral. Folded into P1.2 as one persistence epoch; atomic
  multi-account commits ride P3.4.

## Vocabulary (merged into CONTEXT.md, Provenance Ledger section)

- **Serial** — the permanent server-global identity of one Instanced item. Never on the wire.
- **Provenance Ledger** — the append-only `itemledger.db` recording instances and events.
- **Instanced / Counted** — the two tracking tiers (D2). _Avoid_: "tracked/untracked" (everything
  is tracked; the tiers differ in granularity).
- **Biography** — one Serial's ordered event history.
- **Reconciliation** — the snapshot-vs-ledger holder comparison; the dupe detector.
- **Mint** — creating an item instance and assigning its Serial (aligns with GmMint / #56 usage).
- **Game DB** — the single consolidated account/character store (`game.db`) that replaces
  `accounts/*.db` (D7 / ADR 0004).
