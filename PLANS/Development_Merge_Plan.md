# Development Merge Plan — `origin/Development` → `master`

**Goal:** Adopt ShadowEvil's final 35 commits (2026-02-24 → 2026-03-04, tip `506f7f1` "Remove Guild System (Full Gut)") into `master` without losing any of Nick's July work — 7 committed fixes plus the uncommitted Trading Post (phases 1–5) and war-event admin commands.

**Execution model:** All `git` operations are run by Nick (per project rules). Claude assists with conflict resolution content, compile fixes, DB reconciliation scripts, and verification. `bak.py` is not used for the merge itself (git is the checkpoint mechanism here); it resumes for normal work after the merge lands.

**Analysis basis (2026-07-22):** merge-base `0de3d1d`; Dev side = 342 files, +29,939/−13,545; master committed side = 39 files; uncommitted worktree = 74 entries. `git merge-tree master origin/Development` reports 16 conflicted paths at the committed level; committing the Trading Post work first adds a small, known set on top.

---

## What Development brings (why this merge is worth it)

- **Guild system full gut** (`506f7f1`) — legacy file-based guild + fightzone removed everywhere (protocol, server, client, DB columns); shell classes kept for a clean SQLite-backed rebuild. Includes `Scripts/migrate_strip_guild_columns.py`.
- **Item instance redesign** (`23798ed`, `7786315`) — `cur_lifespan` → `cur_durability`; packed `attribute` bitfield → `custom_made`/`prefix_type`/`prefix_value`/`secondary_type`/`secondary_value`/`enchant_bonus`; new `ItemInstanceData.h` (`CItem::m_instance`); 32-bit damage pipeline; account-DB migrations.
- **Item type system redesign** (`94e6151`) — `EquipPos` renamed/shifted (`Pants`→`Leggings`, `Leggings`→`Boots`); config-driven `m_hide_armor`/`m_is_skirt`; equipment atlas paks renamed (`item-equipm.pak`→`equip_models.pak`, etc.).
- **NPC damage/XP rework + DB-driven creation items** (`d641f13`, `7786315`) — hit-dice migration, exp rebalance, `creation_items` table replaces hardcoded starter items, creation-stat validation.
- **Config push to client** — new `ServerConfigUpdate`, `ColorPaletteConfigContents`, `AttributeTypeConfigContents` messages sent at login.
- **RegenManager extraction** (`8ceb462`), **IGameScreen/GameModeManager simplification** (`1e806f0`), color palette migration, stat clamping/enforcement, new tools (`NpcEditor`, `ExpCurve`, `MapRenderer`, `CreationItemManager`, `regen_sim`).
- Docs: `CLAUDE_AGENTS.md`, `CLAUDE_LOGGING.md` added.

## The one big semantic collision

`TradingPostStore` (tables `listing_items`/`offer_items`) **mirrors `character_bank_items` columns**, and `deliver_to_bank` does direct INSERTs into account DBs. Dev reshaped that exact schema and the `AccountDbItemRow`/`AccountDbBankItemRow` structs. The TP wire structs (`TpItemBrief` carries `attribute`; `TpItemFull` mirrors the old bank row) encode the old model too. → Phase 5 reworks TP to the new item-instance model. This is contained (TP protocol is unreleased — changing it is free) but must be planned, not discovered mid-merge.

---

## Phase 0 — Preserve & back up (Nick, git + file copies)

1. Pin the abandoned upstream work locally: `git branch shadowevil-development origin/Development` and `git branch legacy-ddraw origin/Legacy-DDRAW`.
2. Push `master` + both branches to a remote Nick owns (origin is shadowevil's abandoned repo; assume no push rights, and don't push there anyway).
3. Plain-file backups (outside git) of: `Binaries/Server/gamedata.db`, `MapInfo.db`, `tradingpost.db`, `Binaries/Server/accounts/` (all `.db`), `Binaries/Server/gameconfigs/*.sql`.

## Phase 1 — Commit the in-flight work (Nick)

Commit the working tree BEFORE merging — merging over a dirty tree is not an option, and stash-reapply across a 342-file merge is worse. Suggested slicing:

1. **Commit "Trading Post phases 1–5 (Vince, escrow, dialog; phase-6 test pending)"** — untracked: `PacketTradingPost.h`, `TradingPostStore.*`, `TradingPostManager.*`, `DialogBox_TradingPost.*`, `NetworkMessages_TradingPost.cpp`; modified: `NetMessages.h`, `NetConstants.h`, `SharedPackets.h`, `OwnerType.h`, client `Game.cpp`/`Screen_OnGame.cpp`/`Screen_OnGame.Network.cpp`/`DialogBoxIDs.h`/`DialogBoxManager.cpp`/`MapData.cpp`/`NpcRenderer.cpp`/`EquipmentIndices.cpp`/`LAN_ENG.H`/`lan_eng.h`, server `Game.cpp`/`Game.h`/`LoginServer.cpp`, project files, `MapInfo.db` + `gamedata.db` data rows, both `gameconfigs/*.sql` seeds, version files (compat 0.3.1 bump).
2. **Commit "War-event admin commands"** — `GameCmdBegin*/GameCmdEnd*` (12 new files), `WarManager.cpp/.h`, `GameChatCommand.cpp`, `ServerMessages.h`, server version 0.2.0 bump. (If untangling version-file overlap between the two commits is annoying, one combined commit is acceptable.)
3. Leftovers: `.claude/settings.local.json`, `Binaries/Game` caches/settings, `.lib` files, deleted `.obj.enc` — commit separately as housekeeping or leave dirty-but-known; decide `Binaries/Server/GameData/` (untracked, unknown) and `janglez*.db` test accounts deliberately (test accounts probably stay untracked).

Gate: build `-Target All` green before merging (should be — this is the tested TP state).

## Phase 2 — The merge (Nick runs git; resolve together)

```
git checkout -b merge-development master
git merge origin/Development        # expect ~20 conflicted paths
```

### Conflict resolution map

**Group A — "take both", mechanical:**
| File | Dev did | Ours did | Resolution |
|---|---|---|---|
| `NetMessages.h` | removed guild/fightzone IDs; renamed `ItemLifeSpanEnd`→`ItemDurabilityEnd`, `CurLifeSpan`→`CurDurability`; added config-push IDs | added `Tp*` (0x0FA31600–0A) + tester IDs | union; no value collisions (verified: Dev's new IDs are 0x0FA314E8–EA) |
| `NetConstants.h` | removed `MaxGuildsmen`, `GuildNameLen` | added `Tp*` limits block | union |
| `SharedPackets.h` | −`PacketGuild.h`, +AttributeType/ColorPalette includes | +`PacketTradingPost.h` include | union |
| `DialogBoxIDs.h` | `GuildHallMenu`→`CommandHallMenu` (id 51 kept) | +`TradingPost = 56` | union |
| `DialogBoxManager.cpp` | CommandHallMenu rename, guild shells | +TradingPost registration | union |
| `Screen_OnGame.Network.cpp` | removed guild/fightzone cases + handlers; +`ServerConfigUpdate` case | +Tp dispatch cases | union |
| client `Game.cpp` | guild/fightzone handler removal, screen-arch changes | +auctioneer click case (opens TP dialog) | union |
| `LoginServer.cpp` | config sends, creation-items rework, appearance flags, guild/fightzone init removal | +TP `void_character` hook in `delete_character` (region Dev didn't touch) | take Dev's rework + keep our hook |
| `WarManager.cpp` | guild-teleport/fightzone logic stubbed (non-overlapping regions ~L300–1500) | rewrote `manual_start/end_heldenian_mode` (~L1802) + kept `WarManager.h` signature change | expect auto-merge; verify our rewritten functions and Dev's stubs both present |
| vcxproj/filters/CMakeLists/sln | file renames/adds/removals | +TP/war files, +NpcSpawner | union of file lists, honoring Dev's renames (`DialogBox_GuildHallMenu`→`DialogBox_CommandHallMenu`) |

**Group B — substantive manual merges:**
- `Sources/Server/Game.cpp` (biggest): Dev removed guild dispatch/notify builders and reworked formula/config/creation; ours adds TesterAction case 10 (Spawn NPC), batch-swing frozen fix, TP dispatch cases, war-command wiring. Resolve hunk-by-hunk; every ours-side addition survives, every Dev removal applies.
- `Sources/Server/Game.h`: member/forward declarations — union (our `trading_post_*` members + Dev's removals/additions).
- `CombatManager.cpp`: take Dev's 32-bit damage pipeline wholesale, then **re-apply our fix by intent, not by hunk**: no `frozen` term in `check_client_attack_frequency` (original-source-verified). Grep `frozen` in both swing checks afterward.
- `EntityManager.cpp/.h`: keep our pending-drop system (`queue_pending_drop`/`spawn_pending_drops`, `PendingDrop[25]`, scatter table, `SecondDropMaxDelayMs`); adopt Dev's `create_new_npc` signature (guild_guid param dropped) and NPC rework. Re-check `npc_behavior_dead` integration — Dev reworked NPC death/XP.
- `DialogBox_Character.cpp`: Dev redesigned parts; re-apply our max_load display `/100` fix by intent.
- `lan_eng.h` **and** `LAN_ENG.H` (both tracked, case-colliding on Windows — in both branches): resolve BOTH index entries to identical content = Dev's durability wording + our `TP_TOAST_*` + corrected freeze notice. Schedule the collapse-to-one-file cleanup as a separate post-merge task (verify the Linux client build, where both paths exist as distinct files and `#include "lan_eng.h"` picks lowercase).
- `CHANGELOG.md`: keep both histories — ours on top, Dev's 2026-03-01…04 entries beneath.

**Group C — version & generated files:**
- `version.cfg`: per-track max of (ours 0.3.1/0.2.0/0.2.48) vs (Dev 0.2.14/0.1.24/0.2.52), then bump for the merge: **compat → 0.4.0** (protocol changed in both directions: guild/fightzone messages removed, durability renames, config-push messages, Tp family), **server → 0.3.0**, **client → 0.3.0** (or minimal +1 bumps — Nick's call; document in changelog).
- `version_info.h`/`version_rc.h`/`version.cmake`/build counters: never hand-merge — resolve `version.cfg`, take max build counters, let `version_gen.py` regenerate on first build, commit the regenerated output.

**Group D — binaries & data (placeholder at merge time, real work in Phase 4):**
- `gamedata.db`: take **theirs** (Dev) at merge; Phase 4 re-applies our deltas.
- `MapInfo.db`: take **ours** at merge (has Vince rows); Phase 4 dump-diffs Dev's changes and re-applies if real.
- `gameconfigs-02162026.sql` (modify/delete — Dev deleted the seed): keep **ours** for now; convention decision deferred (Dev's `Binaries/Server/gameconfigs/mapinfo.db` addition looks accidental — verify then drop).
- `accounts/*.db`, `settings.json`, caches: **ours** (live/local data; Dev's copies are his test data).
- Tracked build artifacts (`.lib`, `.obj.enc`, `Debug_x64/`): take either, first build refreshes them.

Gate: `git merge --continue` only after every conflict is deliberately resolved. Do not build-fix inside the merge commit beyond what resolution requires — mechanical compile fixes go in the next commit for reviewability (or fold in if trivial; Nick's call).

## Phase 3 — Compile-fix pass (expected, mechanical)

- `create_new_npc` call sites on our side (TesterAction case 10) — drop the trailing `guild_guid` arg.
- Renames rippling into our additions: `cur_life_span`→`cur_durability`, `CurLifeSpan`→`CurDurability`, `ItemLifeSpanEnd`→`ItemDurabilityEnd`, `m_attribute`→`m_instance.*`, `EquipPos::Pants` no longer exists (values shifted — audit any numeric equip assumptions, incl. the Vince composite layer list in `NpcRenderer`).
- TP files will compile against old field names in a few places (`TpItemFull`, store row copies) — fix to compile now; semantic rework is Phase 5.
- Grep checklist over ours-side files: `guild_`, `fightzone`, `cur_life_span`, `m_attribute`, `EquipPos::`, `CurLifeSpan`, `item-equipm`, `gamedialog.pak`.
- Gates: Windows `-Target All` Debug green → Linux server + client builds green (case-sensitivity + GCC strictness will catch what MSVC forgives).

## Phase 4 — Data reconciliation (backups from Phase 0 mandatory)

**`gamedata.db`** — base = Dev's (their balance/creation/palette/attribute data lives only in the binary; our deltas are small and fully documented):
1. Re-apply ours: `drop_tables.guaranteed_secondary` (=1 for 20049/20050/20090/20091/20092) and `scatter_count` (15/15/20 for 20090/20091/20092) — the `LoadDropTables` self-healing `ALTER TABLE` recreates the columns automatically; set the values.
2. `active_maps`: insert `(78, 'icebound', 1)`.
3. Formulas: verify `max_load` = `(str + angelic_str) * 500 + level * 500` — Dev's seed likely still carries the 100× regression we fixed; also merge `Scripts/setup_gamedata.py` (conflicted) so its seed matches.
4. `npc_configs`: insert Vince (npc_id 116, type 111, Warehouse-Keeper-derived stats) — confirm the table gained no new required columns first.
5. Verify by dump-diff: `git show <base>:… > base.db`, `sqlite3 x.db .dump`, diff the three states; confirm `creation_items`, palette/attribute tables exist and our rows landed.

**`MapInfo.db`** — base = ours (Vince map_npcs + waypoints ×6). Dump-diff Dev's version vs base; if Dev made real changes, re-apply them onto ours.

**Account DBs** (`shadowevil.db`, `janglez*.db`, any others): run Dev's migrations **in commit order**, reading each script header first: ① `convert_account_db.py` (attribute redesign, Mar 1) → ② `migrate_durability_accounts.py` (Mar 3) → ③ `migrate_strip_guild_columns.py` (Mar 4, makes its own `.guild_migration_bak`). The self-healing `AddColumnIfMissing`/schema paths cover fresh DBs; migrations cover existing ones.

**`tradingpost.db`**: after Phase 5 changes the escrow schema, delete the local file and let the server recreate it (it's dev-test data; escrowed test items are expendable). If real data ever needed preserving, mirror migrations ①② for `listing_items`/`offer_items`.

**Seeds**: refresh `gameconfigs-*.sql`/`mapinfo-*.sql` snapshots from the reconciled DBs (keep our snapshot convention until deliberately changed).

## Phase 5 — Trading Post adaptation to the new item model

Small contained workstream (protocol unreleased → free to change; keep ADR 0001 escrow physics untouched):
1. **Wire**: `TpItemBrief` (id/count/`attribute`) and `TpItemFull` → carry the new instance fields (`cur_durability`, `custom_made`, `prefix_type/value`, `secondary_type/value`, `enchant_bonus`). Compat already bumping to 0.4.0 in this merge.
2. **Store**: `listing_items`/`offer_items` columns re-mirror the new `character_bank_items`; escrow-in/out field copies via `AccountDbBankItemRow`; `deliver_to_bank` offline INSERT column list.
3. **Client**: Detail-view tooltip rebuild from new fields; `item_name_formatter` calls against Dev's changed `ItemNameFormatter` API; verify Vince renders correctly against the renamed `equip_models` atlas and shifted equip indices.
4. Update `PLANS/TradingPost_Plan.md` + `CONTEXT.md` if any contract vocabulary or field names shift.

## Phase 6 — Verify & land

Test matrix (server + client, then a Linux build pass):
- Login + create character on a migrated account (creation-items path, stat validation), inventory/bank/durability display.
- TP full loop: list → browse → offer → finalize → delist → rescind → expiry sweep → offline Warehouse delivery; toasts; My Listings/My Offers.
- War commands: `/begincrusade`…`/endapocalypse` incl. mutual exclusion; manual Heldenian start/end.
- Boss drops (guaranteed tier-2, delayed second drop, 5×5 scatter), icebound entry, tester Spawn NPC (new signature).
- Guild absence sanity: no guild UI reachable, CommandHall dialog fine, no crashes from stripped handlers.
- Changelog entry per `CLAUDE_CHANGELOG.md`; then Nick merges `merge-development` → `master` and pushes to his own remote.

**Deferred follow-ups (separate efforts):** collapse `LAN_ENG.H`/`lan_eng.h` to one file; decide seed-vs-migration content-delivery convention; untrack build artifacts (`.lib`/`.obj.enc`) someday; **guild system rebuild on SQLite into the shells Dev left** — the flagship next feature and ShadowEvil's stated intent for the gut.

## Known landmines

1. **Item-instance redesign vs TP escrow** — the headline risk; handled by Phase 5. Don't "fix up" TP columns ad hoc during conflict resolution.
2. **`LAN_ENG.H`/`lan_eng.h` dual index entries** on a case-insensitive checkout — resolve both, keep identical; Linux picks lowercase.
3. **Binary SQLite conflicts** — never let git "resolve" them; always explicit theirs/ours + scripted re-apply + dump-diff verification.
4. **Generated version files** — resolve `version.cfg` only; regenerate the rest.
5. **Equip/display index shifts + pak renames** — Vince's composite render and any hardcoded equip indices need eyeball verification in-game.
6. **Dev's stray `Binaries/Server/gameconfigs/mapinfo.db`** — probably accidental; verify and drop rather than blindly keeping.
