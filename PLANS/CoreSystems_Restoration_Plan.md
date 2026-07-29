# Core Systems Restoration & Modernization Plan

**Status: RATIFIED 2026-07-29.** Decision outcomes: **D1–D6, D8, D9, D10 locked per the recommendation column** (§7). **D7 deferred** to the Party design session. Owner directive at ratification: **design wayfinder sessions precede Guild (Phase 2) and Party behavior work** — the owner intends deviations from the original in both systems; §3's contracts are the baseline those sessions edit against, and their outputs amend this plan.

Two axes, one trajectory:

- **Axis 1 — Restore what upstream removed.** ShadowEvil's final commit (`506f7f1`, 2026-03-04, "Remove Guild System (Full Gut)") stripped the guild system *and* the interleaved fightzone/arena system, and beheaded Crusade's command layer. His stated intent — "in preparation for a clean SQLite-backed rebuild" — was never started; the shells he left are byte-identical today. `PLANS/Development_Merge_Plan.md:131` already names this "the flagship next feature."
- **Axis 2 — Rewrite what survived but is still original-shaped.** The 2026-07-29 architecture census shows the god objects are alive and load-bearing: `Server/Game.cpp` 14,727 lines (24% of the server), `CClient` ~250 public members, `Client/Game.cpp` 7,137, `MapData.cpp` 4,281 with a single 2,220-line function. The debt is structural, not commented (4 genuine TODO markers in ~130k lines), and **no existing plan doc claims this roadmap**.

**Baseline authority:** original 3.82 source at `C:\Users\miste\source\repos\HelbreathServer` (primary) and `...\repos\HB382_CENTUU` (cross-check). All four target systems are byte-identical between the two at the protocol level.

**Explicit non-goals:** the deliberate customizations stay — Item Tiers, Trading Post, Dark Items automation, prefix-tint deviation, the fork-built Apocalypse chain, admin command framework. "Baseline" means the original's *systems and rules*, not reverting fork improvements.

**Provenance:** four read-only investigations on 2026-07-29 (git archaeology of `506f7f1` + the 35-commit merged tail; current-tree sweep at `feature/item-tiers` HEAD; original-source behavioral inventory; architecture census). File:line anchors below were verified by those sweeps; re-verify before editing, as the tree moves.

---

## 0. Terminology (the events, disambiguated)

- **Crusade** — the scheduled Aresden-vs-Elvine total war on `middleland` + both cities: duties (Soldier/Constructor/Commander), buildable structures, Grand Magic Generator → meteor strike victory. Original GM command was `/begincrusadetotalwar`; ours is `/begincrusade`.
- **Heldenian** — the separate battle event, two types: **Type 1 "Total War"** on `btfield` (tower destruction) and **Type 2 castle siege** on `hrampart` (previous winner defends gates; attackers plant the flag). Ours: `/beginheldenian 1|2`.
- **Fightzone** — the guild arena system (9 `fightzone*` maps): guildmaster reserves a 2-hour slot for gold, gets tickets, energy-sphere matches.

---

## 1. Verdict map (current tree)

| System | Verdict | One-line evidence |
|---|---|---|
| **Party** | **FUNCTIONAL** (legacy-shaped) | Never removed anywhere in history. Full server+client+wire chain; but handlers still live inline in `CGame` (`Game.cpp:12977-13723`), `PartyManager` is a thin bookkeeping shell — original PartySvr shape. |
| **Guild** | **SHELL ONLY** | Empty `GuildManager` pairs (server+client), two 35-line dialog shells (IDs 7/8 registered, unreachable), empty `PacketGuild.h` (not even included), zero message IDs, 6 DB columns stripped. Shells unchanged since the gut. |
| **Crusade** | **PARTIAL** | Engine ~95% ported and GM-startable (structures, meteor chain, contribution, payouts). Dead: the entire command layer (see §2.2). Scheduler hardwired off. |
| **Heldenian** | **PARTIAL** | Type 1 works (92 tower rows). Type 2 starts and spawns **nothing** — `map_heldenian_gate_doors` has 0 rows. Latent `break`-vs-`continue` loop bugs. No scheduler (the original never wired one either). |
| **Fightzone** | **SHELL ONLY** | 3 empty `WarManager` methods, zero callers; 3 DB columns stripped; all 9 maps + energy-sphere data still live; `is_fight_zone` still gates combat rules. |
| **Apocalypse** | **FUNCTIONAL** | Fork-complete (0.8.4–0.9.0 work), exceeds the original (which never implemented the progression chain). |

---

## 2. Axis 1 — what was removed or broken

### 2.1 The gut itself (`506f7f1`, in our ancestry via the 2026-07-22 merge)

Nothing was `git rm`'d — every source file was **emptied in place** (which is why deletion-hunting greps find nothing). Removed:

- **Wire:** 29 message IDs (6 top-level `MsgId`, 11 `CommonType`, 12 `Notify`), 13 notify structs, `PacketRequestGuildAction`, fightzone request/response, `guild_name`/`guild_rank`/`fightzone_number` fields from the char-init response, `GuildNameLen`(21) + `MaxGuildsmen`(128) constants. Full ID inventory: Appendix A.
- **Server:** both guild dispatch families, 12 notify builders inside `send_notify_msg`, guild chat (`^` prefix now dead → `[Local]`), guild file checks, the whole fightzone reserve/ticket flow, `create_new_npc`'s `guild_guid` param, guild fields on `CClient`/`CNpc`.
- **Client:** guild menu (721 lines) and operation queue (215) stripped to shells, guild-name overhead rendering, the lazy name-cache, `killer_guild` on kill notices, character-dialog guild suffix.
- **DB:** `Scripts/migrate_strip_guild_columns.py` dropped 6 `characters` columns: `guild_name`, `guild_guid`, `guild_rank`, `fightzone_number`, `reserve_time`, `fightzone_ticket_number`. Backups (`*.guild_migration_bak`) still on disk.

**Kept intentionally** (the rebuild hooks): shell `GuildManager` classes *already instantiated and lifetime-managed* (`Server/Game.h:546`, `new`'d at `Game.cpp:294` — note a duplicate `new` at `:12346`; client member at `Screen_OnGame.h:191`), dialog IDs 7/8 registered, Kennedy the GuildHall Officer NPC (`OwnerType.h:38`, handled `EntityManager.cpp:252`), guild ticket items 88/89 still in `ItemEnums.h`, both guildhall maps active, `construction_point` (war economy, not guild).

### 2.2 Crusade — collateral damage (the command layer)

The war *engine* survived; the guild-anchored *command layer* did not:

1. **Only Soldier is selectable.** `DialogBox_CrusadeJob` lost the guild-rank branch that offered Commander (guildmaster) and Constructor (guild member). The server's `select_crusade_duty_handler` still accepts duties 1/2/3 — and lost its own gate (`m_iGuildRank != 0 && duty == 3 → return`), so it currently trusts the client entirely.
2. **`Notify::TcLoc` has no sender.** The enum case survives (`Game.cpp:7357` arm) but nothing sends it, so `m_construct_loc_x`/teleport markers are `-1` forever; every Commander/Constructor map-click UI path is inert (`DialogBox_Commander.cpp:502/524/707`, `DialogBox_Constructor.cpp:277/377`, `DialogBox_Soldier.cpp:232` — buttons render, play the click sound, do nothing).
3. **Three empty stubs, zero callers:** `request_guild_teleport_handler` / `request_set_guild_teleport_loc_handler` / `request_set_guild_construct_loc_handler` (`WarManager.cpp:1243-1256`); `map_status_handler` mode 1 no-ops while the client still sends it (`WarManager.cpp:945` vs `DialogBox_Commander.cpp:506,711`).
4. **Structure placement gating gone** for types 36-39 (`WarManager.cpp:1173-1178`): the construct-zone ±10-tile check and the 10-structures-per-guild cap were removed — war units placeable anywhere, uncapped (only the guard-tower proximity check at `:1198-1215` survives).
5. **Economy rebound guild→side:** CP pooling (`check_commander_construction_point`), `apply_crusade_contribution` NPC routing, and help-request routing now match on `m_side`; the NPC-killer construction-point award (summons crediting their commander) was deleted outright.
6. **Occupy-flag rank gate removed**; `[Guild]` chat channel dead; stale copy "You should join a guild to hire soldiers." at `DialogBox_CommandHallMenu.cpp:135`.

### 2.3 Heldenian — gaps that are *not* the gut's fault

- **`map_heldenian_gate_doors` = 0 rows** (columns exist). The original `HRampart.txt` carries 4 `HeldenianGateDoor <dir> <X> <Y>` rows; the SQLite conversion never migrated them. Same bug class as the icebound miss (`CHANGELOG.md:1119`). Gate NPC type 91 (ids 51/52) is configured and ready. **Highest-value single data fix in this plan.**
- **`HeldenianWinningZone`** (type-2 win trigger location) — documented in the original map settings; absent from our data model entirely.
- **Latent port bugs** in `local_start_heldenian_mode`: `break` where `continue` is meant (`WarManager.cpp:1306, 1317, 1320-1322, 1327-1329` — one uninitialized client or off-map player aborts whole notify/teleport loops), uninitialized `bool flag` in `update_heldenian_status` (`:1483`), inconsistent owner-handle sourcing (`:1360` vs `:1398`).
- **Scheduler never existed:** `m_heldenian_schedule[10]` is loaded from DB and read by nothing; the original's `HeldenianWarStarter` was never called from `OnTimer` either (and is internally broken) — **GM-only start is the authentic baseline**; a scheduler is an improvement, not a restoration.
- **Semantics mismatches:** `HeldenianCount` fields 3/4 mean deaths on the server, flags on the client (inherited from the original, where the client also prints "Aresden Flags" twice); EK occupy-flag price server 12 vs client text 10 (`WarManager.cpp:2382-2388` vs `DialogBox_CommandHallMenu.cpp:146`); `notify_heldenian_winner()` is degenerate (`var_88C` check only).

### 2.4 Fightzone

Fully guild-dependent in the original (guildmaster reserves). Three empty handlers (`WarManager.cpp:2634-2644`), client handlers removed, 3 DB columns stripped. Still present: all 9 maps active, `map_energy_sphere_creation/_goal` rows (fightzone5-9), `m_is_fight_zone` combat-rule gating. Restoring it is a contained effort (~600 lines original scope) but is gated on Guild.

### 2.5 Party — functional; needs a parity audit, not a rebuild

Never removed (verified across all branches incl. Legacy-DDRAW). Small audit ticket:

- **Full-party bonus quirk:** the original's group-bonus term `n / DEF_MAXPARTYMEMBERS(100)` is dead code (always 0 → flat even split). Our port uses `MaxPartyMembers = 9` (`Game.cpp:12070`), so at exactly 9 members the term activates and the split doubles (`(v1 + v1*1)/9`). Accidental behavior change dressed as faithful code — decide: restore flat split, or keep as a deliberate full-party incentive (Decision D7).
- **Constant soup:** `MaxParty=5000` / `MaxPartyMember=100` (`PartyManager.h`) vs wire `MaxPartyMembers=9` — the original's 100-vs-`iIndex[9]` overrun class; verify our loops are bounded by 9 everywhere.
- **Stale party resurrection:** original read `party_id` at load but never saved it; we persist it (`AccountSqliteStore.cpp:1414`, restored at `Game.cpp:4565`) — verify a restored id is validated against the live party registry on login (the original defect resurrected ghost parties).
- Architecture note for Axis 2: handlers inline in `CGame` + the 5-hop `PartyOperation` byte-buffer round-trip (a fossil of the out-of-process PartySvr) are a candidate for eventual absorption into a real `party_manager` — low priority, it works.

### 2.6 Event schedulers (all three wars)

All hardwired off: `m_is_crusade_war_starter = false`, `m_is_apocalypse_starter = false` (`Game.cpp:1070-1071`), `ApocalypseStarter()` fully commented out (`:12955-12973`), no Heldenian starter exists. `event_schedule` table already has the right shape (2 rows, `is_active=0`, no heldenian rows). The original's `CrusadeWarStarter` ran on the 5s tick gated by `crusade-server-name`; Schedule.cfg shipped `crusade-schedule = 1 1 1` (Monday 01:01).

### 2.7 Salvage assets (don't start from zero)

- `Scripts/output/DialogBox_GuildMenu.cpp` (**734 lines**) and `DialogBox_GuildHallMenu.cpp` (**388 lines**) — complete pre-gut client implementations, preserved by upstream for reference.
- `lan_eng.h:834-937` — all ~100 guild dialog strings intact (132 guild defines total, currently orphaned).
- `Sprite Work/GameDialog/guild_menu_recreation.psd` + the dialog frames still resolve (`InterfaceNdGame2` 0/2, `InterfaceNdText` 19).
- `*.db.guild_migration_bak` account backups — old guild data recoverable if wanted.
- Free dialog IDs 5, 30, 43-48 for any new UI.
- Original source: full reference for every flow, plus `repos/helbreath-v3.82/Server/Docs/` (the only written spec for map settings and gserver heldenian knobs).

---

## 3. Restoration contracts (original behavior = the spec)

Condensed from the 2026-07-29 original-source inventory. These are the *rules to preserve*; §3.5 lists where we deliberately deviate.

### 3.1 Guild

- **Create** (dialog-driven, **no NPC, no gold**): level ≥ 20, charisma ≥ 20, citizen (`location != NONE`), standing in your own town's map, not during crusade. GUID minted server-side. Creator becomes rank 0.
- **Ranks:** `-1` none / `0` guildmaster / `12` guildsman. No elder tier, no enforced member cap (128 constant existed, never checked).
- **Join:** applicant buys **GuildAdmissionTicket (item 88)**, *gives it to the guildmaster in person* → server queries master (`0x0B02`) → master approves/rejects from the operation-queue dialog. Ticket consumed either way. Same-location required.
- **Leave:** **GuildSecessionTicket (item 89)** to the master (approve flow), or to **Kennedy** for self-dismissal at **−300 exp** (rank 12 only, not during crusade).
- **Ban:** `/banguild <name>`, rank 0 only, works cross-map. **Disband:** dialog, rank 0 + typed name match, not during crusade; broadcast clears members inline.
- **Wire:** own guild rides the char-init contents; *other* players' names resolve through the lazy client cache (`ReqGuildName 0x0A59` → 32-byte answer `0x0BA6`, LRU-stamped) — scales fine, keep the pattern. Overhead render: "(Guild Guildmaster/Guildsman)" line under the name, suppressed during crusade.
- **Gates elsewhere:** rank 0 required for crusade Commander duty and fightzone reservation; rank 0 cannot drop charisma below 20; guild membership required for Constructor (client) and help-routing.
- **Persistence (original):** flat file per guild with an empty `[GUILDSMAN]` section and *no reader* — membership lived only in character files. **The rebuild replaces this wholesale with SQLite** (that was the entire point of the gut): normalized `guilds` + `guild_members` tables behind a store, `CClient` hydrated at login, no flat files (Decision D1 picks the DB home).

### 3.2 Crusade command layer

- **Duties:** Commander = **guildmaster only, server-enforced**; Constructor = guild member (client-gated in the original — enforce server-side too in the rebuild); Soldier = any citizen. No level gates. Travellers excluded (client).
- **Commander:** sees the full middleland structure map (others see only Mana Stones); sets the guild **rally point** and **construct zone** via minimap click (→ `TcLoc` broadcast to the guild); summons war units (beetle 1000 / knights 2000 / cavalry+golem 3000 / catapult 1500 CP); receives pooled CP from guildmates **+10% WC kickback** (points destroyed if no same-guild commander online — soften? Decision D5); winner-side commander seeds 3000 CP next war.
- **Constructor:** places Arrow/Cannon Tower, Mana Collector, Detector within ±10 tiles of the construct zone, **max 10 structures per guild**, guard towers need ±2 clearance; structures spawn inactive (`BuildCount 10`) and are finished by **pickaxe hits** (thresholds 10/5/1 → appearance stages → active), rewarding WC 700/700/500/500.
- **Soldier:** rally teleport; `/help` pings routed to the *same-guild* commander.
- **Engine (already working, listed for completeness):** ManaStone→Collector (3 mana/tick, ±5) → 5s flush → GMG stock (+1 per 15 mana) → stock > 3000 → 5s warning → meteor vs the enemy city's 4 strike points (HP 13; ESGs within ±10: two = immune, one = half) → all 4 down ends the war. Payout on next login: win = full WC + level bonus, loss = WC/10, draw = WC/6, not party-split. Our tree already persists `crusade_job`/`crusade_guid`/`construct_point` — keep (fixes the original's reward-forfeit-on-relog).

### 3.3 Heldenian

- **Type 1 (btfield):** both sides' towers spawn (ours: 92 rows, working); destroy all enemy towers → instant win; tie-break towers → deaths → previous winner.
- **Type 2 (hrampart):** previous winner defends; gates (type 91) spawn at the 4 gate-door rows; attackers breach and **plant the flag in the winning zone**. Needs: gate-door rows, winning-zone data + the plant-to-win hook.
- **Start:** GM-only is baseline (original scheduler was stillborn). Prep phase → start notify at T−300s. Entry via Gail NPC, combatants only. Every client gets **CP = charisma × 300, cap 12,000** (mercenary budget through the same summon path as crusade).
- **Rewards** (via `GetExp`, party-split, unlike crusade): winner `WC × 1.2` + level×200/100/30 bonus — the original literally computes `currentExp + WC×1.2` (transcription bug, doubles the character's exp); implement the intent, not the bug. Loser `WC / 5`.
- **Winner privileges:** defender status next siege + flag-planting rights. (No shop discount exists in code despite client text.)

### 3.4 Fightzone

Guildmaster reserves zone 1-8 for **1,500 gold** in 2-hour slots (booking closes 5 min before), **day-parity rule** `(day + side + zone) % 2 == 0`, grants **50 tickets**; tickets admit guildsmen; energy-sphere match on the zone map. Original stored the reservation against a *client handle* (evaporates on logout) — store against the guild in the rebuild.

### 3.5 Faithfulness stance

- **Preserve (player-facing rules):** all §3.1-3.4 numbers and flows — creation gates, ticket ceremonies, duty gates, CP/WC economy, build thresholds, meteor math, reward formulas, day-parity.
- **Fix (mechanical defects, no gameplay identity):** the `break`/`continue` loop bugs; uninit `flag`; array-bound classes (`iIndex[9]` vs 100); the CTeleportLoc LRU `[i]`-vs-`[iIndex]` copy-paste bug family and its never-reset per-guild build counter (moot if D3 replaces the structure); heldenian reward `currentExp+` transcription; `HeldenianCount` field semantics (+ the double "Aresden Flags" label); reward-forfeit-on-relog (already fixed by our persistence); guild-file liveness check (gone with SQLite); dead-code deletions (`CrusadeCore.h`, `rusadeCore.h`, orphan constants).
- **Decide (Decisions §7):** EK flag price 12 vs 10; constructor structures free (original defect: `m_iNpcConstructionPoint[36..39]` never set, client advertised prices) vs priced; party full-party double; guild chat `^` restoration; occupy-flag rank gate (original's looked inverted — blocked guildmasters); CP-pool destruction without a commander.

---

## 4. Axis 2 — the modernization census (2026-07-29)

### 4.1 What's still god-object

| Hotspot | Size | What it still owns |
|---|---|---|
| Server `CGame` (`Game.cpp` 14,727 / `Game.h` 932) | ~156 members, 189 methods | World arrays (`m_client_list[2000]`, maps, configs); 17 raw-`new` managers constructed inside `on_timer`; inbound dispatch (41 + 62-case switches); **`send_notify_msg` — 1,457 lines / 153 cases**; chat parsing; 535-line teleport handler; login bootstrap; party handlers; config hashing |
| Server `CClient` (`Client.h` 455) | ~250 public members, no methods | The reason every manager reaches `m_game->m_client_list[h]->…` (3,000+ derefs) |
| Client `CGame` (`Game.cpp` 7,137) | ~105 members | 1,555-line click/command pipeline (`process_left_click` 654); **374-line hardcoded `strcmp` map table**; item tint/palette; config-cache replication; partial player state split against `CPlayer` |
| Client `CMapData` (`MapData.cpp` 4,281) | — | **`object_frame_counter` = 2,220 lines** (every entity's animation/motion/sound/effect per frame); 805-line constructor of animation tables; 700-line `set_owner` |
| Client `Screen_OnGame` (3,517 across 4 files) | ~40 loose members + Hungarian survivors | `draw_objects` 679; accumulating what leaves CGame |

Also: **0 of 47 in-game dialogs use CControls** (~14,000 lines of hand-blitted draw + coordinate-duplicated hit-tests — the standing bug generator); the legacy DLGBOX *dispatch* is however gone (proper `IDialogBox` + `DialogBoxManager`).

### 4.2 Already modern (don't touch, reuse as patterns)

Networking (`ASIOSocket`), SQLite stores (accounts/gamedata/MapInfo/tradingpost, idempotent migrations), logging (channels), admin command registries (console + chat — the best-shaped server subsystem), `RegenManager` (cleanest extraction), `StatusEffectManager`, roll-strategy seam, client screen machine + `NetworkMessageManager`'s 23 domain files, CControls on the login flow, formula engine. HYBRID (extracted shell, procedural guts): ItemManager, MagicManager, CombatManager, EntityManager, SkillManager, QuestManager, CraftingManager, LoginServer.

### 4.3 The ranked ladder (census verdict, guild/war systems excluded)

1. **`send_notify_msg` → typed sends** (1,457 lines; the typed structs already exist in `PacketNotify.h` — replace the 14-param varargs façade, delete the switch). Highest value-per-effort.
2. **`MagicManager::player_magic_handler`** (2,345-line function) → spell-effect registry keyed by magic type.
3. **`CombatManager::calculate_attack_effect`** (1,386) + the near-duplicate 518/468-line AoE pair → staged pipeline over the formula engine.
4. **`CClient` decomposition** into components (inventory / stats / mastery / exchange_session / quest_state / combat_state / anti-cheat) — the coupling keystone; unblocks every other extraction.
5. **`CMapData::object_frame_counter`** dismantled into the existing `AnimationState`/`EntityMotion`/renderer seams.
6. **Client input pipeline** (1,555 lines) → interaction controller over `CursorTarget`/`PlayerController`.
7. **`ItemManager` split** along its own comment banners (inventory/equip/drop/give/bank/shop/repair/upgrade); `calc_total_item_effect` (525) is the hard center.
8. **Dialogs → CControls**, largest-first (`SysMenu` 1481, `ItemCreator` 1332, `Manufacture` 1249, `TradingPost` 1156…), or opportunistically whenever a dialog is touched.
9. **Maps & teleport**: `CMap`/`CTile` runtime model (20,001-flag arrays × 100 maps, `strcmp` faction checks), 535-line teleport handler, client hardcoded map table → data-driven map registry.
10. **Server inbound dispatch → handler registry** (mirror the client's 23-file pattern; type the 62 `CommonType` payloads instead of `v1..v4 + text`).

Honorable mentions: the 4-parallel-switch effect system (3,014 lines, ~150 types × 4 files), `process_random_spawns` (813) + 17-param `create_entity`, manager lifetime (raw `new` in a timer → RAII/DI), F2F exchange (dupe surface; ItemLedger will need it instrumented).

---

## 5. Trajectory

### Principles

1. **Restoration rides modern rails.** New guild/war code never extends the god switches: guild packets get typed handlers via a registry seam (Ladder #10) and typed notify sends (Ladder #1) from day one. The restoration phases are the *pilot* for those two patterns; retrofitting the other 200 cases comes later and mechanically.
2. **One Compatibility bump window per wire batch** (the Item-Tiers deploy-batch practice). Phase 0 is deliberately wire-silent; Guild + Crusade share one bump.
3. **Server-only and data-only first** — ship value while Item Tiers still owns the branch/deploy cadence.
4. Per-phase: own plan doc (this doc is the map, not the per-phase spec), GitHub epic + sub-issues, `bak.py` cycle discipline, Linux gates at phase exit.

### Phase 0 — War-event repair (data + server-only; no wire change)

Make what exists actually work. No guild dependency.

- Seed `map_heldenian_gate_doors` from the original `HRampart.txt` (4 rows); add winning-zone data + the type-2 flag-plant win hook; verify `/beginheldenian 2` end-to-end.
- Fix the latent loop bugs (`WarManager.cpp:1306-1329`), uninit `flag` (`:1483`), owner-handle inconsistency; reconcile `HeldenianCount` semantics + client labels; EK price mismatch (D4); heldenian reward formula (implement `WC×1.2`, not `exp+WC×1.2`).
- **Scheduler:** replace the hardwired-false starters with one `event_scheduler` in `WarManager` reading `event_schedule` (crusade + heldenian + apocalypse rows, `is_active` respected, once-per-day dedupe) — the client already handles every start/end notify, so this is server-identity only.
- Cleanup: delete `CrusadeCore.h` / `rusadeCore.h`, orphan guild constants (`starting_guild_rank`, `max_guild_names`, `guild_name_allowed_chars` — they'll be re-created properly by Phase 2), the stale CommandHall copy.
- **Party mechanical audit** (§2.5): loop bounds vs the 9-cap, stale party_id validation on login. Behavior questions — including the D7 full-party split — belong to the Party design session, not this phase.

*Exit:* castle siege playable via GM; all three events schedulable; heldenian multi-player smoke on the Debian box.

### Phase 1 — Protocol seams (Axis-2 pilot, scoped)

Just enough to host Phase 2 cleanly, not the full retrofit:

- Typed notify send path: `send<PacketNotifyX>(client_h, pkt)` beside `send_notify_msg`; new code uses it exclusively.
- Server handler-registry seam for `CommonType`/`MsgId` families (shape: the chat-command registry / client `NetworkMessages_*` pattern), with the first `Sources/Server/Handlers/*` (or `NetworkMessages_*.cpp`) domain file proving it.
- Optional proof: migrate one small existing family (e.g. party) onto both seams.

*Exit:* a new packet family can be added end-to-end without touching `msg_process`/`client_common_handler`/`send_notify_msg`.

### Phase 2 — Guild rebuild (the flagship)

**Gated on the Guild design session** (owner directive at ratification): the owner has changes in mind that will deviate from the original; that session scopes fidelity-vs-new against §3.1, amends this plan, and seeds the Phase-2 epic. Then, into the shells upstream left, on the Phase-1 rails:

- **Store:** `guild_sqlite_store` — `guilds` (guid, name, side/location, master, created) + `guild_members` (guid, char_name, rank, joined); home per D1. Boot-validated like TierConfig; no flat files.
- **Server `GuildManager`:** create/disband/join-approve/dismiss/ban/name-query handlers per §3.1, membership index (guid → member handles — kills the original's 2000-slot linear scans), login hydration of `CClient` guild fields, `[Guild]` chat (D6), Kennedy secession flow, rank-0 charisma floor.
- **Wire:** new `PacketGuild.h` family + re-allocated IDs (Appendix A is the reference; don't reproduce the original's two ID collisions). Own-guild fields re-enter the char-init contents; lazy name-cache protocol restored. **Compatibility bump** (shared with Phase 3).
- **Client:** `guild_manager` name cache, `DialogBox_GuildMenu`/`GuildOperation` rebuilt (salvage: the 734-line pre-gut reference + strings + PSD; stay on `IDialogBox` per D2), overhead guild line, character-dialog suffix, kill-notice guild fields.
- **DB migration:** additive columns/tables only; decide whether to resurrect old guild data from the `.guild_migration_bak` files (probably not — fresh start).

### Phase 3 — Crusade command layer (guild-dependent restorations)

- Server: duty gates (Commander = rank 0, Constructor = member — server-enforced now), a proper `war_locations` service keyed by guild guid (rally + construct zone + build count; replaces the original's `CTeleportLoc[1000]` LRU abuse and its bug family; reset at war end), `TcLoc` sender restored, guild teleport/set-loc handlers refilled, placement gating + per-guild cap for types 36-39, CP pooling + help routing back to same-guild commander, NPC-killer CP award restored.
- Client: CrusadeJob offers all three duties again (rank-gated); Commander/Constructor/Soldier dialogs' buttons wired back up.
- Rides the Phase-2 Compatibility bump (one deploy).

*Exit:* full three-duty crusade on the box with a real guild.

### Phase 4 — Fightzone (decision-gated, D8)

Reservation flow on guild (stored against guild guid, not client handle), ticket issuance, energy-sphere match wiring, client dialog modes. Small; only if wanted.

### Phase 5+ — The standing modernization ladder

Work §4.3 top-down between feature efforts; each rung is its own bounded effort with the same discipline. Suggested first pulls after the war phases: finish #1 (retire `send_notify_msg` wholesale), then #4 (`CClient` components) since every later rung leans on it; #8 proceeds opportunistically forever.

### Dependencies at a glance

```
Phase 0 ──────────────┐            (no deps; server+data only)
Phase 1 ──► Phase 2 ──► Phase 3 ──► Phase 4
                 (one Compatibility bump for 2+3[+4])
Ladder: #1/#10 piloted in P1; #4 next; rest ordered by §4.3
```

---

## 6. Versioning & sequencing vs Item Tiers

- Phase 0 = Server identity bumps only (+ MapInfo.db/gamedata.db data syncs to the box — remember the admins-table gotcha on any world-DB sync).
- Phases 2+3 = **one Compatibility bump** + client/server ship-together deploy.
- **Branching:** start this workstream from `master` *after* `feature/item-tiers` lands (it currently owns Compatibility 0.9.0 and the deploy cadence; #62's dual-mode test matrix is its gate). Phase 0 could run parallel on a separate branch if desired — it touches `WarManager`/data, which Item Tiers doesn't.

## 7. Open decisions (owner)

**Ratification outcome (2026-07-29):** D1–D6, D8, D9, D10 **locked as recommended**. D7 **open** — deferred to the Party design session, where the owner will scope legacy-fidelity vs improvements for the whole party system; guild deviations are likewise scoped in the Guild design session before Phase 2.

| # | Decision | Recommendation |
|---|---|---|
| D1 | Guild DB home: tables inside each account-store `.db`, or a separate `guilds.db` (TradingPost precedent) | Separate `guilds.db` — guilds are world-state, not account-state; account DBs stay per-account; mirrors `tradingpost.db` |
| D2 | Guild UI paradigm: rebuild on `IDialogBox` (all 47 dialogs' pattern) vs first CControls in-game dialog | `IDialogBox` now — don't fork the paradigm mid-feature; CControls migration is Ladder #8 |
| D3 | War-locations service replaces `CTeleportLoc` reuse | Yes (listed as a fix, ratify) |
| D4 | EK occupy-flag price: server 12 vs client text 10 | 12 + fix the text (server was authoritative in the original) |
| D5 | CP pooled with no same-guild commander online: destroyed (original) vs banked | Keep original (destroyed) for baseline; revisit from play |
| D6 | Restore `[Guild]` chat on `^` | Yes — original behavior, trivial once membership exists |
| D7 | Party full-party (9/9) double-exp quirk | Restore the original flat split; if a full-party incentive is wanted, make it a deliberate tunable, not an integer-division accident |
| D8 | Fightzone: restore in Phase 4 vs park indefinitely | Restore — it's the guild system's PvP outlet and cheap once guilds exist |
| D9 | Constructor structures: free (original defect) vs charge the client-advertised prices | Charge the advertised prices (client UI was the design intent; "free" was a missing-assignment bug) |
| D10 | Occupy-flag rank gate (original blocked rank 0 — looks inverted) | Leave ungated (current state); note as accepted deviation |

## 8. Issue-tracker mapping

One epic per axis-1 phase, sub-issues per §5 bullets, mirroring the Item-Tiers epic #36 structure. Ladder rungs become standalone issues tagged `modernization`, pulled between feature efforts. This doc is the wayfinder; each phase gets its own plan/implementation doc at kickoff.

**Created at ratification (2026-07-29):**
- **Epic #92** — Phase 0: War-event repair (`wayfinder:map`), sub-issues **#95** (castle-siege data + victory hook), **#96** (heldenian engine fixes incl. D4), **#97** (event scheduler), **#98** (dead-code cleanup), **#99** (party mechanical audit, defects-only).
- **#93** — Guild design wayfinder session (`wayfinder:grilling`) — **gates Phase 2**; scopes owner deviations from §3.1, seeds the Phase-2 epic.
- **#94** — Party design wayfinder session (`wayfinder:grilling`) — owns **D7** and all party behavior decisions.
- Phase 1/3/4 epics are created at their kickoffs (Phase 2's by #93's output).

---

## Appendix A — Removed protocol families (re-add reference)

From `git show 506f7f1` (authoritative). Original values listed for the record; the rebuild may re-allocate freely within one Compatibility bump — but **do not reproduce the original's collisions**: `0x0A25` (`ReqGetOccupyFightZoneTicket` server vs `ClearGuildName` client) and `0x0EA03206` (heldenian-teleport request vs TP-list — the latter still lives in our client as `RequestHeldenianScroll`-adjacent flows).

- **Top-level:** `RequestCreateNewGuild/Response` `0x0FC94208/09`, `RequestDisbandGuild/Response` `0x0FC9420A/0B`, `RequestFightZoneReserve/Response` `0x12A01007/08`. (Inter-server `0x0FC9420C/0D` + `GuildNotify 0x0DF30760` still sit unused in `ServerMessages.h`.)
- **CommonType:** `JoinGuildApprove 0x0A06`, `JoinGuildReject 0x0A07`, `DismissGuildApprove 0x0A08`, `DismissGuildReject 0x0A09`, `ClearGuildName/ReqGetOccupyFightZoneTicket 0x0A25`, `BanGuild 0x0A26`, `SetGuildTeleportLoc 0x0A54`, `GuildTeleport 0x0A55`, `SetGuildConstructLoc 0x0A57`, `ReqGuildName 0x0A59`.
- **Notify:** `QueryJoinGuildReqPermission 0x0B02`, `QueryDismissGuildReqPermission 0x0B03`, `WaitForGuildOperation 0x0B04`, `GuildDisbanded 0x0B0B`, `CannotJoinMoreGuildsman 0x0B0D`, `NewGuildsman 0x0B0E`, `DismissGuildsman 0x0B0F`, `FightZoneReserve 0x0B76`, `NoGuildMasterLevel 0x0B77`, `SuccessBanGuildman 0x0B78`, `CannotBanGuildman 0x0B79`, `ReqGuildNameAnswer 0x0BA6`.
- **Structs:** 13 guild notifies + `PacketRequestGuildAction` + fightzone request/response + `killer_guild[21]`/`killer_rank` on the EK reward + `guild_name`/`guild_rank`/`fightzone_number` on char-init. Constants `GuildNameLen 21`, `MaxGuildsmen 128`.

## Appendix B — Original defect ledger disposition (35 items, condensed)

| Disposition | Items |
|---|---|
| **Fix in rebuild** | party `iIndex[9]`-vs-100 overrun class; `m_stPartyInfo` sized by clients but indexed by party id; stale `party_id` resurrection; guild-broadcast leak to `"NONE"` members; TP/construct LRU `[i]`-vs-`[iIndex]` ×4; per-guild build counter never reset; heldenian starter `!=`/`&&` logic + unbraced `for`; `RequestHeldenianTeleport` CONFIRM-overwrite + uninit reply buffer; `RemoveOccupyFlags` first-hit return; tower-side read through the wrong array; `HeldenianGUID.Txt` missing newline; heldenian winner `exp+WC×1.2` doubling; `HeldenianCount` deaths/flags mismatch + double "Aresden Flags" label; reward-forfeit-on-relog (crusade side already fixed by our persistence; do the same for heldenian participation); `/summonguild` checks-100k-deducts-50k (if that command returns); duplicate `AdminOrder_SummonGuild` |
| **Moot by design** (SQLite/modern rails) | `[GUILDSMAN]` never written / no guild-file reader; guild file as liveness check; fightzone reservation stored by client handle; PartyOperation 5-hop byte round-trip; in-process GSM stubs |
| **Already fixed in our tree** | slate ×3 loop-variable cascade (ours applies per-member); dead `CalculateGuildEffect` / empty `UserCommand_DissmissGuild` / stub `GuildNotifyHandler` (gone with the gut) |
| **Decisions** | constructor structures free vs priced (D9); party full-party double (D7); occupy-flag rank gate (D10); EK price text (D4) — *resolved 2026-07-29: D9 charge the advertised prices; D4 12 EK + fix the client text; D10 stays ungated; D7 deferred to the Party design session* |
| **Not carried** (original-only) | client/server ID collisions `0x0A25`/`0x0EA03206` (do not re-create); Snoopy client TP-list protocol without server counterpart; party UI "level-ratio" copy vs even-split reality (fix the copy when touched) |
