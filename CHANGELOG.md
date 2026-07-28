# Item Tiers Phase-1 exit gate — legacy-mode smoke through the new wire envelope

Issue #44 (epic #36). No code changes — verification that Phase 1 (cycles 1-A through 1-E) only moved the encoding, not the semantics. Closes Phase 1; Compatibility stays exactly 0.8.0 with no further wire changes queued.

- Builds: Windows `-Target All` Debug green (0 errors); Linux server + Linux client both green via `build_server_linux.sh` / `build_client_linux.sh` on the test box's `~/hb38-ci/` scratch.
- 1-E stale-schema gate verified live: booting against pre-1-E dev DBs → loud refusal and exit (`schema version '1' does not match required '2' - stale dev database refused`); after clearing, clean boot (79/79 maps, ports listening) with fresh DBs stamped at the new versions.
- Legacy smoke loop passed: login → prefixed drop (Strong Cape) → pickup → tooltip correct (catalog-fed, Strong inline as durability) → bank round-trip → exchange → Trading Post list/withdraw → enchant attempt — behavior identical to pre-phase.
- Storage evidence read directly off disk: the cape persisted as `tier 0`, `mod1_type 10` (strong, unified ID), `mod1_value 5` through both paths — account-DB inventory (schema v7) and trading-post escrow (schema v2) — with the modifier POD byte-identical across drop → TP listing → withdraw → logout save, including across a hard server kill mid-loop.
- Repo dev DBs reset to post-1-E schemas: stale v6 account DBs (`janglez2`, `janglez3`, `shadowevil`, `shadowtest`) removed; `janglez.db` and `tradingpost.db` recommitted as fresh v7/v2 files so a clean checkout boots. Pre-wipe copies parked untracked in `Binaries/Server/pre1E_stale_backup/`.

# Item Tiers Cycle 1-E — persistence DDL swap to the flat tier/modifier columns

Issue #43 (epic #36). All three item-carrying table pairs — `character_items` + `character_bank_items` (per-account DBs) and trading-post `listing_items` + `offer_items` — replace the four legacy `prefix_type`/`prefix_value`/`secondary_type`/`secondary_value` columns with `tier` plus the twelve `mod1..mod4 type/value/value2` columns, so the 15 attribute columns now mirror the 15-byte `item_attribute_data` POD field-for-field and the whole POD round-trips as one unit. **Fresh start per spec §12: straight DDL swap, no data migration.** Server-only (0.9.2 → 0.9.3); no Compatibility change.

- Loud stale-schema gate: new `VerifySqliteSchemaVersion` (AccountSqliteStore.h) — the first code that *reads* `meta.schema_version` back. A DB with no tables is fresh (DDL builds it); a matching version skips the DDL block entirely (removing the `INSERT OR REPLACE` write transaction the old code committed on every open); anything else is refused with a delete-to-recreate error. Account store bumped 6 → 7, trading post 1 → 2 — pre-existing dev DBs (`accounts/*.db`, `tradingpost.db`) must be deleted. GameConfig/MapInfo stores intentionally stay ungated (they rebuild/self-heal in place).
- Single source of truth for the column set: `HB_ITEM_ATTR_COLUMNS_SQL` / `_PLACEHOLDERS_SQL` / `_COLUMNS_DDL` macros (macros because they splice into adjacent SQL literals) plus `BindItemAttributeColumns`/`ReadItemAttributeColumns`, shared by both stores' six load/insert sites and all four CREATE TABLEs. Per-store `ACCOUNT_DB_SCHEMA_VERSION` / `TRADING_POST_SCHEMA_VERSION` macros keep the gate argument and the DDL stamp from drifting.
- `item_attribute_data::from_legacy_fields` deleted (its four store-reader call sites were the last users); `AccountDbItemRow`/`AccountDbBankItemRow` drop their six shadow legacy ints — everything flows through the embedded `.attributes` POD, and the trading-post escrow→bank handoff is now `r.attributes = e.attributes`.
- Retired with the version gate (all only served pre-v7 DBs the gate now refuses): the `item_name`→`item_id` migration (`MigrateItemNamesToIds` + `LoadItemNameMapping`), the 38-call `AddColumnIfMissing` ladder (columns folded into the `characters` CREATE TABLE), the per-open lowercase `UPDATE`s (lowercase is enforced at account creation), and the `ColumnExists` helper. Net −300 lines.
- Verified: Windows `-Target All` then `-Target Server` green (0 errors, 0 compiler warnings); git-diff audit — column lists, placeholder counts, and bind/read orders match the POD order at every site; /simplify 4-agent pass applied (DDL macro, version-literal unification, DDL-skip-when-current, stale comment fixes) — declined rehoming the gate into a neutral SqliteUtil header (new-file churn; becomes relevant only if the ungated stores ever adopt it). Linux builds remain the phase-exit gate (#44).

# Item Tiers Cycle 1-D — unified modifier catalog + tier presentation replication

Issue #42 (epic #36). One `PacketModifierCatalogEntry` stream replaces the two legacy attribute-type config packets (`PacketAttributeTypeConfig.h` deleted); per entry it carries `modifier_id`, `display_name`, `effect_label`, `effect_format`, `multiplier`, `bucket_id`, `min_tier`, `marquee`. The tier presentation table (Common/Rare/Epic/Legendary names, §11 palette colors, `"{tier} {name}"` name template) and the item-system mode flag ride in the chunk-0 header of the same stream, so all three replicate through config-cache slot 7's existing SHA-256 negotiation as one cached unit. Rides inside the 0.8.0-alpha envelope — msg id renamed `AttributeTypeConfigContents` → `ModifierCatalogContents` (same value), no further Compatibility change.

- Server source is the planned interim shim: a hardcoded `k_modifier_catalog_shim` table (23 rows) equal to today's legacy prefix/secondary entries, with multipliers read from the DB-fed `m_modifier_multiplier` lookup so replicated display scale can never diverge from gameplay. Dies in 2-B when the catalog reads the tiered config tables. The legacy `attribute_prefix_types`/`attribute_secondary_types` DB tables and min/max/multiplier gameplay lookups are untouched.
- `build_modifier_catalog_packets()` is shared by the hash computation, login send, and reload push (color-palette precedent); /simplify factored the shared `send_packet_stream()` + `hash_packet_stream()` helpers out of the color-palette twin instead of cloning its send/hash scaffolding a seventh time, and the reload push now builds the stream once for N clients instead of per client.
- Client caches the catalog (`modifier_catalog_entry m_modifier_catalog[256]`), tier presentation (reusing `hb::shared::render::Color`), name template, and mode flag; `ConfigCacheType::AttributeTypes` → `ModifierCatalog` (same slot + cache file — hash mismatch auto-invalidates old-format caches). Config-cache negotiation fields renamed `attributeType*` → `modifierCatalog*` across all four packet structs.
- Tooltip labels, effect formats, and item-name prefix words now render from replicated catalog data: `ItemNameFormatter`'s two hardcoded switch blocks are gone (`append_modifier_effect` formats from `effect_label`/`effect_format`, guarding `std::vformat` against malformed replicated strings), the client's hardcoded multiplier fallback table died with them, and `lan_eng.h` `GET_ITEM_NAME3-13` are retired. Only line *placement* (Sharp/Ancient inline damage, Light inline weight, Strong inline durability) stays client-side, keyed by modifier id.
- New Shared vocabulary in `ModifierIds.h`: `tier_bucket` enum (damage=1 … marquee=9, §4 catalog order), `item_system_mode` enum (legacy/tiered — hardcoded legacy until `meta.item_system` lands in 2-A/2-B), `tier_count` constant sizing the wire table and every tier array. Console `reload attributes` renamed to `reload catalog` (old token kept as alias).
- Verified: Windows `-Target All` green ×2 (0 errors, 0 compiler warnings); rendering equivalence audited case-by-case against the deleted switches (shim rows reproduce today's tooltip strings byte-for-byte, including Righteous's label-less prefix and the trailing-space quirks); /simplify 4-agent pass applied (fixes above) — declined the wire-shape suggestions (placement/line-style bytes: entry layout is spec-locked §12) and the codebase-wide chunk-loop refactor (6 pre-existing copies, beyond cycle scope). Linux builds remain the phase-exit gate (#44).

# Item Tiers Cycle 1-C — item-carrying packets embed the attribute POD; status broadcast gains the §5 speed fields

Issue #41 (epic #36). Every item-carrying packet now embeds the locked 15-byte attribute POD directly, and the duck-typed `copy_attributes_to`/`load_attributes_from` template helpers are gone from the codebase — encode/decode is plain struct assignment. The POD moved into its own Shared header `Item/ItemAttributeData.h` (`item_attribute_data` + `item_modifier`, packed, `static_assert`ed at 15 bytes); `item_instance_data` holds it as its `attributes` member with the legacy prefix/secondary accessors reimplemented as views over it. Rides inside the 0.8.0-alpha envelope opened in 1-B — no further Compatibility change.

- Reshaped packets (11): `PacketEventGroundItem`, `PacketMapDataItem`, `PacketNotifyItemAttributeChange`/`GizonItemChange`/`ItemObtained`/`ItemToBank`/`ExchangeItem`, `PacketResponseItemListEntry`/`BankItemEntry`, `TpItemBrief`/`TpItemFull` — each swaps its six legacy attribute bytes for the POD at the same offset, so tier + modifier slots [2]/[3] (+ pair `value2` bytes) now travel end-to-end (structurally zero until Phase 3).
- Status broadcast: `PlayerStatus` gains `cast_reduction_pct` and `move_speed_pct` (spec §5), appended after the player-only flags. Server sends zeros until the Phase-3 marquee mechanics land (covered by the existing `clear()` memset); client animation scaling hooks up in 4-C.
- Server encode sites (`Game.cpp` ×8, `ItemManager.cpp` ×5, `TradingPostManager.cpp` fills) and client decode sites (`NetworkMessages_Items.cpp` ×10, `NetworkMessages_Bank.cpp` ×2, `Client/Game.cpp` ×4, `DialogBox_TradingPost.cpp`) all became one-line POD assignments via new `CItem::get_attributes()`/`set_attributes()`.
- Legacy DB bridge confined to the sqlite edge until the 1-E DDL swap: `item_attribute_data::from_legacy_fields` is called only inside the store readers. `AccountDbItemRow`/`AccountDbBankItemRow` and trading-post `escrow_item` now carry the POD in memory (`escrow_item`'s six legacy fields deleted); the legacy prefix/secondary columns are composed/decomposed only at the SELECT/INSERT bind sites (/simplify altitude finding — bridge knowledge no longer leaks into `CGame::load_player_data_from_db` or the TP packet-fill layer).
- Retired with the helpers: dead `CItem::copy_attributes_from`, the duck-typed `from_ground_item_packet` template (inlined at its single client call site), stale funnel/dependency comments. Kept `has_special_attributes` chain — the /simplify dead-code claim was a false positive (3 live `ItemManager.cpp` callers).
- Pre-existing quirk noted, unchanged: `Notify::GizonItemUpgradeLeft` sends an `ItemAttributeChange`-shaped packet whose attribute bytes the client never reads (`PacketCast` tolerates the larger payload; zeros before and after).
- Verified: acceptance grep — zero remaining uses of the legacy copy helpers; Windows `-Target All` green ×2 (0 errors, 0 compiler warnings); git-diff audit of all 40+ touched sites (pure envelope moves, no logic change); /simplify 4-agent pass applied (reuse + efficiency clean; simplification/altitude fixes above) — declined the packing-macro layering hoist and Tp wire-struct common-head refactor as churn beyond cycle scope. Linux builds remain the phase-exit gate.

# Item Tiers Cycle 1-B — item instance reshape to the 15-byte modifier POD + THE Compatibility bump

Issue #40 (epic #36). The attribute portion of `item_instance_data` is now the locked spec §12 POD verbatim — `custom_made`, `tier` (0 = untiered, 1–4 = Common..Legendary), `modifier modifiers[4]{type, value, value2}`, `enchant_bonus` — all `uint8_t`, non-attribute instance fields untouched. The positional `AttributePrefixType`/`SecondaryEffectType` enums are dead (`ItemAttributes.h` deleted); every roll/apply/price/display site speaks Unified modifier IDs. **Compatibility bumped 0.7.0-alpha → 0.8.0-alpha — the single bump of the whole Item Tiers effort**; mixed old/new client-server pairs are rejected pre-auth by the existing `LoginServer` version gate.

- Interim envelope (until 1-C/1-E): wire packets and DB rows keep their legacy field *names* (`prefix_type`, …) but now carry unified IDs; `copy_attributes_to`/`load_attributes_from` map `modifiers[0]` ↔ prefix fields, `modifiers[1]` ↔ secondary fields. Tier + slots 3/4 don't travel yet — they're structurally zero until the tiered Roll strategy (Phase 3). A new instance-to-instance `load_attributes_from` overload preserves the whole POD on item copies.
- The ONE translation table: constexpr `legacy_prefix_to_modifier_id` / `legacy_secondary_to_modifier_id` in `ModifierIds.h`. Translating boundaries: crafted-recipe nibbles (`ItemManager.cpp` build path), GM tester bitmask nibbles (`Game.cpp` TesterCreateItem — the dialog still sends legacy 4-bit ids since unified IDs don't fit a nibble), and the legacy-keyed `attribute_prefix_types`/`attribute_secondary_types` rows at SQLite load (`GameConfigSqliteStore.cpp`; tables die in 1-D). `AddEffectType` reconciliation recorded there too: accessory add-effects are base-item config, outside the unified instance space by design (spec §1).
- `[16]` lookup arrays dead on both sides: server `m_prefix_*`/`m_secondary_*` sextet → unified-keyed `m_modifier_multiplier/min/max[256]`; client's two multiplier arrays → one `m_modifier_multiplier[256]`; all `< 16` guards gone. Config-cache attribute-type packets now ship unified IDs (hash changes → clients refetch).
- Drift #7 fixed: the nibble-era value-15 clamp in `generate_item_attributes` is deleted (band legality becomes data in Phase 2). Everything else byte-identical: roll weights, 40% secondary gate, MC/CC halving (3-F fixes those), sell-price multipliers, enchant behavior.
- Rekeyed sites — server: roll tables, `calc_total_item_effect`, `adjust_rare_item_value`, sell-price switches ×2, enchant Ancient/Strong checks, poison-on-hit, Critical/Poisoning/Righteous combat switch (`CombatManager`), wand spell bonus (`MagicManager`); client: `ItemNameFormatter` (incl. killing the bogus `hb::shared::owner::Slime`-as-10 constants), `DialogBox_Magic`, `DialogBox_ItemCreator` (options now derive multipliers through the translation table — /simplify altitude finding), config receive + fallback defaults.
- Verified: Windows `-Target All` green ×2 (0 compiler warnings); git-diff audit of every rekeyed switch against the mapping table; /simplify 4-agent pass applied (translation-table reuse in the GM dialog, redundant memset, orphaned enum casts) — declined `std::array` for `modifiers[4]` (spec shape is locked verbatim). Linux builds remain the phase-exit gate.

# Item Tiers Cycle 1-A — Unified modifier ID space (Shared header)

Issue #39 (epic #36). First cycle of the Phase-1 wire batch, compile-only: new Shared header `Dependencies/Shared/Item/ModifierIds.h` establishes the flat Unified modifier ID namespace the whole tier system stores/ships (`uint8_t`, 0 = empty, renumbered from scratch). No runtime behavior change; legacy enums stay until 1-B.

- `modifier_id` — the authoritative ID assignment (recorded implementation-time pick), catalog order: 1–5 DAMAGE, 6–7 PRECISION, 8–10 HANDLING, 11–12 ECONOMY, 13 CASTING, 14–21 SET-AXIS, 22–23 COMBAT UTILITY, 24–34 ATTRIBUTES (singles/pairs/all-stats), 35–40 MARQUEE. Light/Strong are one ID each; per-class eligibility is data.
- `effect_id` — code-side behavior keys catalog rows carry (`add_attribute`/`add_attribute_pair`/`add_all_attributes` take `tier_attribute` params; six marquee behaviors). Values are arbitrary stable keys — the 1-B translation table is the authority, and must reconcile all THREE legacy enums (prefix, secondary, `AddEffectType`).
- `tier_attribute` param keys match the existing `hb::shared::net::StatId` wire ids (Str/Dex/Int/Vit/Mag/Chr) so the two attribute keyspaces can never silently diverge (caught by the /simplify reuse reviewer).
- `tier_item_class` + constexpr `derive_tier_item_class(sub_type, weapon_class, equip_pos)` — the eight §1 gear classes, derivation verified against live `items` rows: capes are `sub_type` armor at `equip_pos` Back (NOT accessory — stale `ItemEnums.h` comment fixed), hauberks/shirts are Arms, robes Body, costumes FullBody → all body_armor; shields are `sub_type` weapon at LeftHand. Plan 3-F updated: the enchant handler's hand-rolled route switch dies in favor of this single classifier.
- Compile coverage rides `ItemInstanceData.h` (the struct whose modifier slots rekey onto these IDs in 1-B) — deliberately NOT the wide legacy headers. Both new headers registered as `ClInclude` in Client/Server `.vcxproj` + `.filters`.
- Verified: Windows `-Target All` green; header is include-complete and platform-neutral (Linux gate at phase exit per plan).
- No version bump: compile-only addition; the single Compatibility bump 0.7.0 → 0.8.0 lands in 1-B.

# Item Tiers Cycle 0-B — item attribute field access funneled through accessors (shared/server/client)

Issue #38 (epic #36). Prefactor per `PLANS/ItemTiers_Implementation_Plan.md` Cycle 0-B: every direct read/write of the item-instance attribute fields (`custom_made`, `prefix_type/value`, `secondary_type/value`, `enchant_bonus`) now routes through accessors, so the Phase-1 struct reshape only touches the accessor bodies. Zero behavior change (verified by `git diff` review + acceptance grep: no `m_instance.<attr>` access remains outside the two shared headers).

- `ItemInstanceData.h` is the canonical funnel: raw getters/setters (`get_prefix_type`, `set_prefix(type, value)`, `set_enchant_bonus`, `is_custom_made`, `has_prefix`, …) plus `copy_attributes_to` / `load_attributes_from` templates moved down from `CItem`.
- `Item.h` (`CItem`): existing enum-typed getters kept as legacy views; all attribute helpers (incl. `get_light_percent`, `has_special_attributes`, `copy_attributes_from`) now delegate to the instance-data funnel; new value/enchant getters and setters added.
- Server: `ItemManager.cpp` (~70 sites — pricing, `calc_total_item_effect`, enchant/upgrade paths, crafting nibble decode, attribute roll), `Game.cpp` (map-data encode via `copy_attributes_to`, GM item creation, attack prefix reads), `AccountSqliteStore.cpp` (save binds), `CraftingManager.cpp`, `TradingPostStore.cpp`.
- Client: packet→instance copy blocks collapse to `load_attributes_from` (`NetworkMessages_Items.cpp` ×4, `Game.cpp` map item, `DialogBox_TradingPost.cpp`); reads funneled in `ItemNameFormatter.cpp`, `InventoryManager.cpp`, `DialogBox_Magic/ItemUpgrade`, `Screen_OnGame*` (shared `has_prefix()` replaces two divergent inline predicates).
- Out of scope by design: DB-row/wire-packet struct field names (`AccountSqliteStore` rows, `TradingPostStore`/`TradingPostManager` escrow, `LoginServer`, packet headers) — those reshape in Phase 1-C/1-E.
- Also: `DialogBox_NpcSpawner.cpp` — pre-existing Linux/GCC build break (packed `npc_id` field can't bind to `std::format`'s reference parameter) fixed with an int cast; found by the first full Linux client build in a while.
- Verified: Windows `-Target All`, Linux server, and Linux client builds all green (Linux pair built on the test box; client prereqs now include `libxi-dev`, which the documented apt line was missing).
- No version bump: internal refactor, no behavior or protocol change.

# Item Tiers Cycle 0-A — drop-pipeline "tier" vocabulary renamed to "stage" (server)

Issue #37 (epic #36). Pure rename per the Item Tiers spec §8 naming mandate — "tier" now means item Tier exclusively; drop-table rungs are **stages**. Zero behavior change (verified by `git diff` review: identifiers and comments only).

- `Game.h` `DropTable::tierEntries` → `stage_entries`.
- `EntityManager.h/.cpp` `roll_drop_table_item(table, tier, …)` → `(table, stage, …)`.
- `GameConfigSqliteStore.cpp` loader local `tier` → `stage`. The `drop_entries.tier` **DB column keeps its name** (recorded implementation-time pick) — SQL strings untouched.
- Drop-pipeline comments in `EntityManager.*`, `Game.h`, `Npc.h` follow ("tier-2 drop" → "stage-2 drop"). The magic-tier comment at `EntityManager.cpp:593` is unrelated and untouched.
- No version bump: identifier rename only, no behavior or protocol change.

# Griffin polish — correct diagonals, family-length E, gust-of-wind ranged attack

Follow-up round driven by in-game testing of the re-shoot.

### Client data (`griffin.pak` rebuilds, art only)
- **NE/NW walks were crab-walking** ("sliding sideways"): the away-diagonal flip book is NE-native (heads upper-right) but was first built as NW-native, putting mirrored art in both diagonal slots so the body walked opposite its travel. Caught by extracting the *built pak* sprites at zoom — source-sheet thumbnails had been misread twice; that check is now the rule. The gait itself was never wrong, and a planned third re-roll of the book was cancelled.
- Three re-rolls integrated (hardened prompts: FORESHORTENING CHECK in every camera block, corner-axis + narrower-than-side-view rules on diagonals, bottom-right corner kept green): **stop E** at the family camera (aspect 1.39 vs the drifted 1.65 — standing E/W body 129 → 109 px, dire-boar-class overhang when fought from the side, and no more length pop against the 104–116 px walk frames; attack/die inherit it via their stand frames); **SE/SW walk book** properly 45° (came back SW-native, SE is the mirror); **NE/NW walk book** tightened. Bbox aspect lies when the tail streams horizontally — heading is judged by eye at full res.
- Per-facing verification vs the shipped quadrupeds now covers width×height and area ratios per direction, not just heights.

### Server — gust-of-wind ranged attack
- `EntityManager.cpp` `try_ranged_attack`: new `case hb::shared::owner::Griffin` mirroring the Dark Elf shape — attack event with **v3 = 81 (client `EffectType::STORM_BLADE`, the StormBringer strike visual)**, damage via `calculate_attack_effect` mode 2 (ranged physical). Range comes from the existing `attack_range 3`.
- `gamedata.db` `npc_configs` griffin `magic_level` 8 → **10** — the deliberately empty Frost/Nizie tier: spellcasting (Unicorn/Centaurus tier-8 kit) stops, the `magic_level >= 6` debuff resists stay, and the gust becomes the mob's only distant attack. Tier-10 roster comment updated so the parking is discoverable.

### Client
- `MapData.cpp` NPC attack path (`default:` owner branch) previously drew only `v3 == 2` arrows — any other effect id from an NPC was silently invisible (found by the /simplify altitude reviewer before it shipped broken). It now also draws `STORM_BLADE`, same call shape as the player StormBringer path.

### Versioning
- Server → 0.9.2, Client → 0.7.4. No compatibility bump — v3 rides the existing attack event; an older client simply ignores 81.

# Griffin re-shoot — sculpted golden art set, real 8-frame walks (client)

The griffin's art is fully re-shot against a single golden reference sheet, replacing the first-pass set. 18 sources: 13 singles plus 5 walk flip books (2×4 grids), classified by what they actually show — damaged came back SW where SE was asked, the corpse faces W, and two Stage-1 holdovers cover stop E and the W walk book (it faces left, so W is native and E the mirror, the same trick as the first set's filmstrip).

### Client data
- `sprites/npcs/griffin.pak` rebuilt — 40 sprites, action-major, layout unchanged:
  - **Move is a real 8-frame flip-book gait on all 8 directions** (native N/NW/SE/S/W books, mirrors for NE/E/SW), replacing the 2-pose stride/gather cut. Frames play straight through with per-frame torso-stabilised pivots.
  - **Per-facing target heights.** Normalising every facing to one height made the mob visibly shrink on N and grow on S as it turned: on the shipped quadrupeds the away-facing views are ~25% *taller* on screen than the side views — the body's depth foreshortens into the vertical axis (dire-boar/unicorn/giant-lizard/hellhound/werewolf N ratio 1.19–1.31, S 0.88–1.04 vs E; centaur hides the effect behind its upright torso, which is why comparing E facings never caught it). Griffin now: N 98, NE/NW 97, E/W 78, SE/SW 73, S 74 — ratios 1.26 / 0.94, mid-family.
  - **Sharpen is solved per source** (target: ettin's 19.1 local contrast at final size) instead of a constant — renders arrive at different softness, the same reason the brightness lift has always been solved rather than hard-coded. Solved values ran 60–220 and the whole set lands at 18.6–19.6, where a fixed constant left it spanning 16–27.
  - Verified against the reshoot pass bars with a metric calibrated to reproduce the originals' published figures exactly (dire-boar 17.2, ettin 19.1, centaur 25.5): camera holds across all 40 walk frames (aspects 1.28–1.48 vs the sheet's 1.39 — multi-figure grids keep the camera where standalones drifted to 1.65); palette is coherent across the whole set including both holdovers (hue 29–35°, sat 0.46–0.52 — saturation now sits at family level, where the first-pass set measured 0.75 and read pasted-on); walk phase-opposition passes on N/S and is weak-but-animating on NE/SE.
  - Gemini's engine was benchmarked for the walk books and lost: best gait of any render (the far-side legs finally animate) but its surface never reached the contrast bar (caps ~17.5 at sharpen 340, visibly softer at final size) and its watermark sparkle fused onto a frame's front talon. The regenerated reshoot doc carries the countermeasures for any future pass: a sharp-focus STYLE rule (smooth shading ≠ soft focus), keep the canvas's bottom-right corner flat green (isolates the watermark for the keyer), and a concrete phase rule (each bottom-row frame shows the opposite legs vs the frame directly above) — the last correlates with the passing phase scores in this batch.
- Builder `build_griffin_pak_v2.py` (handoff folder, promotion to `tools/` still pending): filename-stamp classification instead of mtime order (dropping a file can no longer silently remap the pak), self-contained helpers (importing the vince builder executes its build — learned the hard way), `__main__`-guarded, flip-book segmentation by connected components with largest-blob cleanup that also strips watermark sparkles.

### Client
- `MapData.cpp`: griffin Move now takes the init loop's 8-frame default (centaur-standard); the explicit `m_sMaxFrame = 7` written first was redundant with the loop and is gone (simplify finding — the file's convention is omit-when-default). Comment corrected while there: the other actions get no init-loop values at all (that loop covers player bodies only), so their explicit 4-frame caps are load-bearing, not overrides of a 7 default.

### Versioning
- Client → 0.7.3 (client-only: frame table; pak is data). No compatibility bump.

# Griffin — first AI-assisted monster (test spawn)

New PvE creature wired end to end so it can be seen in game. Stats, drops and spawn are deliberately provisional — cloned from the Centaur and dropped next to the Middleland arrival point for a look, not balanced.

### Client data
- `sprites/npcs/griffin.pak`: 40 sprites = 5 actions × 8 facings, **action-major** (`sprite = base + pose*8 + (dir-1)`, poses stop/move/attack/damaged/dying = 0-4). Built from 18 renders classified by content: stop and attack have all five source facings, move and damaged came back SW where SE was asked for (free — one of each SE/SW pair is a mirror either way), damaged has only S and SW, die only E. E facing measures stop 112×65, move 122×65, against centaur 115×107 and dire-boar 121×73.
- Pak notes: pivots are bbox-centre with a 5 px overhang, matching every shipped quadruped (centaur 53%, dire-boar 51%, unicorn 51%) — a foot-centroid anchor lands at 59% on a profile view, because the front talons reach lower than the hind paws, and would sit the griffin off its tile. Each render is a separate generation and their framing is not identical (standing height varied 514-580 px), so standing poses are normalised to a common height and the rearing/collapsed poses take the mean of those factors; height-normalising a rear-up attack would wrongly shrink it. No de-ink pass and no edge darkening: the source is a 3D render whose dark feather barring is real shading, and de-inking visibly flattens it even though the thin-line metric prefers it.

### Client
- `OwnerType.h`: `owner::Griffin = 92`. `Screen_Loading.cpp`: registered at slot 82 (`owner_type - 10`), 40 sprites.
- `MapData.cpp`: frame timings follow the Centaur but **every action is capped at 4 frames**, matching the 4 rects per sprite. `Type::Move` in particular has to be set explicitly — it defaults to 7 for every character in the constructor's init loop, which would index three rects past the end of the sprite.

### Server data
- `gamedata.db` `npc_configs` row 117 "Griffin", npc_type 92 — every combat field cloned from the Centaur (1751-2100 HP, 12-132 damage, magic level 8, attack range 3) including its drop table 20085.
- `MapInfo.db` `map_spot_mob_generators` middleland index 33: 5 griffins in a 20×20 rect at (184,220), around the map's arrival point (192,228).

### Animation from the art already in hand
First in-game test read as "paper sliding across the screen" — correctly, because every frame of a sprite was identical while its screen position interpolated, so only the position moved. Fixed without new renders: each facing already has **two** genuine poses, its own action render and the standing render, so every action now cuts between them.
- Idle now breathes. Every shipped mob does — measured silhouette change across their idle frames is dire-boar 0/2/11/18/18/11/3/0%, centaur peaking at 53%, hellhound 45%, while the griffin sat at a flat 0%. A single render cannot move limbs, so the body is stretched a few px upward from a planted baseline on a rise-and-fall arc, which reads as breathing and costs nothing. Scaling rather than shifting a region avoids a seam across the shoulders; the rect height and pivot are constant across the frames so the feet never leave the ground. Peak is ~10% — deliberately under dire-boar's 18%, whose figure comes from real limb movement, so matching it with a whole-body stretch would look rubbery.
- move `[stride, gather, stride, gather]` — a real 2-beat gait, legs visibly extending and closing.
- attack `[stand, strike, strike, stand]` — a lunge and recovery rather than a frozen talon-swipe.
- damaged `[flinch, flinch, stand, stand]`, die `[stand, collapsed, collapsed, collapsed]` — a fall instead of the corpse popping in.
- **Every frame carries its own box and its own pivot**, which is how the shipped mobs are built — ettin's move frames run 40 to 87 px wide with pivots from -27 to -50, the artists moving the pivot per frame so the body stays planted while the limbs swing. Padding all frames to a shared canvas and centring them does the opposite: when a foreleg reaches forward the box grows forward and centring shoves the torso backward, so the body slides against the motion. Placement and stability need different references — frame 0 anchors at its bbox centre (the quadruped convention: centaur 53%, dire-boar 51%, unicorn 51%), and later frames offset by how far the *torso* centroid moved, since anchoring on the torso alone would sit a profile view at ~67% and stand the griffin off its tile. Measured result: anchors land at 47-53% of width and the torso holds within 1 px across a cycle.
- Ruled out while diagnosing, all verified rather than assumed: shadows do render (`draw_npc_layers` → `draw_shadow`, and Griffin is not in `ShouldSkipShadow`); alpha is correctly binary (every shipped pak has exactly zero partial-alpha pixels); camera pitch is in family (griffin aspect 1.72 vs dire-boar 1.66).

### Provisional, not yet balanced
- Centaur-strength monsters sitting on the Middleland arrival tile will kill anything that walks in. This is a look-at-it spawn, not a placement.
- 4 frames per action against the originals' 8 (and 11 for centaur's death). Smoother motion needs filmstrip renders, not more compositing.
- Damaged reuses the SW recoil across 7 of 8 facings and die is one corpse mirrored; closing those needs 3 more damaged renders (N, NE, E) and 4 more die (N, NE, SE, S).
- Saturation measures 0.75 against centaur 0.56 and dire-boar 0.22 — the griffin is the most saturated thing on screen, which is part of why it reads as pasted on rather than lit by the scene.

### Versioning
- Client → 0.7.2 (client-only: sprite registration + frame table). No compatibility bump — no protocol change; NPC configs stream to the client at login.

# Vince the Auctioneer gets his own sprite (first AI-assisted NPC)

Vince has been shipping since the Trading Post landed, but with no art of his own — the client drew him with a stand-in built from the *player* compositor (a female body in full plate with green boots and a green cape). He now has a real pak, and the whole stand-in is gone.

### Client data
- `sprites/npcs/vince.pak`: new town-NPC pak — 8 sprites (one per facing) × 8 idle frames, 64 px tall, feet-centre pivots with william's 5 px overhang (foot anchor taken from the bottom 8% of rows, not the bbox centre, or he slides sideways as he turns). Built from a three-view pre-rendered 3D sheet (front, right profile, rear) plus mirrors for W/SW/NW; NE and SE substitute the N and E art for now. Idle animation is a synthesised 1 px head settle over the 2 s cycle.
- Pipeline notes, all learned by measuring against the shipped town NPCs:
  - **Palette** — the originals carry ~220-240 unique colours per frame (pre-rendered, not pixel art), so the 24-colour item-icon quantize flattens the face to a single tone. One shared 160-colour palette across all facings, built from opaque pixels only (feeding median-cut the background block lets it hog palette boxes).
  - **No outlines, ever.** The originals have none. The first attempt used cel-shaded illustration renders and added silhouette edge-darkening on top; the useful metric is the fraction of body pixels that are *thin dark lines* (much darker than their local neighbourhood) — originals sit at 2.9-10%, the illustration source hit 12.8%. Counting near-black pixels outright does not work: it cannot tell dark art from outlined art. Edge-darkening is off, and the source is now a 3D render that scores 5.9%.
  - **Brightness is solved, not hard-coded.** Renders arrive dark, but how dark varies per generation — two sheets of the same character came in at mean luma 42 and 56 — so a gamma tuned on one blows out the next. The lift gamma is now searched across all facings at once to land the set on 84 (tom and gandalf are 82.5, howard 85.7; william is an outlier at 143.9). The search has to include the contrast step, since PIL blends toward a grey averaged over the whole image including the transparent region, which is near-black here and pushes the body ~7 luma brighter.
  - **Lift luminance, not RGB.** The gamma runs on luma and is applied back as a per-pixel scale factor across all three channels, which leaves saturation exactly as rendered. Running it on RGB directly drags every colour toward white: an earlier pass lost saturation 0.526 → 0.463 and read as if white paint had been brushed over him. Fixed, it sits at 0.62 against the originals' 0.36-0.52.
  - **Match the originals' surface smoothness, measured as local contrast** — the mean luma step between adjacent body pixels. This is what separates a sprite that reads as a solid volume from one that reads as a flat patterned shape, and it is not the same thing as overall contrast. The shipped mobs sit at dire-boar 17.2, ogre 17.5, troll 18.8, ettin 19.1, centaur 25.5: ettin's skin is one continuous tonal sweep per muscle, so the eye integrates it into form. At the 140% unsharp inherited from Vince the griffin measured 31.6 — 65% noisier than ettin — because every feather edge survives the downscale as a hard pixel step and reads as texture rather than shape. Detail at 78 px does not stay detail; it becomes noise that flattens. Dropped to 40% unsharp, landing at 20.8.
  - **Sharpen the body, not the face.** Photorealism does not survive to a 19 px head, but sharpening it does — the unsharp mask manufactures eyes, cheekbones and beard edges the originals do not have, and the face reads as a portrait beside their suggestions of one. The mask is held off the top 28% and feathered in across the shoulders. With that in place the head measures 258 px in 60 tones (spread 44.7) against william 240/96/45.8, tom 257/99/49.0, gandalf 269/113/52.6 and howard 305/126/65.2 — i.e. less facial detail than any original, which is the bar that matters. A photoreal source still *looks* photoreal at full size; that impression does not reach the sprite.

### Client
- `Screen_Loading.cpp`: registers `npcs/vince` at slot 101. The slot is fixed by the owner type, not the next free index — the body sprite is derived as `Mob + (owner_type - 10) * 8 * 7` and `owner::auctioneer` is 111.
- `NpcRenderer.cpp`: `build_auctioneer_equipment()` and the `is_auctioneer` branches in `draw_stop` deleted. Vince now takes the ordinary NPC path (`CalcNpc` + `draw_npc_layers`), so he picks up frame animation and colour handling like every other NPC instead of being pinned to a single player idle frame.
- `MapData.cpp`: the frame table no longer copies William's row wholesale; the auctioneer gets the town-merchant idle directly (250 ms × 8 frames, matching William and Kennedy). Only the stop action is ever played.
- `OwnerType.h`: `sprite_render_type()` removed — it existed solely to redirect the auctioneer to William's sheet. Its three call sites (`EquipmentIndices.cpp`, `Game.cpp`, `NpcRenderer.cpp`) now index by owner type directly.

### Server
- `EntityManager.cpp`: Vince's spawn facing is pinned to south. `action_limit 2` routes to `Behavior::stop`, which never writes `m_dir` again, so the spawn value is permanent — and as `npc_type 111` he was falling into the `dice(1,8)` default and could spawn facing any direction. He mans one fixed post, so square to the camera beats the merchants' random SE/S/SW spread.

### Verified, no change
- Stationary town NPCs genuinely never turn: `npc_behavior_stop` does work only for `action_limit 5` (towers, generators, crops) and falls straight through for the merchant class. The only remaining writer of an idle NPC's direction is combat knockback (`CombatManager` `damage_move_dir`, 4 sites) — which is why all 8 direction slots are filled rather than just south.

### Versioning
- Server → 0.9.1, Client → 0.7.1. No compatibility bump: no protocol change, and owner type 111 was already on the wire.

# Crossbow — first AI-assisted content item (data-only, no code)

New bow-class weapon wired entirely through data — proof of the content pipeline end to end (Gemini art → chroma-key/downscale scripts → atlas/JSON/DB).

### Client data
- `sprites/items/item_atlas.pak`: three new frames appended — equip preview 63×100 (atlas 0, frame 221), ground 34×20 (atlas 1, frame 362), pack 45×71 (atlas 2, frame 362). Pivots derived from the long bow's by preserving visual center.
- `CONTENTS/ItemSpriteMetadata.json`: entry `id: 313` → `cross_bow.pak`, both genders share the equip frame (synced to the DisplayIDManager and Release copies).
- `sprites/items/cross_bow.pak`: generated paperdoll overlay — long bow's frame layout (rect counts, pivots, visual centers) kept 1:1, pixels replaced per pose: carry poses (stand/walk/run/damage) show a two-handed port-arms diagonal carry (rest view rotated 40°, mirrored per facing side); attack pose aims per direction from five dedicated Gemini renders (N/NE/SE/S + side for E/W, mirrors for SW/NW). Engine indexing `(dir-1)*8+frame` and per-direction draw order (`weapon_draw_order`: behind body for N/W/NW) verified. Icons rebuilt with a sharpen → 24-color quantize → edge-darken pass plus hand-redrawn 1px strings.
- Verified, no change: bows overlapping the body while running NE (and E/SE) is faithful retail behavior — original `_cDrawingOrder` (Game.cpp:19) and run-pose weapon art are identical to ours; retail never gave weapons the run-specific order table mantles have.

### Server data
- `gamedata.db` items row 604 "Crossbow": bow class (type 2/sub 3, weapon_class 8, archery skill 6 — inherits bow attack pose, arrow consumption), 2d5 dmg, speed 7, durability 900, level 30, dyeable, display_id 313, attribute pool 1.

### Versioning
- None — pure content; item configs stream to the client at login.

# Abaddon lightning storm — the missing retail apocalypse hazard (all targets)

Retail Helbreath periodically dropped a full-screen lightning storm on the abaddon map that damaged everyone not magically protected. Surveyed all 11 reference lineages: only Snoopy's helbreath-v3.82 has a server implementation (`DoAbaddonThunderDamageHandler`); HBCursed carries the retail full-screen bolt renderer as a client-side orphan (its `m_bThunder` latch is never set by anything); the retail client's handler for notify 0x0BE5 survives only as a disassembly comment. Reconstructed from those three pieces.

### Shared (protocol)
- New payload-less `Notify::AbaddonThunder = 0x0BE5` — the retail notify id, slotted into the existing empty-notify packet group.

### Server
- `WarManager::abaddon_thunder_tick()` on the 20s weather tick: while Apocalypse runs, a 1-in-15 roll (~one storm per 5 minutes on average — Snoopy's cadence) calls `unleash_abaddon_thunder()`: every player on an apocalypse boss map (new `CMap::is_apocalypse_boss_map()` = `apocalypse_map` AND `gen_type 2`) gets the storm notify; damage `dice(1,20)+100` spares admins, executors, and the dead. Protection-From-Magic halves (`/2 - 2`), Absolute-Magic-Protect nulls — meteor-strike semantics, per retail behavior (Snoopy's thunder handler wrongly halved for both; his meteor handler carries the correct retail split). Kills route through `client_killed_handler`; survivors get the HP notify, damage motion, tile-owner refresh (unless hiding), and hold/paralyze break — identical to meteor strike.
- New `/thunder` admin command (Developer): fires one wave immediately, bypassing mode and dice — the test hook; audit-logged to the commands channel.

### Client
- `HandleAbaddonThunder` latches a 700 ms full-screen storm; `Screen_OnGame::draw_abaddon_storm()` sweeps sky-to-ground bolt clusters across the whole viewport every frame via `weather_manager::draw_thunder_effect`, re-randomized per frame — the HBCursed `ThunderEffectAbaddonMap` visual, thinned to ~10 clusters × 3 bolts because each bolt costs ~56 unbatched `draw_line` calls in the SFML renderer (the original 32×6 sweep would be ~21× the heaviest rain frame).

### Noted for later (from the /simplify panel)
- The environmental-damage tail (protect mitigation, owner refresh, hold break) now has two WarManager copies and ~4 relatives across CombatManager/DynamicObjectManager — a shared `apply_environmental_damage()` in CombatManager is the candidate extraction.
- The renderer has no batched-line path; a `draw_lines()` API would collapse storm/corpse/spell-thunder from thousands of GPU calls to one.

### Versioning
- Compatibility → 0.7.0 (new notify: client and server must update together). Server → 0.9.0, Client → 0.7.0.

# Infernia minimaps restored + maze navigates blind (client)

- `get_hardcoded_map_index` still compared the CENTUU-era `"inferniaA"`/`"inferniaB"` capitalizations against our lowercase server map names — the strcmp miss left `m_map_index` at -1, killing both the guide map and the infernia ambience path in Screen_OnGame. Now lowercase, matching the server. Same case fix in the music picker.
- `maze` now deliberately returns -1: retail gave the maze no guide map (navigated blind); the display name stays.

### Versioning
- Client → shipped in 0.7.0 (staged as 0.6.3, superseded by the storm release's protocol bump before it ever built — Visual Studio held the client PDB hostage all evening).

# Apocalypse gates: the real reason portals didn't teleport (GM-bypass scope) + tile-teleport auth unblock

The 0.8.5 retest still failed: portals lit up but nobody travelled. Two root causes, both now fixed.

### Server
- Gate travel (the 1s re-notify + stand-on-portal teleport) sat inside `check_client_response_time`'s `if (!m_is_gm_mode)` forced-recall wrapper. Every admin chat command requires GM mode (`requires_gm_mode()` defaults true for any privileged command), so the admin who ran `/beginapocalypse` + `/clearnpc` could never trigger a gate — the exact test scenario. The block now lives in `WarManager::apocalypse_gate_travel_tick()`, called from the existing `apocalypse_progress_tick()` 1s poll: same cadence, applies to everyone including GM-mode admins, and the whole gate lifecycle (open → notify/travel → reset) is now in one file. Also swaps the asymmetric `.x`/`Right()` bounds test for the `Left()/Top()/Right()/Bottom()` idiom used elsewhere.
- `request_teleport_auth_handler` no longer applies the apocalypse recall-impossible block. Walk-on tile teleports are a two-step client flow (stand on flagged tile → `RequestTeleportAuth` → approved → `RequestTeleport`), and that first step was still rejecting non-admin players wholesale — killing the maze→procella exit. The auth handler's only sender is the tile path, and tile teleports ARE the progression routes; recall-outs stay blocked by mode ('0'/'1'/'3') in `request_teleport_handler`.
- Dead code removed: the leak's commented-out `OpenApocalypseGate` stub (hardcoded gate at 95,31) and `CMap`'s write-only `m_dynamic_gate_coords[10]` parallel gate model (the live model is the singular `m_dynamic_gate_coord` loaded from `map_dynamic_gate`).

### Verified, no change needed
- Map data is byte-faithful to the originals: maze's second client-flagged teleport tile (167,171) has no server row in any original tree (retail no-op), and procella's (119,29)→inferniaB server row has no client-side flag in any original `.amd` either (retail-dead back-exit). Both left alone.

### Versioning
- Server → 0.8.6 (server-only).

# Apocalypse gates: travel fixes (stand-on-portal margin + recall-guard scope)

First live test of the clear-gates found players couldn't actually travel: standing on the portal did nothing on Infernia A/B, and the maze exit and Procella gate were rejected outright.

### Server
- Gate entry now accepts standing within 2 tiles of the gate rect. The gate data is a single tile (original: `115 105 115 105`) but the portal graphic spans ~7x5 tiles around the anchor, so the exact-tile check was nearly impossible to satisfy by standing "on" the visible portal.
- The apocalypse recall-impossible guard in `request_teleport_handler` now blocks only recall-type teleports ('0'/'1'/'3') and restores the original's admin bypass (`m_admin_level == 0` condition, dropped during modernization). It previously rejected *every* teleport from a recall-impossible map during apocalypse — which broke the maze→procella tile exit and the procella→abaddon clear-gate (maze/procella/abaddon are all `recall_impossible`). Tile teleports and server-directed '2' teleports (the gates) now pass; recalls out of the event remain blocked for players. The Teleport-spell auth handler keeps its full block, with the admin bypass restored.

### Versioning
- Server → 0.8.5 (server-only).

# Apocalypse progression: clear-gated portals + Abaddon boss spawn

Completes the retail progression on top of the no-respawn groundwork: Infernia A/B and Procella now open their portal when the map is cleared, and clearing the Abaddon map summons the boss. All driven by data that already existed unused (`map_dynamic_gate`, `map_apocalypse_boss`) and the client's existing `ApocGateOpen`/`ApocGateClose` handlers — no protocol change.

### Server
- New `WarManager::apocalypse_progress_tick()` (1s poll while Apocalypse runs): when a clear-gated map's `m_total_alive_object` hits 0 — gen type 1 (inferniaa/inferniab/procella) marks the map's dynamic gate open, notifies on-map players (`ApocGateOpen` + notice); gen type 2 (abaddon) spawns the boss from `map_apocalypse_boss` at a random point in its rect, once per event. Polling rather than a kill hook was deliberate: deletion paths (summon cleanup on logout) also zero the counter, and a kill hook could spawn the boss mid `/clearnpc` sweep and have the same sweep kill it.
- `check_client_response_time` gate block is now data-driven: while a gate is open it re-notifies on-map players (covers late arrivals) and teleports anyone standing on the gate tile to the dynamic gate's destination. Replaces the leak's degenerate hardcoded abaddon/icebound `ApocGateOpen` spam and the icebound→druncncity tile teleport.
- Gate/boss state lives on `CMap` (`m_apocalypse_gate_open`, `m_apocalypse_boss_spawned`), reset at apocalypse start and end; event end also broadcasts `ApocGateClose`.

### Client
- Apocalypse gate sprite now renders only on the map the server opened it for (`m_gate_map_name` check) — previously a received gate position would draw on any map the player teleported to.

### Data (MapInfo.db)
- `map_apocalypse_boss.npc_id` corrected 71 → 48 (Abaddon). 71 was the leak's raw type id, which maps to Centaurus in the original and to Mana Collector in our npc table.

### Investigated, not changed
- The periodic "mass lightning storm" on the Abaddon map is the map's mob roster working as the original data intends: 140 magic-level 7-8 casters (60 Gargoyles, 20 Centaurs, 20 Dragons, 40 Fire Wyverns) whose spell tables are the lightning family; volleys recur as their mana regenerates. Verified against the original abaddon.txt (same spots/counts). One conversion artifact noted: spots 4-6 hold Orcs where the original had its type-51 mob — if anything our map is tamer than the original.

### Versioning
- Server → 0.8.4, Client → 0.6.2 (existing packets only — compatibility unchanged).

# Apocalypse maps: no mob respawn during the event (retail clear-gating groundwork)

Infernia A/B, Procella, and Abaddon are clear-gated on the official servers — players must kill every mob to advance (portal opens / boss spawns), so mobs must not respawn during the event. Verified against both original source trees (HelbreathServer and HB382_CENTUU): the leak never had this — its spot generators refill all maps unconditionally and the entire progression chain (`OpenApocalypseGate`, the `ApocalypseMobGenType == 2` boss trigger) is commented-out stubs. Implemented per official behavior as described by Nick.

### Server
- New `CEntityManager::apocalypse_suppresses_spawns(map_index)`: while Apocalypse mode is active, both spawn paths (spot generators and the random mob generator) skip maps with `apocalypse_map` set and `apocalypse_mob_gen_type != 0` — exactly inferniaa, inferniab, procella, abaddon. Maze and Druncnian City (`gen_type = 0`) keep regenerating, matching their traverse-only role.
- Between events the generators run normally, so each apocalypse starts with fully populated maps. Kill-time/decay counters are untouched — the suppression is a pure computed predicate, so no state can desync when the event ends mid-corpse-decay.

### Versioning
- Server → 0.8.3 (server-only).

# /clearnpc: optional "rewards" flag (exp/drops), gated by its own admin level

### Server
- `/clearnpc rewards` (combinable with `all`, any order) attributes every kill to the admin as attacker, so the normal death rewards fire: exp share, loot rolls, quest credit — and the type-81 `AbaddonKilled` broadcast, which makes boss-death paths testable via the command. Without the flag, kills stay attacker-less (no exp/drops), as before.
- The flag is gated by a new **sub-permission** `clearnpc.rewards` in the existing `admin_command_permissions` table, seeded at default level 1000 (Administrator) while base `/clearnpc` stays 500 (Developer). Tunable like any command via the DB or `setcmdlevel clearnpc.rewards <level>`; dotted keys can never collide with real command dispatch. An under-leveled admin using the flag gets a notice with the required level.
- `CEntityManager::kill_entities_on_map` gained an `attacker_h` parameter (0 = system kill) — no new reward logic; the flag just selects the attacker identity the existing death path already branches on.
- `setcmdlevel` listing now prints sub-permission keys without the `/` prefix so they don't read as typeable commands.

### Versioning
- Server → 0.8.2 (server-only).

# Admin command: /clearnpc — kill all NPCs on the current map

New admin chat command, built for event testing — e.g. simulating players clearing Apocalypse maps (Infernia → Maze → Procella → Abaddon progression) without hand-killing hundreds of mobs.

### Server
- `/clearnpc` kills every monster (side 10), summon, and spot-generator mob on the admin's current map. `/clearnpc all` additionally kills static town/faction NPCs (shopkeepers, guards, gates) — those spawn once at boot, so the reply warns they only return on server restart.
- Kills route through the shared death path (`on_entity_killed` with no attacker — the summon-expiry / war-teardown idiom): death animation, corpse and decay, natural respawn cycle, and the same `m_total_alive_object` bookkeeping player kills drive. No exp and no drops (both are attacker-gated), so the command can't be farmed. Explosive mobs still detonate on death, as they would for a player kill.
- New `CEntityManager::kill_entities_on_map(map_index, include_static)` owns the map-scoped kill loop and the static-vs-regenerating classification; the command class is a thin arg-parse + notify shell like `/spawn`.
- Default permission: Developer (500), GM mode required; seeded into `admin_command_permissions` on first boot like every command, adjustable in the DB.

### Versioning
- Server → 0.8.1 (server-only; chat commands ride the existing protocol — no compatibility bump).

# Faithful frozen recolor + weapon afterimage (completes issue #3 code work)

Replicates the original DDraw client's two **scaled-base** special-case draws — the last code items on the #2/#3 faithful-coloring workstream. Both now use the additive-offset blends (`offset_tinted`/`additive_tinted`) that #21 established for worn gear, instead of the multiplicative stand-ins, so sprite highlights survive.

**Naming correction:** the `−base/2` body recolor is the **Frozen** status (original `sStatus & 0x40`, drawn opaque via `PutSpriteRGB`), not "berserk" as #3's notes loosely labeled it. Berserk in the original (`& 0x20`) is only a subtle `(0,−5,−5)` overlay, and the `−base/2`/`−base/3` draws are gated on Frozen / the swing frame, not berserk. The ticket body's "replicate where they exist" settles it: `−base/2` lands on Frozen, `−base/3` on the swing afterimage.

### Client
- New `CGame::frozen_tint_params()` — the original Frozen body recolor: opaque additive offset `clamp(src + (regular[10] − regular[0]/2))` (icy blue, highlight-preserving). Replaces the multiplicative `tint(94,160,208)` in `RenderHelpers::draw_body`, the single body-draw chokepoint every render state routes through (the original repeated this recolor in ~20 places).
- New `CGame::afterimage_tint_params()` — the original weapon/body swing afterimage: transparent additive offset `clamp(src + (regular[10] − regular[0]/3))` (DDraw `PutTransSpriteRGB`). Replaces the fixed `tinted_alpha(126,192,242,0.7)` swing trail at all six original-afterimage sites: the attack swing trail (`PlayerRenderer` OnAttack + OnAttackMove) and the magic-dash body+weapon+shield ghost (`PlayerRenderer` + `NpcRenderer`).
- Both helpers read the runtime palette (regular indices 10/0), so they track server palette hot-reloads like the rest of the dye system. Default additive alpha matches the sibling weapon-glare overlay.
- The Haste trail (5 stacked body ghosts) keeps its own fixed light-blue tint — a modern-only effect with no `−base/N` analogue in the original; now commented as intentional.

### Not changed
- Berserk `(0,−5,−5)` and protection-from-magic `(−5,0,+5)` body overlays are fixed offsets, not scaled-base draws — outside this ticket's scope.
- Manual side-by-side of the five reference items on the character doll vs the original client remains the open QA item on #3.

### Versioning
- Client → 0.6.1 (client-only render change; compatibility and server unchanged).

# Worn equipment coloring: original additive-offset dye (fixes DK/DM flat-black armor)

Resolves issue #21 — the worn-equipment slice of the #2/#3 faithful-coloring workstream. Gear on in-world characters and the character select/create preview was tinted **multiplicatively** (`DrawParams::tint`), which scales each texel by `palette[color]/255` — crushing a 255 specular highlight to 40 for the Dark Knight/Mage armor (palette entry 6 = 40,40,40) and rendering it flat black. The original DDraw client (and the already-fixed inventory icons, `draw_item_sprite`) instead **add a signed offset** `clamp(src + palette[color] - palette[0])` (base = regular index 0, gray 100,100,100), which preserves highlights for the glossy dark look.

### Client
- New `CGame::worn_tint_params(color, weapon)` — the original PutSpriteRGB additive-offset dye, mirroring `draw_item_sprite`'s math so worn gear matches its icon. Weapon colors 1-9 read the weapon sub-palette (dual-palette rule); armor/shield/mantle/pants/boots/arm/helm read the regular table. Worn colors are 4-bit (0-15); color 0 = plain draw (unchanged).
- `RenderHelpers.cpp` (`draw_equip_layer`, `draw_weapon`, `draw_shield`) and the menu-preview path (`draw_object_on_move_for_menu`) now route through it. Hair keeps its extended-palette (32-47) absolute tint per #3.

### Engine (SFMLEngine)
- The additive-offset fragment shader now multiplies its result by `gl_Color` (was applying only `gl_Color.a`), and the offset draw path no longer force-overwrites the sprite color to white. Together these let night-mode darkening (and alpha) reach offset-tinted draws, so worn gear keeps dimming at night in step with the body it sits on. Backward-compatible: existing offset callers pass white RGB (a no-op multiply), and item icons (no alpha-effect) are unaffected.

### Not changed (remaining #3 follow-ups)
- Weapon/shield glare overlays and berserk glow are untouched; the original's scaled-base special-case draws (−base/2 berserk, −base/3 afterimage) are not yet replicated.

# Dark items: evolution trees corrected per official-server behavior

### Evolution chains (player clarification of official HB USA behavior)
- **Dark Knight**: Flameberge → Giant Sword at **+4**, Giant Sword → Dark Knight Templar at **+8** (not glowing), Templar starts glowing when maxed at **+15**. The Great Sword (battle-mage weapon) stays claimable and never evolves — the prior Great Sword→Giant Sword wiring is removed.
- **Dark Mage**: Magic Staff → Magic Wand at **+4**, Magic Wand → **Dark Mage Dragon Wand** at **+8** (not glowing), Dragon Wand starts glowing at **+15**. Item 746 renamed "Dark Mage Templar" → "Dark Mage Dragon Wand" (its real name on official servers; BlackMageTemple in the CENTUU cfg); `ItemId::DarkMageStaff` constant renamed `DarkMageDragonWand`.
- The +15 glow is now its own final stage on both lines: reaching max sets instance color 9 in place (no model change), lighting the client's pulsating `dk_glare`.
- Thresholds fire on the +2 upgrades that reach +4/+8/+15 (conditions ≥2/≥6/≥14), so odd-valued legacy items still evolve on their next upgrade.

### Versioning
- Server → 0.8.0 (server-only; claim menu and protocol unchanged).

# Dark items: official HB USA progression confirmed and implemented

### Primary source found
- The official Helbreath USA Dark Knight guide (helbreathusa.com/darkknightinfo.php, archived 2008-03-08) settles the design: at 180 a GM email placed the armor plus "a weapon or wand depending on your character stats"; upgrades step **+2** (+0→+2→…→+14→+15) with costs 2, 4, 7, 11, 16, 22, 29, 37 — exactly the CENTUU `x(x+6)/8+2` formula at even levels, 128 points total, no fail chance. Our implementation now reproduces that table digit-for-digit.

### Progression tree (documented in CONTEXT.md)
- Symmetric evolution chains, base forms claimable at City Hall, evolutions earned: **Great Sword → Giant Sword (+12) → Dark Knight Templar (+15)** and **Magic Staff → Magic Wand (+12) → Dark Mage Templar (+15)**. Flameberge and Rapier are standalone.
- Claim menu trimmed to base forms: Giant Sword and Magic Wand removed (earned via the chains); male-only Scale Mail / Leather Armor removed outright, which also retires the client's sex-greying machinery.
- Both Templar transforms stamp instance color 9 — the client's existing `dk_glare` renders the legacy pulsating glow (blue DK / green DM).

### Server (ItemManager.cpp)
- Majestic weapon/wand upgrades step +2 (thresholds re-anchored to >=10 / >=14 so stages fire on the upgrades reaching +12 / +15).
- New `transform_majestic_item()` helper replaces six copy-pasted delete/re-init/re-bind blocks; dead shark-item ids (703/709/736) dropped from the weapon route; ids now use named `ItemId` constants.

### Data (gamedata.db)
- Dark Knight Full-Helm (707/725) had the Hauberk's `display_id` (21) — claiming a Full-Helm visually produced a hauberk. Corrected to the full-helm visual (0).

### Tracked
- Armor "flat black vs original glossy" tint difference filed as issue #21 (multiply-tint vs DDraw palette swap — rendering-model work, separate pass).

### Versioning
- Server → 0.7.0, Client → 0.6.0 (compatibility unchanged at 0.6.0 — no protocol change).

# Fix: Dark item majestic evolution stages now actually fire

### Research findings (vs HB382_CENTUU)
- The majestic upgrade **cost formula was already faithful** on both sides: `x(x+6)/8 + 2` points at level x (2, 2, 4, 5, 7, 8, 11, 13, 16, 18, 22, 25, 29, 32, 37) — server routes and the client upgrade dialog agree. Not n+1.
- The **stat gates were already faithful**: `equip_item_handler` enforces the original per-stat armor requirements (`item_effect_value4/5`; the Dark Mage Robe requires MAG 100, same as wizard robes) plus the weight rule (`weight > STR x 1000` — Dark Knight Plate Mail needs STR 100). The original gates at equip time, not at claim time; most DK/DM pieces carry no stat requirement in any legacy data.
- The **evolution stages were dead code in CENTUU itself** (inherited by our port): the handler rejects items already at +15 before the `== 15` transform branches can run, so only staff→wand at +11→12 was ever reachable. The 746→892 stage references an item defined in no known config or DB — had it ever fired it would have destroyed the wand.

### Server (ItemManager.cpp)
- Wand line now completes: Dark Mage Magic Staff → Magic Wand at +12 (unchanged), Magic Wand → **Dark Mage Templar** on the upgrade that reaches +15 (re-anchored from the unreachable `== 15` to `== 14`). The 892 branch is removed; the Templar is the final stage.
- Sword line now has its stage: Dark Knight Giant Sword → **Dark Knight Templar** on the upgrade that reaches +15, mirroring the wand line. Designed deviation: legacy data ships 737/745 with identical stats (the stage is the prestige appearance) but no legacy source ever wired the transform.
- Legacy bug fix: the female Dark Mage Magic Staff (732) was excluded from the staff→wand transform (only 714 was checked) and could never evolve.
- Great Sword, Flameberge, and Rapier have no evolution in any legacy source — left as plain majestic-upgradeable, matching CENTUU.

### Versioning
- Server → 0.6.0 (server-only behavior; transforms ride the existing GizonItemChange notify, no protocol change).

# Feature: Dark Knight / Dark Mage items claimable at City Hall at max level

### Design note
- No legacy source implements a City Hall handout of the DK/DM sets (CENTUU only reaches them via majestic upgrades; Snoopy 3.82 has an evil-side event-666 set). This is a deliberate designed addition modeled on the hero-item flow: at max level (180), citizens claim the Dark items from the City Hall Officer — free, re-claimable, and unique-owner bound (never tradeable).

### Server
- New `ItemManager::get_dark_item_handler` (separate from the hero mantle handler): validates level >= max_level and citizenship, resolves the sex variant server-side from a named-constant table (`female == 0` marks male-only pieces like the mage scale mail / leather armor), grants with `UniqueOwner` binding, logs to the events channel.
- New `CommonType::ReqGetDarkItem` (0x0A7B, placed clear of the tester-only id block).

### Client
- Three new City Hall dialog pages (separate from the hero pages): class select (Dark Knight / Dark Mage), 8-piece item list per class, confirm. Entry row "Take the DARK's items" on the main City Hall menu, enabled at max level. Male-only pieces grey out for female characters. Draw and click share the same `ui_rect` hit-rects, and the hero/dark confirm pages now share one draw/click implementation.
- The client sends only the base item id; the server owns variant resolution (unlike the hero flow, where the client picks the final id).

### Shared / data
- `ItemId`: named constants added for the full DK/DM sets (706-738). Fixed stale constants `SwordofMedusa`/`SwordofIceElemental` (724/725 → 613/614, matching gamedata.db) — they had collided with the female DK hauberk/full-helm ids in `is_special_item()`.
- gamedata.db: item 706 renamed "Dark Knight Full-Helm (M)" → "Dark Knight Hauberk (M)" (its stats were always the hauberk's; 707 is the helm). The Flameberge stays consolidated as unisex 727; 709 intentionally not re-added.

### Versioning
- Compatibility → 0.6.0 (new packet), Client → 0.5.0, Server → 0.5.0.

# Fix: Illusion-Movement spells dealt damage instead of inverting movement

### Game data (gamedata.db, magic_configs)
- `Illusion-Movement` (77) and `Mass-Illusion-Movement` (95) were configured as magic type 21 (`damage_area_nospot`, the Meteor-Strike class), so they dealt 7d7+20 area damage. This bug is inherited from the original HGServer/CENTUU `Magic.cfg`; the confuse handler (type 16, value4=4) that inverts the victim's movement was never reachable. Server (`MagicManager` Confuse case 4) and client (`m_illusion_mvt` direction reversal) code paths were already correct — data-only fix, no rebuild.
- New values per the Snoopy 3.82 reference cfg: type 16, value4=4, damage dice zeroed; durations 30s/60s; Mass radius 3x3 (matching Mass-Illusion). Mana/gold/int-limit untouched.

# Release 0.4.7 DEPLOYED: launcher + Medieval Times loading screen live

### Deployed (2026-07-23, via ssh to the Debian box)
- `Launcher_x64_win.exe`, `Game_x64_win.exe` (0.4.7), `intermediate_screens.pak`, and the 0.4.7 manifest uploaded to `~/helbreath/updates/game` (manifest last, so no client ever saw a stale-file window). All three payloads SHA-verified back through the running update server; live manifest confirmed 0.4.7 / 652 entries from the public endpoint.
- Clean-room verification: the shipped Release launcher in an empty folder against production shows "Latest: 0.4.7" with the Install flow.
- Existing players now pull ~11 MB (new client + loading screen + the launcher itself) on next launch; once on 0.4.7, launcher-spawned sessions skip the embedded update check.

# Release 0.4.7 staged: launcher ships + Medieval Times loading screen

### Versioning
- **Client → 0.4.7** (launcher env-gate in Wmain; `_CRT_SECURE_NO_WARNINGS` for the `std::getenv` read, matching the error_monitor pattern). Release built, Sentry symbols uploaded (client + launcher + server PDBs).
- Launcher 0.1.0-alpha Release built.

### Deploy staging (Binaries/Game)
- `Game_x64_win.exe` (0.4.7), `Launcher_x64_win.exe` (new), rebranded `sprites/interface/intermediate_screens.pak` staged; production manifest regenerated as 0.4.7 (652 entries). Diff vs live 0.4.6: +1 added (launcher), 2 changed (client exe, loading screen), 0 dropped — ~11 MB player download.

# Launcher E2E verified + update-endpoint override + skip-list fixes

### Launcher / AutoUpdater
- New `update_override.txt` (beside launcher or game exe): "host port" redirects the update server — staging/testing analogue of `server_override.txt`. Read by both the launcher and the client's embedded updater.
- Full E2E pass against a local `update_server.py` instance: fresh 654-file install (SHA-verified, launcher excluded from install, staging cleaned), instant up-to-date relaunch, settings read-modify-write (foreign JSON keys preserved), 1-file update flow, launcher self-update round-trip (binary swapped, no residue, no relaunch loop), Play spawning the game with correct working directory.

### Tooling
- `server_override.txt` and `update_override.txt` added to `gen_update_manifest.py` and `make_installer.py` skip lists — machine-local overrides are never shipped or clobbered by updates (restores the documented `server_override.txt` guarantee, which had regressed).

# Launcher: 3x window, branded logo overlay, artwork as embedded binary

### Launcher
- Window scaled to 1440x1080 via a single `layout::ui_scale` knob (rects, fonts, border widths all derive from it; art regenerated to match).
- The silver "Helbreath: Medieval Times" logo is now composited onto the launcher backdrop (top-center, matching the game loading screen). Logo asset lives at `tools/hbmt_logo.png`; `gen_launcher_art.py` composites it during baking.
- Logo cleanup: the left subtitle ornament carried emblem fragments from the original luminance-key — both ornaments now use the clean right-side winged cross (mirrored). The in-game loading screen PAK was rebuilt with the corrected logo too.
- Artwork embedding switched from a C array header (6M elements blew MSVC's compiler heap at 3x) to `launcher_art.bin` + RCDATA resource on Windows; Linux will use `.incbin`. `launcher_art.h` now carries dimensions only.

# Launcher: full state machine — Install / Update / Play (Windows)

### Launcher
- The launcher now checks first and acts second: fetches the manifest, then presents exactly one primary action — **Play Game** (up to date), **Update** (N files, size shown), **Install** (game not on disk: folder picker with default path, writability probe, non-empty-folder double-confirm), or **Retry** (server unreachable with nothing installed). Installed + unreachable = playable offline with an indicator.
- Launch options in the launcher: Windowed/Borderless/Fullscreen and resolution cycle selectors, persisted straight into the game's `settings.json` (read-modify-write preserving all client-owned keys, `.bak` before write).
- Self-update: if the manifest carries a newer launcher, it stages its own `.update`, swaps and relaunches — with a loop guard against bad uploads. Launcher entries are always excluded from game plans.
- `version.txt` written to the install dir after every successful install/update; "Installed: X | Latest: Y" shown in the window.
- Game processes spawned with `HB_LAUNCHED_BY_LAUNCHER=1` and CWD = install dir.
- Cancel/close mid-download keeps staging — next Install/Update resumes where it left off.

### Client
- Embedded update check now skipped when spawned by the launcher (env flag); still runs on direct exe launches as a safety net. **Client → 0.4.7** on next release build.

### Tooling
- `gen_update_manifest.py` + `make_installer.py` skip `launcher.json`/`version.txt` (installer also skips `settings.json` + manifest); installer shortcut/run target is now `Launcher_x64_win.exe`, display name "Helbreath: Medieval Times".

# Launcher skeleton: branded window with custom widgets (Windows)

### Launcher
- New `Sources/Launcher/` project (`Launcher_x64_win.exe`, in the solution + `build.ps1 -Target Launcher`): 480x360 branded window — splash artwork backdrop, status line, progress bar, primary CTA button, launch-mode + resolution cycle selectors, install-path field with native folder browser (IFileDialog). All widgets custom-drawn from shared layout rects (`launcher_layout.h`) so the upcoming X11 backend renders identically; double-buffered GDI, event-queue interface (`launcher_gui.h`).
- Pending self-update swap (`.update`/`.old` + re-exec) wired at entry.
- State machine, install/update/play flows land next — current build is the UI shell.

# Launcher version track + embedded artwork generator

### Build
- `[Launcher Version]` (0.1.0-alpha) added to `version.cfg`; `version_gen.py` now emits `hb::version::launcher`, `VER_LAUNCHER_*` RC defines, `HB_LAUNCHER_*` CMake vars, and manages `build_counter_launcher.txt` (`--target launcher`).
- New `tools/gen_launcher_art.py`: bakes the splash artwork into `Sources/Launcher/launcher_art.h` (480x360 raw BGRX, bottom strip pre-darkened for launcher chrome). Run manually on art changes; header is committed — no build-time Pillow dependency.

# AutoUpdater core refactor: scan/apply split (launcher groundwork)

### AutoUpdater
- New GUI-free engine `updater_core.h/.cpp`: `scan(target_dir)` (manifest fetch + SHA compare → `update_plan`, manifest version now surfaced for display) and `apply(plan, target_dir, exe_strategy, progress_callback)` (parallel download with staged-hash resume, verify, disperse). Callers own UI, retry policy, and target directory — groundwork for the standalone launcher.
- `check_for_updates()` reduced to a thin wrapper over the core; in-client behavior unchanged (same dialogs, same progress window, same `.update`/`.old` exe-swap).
- New `exe_strategy`: `stage_for_swap` (in-process self-update, unchanged) vs `write_direct` (launcher updating a not-running game).
- Data-file disperse now renames from staging (same volume) with copy fallback — cheaper, and atomically replaces busy binaries on Linux.
- New constant `launcher_exe_name` (`Launcher_x64_win.exe` / `Launcher_x64_linux`).

# Sentry dashboard config (not in repo)

### Ops
- Stack Trace Rules added to the Sentry project (Settings → Issue Grouping): `stack.package` globs for our three binaries and `stack.abs_path` globs for both source roots mark our frames `+app`; CRT/runtime scaffolding (`**/vcstartup/**`, `invoke_main`, `__scrt_common_main_seh`, `_start`, `_fini`) demoted `-app`. Verified on both platforms — only project code highlights in-app.
- Symbols current for: client 0.4.6 (Windows PDB), server 0.4.4 (Linux DWARF + Windows PDB), bundled sentry.dll.

# Client --testcrash flag (crash-pipeline verification)

### Client
- `--testcrash` command-line flag crashes immediately after Sentry init — client counterpart of the server's `testcrash confirm`. Verified: Release client crash uploads at crash time, tagged `production`. **Client → 0.4.6**, deployed to the update server.

# Release builds now produce PDBs (Sentry symbolication)

### Build
- Client and Server Release configs link with `GenerateDebugInformation=true` — PDBs are local-only (never in the update manifest) but required for symbolicated crash reports from shipped builds.
- Client 0.4.5 deployed to the update server (651 files incl. `sentry.dll`, `crashpad_handler.exe`, `crashpad_wer.dll`); players self-update on next launch.

# Sentry environment override (SENTRY_ENVIRONMENT)

### Shared
- `SENTRY_ENVIRONMENT` env var overrides the build-type-derived environment tag, so deployments self-describe. The Debian test box now reports `staging` (set in its systemd unit) — `production` stays reserved for the eventual real production host.

### Versioning
- **Server → 0.4.4**, **Client → 0.4.5**.

# Sentry crash reporting + error monitoring (client & server)

### Shared
- New `hb::shared::error_monitor` (`Dependencies/Shared/Util/error_monitor.{h,cpp}`) wrapping the Sentry Native SDK 0.15.4 (crashpad backend). One call at the top of each `main()` — `auto monitoring = error_monitor::start("helbreath-server", hb::version::server::full_version);` — returns an RAII session that flushes and closes on any exit path. Release string, environment (`development` for debug builds, `production` otherwise), DSN, and crashpad handler location are all owned by the wrapper; `SENTRY_DSN` env var overrides the DSN (empty disables). Manual `capture_message`/`capture_error`/`add_breadcrumb` available for non-fatal reports. Targets without the SDK compile the same API as no-op stubs (`HB_SENTRY` gate).

### Build
- Windows: prebuilt SDK bundled at `Sources/Dependencies/sentry-native/` (import lib + `sentry.dll` + `crashpad_handler.exe`, mirroring the SFML layout); rebuild via `Scripts/build_sentry.ps1`. Runtime DLLs copied to `Binaries/Game/`, `Binaries/Server/`, `Sources/Debug/`.
- Linux: `Sources/cmake/sentry.cmake` (`hb_enable_sentry(<target>)`) fetches and statically links the SDK in both CMake builds and copies `crashpad_handler` next to the binary post-build. Opt out with `-DHB_ENABLE_SENTRY=OFF`. New build-host prerequisites: `libcurl4-openssl-dev zlib1g-dev`. **Not yet built on Linux — verify on the Debian box before next deploy, and ship `crashpad_handler` alongside the server binary there.**

### Server
- New console command `testcrash confirm` — deliberate null-dereference to verify the crash pipeline end-to-end (warns and requires the literal `confirm`; does not save players). Verified 2026-07-23: minidump written and uploaded by crashpad at crash time (crashpad metadata shows `uploaded=1`, attempts=1); uploaded `.dmp` files linger in `.sentry-native/reports/` until crashpad's own pruning — that is not a stuck queue.
- `SENTRY_DEBUG` env var (any value) turns on Sentry SDK internal diagnostics to stderr.

### Tooling
- `Scripts/upload_symbols.ps1` — uploads PDBs + binaries (with source context) to Sentry via sentry-cli (auto-downloaded to gitignored `Scripts/tools/`) so crash stacks symbolicate. Auth via `SENTRY_AUTH_TOKEN` env var or `sentry-cli login`; org/project via params, env, or `.sentryclirc` defaults.
- `Sources/build.ps1` gained an opt-in `-UploadSymbols` switch (runs the upload after a successful build; intended for release/deploy builds).

### Versioning
- **Server → 0.4.3**, **Client → 0.4.4** (no protocol change).

# Fix: shutdown double-free of every NPC (SIGSEGV)

### Server
- `CGame::m_npc_list` is an alias into `CEntityManager`'s entity array, yet `CGame::quit()` (and the re-init sweep in `CGame::init()`) deleted the NPCs through it without nulling the slots — `~CEntityManager`, the real owner, then deleted them all a second time. Every shutdown segfaulted in libc `free` (confirmed via core dump + backtrace on the deployed server). Removed both non-owner delete loops; `~CEntityManager` remains the single authoritative owner.

### Versioning
- **Server → 0.4.2**.

# Headless server console (pipe mode)

### Server
- `ServerConsole` now detects a non-TTY stdin (POSIX) and runs in pipe mode: no raw-terminal setup (previously `tcgetattr` failed and the console went permanently dead under systemd), no prompt/ANSI redraws — commands are read as plain newline-terminated lines. Windows console behavior unchanged.
- Deployed pattern (Debian box): systemd socket unit `helbreath-server.socket` with `ListenFIFO=/run/helbreath.console` feeds the service's stdin — `echo '<command>' > /run/helbreath.console` from any SSH session, output in `journalctl -u helbreath-server -f`.

### Versioning
- **Server → 0.4.1**.

# Local server override for LAN play

### Client
- Optional `server_override.txt` next to the exe (one line, an IP) overrides the compiled-in login server address. Lets LAN players connect directly to the server instead of through router NAT loopback (which proved unreliable for the game handshake on consumer routers). The file is not in the update manifest, so self-updates leave it alone.

### Versioning
- **Client → 0.4.3**.

### Ops (Debian box, not in repo)
- Server logs now line-buffered under systemd (live `journalctl -f`); core dumps enabled (`systemd-coredump` + `LimitCORE=infinity`); server binary rebuilt as RelWithDebInfo — a repeat of the 19:19 segfault will produce a symbolized backtrace via `coredumpctl gdb`.

# Auto-updater hardening + fast parallel downloads

### AutoUpdater (client lib)
- Per-file downloads now retry transient failures (3 attempts with backoff) instead of aborting the whole update on the first dropped connection — the cause of half-installed clients crashing at the sprite-loading screen.
- If a file still fails after retries, the user gets a retry/skip dialog (`show_retry_dialog`, replaces the manifest-only `show_server_unreachable_dialog` with cause-specific messages) instead of silently launching a partially-updated client.
- Downloads run on 4 parallel workers, each reusing one keep-alive HTTP connection (`http_client` class, pimpl over httplib) — collapses the per-file connection-setup cost that made 648-file installs crawl.
- Responses stream straight to disk in chunks instead of buffering whole files in memory.

### Update server (tools/UpdaterServer)
- `update_server.py` speaks HTTP/1.1 so clients can keep connections alive.
- Deployed config: per-IP request cap removed (300/min silently dropped fast LAN installs mid-download), bandwidth cap removed.

### Versioning
- **Client → 0.4.2** (updater is client-side; two bumps: 0.4.1 retry hardening, 0.4.2 parallel/streaming).

# Self-hosted deployment: dedicated Linux server + internet distribution

### Endpoints (client + updater)
- `DEF_SERVER_IP` (release builds) and the auto-updater host now point at the self-hosted dedicated server's public IP (was the old 199.187.160.239 test server). Updater port moved 8080 → 8090 (8080 is occupied by Docker on the host).

### Cross-platform fixes (Linux/GCC server build)
- `GameConfigSqliteStore.h`: added missing `<cstdint>` include (uint8_t members compiled only via MSVC's transitive includes).
- `Server/Game.cpp`: packed struct field passed to the variadic logger now goes through `static_cast<int>` (GCC cannot bind packed fields to references).

### Deployment notes (Debian box, not in repo)
- Server runs as systemd `helbreath-server` (login 2500 / game 9907), update file server as `helbreath-updates` (HTTP 8090) serving the manifest + game data; `mapdata/` filenames and `mapinfo.db` lowercased to match the server's case-sensitive lookups.
- No version bump — rides the 0.4.0 client/server, 0.5.0 compatibility from the entry below.

# Faithful item coloring: dual palette + additive-offset rendering — issue #2

### Rendering (engine + client)
- New `BlendMode::AlphaOffset` + `DrawParams::offset_tinted` in the shared render layer — opaque draw with a signed per-channel offset, matching DDraw `PutSpriteRGB` (`dest = clamp(src + offset)`); reuses the existing colorOffset shader with alpha blending. `additive_tinted` (the `PutTransSpriteRGB` equivalent) gained an alpha parameter.
- `draw_item_sprite` now colors item icons the original way: `pixel + (palette[color] − palette[0])` with base gray (100,100,100) at index 0, replacing the multiplicative tint. Disabled colored icons use the transparent-additive variant at alpha 0.7 (original disabled path); plain disabled unchanged. Extended entries (22+, hair) keep absolute multiplicative tinting.
- Dual palette restored with the original selection rules: in dialogs, hand-equipped items (left/right/two-hand) use the weapon table; on the ground, sword/bow/axe/hammer weapon classes and shields do (wands/rods stay regular, matching the original sprite-category rule). Weapon indexes 10+ fall back to the regular table, replicating the original's unwritten weapon entries ("Gold, buggy" quirk).
- Prefix tints (colors 16–21) draw with their original weapon-table colors (Agile/Light/Strong light-blue, Poisoning green, Critical gold, Sharp heavy-blue, Righteous white, Ancient violet) — stored server-side in the weapon table, so they stay DB-tunable. Name-text dye colors keep the absolute 16–21 entries, unchanged. Prefix tint still applies to all item kinds (owner decision on #2; deviation from the original's ATTACK-only gate).

### Protocol
- `PacketColorPaletteConfigEntry` gained `tableId` (0 = regular, 1 = weapon); both tables stream through the existing hash/cache/hot-reload pipeline. Shared palette band constants (`first_prefix_color`/`last_prefix_color`) live next to the packet.
- Server-side: hash + game-send + login-send now share one `build_color_palette_packets()` builder (removes the triplicated chunking loop).

### Data (gamedata.db)
- One-shot startup migration (keyed on missing weapon-table prefix rows): restores regular entries 0–15 to the original palette values (previous rows held doubled absolutes for multiplicative tinting), creates and seeds `weapon_color_palette` (1–9 originals + 16–21 prefix tints). Entries 16–21/32–47 in the regular table untouched. No item/instance color remapping needed — the earlier unified-palette remap is exactly reversed by table selection + the 16–21 weapon mapping.
- Known transitional look: paper-doll/equipped-weapon and hair rendering still tint multiplicatively from the regular table (issue #3's scope), so those tints render darker until #3 lands.

### Versioning
- **Compatibility → 0.5.0** (palette packet format change; old client/new server rejected at the version gate).
- **Server → 0.4.0**, **Client → 0.4.0**.

# Prefactor: unified item-sprite drawing into one shared helper

### Client (refactor, no behavior change) — issue #1
- Added `CGame::draw_item_sprite(item_draw_ref, x, y, item_color, state, ignore_pivot)` — the single place that owns item-icon coloring semantics: plain draw when `item_color == 0`, palette tint otherwise; `item_draw_state::disabled` covers the locked/grayed variants (alpha 0.25 plain / tinted-alpha 0.7 colored); `ignore_pivot` covers ground items.
- Routed all nine inline tint sites through it: inventory, bank, character screen (equipped items), exchange, sell/repair, item upgrade (both modes), Trading Post icons, the drag-preview cursor, and ground items. The follow-up coloring-math change is now a one-place edit.
- Verified by diff review that identical draw params reach the engine; remaining `m_color_palette` uses are out of scope (hair/paper-doll entity rendering and tooltip *text* dye color). Site-specific overlays (character-screen hover highlight, upgrade flicker) stay with their callers.
- Cleanup along the way: dropped now-single-use `item_color` locals and dead `time` locals in the touched functions.

### Versioning
- **Client → 0.3.1** (client-only internal refactor).

# Original-faithful balance restore (post-merge ratification)

### Balance / Formulas
- Applied the full restore set from `PLANS/Formula_Faithfulness_Audit.md`, reverting ShadowEvil's final-week live tuning to the original game's formulas (each verified line-by-line against `HGServer`): `max_sp` back to `(str + angelic_str) * 2 + level * 2` (his live value had halved the STR term); `level_exp` back to the original per-level curve `level * (50 + level * trunc(level/17)²)` (his cubic was ~5–6× grindier mid-game, converging only near the cap); regen rolls restored to original means — HP keeps its original `1d(vit)` floored at `vit/2` with Dev's added variance bonus removed, MP back to a plain `1d(mag + angelic_mag)` (floor and variance removed), SP back to `1d(vit/3)` (was a flatter `0.15·vit` curve).
- `mana_regen_ms` 15000 → 20000, matching the original `DEF_MPUPTIME`; combined with the roll restore this brings MP regen down from ≈2.2× original to original rate.
- Data-only change (formulas table + `server_config.json`) — hot-swappable per row via console `reload formulas` for A/B comparison during the Phase-6 play pass. Known remaining deviation (deliberate, would need code): the original low-level SP regen band (+15/+10/+5 under levels 20/40/60) is not restored; revisit if low-level play feels stamina-starved.
- Seed mirrors kept in sync: `Scripts/setup_gamedata.py` and `gameconfigs-07222026.sql`.

### Versioning
- **Server → 0.3.1** (server-only gameplay change; no protocol or client impact).

# Development merge — ShadowEvil's final 35 commits adopted

### Merge (branch `merge-development`)
- Merged `upstream/Development` (2026-02-24 → 2026-03-04, tip `506f7f1`) into master's July line: item instance redesign (`cur_durability` + unpacked `custom_made`/`prefix`/`secondary`/`enchant` fields, `ItemInstanceData`), item type system redesign (EquipPos `Pants`→`Leggings`→`Boots` shifts, renamed equipment atlases), NPC damage/XP rework with DB-driven creation items, config push to client at login (server config, color palette, attribute types), RegenManager extraction, and the guild system full gut (shell classes kept for the SQLite rebuild). All July work preserved: Trading Post phases 1–5, war-event admin commands, boss drop system (guaranteed tier-2, delayed second drop, 5×5 scatter), anti-hack fixes, icebound activation.
- Swing anti-hack checks kept our frozen-term removal by intent on Dev's new code-based `swing_time`/balance-constants API (both the per-swing and 7-swing-batch checks; original-source-verified — frozen never speeds swings).
- `GameCmdSpawn`'s `create_new_npc` call had a latent pre-merge bug: its final `true` bound to the old `guild_guid` parameter, so `bypass_mob_limit` silently defaulted to `false`. The signature change makes the same call bind as its comment always intended.

### Data
- `gamedata.db` rebased onto Dev's (his balance/creation/palette/attribute data), then our July deltas re-applied by script and dump-diff verified: drop-table `guaranteed_secondary`/`scatter_count` values, `active_maps` icebound row, Vince (`npc_configs` 116) translated through Dev's own hit-dice/damage migration formulas (hp 51–60, 1–1 damage).
- **`max_load` re-scaled for Dev's weight system**: his rework stores weights in thousandths of a stone (all DB weights ×10; `weight_units_per_stone` 100→1000), and his seed still carried the ×100-too-small regression we fixed in July. The original-faithful capacity in his units is `str*5000 + angelic_str*5000 + level*5000` (was ×500 in our hundredths units). Character-screen weight display now shows both current and max load in stones with 2 decimals (Dev's side showed raw max units).
- `MapInfo.db` kept ours (Vince map_npcs/waypoints); Dev's only change since merge-base — the `map_teleport_locations` rework routing arebrk11/elvbrk11/middled1n exits through arefarm/elvfarm — re-applied wholesale and dump-diff verified.
- Account DBs migrated with Dev's scripts in commit order: packed `attribute` → instance columns, `cur_lifespan` → `cur_durability`, guild/fightzone columns stripped.
- `Scripts/setup_gamedata.py` seed now mirrors the reconciled live formulas table (Dev's in-DB tuning of `level_exp`/`max_sp`/`sp_regen_*` was newer than his own seed; swing/stat-pool rows retired to `BalanceConstants.h`/ServerConfig). Fresh `gameconfigs-07222026.sql`/`mapinfo-07222026.sql` snapshots; stale 02162026 pair retired.

### Trading Post (adaptation to the item-instance model)
- Escrow store schema (`listing_items`/`offer_items`) re-mirrors the new `character_bank_items`: `cur_durability` + the six attribute columns replace `cur_lifespan` + packed `attribute`. Escrow-in/out copies use `CItem::copy_attributes_to`/`load_attributes_from`; `build_item` mirrors the bank-row deserialization exactly (custom-made durability override included); offline Warehouse delivery writes the new `AccountDbBankItemRow` columns.
- Wire structs reworked (protocol unreleased, compat bumped to 0.4.0 with the merge): `TpItemBrief` carries id/count + the name-affecting instance fields; `TpItemFull` fully mirrors `item_instance_data`. Client renders listing/offer names via `item_name_formatter::format(item_id, item_instance_data)` — prefixed/enchanted names and dye colors now correct in board rows, detail bundles, and offer rows; icon tint uses the server-pushed color palette.
- Local `tradingpost.db` deleted (dev-test escrow, backed up pre-merge); the server recreates it with the new schema on startup. ADR 0001 escrow physics unchanged.

### Versioning
- **Compatibility → 0.4.0** (protocol changed in both directions: guild/fightzone message families removed, durability renames, config-push messages, Tp wire rework). **Server → 0.3.0**, **Client → 0.3.0**. Build counters continue from the max of both branches (client 357, server 247).

### Pending
- Phase-6 test matrix (login/creation on migrated accounts, TP full loop, war commands, boss drops, guild-absence sanity) and the Linux server/client build gates (no WSL distro on this machine).

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
