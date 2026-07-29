# Item Tiers — v1 Design Contract

Designed 2026-07-26/27 across twelve wayfinder decision sessions ([map: Tiered item modifiers](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/22)). Vocabulary: see `CONTEXT.md` (Item Tiers section). Architecture decision: see `docs/adr/0002-tiered-item-modifiers.md`. This is the binding design contract for v1; implementation is a separate follow-on effort. Rationale lives in the linked ticket resolutions — this document states what was decided.

## The system in one paragraph

Monster-dropped gear rolls a **tier** — Common / Rare / Epic / Legendary — where tier is purely **modifier count** (1/2/3/4); base stats never change with tier. Modifiers come from a thematic-bucket catalog (at most one modifier per bucket per item), roll values from one shared band per modifier under a tier-selected probability curve, and everything tunable lives in `gamedata.db`. Legendary god-rolls are the game's power apex, decisively above Dark Items and named uniques. The whole system sits behind a **roll-strategy seam**: `legacy` (retail-faithful prefix + secondary) and `tiered` both ship, selected by a world-defining config switch, both live launch candidates until the test verdict.

## 1. Scope

- **Tier-rolls**: droppable melee weapons, **bows** (deliberate pivot — original bows never rolled), wands, body armors (incl. hauberks/robes), helms, leggings, shields, capes.
- **Outside the tier system**: named uniques (Xelima/Kloness/Merien and kin), necklaces/rings, angels, Dark Items, reward capes (Hero's 400/401/427/428, Combatant 429), crafted items (untiered in both modes, recipe-fixed modifiers, exclusive +10 enchant ceiling).
- **Fresh start**: launches on a clean world; no migration of old rolled attributes.
- Tier decides modifier *count*; the catalog decides *which*; the value model decides *values*; loot grades decide *tier odds*. Each is an independent dial.

## 2. Dual-strategy architecture ([Configurability #24](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/24))

- **Two roll strategies behind one seam**: `legacy` and `tiered`. Mode switch: `item_system = legacy | tiered` in `gamedata.db` `meta` — read once at boot, **restart-only**, replicated to clients via the config cache.
- **Launch verdict policy**: the live game launches permanently in ONE mode. Post-verdict, features target the chosen mode by default; the unchosen strategy stays compiling and switchable but is frozen at its launch feature-set.
- **Both strategies data-driven**: the half-landed `attribute_pools` / `attribute_pool_entries` wiring gets finished for legacy (today: zero code references; hardcoded C++ tables are live). Tiered gets its own purpose-built schema. Each strategy owns its tables. The 243 curated item→pool assignments in the orphan tables are mined as reference input when seeding tiered bucket membership.
- **The code/data line**: in code — effect behaviors (enumerated effect ids), roll algorithms, the tier-count rule, bucket exclusivity, cap enforcement, anti-cheat floors' existence. In data — the modifier catalog, pool membership + weights, tier roll rates, bands, curves + overrides, per-tier and aggregate caps, posture knobs. **Test: a balance change never needs a build; a new mechanic always does.**
- **Fail-fast boot validator** over BOTH strategies' datasets (unknown effect id, empty required bucket, min>max band, cap below band floor, dangling refs, duplicate bucket assignment…). Any error blocks boot with a precise table/row report — no clamping, no skip-and-warn.
- **Live reload, transactional**: all tier + legacy-pool tables join the existing `reload` console command; validator runs on candidate data first; failed validation rejects the reload and leaves running config untouched. The mode switch alone is restart-only.
- **Server-owned DDL**: tier tables created/migrated at boot per the `GameConfigSqliteStore.cpp` `CREATE TABLE IF NOT EXISTS` pattern, seeded empty; empty tables failing validation in tiered mode is the intended unconfigured-world guard.
- **Zero-client-patch labels**: the client renders modifier names / labels / tooltip formats from replicated catalog data (`display_name` / `effect_label` / `effect_format` already ride the config-cache packets and are ignored today). Hardcoded strings in `ItemNameFormatter.cpp` demote to missing-data fallback.

## 3. Tier model ([Encoding #31](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/31), map locks)

- Tier = modifier count only: **Common 1 / Rare 2 / Epic 3 / Legendary 4**.
- **Explicit stored tier byte**: 0 = untiered (legacy-rolled or plain), 1–4 = Common..Legendary. Never derived from modifier count — both strategies' items can share one world DB across mode switches during testing.
- Invariant *tiered ⇒ modifier count == tier* is enforced at roll time and by the boot/reload validator, never by the storage format (keeps the door clean for out-of-scope tier-up/reroll systems).
- **Plain** gear (tier 0, no modifiers) enters the world only via NPC shops, blacksmith, and granted rewards. Monster-dropped gear is always tiered, Common floor.

## 4. Modifier catalog ([Catalog #29](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/29), amended by [Marquee #33](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/33))

**Laws:**

1. **Membership** — every working legacy effect carries over; plus the attribute ladder and marquee exotics. No clean-slate reset.
2. **Bucket law** — every modifier belongs to one thematic bucket; an item carries **at most one modifier per bucket**; position is meaningless. Structurally forbids identical repeats and same-kind stacking. Bucket membership, per-class eligibility, weights, min-tiers: all `gamedata.db` config.
3. **Min-tier ladder** (per-modifier integer in data, consulted at drop-roll only — loosening later is clean, tightening strands items, so start tight): legacy lines and attribute singles any tier; archetype pairs **Epic+**; ALL STATS and every marquee modifier **Legendary-only**.
4. **Roll semantics** — attribute pairs roll **two independent values**; ALL STATS rolls **one shared N**.
5. **Stat-touchers derived, outcomes frozen** — Agile / Light / Strong / Ancient-durability compute at equip time from pure modifier data; stored item stats never mutate. Acceptance tests: a Light Full Helm is equippable by a low-STR mage (STR gate uses effective weight); Agile grants full-speed swings at lower STR (`SharedCalculations.h:152`).
6. **Signature test** — an effect is unique-exclusive iff it exists today only on named uniques. **Banned forever**: Xelima %-HP drain, Medusa on-hit hold (special ability 3), Merien activated total-immunity (`DefenseSpecAbility`), Sword of Ice Elemental on-hit freeze (special ability 2). *Kloness rep damage is NOT banned* — retail evidence showed common Righteous drops carried the same mechanic (§6). Passive Physical/Magic Absorb % are generic rolls today and carry.
7. **Mandatory-bucket flag** — schema supports marking a bucket mandatory per item class; **OFF everywhere in v1**.
8. **Bows join** as full weapon-class members (recorded pivot). Sharp on a bow adds launched-arrow damage.

**Buckets — weapons (melee and bows):**

| Bucket | Members |
|---|---|
| DAMAGE | Sharp, Critical, Poisoning, Righteous, Ancient (keeps durability rider) |
| PRECISION | Hitting Probability, Consecutive Attack |
| HANDLING | Agile, Light, Strong |
| ECONOMY | Experience, Gold |
| ATTRIBUTES | the ladder (below) |
| MARQUEE (Legendary) | Sunder, Bleed, MP drain, SP drain (§5 — freeze retired to the signature ban) |

**Wands:** CASTING (Spell Success % — today's `Special`; bucket expected to grow), PRECISION, ECONOMY, ATTRIBUTES, MARQUEE = % cast-time reduction.

**Armor family — body armor, helms, leggings, shields, capes:**

| Bucket | Members |
|---|---|
| SET-AXIS | Defense Ratio, Physical Absorb, Magic Resist, Magic Absorb, HP Recovery, SP Recovery, MP Recovery, Poison Resist — one line per item, ever; set-building stays across-slots |
| HANDLING | Strong, Light |
| COMBAT UTILITY | Mana Converting, Crit Chance |
| ATTRIBUTES | the ladder |
| MARQUEE (Legendary) | Movement speed % — **capes and leggings only** |

Eligibility data note: **Physical Absorb is excluded from cape eligibility** (capes cover no hit location — a PA roll there is a dead line).

**The ATTRIBUTES ladder** (one form per item):
- Singles: +N STR / DEX / MAG / INT / VIT / CHR (any tier)
- Archetype pairs (Epic+, independent rolls; seed list, data-extensible): STR&DEX · INT&MAG · VIT&STR · VIT&INT
- +N ALL STATS (Legendary-only, one shared N)

Body armor / helms / shields have exactly four eligible buckets, so Legendary composition there is fixed — accepted; widening a class's eligibility later is a clean future-drops-only data edit.

## 5. Marquee mechanics ([Marquee #33](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/33))

**Cast-time reduction % (wands, Legendary-only):**
- Scales the Magic cast animation's frame time (16 × 88 ms → × (1 − r)); the spell lands sooner and the next cast is available sooner.
- **Max 20%** (1408 → ~1126 ms). Anti-cheat: per-player cast floor = 1500 × (1 − r), server-computed from the equipped wand.

**Weapon exotics (melee + bows, Legendary-only, at most one per item).** One rolled value each; fixed constants are `gamedata.db` launch defaults:

| Exotic | Rolled value | Max | Fixed launch defaults |
|---|---|---|---|
| **Sunder** | proc chance per landed hit | 15% | −50 defense ratio for 5 s; re-proc refreshes |
| **Bleed** | proc chance per landed hit | 15% | 25 dmg per 1 s for 8 s; refreshes; ignores poison resist; not Cure-curable |
| **MP drain** | flat MP per landed hit | 15 | always-on, deterministic |
| **SP drain** | flat SP per landed hit | 15 | always-on, deterministic |

Bleed stays distinct from Poisoning on every axis (fixed tick vs `dice(1,level)`; refreshes vs blocked-while-poisoned; unresistable; uncurable); one weapon can roll Poisoning + Bleed (cross-bucket). All four work on players and NPCs; no new resist axes in v1; no boss special-casing.

**Bleed tuning amendment (owner, 2026-07-28).** The launch default was 5 dmg per 2 s for 8 s — four ticks, 20 damage per proc — which at the 3–15% proc band contributed roughly 3 DPS and read as "bleed is not working" in the first live wave. Retuned to **25 dmg per 1 s for 8 s** (eight ticks, 200 damage per proc). Sunder's constants are unchanged. These are `gamedata.db` values per the rule above, so this is a data edit applied with `reload tiers`, but the recorded default is amended here so the contract and the seed agree. **Tick cadence caveat:** the two `tick_bleed` poll sites differ — NPC victims are polled every 300 ms by `npc_process()`, player victims every 1000 ms by `check_client_response_time()` — so a 1 s interval is exact on NPCs and marginal on players until the player tick moves onto the faster poll.

**Movement speed % (capes + leggings only):**
- Scales locomotion only — walk (560 ms) and run (312 ms). Composition multiplicative: `duration = base × haste(0.7) × frozen(1.25) × (1 − speed_total)`. AttackMove/DamageMove untouched.
- **Max 10% per item**; equipped speed modifiers **sum and hard-clamp at 20% total**.

**Anti-cheat + protocol contract (both speed mechanics):**
- Server computes per-player legal floors from equipment + statuses it owns; move min-gap = expected tile time × 0.85 grace factor.
- **Launch posture: log-only everywhere** — including downgrading today's flat-200 ms move-check disconnect (`Game.cpp:12149`). Violation logs carry expected floor, observed gap, and speed%/haste/frozen state.
- Escalation to disconnect is the documented intent once live data shows clean floors; per-check posture is a `gamedata.db` knob (`log` | `disconnect`).
- Cast-reduction % and total move-speed % join the per-entity status broadcast (remote clients render true animations). Sunder/Bleed/drains add no broadcast bits in v1 — victims get notify messages.

## 6. Value model ([Value model #30](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/30), [Retail formulas #34](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/34))

**Laws:**

1. **Shared band** — one min–max band per modifier, designed in display units. All four tiers roll the full band. **Tier selects a probability curve; it never changes value legality.** Schema carries per-tier window columns defaulting to full band; deviations are data edits (none used at launch, §7).
2. **Curves** — four global named curve objects over normalized band position, in `gamedata.db`; sparse per-modifier/per-tier overrides for outliers. The hardcoded 13-step weight ladder (`ItemManager.cpp:4775`) dies. Future roll contexts (unique_forge, reroll) are new named rows.
3. **Rarity contract** — perfect single on Legendary ≈ 1/25,000+; both-perfect pair ≈ 1/100,000; ALL STATS caps at least as rarely as a perfect single. Lower tiers monotonically harsher. Delivered by §7's tables.
4. **Ratchet law** — every knob (rarity anchors, band caps, aggregate caps, curve shapes, min-tiers, tier rates) launches **strict and moves only by loosening**. Rolled items are permanent world facts. When in doubt, harsher.
5. **Aggregate caps everywhere** — same-modifier lines across equipped pieces sum, then hard-clamp at a per-modifier `aggregate_cap`, populated for the entire catalog at launch, computed at the equip-recalc choke point (`calc_total_item_effect`). **Exempt**: angels, unique built-ins, hero armor. No global cross-modifier budget.

**Righteous restored (retail formula, [#34](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/34) — primary source: Rowechelon guide, official helbreathusa.com, Wayback 2012-12-14):**

Retail rep damage = two independent clamped additive terms, not a diff formula:
- **Righteous prefix** (any Righteous weapon, valued 3..10): `if (target is player && target_rep < 0) damage += min(V, |target_rep| / 10)` where V is the rolled value (retail cap was 10; the roll clamps it). Nothing else — no attacker-side term.
- **Kloness weapons/wands** (ids 849/850/851/863/864, built-in, unchanged by tiers): attacker side `if (rating > 0) damage += min(5, rating / 100)` — **cap 5, no floor** (drops the leaked min-5 floor and 15 cap; retail-strict). Target side identical to Righteous cap 10. Wands apply both terms to spells. Necklace 859: `min(5, |target_rep| / 20)`.

**ManaConverting / CritChance (retail-confirmed):**
- Per-item roll **1..7**, weighted low — `(roll+1)/2` on the 1..13 table, min 1 (kills today's dead-zero roll from `v/2`).
- Set-wide aggregate caps **MC 13, CC 20** — retail survivors, unchanged.
- Application: MC mana gain `floor(sum × damage / 100) + 1`; CC trigger `dice(1,100) < sum` (strict `<`, honoring retail's effective −1%).

**Legacy line cleanups:** Gold and Experience become rolled bands (provisional caps = today's fixed +50% / +20%). **Agile stays retail-faithful fixed −1** (width-1 band; redesign parked in fog).

## 7. Tuning annex ([Tuning tables #35](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/35) — these tables ARE the shipped launch data)

**Curve representation:** a curve row is `p50`, `p90`, `p99` (normalized band position) + explicit `cap_chance`. Roll: with `cap_chance` → band max; otherwise sample the piecewise-linear CDF through the anchors, mapped strictly below max. Width-independent; the published table is the config.

**The four global curves:**

| Tier | p50 | p90 | p99 | `cap_chance` |
|---|---|---|---|---|
| Common | 0.15 | 0.40 | 0.65 | 1 / 2,000,000 |
| Rare | 0.25 | 0.50 | 0.75 | 1 / 500,000 |
| Epic | 0.35 | 0.60 | 0.82 | 1 / 100,000 |
| Legendary | 0.45 | 0.70 | 0.88 | 1 / 25,000 |

**Overrides (tails only; bodies always inherit the tier default):**

| Override | `cap_chance` | Yields |
|---|---|---|
| Pair half, Epic | 1/640 | both-perfect ≈ 1/409,600 |
| Pair half, Legendary | 1/320 | both-perfect ≈ 1/102,400; one-half-perfect ≈ 1/160 |
| ALL STATS (Legendary) | 1/50,000 | hierarchy: single 1/25k < ALL 1/50k < pair 1/102k |

**Per-tier windows: none at launch.** Every modifier rolls its full band on every tier. Adding a window later is a recorded per-modifier design amendment, not tuning drift.

**Bands (display units).** Legacy lines carry their exact current display bands verbatim. Rescales: **Gold +10..+50% (step 10)**, **Experience +5..+20% (step 5)**, **MC/CC 1..7**, **Righteous 3..10**. New: **attribute singles +1..+10**, **pair halves +1..+7 each**, **ALL STATS +1..+7 shared**. Marquee floors well off zero: **cast 5..20%**, **Sunder/Bleed 3..15%**, **MP/SP drain 3..15/hit**, **move speed 3..10%**. Agile fixed −1.

**Percentile tables (acceptance-criteria deliverable; P(cap) per tier = the `cap_chance` column, exact for every band):**

| Modifier | Band | Common P50/P90/P99 | Rare | Epic | Legendary |
|---|---|---|---|---|---|
| Sharp / Critical / Ancient | 1–13 | 2/5/8 | 4/7/10 | 5/8/10 | 6/9/11 |
| Poisoning | 20–65 | 25/35/45 | 30/40/50 | 35/45/55 | 40/50/55 |
| Righteous (cap V) | 3–10 | 4/5/7 | 4/6/8 | 5/7/8 | 6/7/9 |
| Light (wpn) | 4–52% | 8/20/32 | 16/28/40 | 20/32/40 | 24/36/44 |
| Strong | 14–91% | 21/42/63 | 28/49/70 | 35/56/77 | 42/63/77 |
| Hitting Prob | 21–91 | 28/49/63 | 35/56/70 | 42/63/77 | 49/70/77 |
| Consec Attack | 1–7 | 1/3/4 | 2/4/5 | 3/4/5 | 3/5/6 |
| Spell Success | 3–39% | 6/15/24 | 12/21/30 | 15/24/30 | 18/27/33 |
| Gold | 10–50% | 10/20/30 | 20/30/40 | 20/30/40 | 20/30/40 |
| Experience | 5–20% | 5/10/10 | 5/10/15 | 10/10/15 | 10/15/15 |
| Defense Ratio / Poison Resist / Magic Resist | 21–91 | 28/49/63 | 35/56/70 | 42/63/77 | 49/70/77 |
| HP / SP / MP Recovery | 7–91% | 14/35/56 | 28/49/70 | 35/56/70 | 42/63/77 |
| Phys / Magic Absorb | 9–39% | 12/21/27 | 15/24/30 | 18/27/33 | 21/30/33 |
| Mana Converting / Crit Chance | 1–7 | 1/3/4 | 2/4/5 | 3/4/5 | 3/5/6 |
| Attr single | 1–10 | 2/4/6 | 3/5/7 | 4/6/8 | 5/7/8 |
| Attr pair half | 1–7 | — | — | 3/4/5 | 3/5/6 |
| ALL STATS | 1–7 | — | — | — | 3/5/6 |
| Cast reduction | 5–20% | — | — | — | 11/15/18 |
| Sunder / Bleed proc | 3–15% | — | — | — | 8/11/13 |
| MP / SP drain | 3–15/hit | — | — | — | 8/11/13 |
| Move speed | 3–10% | — | — | — | 6/7/9 |

**Aggregate caps (whole catalog; stackers = 6 armor slots, +weapon for attributes):**

| Stacker | `aggregate_cap` | Basis |
|---|---|---|
| Each attribute (STR..CHR; ALL counts into each) | +20 | designed — binds at ~2 god rolls |
| DR / MR / PR / HP / SP / MP Recovery | 546 (structural max) | formality — acquisition cost + diminishing returns govern |
| Physical Absorb | 80 | verified historical per-hit-location clamp (`CombatManager.cpp:2887–2944`) |
| Magic Absorb | 80 | verified historical sum-clamp (`ItemManager.cpp:3327/:3421/:3572`) |
| MC / CC / Move speed | 13 / 20 / 20% | locked upstream (§6, §5) |

Single-source and item-local lines (weapon/wand lines, Strong, Light) get `aggregate_cap` = band max as mandated formality rows.

**Volume validation** vs §8 launch rates — expected perfect lines per 100,000 gear drops: standard 0.09, veteran 0.22, elite 0.46, boss 1.3. A perfect line is a server-history event; the living chase is P90/P99 rolls and the tier ladder. Every knob loosen-only.

## 8. Drop economics ([Drop economics #26](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/26))

> **Amendment A1 (below) is IN FORCE** as of 2026-07-29 (ADR 0005, #68) and supersedes three statements in this section: the gold-preempt ordering, `first_drop_chance` replacing the hardcoded primary chance, and gear emerging from the stage-1 table’s weighted mix. Read the amendment before acting on this section.

**The two-stage drop pipeline is retained, and the tier roll lives in stage 1 only.** Today's pipeline (`npc_dead_item_generator`, `EntityManager.cpp:2614`) is two stages: the **first drop** rolls at the moment of death; the **second drop** is rolled at death but queued and placed when the corpse decays — the venue for named uniques (Demon Slayer, Berserk/Kloness wands, SoIE, Xelima/Medusa jewelry) and the boss guaranteed/scatter machinery. The tier system touches only the first drop:

- **First drop**: when the stage-1 table yields tier-eligible gear (§1 scope classes), it gets the grade→tier roll. Non-gear stage-1 results (meat, potions, gold, nothing) are untouched.
- **Second drop: never tier-rolls — bosses included.** Abaddon, Tiger Worm, Wyvern, Fire Wyvern, Helclaw: a tiered item from a boss is at most **one item per kill, from stage 1, at the moment of death**. Second drops and scatter loot remain the unique/special venue, mechanically unchanged (contents, chances, rating modifier, delay timing, scatter spread).
- **Boot validator addition** (tiered mode): tier-eligible gear classes appearing in any stage-2 drop table is a validation error — stage-2 tables hold uniques, specials, and consumables only. This keeps "hunted gear is always tiered" true structurally, with no runtime branch. Current stage-2 tables get curated to comply at implementation (any ordinary gear they hold today moves to stage 1 or out).

**First-drop chance becomes data.** The hardcoded `BASE_PRIMARY_DROP_CHANCE = 1000` (`EntityManager.cpp:2599`) is replaced in tiered mode by a per-loot-grade `first_drop_chance` column (out of 10000) on the grade's tier-weight row — live-reloadable like the tier weights, consulted at the same point in the pipeline (the gold-preempt ordering — gold drop skips the first item drop — is unchanged). This is the knob that makes bosses a real tiered-gear venue now that scatter is out of the tier picture. Launch values: grades 1–4 = **1000** (today's effective 10%), boss = **10000** (a boss kill always attempts its stage-1 gear roll, subject to the gold preempt). The global `m_primary_drop_rate` server multiplier stays on top as today.

Implementation notes:
- The tiered eligibility gate must **exclude named uniques explicitly** (the `is_special_item` roster), not rely on effect-type branching — Kloness Blade/Axe/Esterk are `ItemEffectType::Attack` and the Kloness wands are `AttackManaSave`, so they enter the legacy roll branches today. Legacy-mode behavior stays as-is.
- Naming collision to avoid: the drop-table code calls its two stages "tier 1 / tier 2" (`DropTable::tierEntries`, `roll_drop_table_item(table, tier, …)`). Rename to **stage** during implementation so "tier" means item Tier only.

- **No plain drops** — every monster-dropped weapon/armor/cape is tiered, Common floor. In tiered mode the grade→tier roll **replaces** the legacy guaranteed-primary + 40%-secondary path entirely (`attribute_pools.secondary_chance` stays legacy-only).
- **Loot grades**: new column on `npc_configs`, enum 1–5 — **vermin / standard / veteran / elite / boss** — each mapping to one live-reloadable tier-weight row. Seed the 117 grades mechanically from exp values, hand-adjust once; balance forever by tuning 5 rows. No per-monster override at launch.
- **Staircase access rule** (zero-weights are the hard gate): Rare needs standard+, Epic needs veteran+, Legendary needs elite+. Vermin drop Common only. Revoking access post-launch is forbidden (ratchet law).
- **Launch rates** (weights out of 10000, applied *after* the drop table has selected a piece of gear):

| Grade | Common | Rare | Epic | Legendary |
|---|---|---|---|---|
| 1 vermin | 10000 | — | — | — |
| 2 standard | 9000 | 1000 | — | — |
| 3 veteran | 7500 | 2200 | 300 | — |
| 4 elite | 6000 | 3200 | 750 | 50 |
| 5 boss | 3500 | 4000 | 2200 | 300 |

Boss-rate note (spec-assembly review, 2026-07-27): the boss row was originally rationalized against scatter-table gear volume ("≈ one Legendary per 10–15 boss kills"). With the tier roll stage-1-only, the per-kill chain is *P(no gold preempt, 70%) × first_drop_chance × P(stage-1 table yields gear) × weight* — and today's boss stage-1 tables are ~92% potions, so 3% works out to ≈ one boss Legendary per ~590 kills (elite Legendary ≈ 1 per ~35,000 kills). **Kept deliberately** (ratchet law: launch strict). All loosen-later levers are data: boss weights, `first_drop_chance`, and stage-1 drop-table gear curation — the last is expected implementation-time work, since boss first-drop tables were authored for the scatter era.

- **No zone layer** — tier odds are the monster's grade, full stop. (A global drop-event tier multiplier was parked to fog.)
- **Capes**: generic Cape (item 402) becomes a restricted-mob drop (Plate Mail pattern; which mobs is drop-table data entry at implementation). It tier-rolls like any gear. Reward capes stay untiered and unchanged.

### Amendment A1 — drop rarity becomes an absolute "1 in N" per (item, monster) (IN FORCE)

Accepted 2026-07-29 via [#68](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/68), recorded as [ADR 0005](../docs/adr/0005-absolute-drop-chances.md). **This amendment supersedes the three §8 statements listed below; every other statement in §8 stands unchanged.** Implementation is [#73](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/73); until it lands the shipped code still behaves as the superseded text describes.

Every `drop_entries` row, **both stages**, carries an absolute rarity for that item from that monster instead of a weight relative to its table — authored and reported as "1 in N kills", stored as `drop_chance_ppb INTEGER` (parts per billion) so the validator’s exact-sum rule stays expressible. Bigger is rarer; the leftover probability is "nothing", so a roll still yields at most one item. Four **generosity** multipliers — global, stage, category (gear / consumable / gold / unique), loot grade — scale the stored chance without changing any ratio between rows (`1.0` = as authored, `2.0` = twice as likely, all defaulting to `1.0` and reloading live; a larger product means a larger `drop_chance_ppb`, i.e. a smaller `N`). The stage layer is load-bearing: stage 1 sits at 37% of kills for an ordinary monster and stage 2 at under 1.5%, so one lever cannot serve both. Multipliers scale **row rarity only** — they shrink a table’s "nothing" remainder and never touch `roll_count`, so turning stage 2 up cannot make a scatter boss spread more items. On a table whose rows already sum to 1.0 a multiplier is a deliberate no-op: something drops on every roll already. The boot validator reports every table a live multiplier cannot move, by name, so the no-op is visible rather than a surprise — deliberate for the five bosses at stage 2, a defect at stage 1 (#87).

Three §8 statements are superseded — and only these three:

1. *"the gold-preempt ordering is unchanged"* — the hardcoded 30% gold chance, the `drop_rates.gold` multiplier and the preempt branch are deleted; gold is an ordinary row with its own chance.
2. *"`first_drop_chance` replaces the hardcoded primary drop chance"* — `first_drop_chance` and `drop_rates.primary` are subsumed by the per-row chances and retire.
3. *"when the stage-1 table yields tier-eligible gear … it gets the grade→tier roll"* — gear no longer emerges from a weighted mix shared with consumables. Each gear row states its own per-monster rate; the tier roll still runs on whatever gear the roll produced.

**A1 reaches stage 2, unlike the other options considered.** Stage-2 *mechanics* are untouched — corpse-decay timing and placement, `guaranteed_secondary`, scatter spread and count, the five guaranteed bosses, the stage-2 validator rule, and the fact that stage 2 never tier-rolls. Only the encoding of its row chances changes, for the same reason as stage 1: identical authored weights currently produce a 216× rarity gap for the same unique on two different monsters.

Unchanged by A1: the grade→tier weight table above, the staircase access rule, the ratchet law, and the no-zone-layer and cape rules.

**Stage 1 and stage 2 split into separate tables.** `drop_tables` declares its `stage`, `drop_entries` loses the stage discriminator, and `npc_configs` references a stage-1 and a stage-2 table independently; `guaranteed_secondary` and `scatter_count` move onto the stage-2 table. Today 61 of 98 tables carry a dead stage-2 half, and stage 1 has 98 row-sets across only 23 distinct contents because the bundling forces every monster to copy the shared potion block. Both stages then run **one identical engine**: same rate model, same multipliers, same validator. Every former stage-2-only behaviour becomes an ordinary table property available to either slot — `base_secondary_drop_chance` retires like `first_drop_chance`, `guaranteed_secondary` becomes a validator-enforced "rows sum to 1.0", and `scatter_count` becomes `roll_count_min`/`roll_count_max` plus a `placement` and `delay` property (which also restores the original’s guaranteed 5–15 scattered items rather than our fixed 15 rolls that mostly miss). "Stage" then means only which slot a monster references. The single deliberate asymmetry is not part of the calculation: §8 keeps stage 2 out of the tier roll.

Migration is mechanical and reproduces every current per-kill rate exactly before any retune, verified by `dropodds` agreeing per grade to four decimals and per row to within one part per billion. Baseline, encoding rationale and migration formula are in ADR 0005.

## 9. Power ceiling ([Power ceiling #25](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/25))

- **Legendary is the apex, decisively**: a god-roll outclasses +15 Dark weapons and top uniques at a glance (double-digit-percent stronger). Dark Items become the reliable free level-180 baseline; uniques a mid-chase lottery with exclusive signatures; god-roll farming the eternal endgame.
- Dominance is a *ceiling* statement — an ordinary Legendary with weak rolls may sit below a +15 Dark weapon.
- **Enforcement is designed, not emergent**: per-modifier per-tier caps in data. Rarity tunes how often; caps tune how strong — two separate dials. No per-item power budget.
- Angels stay additive on their own slot; majestic sinks stand as-is (any rework is out of scope). Legendaries are ordinary droppables — tradeable, the Trading Post's top end.

## 10. System interactions ([System interactions #27](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/27))

**Enchant (+N) — fully orthogonal to tiers, mode-independent.** "+7 Legendary Chain Mail" is legal. The Ancient-prefix upgrade block dies in tiered mode (legacy keeps it). The whole enchant table becomes data-driven (per-step success %, per-step destroy flag, per-category caps). Launch values:

| Knob | Value |
|---|---|
| Cap — weapons, shields, armor, wands | **+7** |
| Cap — custom-made (crafted) weapons | **+10** (retail exception kept) |
| Destroy on fail | **Safe through +3**; +4 and above destroy on failure |
| Armor/wand endurance growth per success | kept (+15% normal / +20% crafted) |
| Majestic paths (angels +10, DK weapons +15) | untouched — outside tier scope |

Recorded deliberate deviations from the original 3.82 source: armor/wands lowered from +10 to +7 (uniform cap); safe-through-+3 (original destroyed from +1 on). Data-driven table makes any later correction a data edit.

**Trading Post**: every tier freely listable and tradeable — no gates, no listing tax. Tier renders wherever item names render (`item_name_formatter` + tier-presentation table; `TpItemBrief` carries the new instance struct mechanically). Tier filtering parked to fog.

**Repair**: zero interaction — durability only; tier, modifiers, enchant never read, rerolled, or degraded. Price stays a function of the base item (tier-scaled pricing parked to fog).

**Crafting**: stays outside tiers — untiered in both modes, recipe-fixed modifiers (`CBuildItem::m_attribute` nibbles translate to unified modifier IDs at the encoding layer), distinct niche: deterministic output, quality score, exclusive +10 enchant ceiling. Future crafting↔tier interaction goes through the out-of-scope augmentation direction.

**GM item creator**: mode-aware; tiered mode drives tier + modifier slots from replicated catalog data. No rarity constraints within legality; **structural legality server-enforced** (one per bucket, min-tiers, bands, count == tier). Minting logged for economy audits.

## 11. Presentation ([Presentation #28](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/28))

- **Tier names, player-facing, final**: Common / Rare / Epic / Legendary. The tier word **leads the item name** ("Legendary Chain Mail"); the whole name renders in the tier color everywhere names appear.
- **Palette (Helbreath-native, from the dye table)**: Common white (255,255,255) · Rare green (128,192,128) · Epic blue (150,160,225) · Legendary gold (255,176,16).
- **Tooltip layout unchanged from today**: name line, gray classification, base stat with inline mods, one standalone gray-label/green-value line per modifier (up to four). No badges, separators, or pips.
- **Sprites never tinted in tiered mode** (legacy keeps prefix dye-tints; the strategies never share the tint channel).
- **Ground drops**: light-beam in tier color on every tiered drop (Common faint neutral). No floating labels.
- **Pickup**: "You got a {name}." with the name in tier color. **No server broadcasts, no map banners.**
- Names, colors, and the name template live in the **tier-presentation table** riding the config-cache slot (§12); `ItemNameFormatter` prepends the tier word as it prepends prefixes today; the name-color picker gains a fourth branch keyed off the tier byte; the beam keys off ground-item tier (present in the drop packets per §12).

## 12. Encoding and protocol ([Encoding #31](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/31))

**`item_instance_data` generalizes to a 15-byte all-`uint8_t` POD** (positional prefix/secondary fields die):

```cpp
struct item_instance_data
{
	uint8_t custom_made;
	uint8_t tier;                 // 0 = untiered, 1-4 = Common..Legendary
	struct modifier
	{
		uint8_t type;             // unified modifier ID; 0 = empty slot
		uint8_t value;
		uint8_t value2;           // second roll for attribute pairs; 0 otherwise
	} modifiers[4];
	uint8_t enchant_bonus;
};
```

- **One unified modifier-ID space**: flat `uint8_t`, 0 = empty, 1–255 usable, renumbered from scratch; both strategies emit unified IDs. The two overlapping enums in `ItemAttributes.h` die; the `[16]` lookup arrays and `id < 16` guards die; the nibble-era value-15 clamp dies (legality comes from bands).
- **Flat DB columns**: `character_items`, `character_bank_items`, trading-post escrow get 13 columns — `tier, mod1_type, mod1_value, mod1_value2, … mod4_value2` — replacing the prefix/secondary columns. No child table, no blob (keeps SQL queryability). Fresh start: straight DDL, no data migration.
- **Packets embed the struct**; `copy_attributes_to<T>` / `load_attributes_from<T>` retire. The ~11 item-carrying packets (ground item, map sync, obtain, bank deposit, attribute-change, gizon-change, exchange, inventory list, bank list, TP brief/full) all reshape.
- **One unified catalog packet** `PacketModifierCatalogEntry` replaces the prefix/secondary type-table packets, riding the existing SHA-256 config-cache negotiation in `m_config_hash[7]`. Per entry: `modifier_id`, `display_name`, `effect_label`, `effect_format` (two-value format for pairs), `multiplier`, `bucket_id` (stable tooltip ordering), `min_tier`, `marquee` flag. The **tier-presentation table** (names + colors + name template) rides the same slot. Server-side balance data (weights, bands, curves, eligibility) never replicates.
- **One Compatibility minor bump, atomic: 0.7.x → 0.8.0**, covering the reshaped item packets, the catalog packet, and the status-broadcast speed fields (§5). All wire changes land and deploy as one batch, client + server rebuilt together; exactly one bump reaches the deployed world.

## 13. Current-code drift ledger (implementation must-fix, both modes unless noted)

| Drift | Current | Spec |
|---|---|---|
| Enchant cap | +15 via stones for everything | +7 (weapons/shields/armor/wands), +10 crafted weapons |
| MC/CC per-item roll | `v / 2` → 0..6 (dead zero) | `(v+1)/2`, min 1 → 1..7 |
| CC trigger | `dice(1,100) <= sum` | `dice(1,100) < sum` (retail strict) |
| Righteous prefix | cosmetic only (no combat hook) | valued target-side rep damage (§6) |
| Kloness attacker term | `rating/100` clamped 5..15 (free floor) | `min(5, rating/100)`, no floor |
| Move-check enforcement | flat-200 ms disconnect (`Game.cpp:12149`) | per-player floor, log-only at launch, posture knob |
| Value clamp | rolled values clamped to 15 | dies; band legality in data |

## 14. Implementation sequencing notes

Implementation is a follow-on effort with its own planning; the spec constrains its shape:

1. **Shared first**: `item_instance_data` layout, unified ID space, catalog/tier-presentation packets, version bump machinery — the atomic 0.8.0 wire batch defines the integration point everything else meets.
2. **Server data layer**: tiered schema + legacy `attribute_pools` wiring + boot validator + reload path, seeded from this document's tables (§7, §8) and the mined pool assignments.
3. **Roll strategies** behind the seam; then effect application (marquee mechanics, aggregate caps at `calc_total_item_effect`), drift-ledger fixes (§13).
4. **Client**: data-driven `ItemNameFormatter`, tier colors, beam, GM creator, status-broadcast animation scaling.
5. Deploy as one batch per the single-bump rule.

## Deferred (parked in map fog — not in the launch spec)

Second-wave catalog growth (CASTING additions, more marquee exotics, armor-class eligibility widening, mandatory-bucket usage) · Agile redesign (rolled depth or % swing-time) · global drop-event tier multiplier · Trading Post tier filtering · tier-scaled repair pricing.

## Out of scope (separate future efforts)

Implementation itself · migration of old rolled items (mooted by fresh start) · tiers on uniques/necklaces/rings/angels/Dark Items · base-stat scaling by tier · wipe logistics · rebalancing majestic sinks/Dark Items/uniques/angels · augmentation systems (tier-up, rerolls, unique forge — the shared-band invariant deliberately accommodates all three).
