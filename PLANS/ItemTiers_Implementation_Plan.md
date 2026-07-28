# Item Tiers — v1 Implementation Plan

Prepared 2026-07-27. This is the build plan for the locked design contract `PLANS/ItemTiers_Plan.md` (v1, spec-locked 2026-07-27). Architecture decision: `docs/adr/0002-tiered-item-modifiers.md`. Vocabulary: `CONTEXT.md` (Item Tiers section) — used exactly here. Decision trail: wayfinder map [#22](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/22); code survey [#23](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/23).

**The design is LOCKED.** This plan sequences the work; it does not reopen decisions. Anything the spec deliberately left open is in "Implementation-time picks"; genuine gaps found while planning are in "Open questions" and need an owner answer before the affected cycle, not before starting.

## Ground rules

- **Workflow**: every step is a `bak.py guard → edit → /simplify → git diff verify → build → bak.py commit` cycle (Mode 1). Cycles in Phases 1–3 will regularly guard more than 9 files — that is expected for a protocol reshape and stays Mode 1 (hand edits); Mode 2 scripts only where flagged. One logical change per cycle. Update `CHANGELOG.md` after every commit.
- **Session context**: maintain `session_context.md` in the memory directory across the whole effort (current phase/cycle, guard ids, open items). Delete when v1 ships.
- **Versioning**: exactly ONE Compatibility bump ships — `0.7.0-alpha → 0.8.0-alpha`, edited in `Sources/version.cfg` in Phase 1, cycle 1-B (the first wire-visible cycle). No further Compatibility changes in any later phase. Client/Server identity bumps happen once, at Phase 5 deploy. Never edit generated version files; never pass `--increment-version`.
- **Deploy discipline**: nothing deploys to the live Debian server until Phase 5. Client and server deploy together as one batch. During Phases 1–4 the dev world runs local only.
- **Cross-platform**: all new code follows CLAUDE.md rules (no Win32 APIs, `StringCompat.h`, headers include what they use, case-sensitive includes). Windows build per cycle; **both Linux build scripts are the phase-exit gate** (`Sources/build_server_linux.sh`, `Sources/build_client_linux.sh`).
- **The code/data line** (spec §2): a balance change never needs a build; a new mechanic always does. Every knob this plan introduces lands in `gamedata.db`.
- **Naming**: new code is `snake_case` C++20 per `CODING_STANDARDS.md`; new files follow the existing PascalCase filename convention (`RollStrategy.h`, not `roll_strategy.h`). "Tier" in code and data means item Tier only — the drop pipeline's rungs are Drop stages after Phase 0.

## Code-survey re-verification (2026-07-27)

Ticket #23's file:line map was re-verified against today's working tree. **All load-bearing anchors hold**; use these verified refs:

| Fact | Verified location |
|---|---|
| `item_instance_data` — 6 positional attribute bytes (`custom_made`, `prefix_type/value`, `secondary_type/value`, `enchant_bonus`) inside the full instance struct (count, touch effects, durability, color) | `Sources/Dependencies/Shared/Item/ItemInstanceData.h:12-59` |
| Positional enums `AttributePrefixType` / `SecondaryEffectType` | `Sources/Dependencies/Shared/Item/ItemAttributes.h:18`, `:39` |
| Duck-typed `copy_attributes_to` / `load_attributes_from` — 29 uses across 9 files (Server `Game.cpp` 9, `ItemManager.cpp` 5, Client `NetworkMessages_Items.cpp` 5, `NetworkMessages_Bank.cpp` 2, `TradingPostStore.cpp` 2, others) | `Item.h`, grep-verified |
| `[16]` multiplier/min/max lookup arrays | `Sources/Server/Game.h:582-587`, filled by `build_multiplier_lookup` (`Game.cpp:588` decl) |
| Drop pipeline: `BASE_PRIMARY_DROP_CHANCE = 1000`; `npc_dead_item_generator`; stage naming `tierEntries[3]` / `roll_drop_table_item(table, tier, …)` | `EntityManager.cpp:2599`, `:2614`, `:2750`, `Game.h:151`, `GameConfigSqliteStore.cpp:1338` |
| Legacy roll: `generate_item_attributes` with hardcoded weight tables; `roll_attribute_value` 13-step ladder; MC/CC `v/2` halving (dead zero); nibble clamp to 15 | `ItemManager.cpp:4792`, `:4769`, `:4874`, `:4946-4948` |
| Post-roll base-item mutation (Agile/Light/Strong/Ancient) | `ItemManager.cpp:4731` `adjust_rare_item_value` |
| Equip aggregation choke point | `ItemManager.cpp:3091` `calc_total_item_effect` |
| Magic Absorb sum-clamp 80 | `ItemManager.cpp:3327` (`m_add_abs_magical_defense`) |
| Physical Absorb per-hit-location clamp 80 | `CombatManager.cpp:2887-2944` |
| CC trigger `dice(1,100) <=` (drift: spec wants strict `<`) | `CombatManager.cpp:623`, `:1109`, `:3119` |
| Enchant handler; +15 cap drift; Ancient special-casing | `ItemManager.cpp:5623` `request_item_upgrade_handler`, cap at `:5637`, Ancient at `:5831`, Strong endurance at `:5884`/`:5970` |
| Crafted-item nibble decode (`CBuildItem::m_attribute`) | `ItemManager.cpp:4651-4654` |
| Move-check flat-200 ms disconnect (drift) | `Sources/Server/Game.cpp:12149` |
| Cast anti-cheat 1500 ms floor | `MagicManager.cpp:2667-2690` `check_client_magic_frequency` |
| Cast animation 16×88 ms; Walk (`Move`) 8×70=560 ms; Run 8×39=312 ms | `Sources/Client/Player.h:38-47` |
| Swing-speed STR formula (Agile acceptance test) | `Sources/Dependencies/Shared/Game/SharedCalculations.h:152` `attack_delay` |
| Config-cache slot 7 (attribute-type tables), hash + negotiation | `Server/Game.cpp:2465` `compute_config_hashes`, `:2942-3000`, `:1876` |
| `reload` console command | `Sources/Server/CmdReload.h` (targets: items/magic/skills/npcs/shops/config/formulas/all) |
| Status broadcast struct (venue for the two speed fields) | `Sources/Dependencies/Shared/Entity/PlayerStatusData.h` |
| `is_special_item` roster | `Sources/Dependencies/Shared/Item/ItemEnums.h:400` |
| Persistence DDL (prefix/secondary columns ×2 table pairs) | `AccountSqliteStore.cpp:164/259/441/467` regions; `TradingPostStore.cpp` |
| Item-carrying packets | `PacketEvent.h`, `PacketMap.h`, `PacketNotify.h`, `PacketResponse.h`, `PacketTradingPost.h`, `PacketAttributeTypeConfig.h` (grep-verified: the five instance-carrying headers all reference `prefix_type`) |

**Live `gamedata.db` state (confirmed today):** `attribute_pools` = 3 rows ("Standard Melee Weapon"/"Standard Armor"/"Wand", all `secondary_chance` 40), `attribute_pool_entries` = 29 rows, `items.attribute_pool_id` populated on **243 of 546** items; **zero C++ references** to any of it. `npc_configs` (117 rows) has **no loot-grade column**. `meta` holds only `schema_version = 7`. `drop_entries.tier` takes values {1, 2} (the Drop stages).

**Interpretation note (encoding, §12):** the spec's "15-byte all-`uint8_t` POD" is the *attribute portion* of `item_instance_data` — `custom_made`, `tier`, `modifier modifiers[4]` (type/value/value2), `enchant_bonus` = 15 bytes. The struct's non-attribute instance fields (count, touch/special effects, durability, item_color) are untouched. The positional `prefix_*`/`secondary_*` fields die; nothing else moves.

---

## Phase 0 — Groundwork (no wire changes)

Small, self-contained cycles that de-risk Phase 1. Server-only; Compatibility untouched.

**Cycle 0-A — Drop stage rename** (spec §8 naming mandate). Rename the drop-table code's "tier" vocabulary to **stage**: `DropTable::tierEntries` → `stage_entries` (`Game.h:151`), `roll_drop_table_item(table, tier, …)` → `(table, stage, …)` (`EntityManager.h:295`, `EntityManager.cpp:2750`), loader at `GameConfigSqliteStore.cpp:1338`, call sites `EntityManager.cpp:2663/:2699/:2737`. The `drop_entries.tier` **DB column keeps its name** (see Picks). Touch: `Game.h`, `EntityManager.h`, `EntityManager.cpp`, `GameConfigSqliteStore.cpp`.

**Cycle 0-B — Accessor funnel.** Route every direct `m_instance.prefix_type`-family access in Server and Client through the existing `CItem` accessors / small helpers, so Phase 1's struct reshape has few mechanical touch points. Candidate Mode 2 justification: this touches 10+ files with one mechanical pattern (direct field access → accessor); if a script is used, follow the full `--dry-run`/`--verify` standard. Touch: `Server/ItemManager.cpp`, `Server/Game.cpp`, `Server/EntityManager.cpp`, `Client/ItemNameFormatter.cpp`, `Client/DialogBox_*.cpp`, others found by `grep.py "m_instance.(prefix|secondary)" -b`.

Verification: Windows `-Target All`; Linux server + client scripts; `git diff` review (pure rename/funnel — zero logic change).

---

## Phase 1 — Shared encoding + Unified modifier IDs + catalog/tier-presentation packets

**The atomic wire batch (spec §12/§14.1). Compatibility 0.7.0 → 0.8.0 — the ONLY bump.** Everything in this phase reshapes protocol or storage; client and server must both build at every cycle exit, and from cycle 1-B on, mixed old/new client-server pairs are rejected pre-auth by the version gate (that is the point). All later phases are behavior inside this envelope — **every new wire field ships here, even ones whose mechanics arrive in Phase 3/4** (they ride as zeros until then).

**Cycle 1-A — Unified modifier-ID space (Shared).** New Shared header (e.g. `Item/ModifierIds.h`): the flat `uint8_t` Unified modifier ID namespace (0 = empty, renumbered from scratch — assignment table is a Pick), the code-side `effect_id` enum the catalog rows key against, and the `tier_item_class` enum (melee weapon / bow / wand / body armor / helm / leggings / shield / cape — derivation from item config mirrors the route switch at `ItemManager.cpp:5643-5662`). Compile-only addition; old enums still present.

**Cycle 1-B — The struct reshape + version bump.** `item_instance_data` attribute portion becomes the 15-byte POD (spec §12 verbatim: `custom_made`, `tier`, `modifiers[4]{type,value,value2}`, `enchant_bonus`). `AttributePrefixType`/`SecondaryEffectType` die (`ItemAttributes.h`); legacy roll/apply code converts to Unified modifier IDs via a translation table in one place. `CItem` accessors from 0-B reimplement as views over `modifiers[0]`/`[1]` for legacy semantics. The `[16]` arrays and `id < 16` guards (`Game.h:582-587`) die with them (multipliers move to the catalog in 1-D; interim: sized to the unified space). Crafted-item nibble decode (`ItemManager.cpp:4651-4654`) and the GM tester nibble path (`Server/Game.cpp:6643` region) translate nibbles → Unified modifier IDs at the boundary. Nibble value-15 clamp dies here (`ItemManager.cpp:4946-4948`) — §13 drift #7. **Edit `Sources/version.cfg` Compatibility → 0.8.0 in this cycle.** Touch: Shared item headers, `Server/ItemManager.cpp`, `Server/Game.cpp`, `version.cfg`, plus whatever 0-B left pointing at accessors.

**Cycle 1-C — Packets reshape.** All ~11 item-carrying packets embed the new POD; `copy_attributes_to`/`load_attributes_from` retire (all 29 uses). Files: `PacketEvent.h` (ground item; server encode at `send_ground_item_event`), `PacketMap.h` (map sync), `PacketNotify.h` (obtain / to-bank / attribute-change / gizon-change / exchange), `PacketResponse.h` (inventory + bank lists), `PacketTradingPost.h` (`TpItemBrief`/full). Client decode sites: `NetworkMessages_Items.cpp`, `NetworkMessages_Bank.cpp`, `DialogBox_TradingPost.cpp`. **Status broadcast**: add the two §5 speed fields (cast-reduction %, total move-speed %) to `PlayerStatusData.h` now, sent as 0 until Phase 3. Touch: 6 Shared packet headers + `PlayerStatusData.h`, server send sites (`Game.cpp`, `ItemManager.cpp`, `EntityManager.cpp`, `TradingPostManager.cpp`), client decode sites.

**Cycle 1-D — Unified catalog + tier-presentation packets.** `PacketModifierCatalogEntry` replaces `PacketAttributePrefixTypeEntry`/`…SecondaryTypeEntry` (`PacketAttributeTypeConfig.h` dies): per entry `modifier_id, display_name, effect_label, effect_format, multiplier, bucket_id, min_tier, marquee`. The Tier presentation table (names + RGB colors + name template) rides the same config-cache slot (`m_config_hash[7]`, `Server/Game.cpp:2942-3000`; negotiation `:1876`; client apply `Client/Game.cpp` catalog handler). The Item system mode flag replicates to the client alongside (mode-aware rendering needs it). Server side sends from the new data layer's tables once Phase 2 lands; interim source is a hardcoded shim table equal to today's legacy entries (deleted in Phase 2). Touch: new Shared packet header, `PacketConfigCache.h`, `Server/Game.cpp` (hash + send), `Client/Game.cpp` (receive + cache), `Client/LocalCacheManager` files.

**Cycle 1-E — Persistence DDL.** `character_items`, `character_bank_items` (`AccountSqliteStore.cpp/h`), and trading-post escrow (`TradingPostStore.cpp/h`) replace prefix/secondary columns with the 13 flat columns `tier, mod1_type, mod1_value, mod1_value2, … mod4_value2`. **Fresh start: straight DDL swap, no data migration** (spec §12) — dev worlds recreate; bump the affected store schema_versions so stale dev DBs fail loudly rather than half-load. Touch: `AccountSqliteStore.cpp/h`, `TradingPostStore.cpp/h`, `LoginServer.cpp` (row structs / save-load), `Server/Game.cpp:4283`-region deserialization.

**Phase-1 exit gate:** Windows both targets; Linux both scripts; manual smoke in **legacy mode**: login → kill mobs until a prefixed drop → pickup → tooltip labels correct (now catalog-fed) → bank round-trip → exchange → TP list/browse/finalize → enchant attempt — behavior byte-identical to pre-phase (legacy Roll strategy semantics unchanged; only the envelope moved).

---

## Phase 2 — Server data layer

Tiered schema DDL, legacy `attribute_pools` wiring, fail-fast boot validator, transactional reload, and the seeding scripts. Server-only; no wire changes (the catalog packet exists since 1-D and now reads real tables).

**Cycle 2-A — Tiered schema DDL** in `GameConfigSqliteStore.cpp` per the `EnsureGameConfigDatabase` `CREATE TABLE IF NOT EXISTS` pattern (`:142`), seeded empty, `schema_version` 7 → 8. Full DDL draft below. Also: `npc_configs` gains `loot_grade` (HasColumn/ALTER, default 2 standard) and `meta` gains `item_system` (INSERT OR IGNORE `legacy`). Touch: `GameConfigSqliteStore.cpp/h`.

**Cycle 2-B — Config load + in-memory model.** New server store/loader (e.g. `TierConfigStore.{h,cpp}` + a `tier_config` aggregate on `CGame`) loading all tiered tables + the legacy pool tables into immutable in-memory structs. Catalog packet (1-D) now sourced from these tables; the shim dies. Touch: new files, `Server/Game.h/.cpp`, `GameConfigSqliteStore.cpp`.

**Cycle 2-C — Legacy `attribute_pools` wiring** (spec §2: finish the half-landed refactor). `generate_item_attributes` (`ItemManager.cpp:4792`) drops its three hardcoded weight tables and reads `attribute_pools`/`attribute_pool_entries` via `items.attribute_pool_id`; `secondary_chance` comes from the pool row (the 40% at `:4830`/`:4880` gate). Weights in the DB are already identical to the C++ tables, so behavior is unchanged — verify with a roll-distribution smoke (Pick: GM bulk-roll command or offline harness). `attribute_pools.secondary_chance` stays legacy-only (§8). Touch: `ItemManager.cpp`, loader files.

**Cycle 2-D — Fail-fast boot validator + reload.** Validator over BOTH strategies' datasets (spec §2 list: unknown effect id, empty required Bucket, min>max Band, Aggregate cap below Band floor, dangling refs, duplicate bucket assignment, Unified-ID collisions, non-divisible band/multiplier, missing pool assignment on droppable gear in legacy, **tier-eligible gear classes in any stage-2 drop table** — the §8 stage-2 rule, *invariant tiered ⇒ count == tier* on load paths). Any error blocks boot with table/row precision. Empty tiered tables failing validation **only in tiered mode** is the intended unconfigured-world guard. All tier + legacy-pool tables join `CmdReload` (new target `tiers`, included in `all`): load candidate → validate → swap or reject leaving running config untouched. `item_system` itself is restart-only — excluded from reload by design. Touch: validator file(s), `CmdReload.h/.cpp`, `Game.cpp` reload plumbing.

**Cycle 2-E — Seeding scripts + data entry** (Python, no C++). See "Seeding plan" below. Ends with: validator-green `gamedata.db` in both modes, grade proposal reviewed and applied, boss stage-1 curation applied, stage-2 tables compliant, Cape 402 placed.

**Phase-2 exit gate:** Windows + Linux builds; boot in legacy mode (validator green); boot in tiered mode on the seeded DB (validator green); deliberately corrupt one row → boot blocked with precise report; `reload tiers` with a bad candidate → rejected, server keeps running config.

---

## Phase 3 — Roll-strategy seam, effect application, marquee mechanics, drift fixes

Server-heavy; client work only where a mechanic's animation contract requires none (all wire fields already exist).

**Cycle 3-A — The seam.** `RollStrategy.h` interface + `LegacyRollStrategy` (extraction of today's `generate_item_attributes` path, now pool-driven) + `TieredRollStrategy` skeleton; selected once at boot from `meta.item_system`. `spawn_npc_drop_item` calls the seam. Touch: new files, `ItemManager.cpp/h`, `EntityManager.cpp`, `Game.h`.

**Cycle 3-B — Tiered roll + drop pipeline.** In tiered mode: First drop only (spec §8) — when the stage-1 table yields tier-eligible gear (§1 scope classes, **excluding the `is_special_item` roster explicitly**), roll Loot grade → Tier (grade's weight row; staircase zero-weights are the gate), then Bucket-law modifier selection (one per Bucket, per-class eligibility + weights, min-tier ladder), then Band/Tier-curve value rolls (piecewise-linear CDF through p50/p90/p99 + explicit `cap_chance`; pair halves roll independently; ALL STATS one shared N). `BASE_PRIMARY_DROP_CHANCE` replaced by the grade's `first_drop_chance` in tiered mode (`EntityManager.cpp:2599/:2631`; gold-preempt ordering unchanged; `m_primary_drop_rate` multiplier stays on top). No Plain drops; Common floor; tier byte stored; *tiered ⇒ count == tier* enforced at roll time. Second drop path untouched. Touch: `TieredRollStrategy.cpp`, `EntityManager.cpp`, `ItemManager.cpp`.

**Cycle 3-C — Effect application.** `calc_total_item_effect` (`ItemManager.cpp:3091`) generalizes: iterate `modifiers[4]` by Unified modifier ID → effect id; **Aggregate caps** — sum same-modifier lines across equipped pieces, hard-clamp at `aggregate_cap` from the catalog (whole catalog populated; angels/unique built-ins/hero armor exempt — spec §6.5). Stat-touchers (Agile/Light/Strong/Ancient durability) compute **at equip time from modifier data**; `adjust_rare_item_value`'s stored-stat mutation (`ItemManager.cpp:4731`) dies for tiered items — stored item stats never mutate (legacy items keep today's behavior). Acceptance tests from spec §4.5: Light Full Helm equippable by low-STR mage (STR gate uses effective weight); Agile full-speed swings at lower STR (`SharedCalculations.h:152`). Touch: `ItemManager.cpp`, possibly `Client.h` accumulators.

**Cycle 3-D — Marquee mechanics** (spec §5). Sunder + Bleed proc-on-hit debuffs (`CombatManager.cpp` hit resolution + `StatusEffectManager.cpp`; fixed constants from `tier_settings`; refresh semantics; Bleed ignores poison resist, not Cure-curable, co-rolls with Poisoning; victims get notify messages, no broadcast bits); MP/SP drain always-on per landed hit; cast-time reduction — server computes per-player cast floor `1500 × (1 − r)` from the equipped wand (`MagicManager.cpp:2667` check generalizes) and publishes r in the status broadcast; movement speed — locomotion only, multiplicative composition (`duration = base × haste(0.7) × frozen(1.25) × (1 − speed_total)`), per-item 10% / 20% aggregate hard-clamp at equip recalc, published in the status broadcast. Touch: `CombatManager.cpp`, `StatusEffectManager.cpp`, `MagicManager.cpp`, `ItemManager.cpp`, `EntityManager.cpp` (NPC victims).

**Cycle 3-E — Anti-cheat posture + move check** (§5, §13 drift #6). Per-player legal move floor = expected tile time × 0.85 grace; the flat-200 ms disconnect at `Game.cpp:12149` **downgrades to log-only**, logs carry expected floor / observed gap / speed%-haste-frozen state; per-check posture is the `tier_settings` knob (`log` | `disconnect`), launch `log` everywhere including the cast floor. Touch: `Server/Game.cpp`, `MagicManager.cpp`.

**Cycle 3-F — Drift fixes #1–#5** (both modes): (1) enchant goes data-driven — `request_item_upgrade_handler` (`ItemManager.cpp:5623`) reads `enchant_categories`/`enchant_steps`; **derive the enchant category from `derive_tier_item_class` (ModifierIds.h) via a class→category lookup** so gear classification has exactly one implementation (the handler's hand-rolled `upgrade_route` if/else chain dies here): caps +7 (weapons/shields/armor/wands) / +10 crafted, safe-through-+3 destroy rule, endurance growth kept; the Ancient-prefix upgrade block (`:5831`) dies in tiered mode, legacy keeps it; majestic paths untouched. (2) MC/CC per-item roll `(v+1)/2` min 1 (`:4874` halving fix — applies to the legacy pool path). (3) CC trigger strict `<` (`CombatManager.cpp:623/:1109/:3119`). (4) Righteous restored — target-side `damage += min(V, |target_rep|/10)` for negative-rep players, valued from the roll (both modes; the cosmetic-only prefix gains its combat hook). (5) Kloness attacker term retail-strict `min(5, rating/100)`, no floor; necklace 859 `min(5, |target_rep|/20)`; wands apply both terms to spells. Touch: `ItemManager.cpp`, `CombatManager.cpp`, `MagicManager.cpp` (wand spell path).

**Cycle 3-G — GM item creator (server) + minting logs.** Mode-aware creation handler: tiered mode drives tier + modifier slots; **structural legality server-enforced** (one per Bucket, min-tiers, Bands, count == tier); minting logged for economy audits (`ItemLogAction` addition, `trade`/`item` channel per existing pattern). Touch: `Server/Game.cpp` GM handlers, `ItemManager.cpp`.

**Phase-3 exit gate:** Windows + Linux builds (both targets — status broadcast consumers changed); targeted manual checks per cycle (below feeds the Phase 5 matrix); log-only posture confirmed by inspecting security-channel logs while moving/casting at legal speeds with rolled gear.

---

## Phase 4 — Client presentation

All client; no wire changes (fields and catalog data all exist since Phase 1).

**Cycle 4-A — Data-driven `ItemNameFormatter`.** Labels/formats from the replicated catalog (`display_name`, `effect_label`, `effect_format` — two-value for pairs); hardcoded strings demote to missing-data fallback; up to four standalone modifier lines in Bucket `sort_order`; `effect_category` inline-merge behavior preserved. Tier word leads the name via the replicated name template; whole name renders in Tier color everywhere names render; name-color picker gains the tier branch keyed off the tier byte. Touch: `ItemNameFormatter.cpp/h`, `Screen_OnGame.cpp` tooltip region, `ItemTooltip.cpp/h`.

**Cycle 4-B — World presentation.** Ground light-beam in Tier color on every tiered drop (Common faint neutral) keyed off the tier byte in ground-item/map packets; pickup line "You got a {name}." with name in tier color; **sprites never tinted in tiered mode** (legacy keeps prefix dye-tints — gate the tint channel on the replicated Item system mode). No broadcasts, no banners. Touch: `Screen_OnGame.DrawObjects.cpp` / effect render files (beam venue is a Pick), pickup notify handler.

**Cycle 4-C — Status-broadcast animation scaling.** Cast animation frame time × (1 − r) from the broadcast cast-reduction field (`Player.h` `Magic` 16×88 ms); Walk/Run frame time × (1 − speed_total) (`Move` 8×70, `Run` 8×39); AttackMove/DamageMove untouched; applies to self and remote entities. Touch: `Player.h/.cpp` animation timing, entity anim update sites.

**Cycle 4-D — GM creator dialog.** `DialogBox_ItemCreator.cpp` mode-aware: tiered mode drives tier + modifier pickers from replicated catalog data (no rarity constraints within legality — server enforces structure). Touch: `DialogBox_ItemCreator.cpp`.

**Phase-4 exit gate:** Windows + Linux client builds; visual pass in both modes (legacy: tints + old-look names; tiered: tier names/colors, beams, tooltips, scaled animations observed from a second client).

---

## Phase 5 — Deploy batch + final test matrix

**Cycle 5-A — Version identity + changelog.** Bump Client and Server identity versions in `version.cfg` (Compatibility already 0.8.0 since Phase 1 — do not touch). `CHANGELOG.md` roll-up entry.

**Cycle 5-B — Deploy as one batch** (single-bump rule, §12/§14.5): server binary + client build + `gamedata.db` (tiered tables seeded, `item_system` set for the test world) deploy together to the Debian server per the standing redeploy steps; **sync `gamedata.db` deliberately** (the DB is the balance source of truth — same lesson as the Apocalypse deploy). Old clients are version-gated out — expected.

**Cycle 5-C — Manual test matrix.** Run in BOTH modes (mode switch = edit `meta.item_system` + restart; same world DB across the switch — Untiered and tiered items must coexist).

*Legacy mode (regression + drift fixes):*
- [ ] Drops: prefix + 40% secondary as today; distribution sanity vs pool weights; prefix dye-tints render.
- [ ] Tooltip/name labels correct from catalog data (no hardcoded-string dependency).
- [ ] MC/CC rolls never 0, range 1..7; CC triggers strictly below sum; MC mana gain formula.
- [ ] Righteous weapon adds target-side rep damage vs a negative-rep player; Kloness attacker term capped 5, no floor; Kloness wand applies both terms to spells.
- [ ] Enchant: +3 fail eats stone only; +4 fail destroys; caps +7 (all categories) / +10 crafted; Ancient block still active in legacy; endurance growth on success.
- [ ] Move/cast at legal speeds → zero security-channel violations; deliberate speed injection → log line with floor/gap/state, **no disconnect**.
- [ ] Bank, exchange, Trading Post list/offer/finalize, character save/load — instance data round-trips (tier byte 0).
- [ ] GM creator legacy behavior.

*Tiered mode:*
- [ ] Monster gear drops always tiered, Common floor; no Plain drops; shop/blacksmith/reward gear Plain.
- [ ] Loot-grade gating: vermin → Common only; standard/veteran/elite/boss unlock Rare/Epic/Legendary per staircase; boss always attempts stage-1 gear roll (gold preempt aside).
- [ ] Second drops never tiered (boss uniques/scatter unchanged); validator rejects a test stage-2 gear row.
- [ ] Bucket law: no item with two modifiers from one Bucket; min-tier ladder (pairs Epic+, ALL STATS + Marquee Legendary-only); pairs roll two values; ALL STATS one shared N.
- [ ] Rolled values within Bands; curve sanity over a large GM-minted sample (P50s near table); per-tier windows absent (full band at every tier).
- [ ] Aggregate caps: stack same-modifier gear past cap → clamped at equip recalc; attribute +20; move speed 20% total; MC 13 / CC 20; angel/unique/hero-armor exemptions.
- [ ] Stat-touchers: Light Full Helm equips on low-STR mage; Agile swing test; Strong/Ancient durability derived, stored stats unmutated.
- [ ] Marquee: Sunder proc + refresh + −50 DR expiry; Bleed ticks through poison resist, not Cure-curable, co-exists with Poisoning; MP/SP drain per hit; cast reduction visibly faster + floor honored; cape+leggings speed stacking clamps at 20%; all four work on players and NPCs; victims get notify lines; remote client renders scaled animations.
- [ ] Presentation: tier word + color in name everywhere; tooltip ≤4 modifier lines, layout unchanged; beam per tier incl. faint Common; pickup line colored; **no sprite tint**.
- [ ] Enchant on a tiered item ("+7 Legendary" legal; Ancient block absent).
- [ ] Trading Post: tiered items list/render/trade; escrow round-trips all 13 columns.
- [ ] GM creator: legal mint OK; illegal (dup bucket, wrong min-tier, out-of-band, count ≠ tier) rejected server-side; minting logged.
- [ ] `reload tiers` live-applies a weight change without restart; bad candidate rejected; `item_system` edit requires restart.
- [ ] Mode switch both directions on one world DB; legacy items (Untiered) and tiered items coexist and render correctly in both modes.

---

## Tiered schema — DDL draft (Cycle 2-A)

Server-owned, `CREATE TABLE IF NOT EXISTS`, seeded empty (empty-in-tiered-mode = intended unconfigured-world guard). Stored roll values are the `uint8_t` wire bytes; `band_min`/`band_max` are **display units** (spec §6.1); validator enforces `band % multiplier == 0` so stored byte = display ÷ multiplier (today's convention).

```sql
-- meta: INSERT OR IGNORE INTO meta VALUES ('item_system', 'legacy');

CREATE TABLE IF NOT EXISTS tier_buckets (
	bucket_id   INTEGER PRIMARY KEY,
	name        TEXT NOT NULL UNIQUE,      -- DAMAGE, PRECISION, HANDLING, ECONOMY,
	                                       -- SET_AXIS, COMBAT_UTILITY, CASTING, ATTRIBUTES, MARQUEE
	sort_order  INTEGER NOT NULL           -- stable tooltip ordering (spec §12)
);

CREATE TABLE IF NOT EXISTS modifier_catalog (
	modifier_id    INTEGER PRIMARY KEY,    -- Unified modifier ID, 1..255 (0 reserved = empty)
	name           TEXT NOT NULL UNIQUE,   -- internal slug
	display_name   TEXT NOT NULL,          -- replicated (client label)
	effect_label   TEXT NOT NULL,          -- replicated (tooltip line)
	effect_format  TEXT NOT NULL,          -- replicated; two-value format for pairs
	effect_id      INTEGER NOT NULL,       -- code-side behavior key (enum in ModifierIds.h)
	effect_param1  INTEGER NOT NULL DEFAULT 0,  -- e.g. which attribute; pair halves (a)
	effect_param2  INTEGER NOT NULL DEFAULT 0,  -- pair halves (b)
	bucket_id      INTEGER NOT NULL REFERENCES tier_buckets(bucket_id),
	multiplier     INTEGER NOT NULL DEFAULT 1,  -- display = stored byte x multiplier
	min_tier       INTEGER NOT NULL DEFAULT 1,  -- 1..4 (consulted at drop-roll only)
	marquee        INTEGER NOT NULL DEFAULT 0,
	band_min       INTEGER NOT NULL,       -- display units (spec §7 bands verbatim)
	band_max       INTEGER NOT NULL,
	aggregate_cap  INTEGER NOT NULL,       -- display units; band_max for item-local formality rows
	-- per-tier windows: NULL = full band (spec §6.1; none used at launch, §7)
	window_min_t1 INTEGER, window_max_t1 INTEGER,
	window_min_t2 INTEGER, window_max_t2 INTEGER,
	window_min_t3 INTEGER, window_max_t3 INTEGER,
	window_min_t4 INTEGER, window_max_t4 INTEGER
);

CREATE TABLE IF NOT EXISTS modifier_eligibility (   -- per-class membership + weights (spec §4)
	modifier_id INTEGER NOT NULL REFERENCES modifier_catalog(modifier_id),
	item_class  INTEGER NOT NULL,          -- tier_item_class enum (melee/bow/wand/body/helm/leggings/shield/cape)
	weight      INTEGER NOT NULL,          -- selection weight within the class's bucket
	PRIMARY KEY (modifier_id, item_class)
);
-- Data note: Physical Absorb gets NO cape row (spec §4 eligibility note).

CREATE TABLE IF NOT EXISTS bucket_class_rules (     -- mandatory-bucket flag; OFF everywhere in v1 (§4.7)
	bucket_id  INTEGER NOT NULL REFERENCES tier_buckets(bucket_id),
	item_class INTEGER NOT NULL,
	mandatory  INTEGER NOT NULL DEFAULT 0,
	PRIMARY KEY (bucket_id, item_class)
);

CREATE TABLE IF NOT EXISTS tier_curves (            -- four global curves + future contexts (§6.2, §7)
	curve_id       INTEGER PRIMARY KEY,
	name           TEXT NOT NULL UNIQUE,   -- 'common','rare','epic','legendary' (later: 'unique_forge','reroll')
	p50            REAL NOT NULL,          -- normalized band position anchors
	p90            REAL NOT NULL,
	p99            REAL NOT NULL,
	cap_chance_den INTEGER NOT NULL        -- P(cap) = 1 / cap_chance_den (explicit, §7)
);

CREATE TABLE IF NOT EXISTS tier_curve_overrides (   -- sparse, tails only; bodies inherit tier default (§7)
	modifier_id    INTEGER NOT NULL REFERENCES modifier_catalog(modifier_id),
	tier           INTEGER NOT NULL,       -- 1..4
	cap_chance_den INTEGER NOT NULL,
	PRIMARY KEY (modifier_id, tier)
);

CREATE TABLE IF NOT EXISTS loot_grades (            -- five rows; the whole drop-economics dial (§8)
	grade             INTEGER PRIMARY KEY, -- 1..5
	name              TEXT NOT NULL,       -- vermin/standard/veteran/elite/boss
	weight_common     INTEGER NOT NULL,    -- out of 10000; zero = staircase hard gate
	weight_rare       INTEGER NOT NULL,
	weight_epic       INTEGER NOT NULL,
	weight_legendary  INTEGER NOT NULL,
	first_drop_chance INTEGER NOT NULL     -- out of 10000; replaces BASE_PRIMARY_DROP_CHANCE in tiered mode
);
-- npc_configs: ADD COLUMN loot_grade INTEGER NOT NULL DEFAULT 2 (HasColumn migration)

CREATE TABLE IF NOT EXISTS enchant_categories (     -- data-driven enchant (§10)
	category             INTEGER PRIMARY KEY,  -- code enum: weapon/shield/armor/wand/crafted_weapon
	name                 TEXT NOT NULL,
	cap                  INTEGER NOT NULL,     -- 7; crafted_weapon 10
	endurance_growth_pct INTEGER NOT NULL      -- 15; crafted 20
);
CREATE TABLE IF NOT EXISTS enchant_steps (
	category        INTEGER NOT NULL REFERENCES enchant_categories(category),
	step            INTEGER NOT NULL,      -- attempting +step
	success_pct     INTEGER NOT NULL,      -- basis points (see Open questions)
	destroy_on_fail INTEGER NOT NULL,      -- 0 for steps 1..3, 1 from step 4 (launch)
	PRIMARY KEY (category, step)
);

CREATE TABLE IF NOT EXISTS tier_presentation (      -- replicated via config-cache slot (§11, §12)
	tier          INTEGER PRIMARY KEY,     -- 1..4
	name          TEXT NOT NULL,           -- Common/Rare/Epic/Legendary (final, player-facing)
	color_r       INTEGER NOT NULL,
	color_g       INTEGER NOT NULL,
	color_b       INTEGER NOT NULL,
	name_template TEXT NOT NULL            -- '{tier} {name}'
);

CREATE TABLE IF NOT EXISTS tier_settings (          -- marquee launch constants + posture knobs (§5)
	key   TEXT PRIMARY KEY,
	value TEXT NOT NULL
);
-- Seeded keys: sunder_defense_delta(-50), sunder_duration_ms(5000),
-- bleed_tick_damage(5), bleed_tick_interval_ms(2000), bleed_duration_ms(8000),
-- cast_check_floor_ms(1500), move_grace_pct(85),
-- posture_move(log), posture_cast(log)
```

Legacy strategy tables (`attribute_pools`, `attribute_pool_entries`, `items.attribute_pool_id`, and the `attribute_prefix_types`/`attribute_secondary_types` display data folded into `modifier_catalog`) are owned by the legacy Roll strategy; each strategy owns its tables (spec §2).

## Seeding plan — `Scripts/seed_item_tiers.py` (Cycle 2-E)

Python, idempotent (transactional DELETE + INSERT per table), targeting `Binaries/Server/gamedata.db`. It does **not** create tables (server owns DDL — run the Debug server once after 2-A to migrate; the script errors clearly if tables are missing). The `Binaries/Server/gameconfigs/*.sql` Navicat snapshots are re-exported by hand afterward, per the Trading Post Phase-4 finding.

1. **Curves + overrides**: the four §7 curve rows and three override rows, verbatim (p50/p90/p99 + `cap_chance_den`: Common 2,000,000 / Rare 500,000 / Epic 100,000 / Legendary 25,000; pair-half Epic 640, Legendary 320; ALL STATS 50,000).
2. **Buckets + catalog + eligibility**: §4's bucket tables (weapons, wands, armor family) → `tier_buckets`, `modifier_catalog` (Bands, multipliers, min-tiers, marquee flags, Aggregate caps — §7's bands table and aggregate-caps table verbatim, incl. formality rows at band max and the 546/80/80/13/20/20 caps), `modifier_eligibility` per class (Physical Absorb excluded from capes). The **243 curated `attribute_pool` item→pool assignments are mined as reference input** for tiered bucket-membership weights (§2). All window columns left NULL.
3. **Grade weights**: §8's five-row table verbatim into `loot_grades`, `first_drop_chance` = 1000 for grades 1–4, 10000 for boss.
4. **Enchant**: categories (+7 ×4, crafted +10; endurance 15/20) and per-step rows — safe-through-+3 destroy flags; success % per Open question Q1.
5. **Presentation**: the four rows with the locked palette (255,255,255 / 128,192,128 / 150,160,225 / 255,176,16) and template `{tier} {name}`.
6. **Marquee constants + posture** into `tier_settings` (values above).
7. **Loot-grade seeding (117 mobs)**: `--grade-report` computes a proposed grade per `npc_configs` row mechanically from exp values (thresholds chosen at implementation; bosses recognized explicitly — Abaddon, Tiger Worm, Wyvern, Fire Wyvern, Helclaw per the Apocalypse roster) → `Scripts/output/loot_grade_proposal.csv`. **One hand-adjust pass by the owner**, then `--apply-grades <csv>` writes `npc_configs.loot_grade`. Balance forever after = tuning 5 `loot_grades` rows.
8. **Boss stage-1 gear curation** (§8 boss-rate note: boss first-drop tables are ~92% potions, authored for the scatter era): `--boss-stage1-report` dumps each boss drop table's stage-1 composition with gear share; owner curates gear entries (data entry); applied as `drop_entries` edits. Deliberately no rate loosening — ratchet law; the levers are data.
9. **Stage-2 compliance sweep**: `--stage2-scan` lists every tier-eligible gear class in any stage-2 table (the boot validator's rule, previewed); ordinary gear moves to stage 1 or out; uniques/specials/consumables stay.
10. **Generic Cape (402)**: add restricted-mob `drop_entries` rows (Plate Mail pattern) for the mobs picked at implementation (Picks).
11. **Legacy pool completion audit**: `--legacy-pool-audit` lists droppable §1-scope items with `attribute_pool_id` NULL (303 items currently unassigned overall, most legitimately non-droppable); assign the gaps so the legacy validator passes.

## Implementation-time picks (open by design — decide during the cycle, record here)

- **Unified modifier ID number assignments** — the renumbered-from-scratch table (spec fixes the namespace, not the numbers). Assign in catalog order, document in `ModifierIds.h`. **PICKED (1-A, 2026-07-27)**: catalog-order block assignment, authoritative table in `Sources/Dependencies/Shared/Item/ModifierIds.h` — 1–5 DAMAGE (sharp, critical, poisoning, righteous, ancient) · 6–7 PRECISION · 8–10 HANDLING (agile, light, strong — one ID each, shared across weapon/armor eligibility) · 11–12 ECONOMY · 13 CASTING (spell_success) · 14–21 SET-AXIS · 22–23 COMBAT UTILITY · 24–29 attribute singles (STR, DEX, MAG, INT, VIT, CHR) · 30–33 pairs (STR&DEX, INT&MAG, VIT&STR, VIT&INT) · 34 ALL STATS · 35–40 MARQUEE (sunder, bleed, mp_drain, sp_drain, cast_time_reduction, move_speed). Effect ids 1–23 mirror legacy enum order (prefix 1–12 minus unused slot 4, then secondary 1–12) to ease the 1-B translation-table review; 24+ are new behaviors (`add_attribute`/`add_attribute_pair`/`add_all_attributes` keyed by a `tier_attribute` param enum whose values match the `hb::shared::net::StatId` wire ids, plus the six marquee behaviors). Effect-id values are arbitrary stable keys — no implied numeric correspondence with the legacy enums; the authoritative legacy→unified translation table is a 1-B deliverable and must also reconcile the third legacy behavior enum, `AddEffectType` (accessory add-effects).
- **Which mobs carry the generic Cape (402)** — restricted-mob roster, Plate Mail pattern (§8).
- **Loot-grade exp thresholds** for the mechanical first pass over the 117 mobs (hand-adjust pass follows regardless).
- **Boss stage-1 gear lists** — which §1-scope gear enters each boss's first-drop table, at what weights.
- **`drop_entries.tier` column name** — kept as-is in 0-A (code says stage, DB column stays `tier`; a rename is cosmetic churn on a live table). Revisit only if it causes real confusion.
- **New server file names** for the seam and data layer (suggested: `RollStrategy.h`, `LegacyRollStrategy.cpp`, `TieredRollStrategy.cpp`, `TierConfigStore.cpp/h`, validator in `TierConfigValidator.cpp` or folded into the store).
- **Ground-beam implementation venue** — which client effect path renders the tier-colored beam (new effect vs existing light-column effect reuse).
- **Roll-distribution verification harness** (2-C, 3-B) — GM bulk-roll command vs offline seeded-RNG harness. **PICKED (2-C, 2026-07-28)**: server console command `rollsmoke <item_id> [count]` (`CmdRollSmoke.cpp`) printing machine-readable `ROLLSMOKE <P|S|C> <id> <value> <count>` histograms, plus a generic headless mode in `Wmain.cpp` — `Server --<console command> [args...]` runs any registered console command after full init and exits before any socket binds (safe next to a live server). Parity methodology used for 2-C, reusable for 3-B: capture before/after histograms and compare against analytic expectations (`smoke_check.py` pattern) — note MSVC's RAND_MAX=32767 makes `rand() % N` visibly biased and table-order-dependent on Windows dev builds; Linux (RAND_MAX=2^31−1, the deployment platform) matches raw weights to ~5e-7, so before/after comparison runs on the Linux build.
- **Tier item-class derivation edge cases** — e.g. robes/hauberks map to body armor (spec §1); confirm each `armor_class`/`equip_pos` mapping when writing `tier_item_class`. **PICKED (1-A, 2026-07-27)**: `derive_tier_item_class` in `ModifierIds.h`, verified against live `items` rows — accessories (rings/necklaces/pendants) → none; `weapon_class` bow/wand decide first; LeftHand → shield (shields are `item_sub_type` weapon, the slot decides); remaining `item_sub_type` weapon → melee; armor splits by slot — Head → helm, Body/Arms/FullBody → body_armor (robes eq 2, hauberks/shirts eq 3, costumes eq 13), Leggings → leggings, Back → cape (capes are `item_sub_type` armor in data, NOT accessory — the `ItemEnums.h` comment is wrong there), Boots → none (outside §1 scope).
- **How the Item system mode rides to the client** — piggyback an existing replicated-settings packet vs a field in the tier-presentation payload (both land in the 1-D cycle either way).

## Open questions (surface before the affected cycle — not blockers to start)

1. **Enchant per-step success % (needed by Cycle 3-F / seeding step 4).** Spec §10 makes the enchant table data-driven with per-step success %, but the annex specifies only caps, destroy flags, and endurance growth — no success-rate values. Proposed default: extract today's effective per-step success rates from the current handler's formula verbatim into `enchant_steps` (behavior-preserving), leaving tuning to data. Confirm, or supply a launch table.
2. **Righteous/Kloness fix scope in legacy mode (Cycle 3-F).** §13 says drift fixes apply "both modes unless noted" — so legacy mode also gains the Righteous combat hook and loses the Kloness floor. Reading the contract as written: yes, both modes. Flagging because it changes the retail-faithful mode's live behavior (in the retail-*correct* direction, per #34's evidence). Silence = proceed as written.
3. **Aggregate-cap basis units.** §7's caps table mixes display-unit values (absorbs 80, attributes +20) with the structural-max 546 formality. The draft stores `aggregate_cap` in display units across the board (clamp applied after multiplier). Confirm this reading — it makes MC 13 / CC 20 caps land on the same axis as their 1..7 rolls × multiplier 1.

## Phase → spec traceability

| Phase | Spec sections | Compatibility |
|---|---|---|
| 0 Groundwork | §8 (stage rename) | none |
| 1 Shared encoding + packets | §12, §2 (zero-client-patch labels), §5 (broadcast fields) | **0.7.0 → 0.8.0, the only bump** |
| 2 Server data layer | §2, §3 (validator invariant), §7/§8 (seed data), §10 (enchant table) | none |
| 3 Strategies + effects + drift | §3, §4, §5, §6, §8, §9, §10, §13 (all seven) | none |
| 4 Client presentation | §11, §5 (animation scaling), §10 (GM creator UI) | none |
| 5 Deploy + matrix | §14.5 (one batch), §2 (launch verdict readiness) | none |
