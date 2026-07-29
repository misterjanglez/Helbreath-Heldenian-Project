# Drop rarity becomes an absolute "1 in N" per (item, monster)

**Status: Proposed** — recommendation from [#68](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/68), pending owner ratification. Nothing in the code or data has changed. Amendment A1 to `PLANS/ItemTiers_Plan.md` §8 activates only if this ADR is accepted.

Every drop-table row stops being a weight relative to whatever else happens to be in its table, and becomes an **absolute rarity — the `N` in "1 in N kills" — for that item from that monster**. Both stages. Bigger `N` is rarer; the leftover probability is "nothing", so a roll still yields at most one item. On top sit four multiplier layers — global, stage, category, loot grade — for holistic tuning that cannot bend the per-monster rarities underneath it.

This is **option G**, added to #68's list during evaluation. It supersedes option D, which was the original recommendation of this ADR.

## What the owner actually needs

The evaluation started from #68's framing — "reduce the number of tuning knobs" — and reached option D, a per-grade outcome profile. Owner review rejected that framing. The requirement is not fewer knobs; it is **knobs that mean something**, stated as three things:

1. **Per-item, per-monster rarity, set directly.** "Sword of Medusa from a snake is 1 in 2,000,000; from a zombie 1 in 1,000,000; from Abaddon 1 in 1,000." The same must hold for ordinary gear: Plate Mail from a Demon rarer than from a Tiger Worm.
2. **The number written down is the odds.** Not a weight to be divided by a table total that differs per table.
3. **A global lever that moves everything together** without disturbing the disparities set in (1).

Option D fails (1) outright: it sets gear frequency per *grade*, and §8 forbids per-monster overrides at launch. Options A, B, E and F fail (2) — they all keep relative weights. None of the six addresses (3) in a way that preserves ratios.

## The measurement this rests on

`Server --dropodds` against the shipped `gamedata.db`, default multipliers, tiered mode, 2026-07-29. `Scripts/dropodds_offline.py` reproduces the same chain offline and **agrees with the running server exactly** — every per-grade figure to four decimals, all 96 per-table gear shares to eight.

| grade | npcs | first roll | any item | gear | 1 in N gear | Common | Rare | Epic | Legendary |
|---|---|---|---|---|---|---|---|---|---|
| 1 vermin | 69 | 10% | 5.58%\* | 0.0592% | 1,690 | 0.0592% | — | — | — |
| 2 standard | 14 | 10% | 7.00% | 0.6546% | 153 | 0.5891% | 0.0655% | — | — |
| 3 veteran | 15 | 10% | 7.00% | 0.7058% | 142 | 0.5294% | 0.1553% | 0.0212% | — |
| 4 elite | 8 | 10% | 7.00% | 0.6252% | 160 | 0.3751% | 0.2001% | 0.0469% | 0.0031% |
| 5 boss | 5 | 100% | 70.00% | 8.8746% | 11 | 3.1061% | 3.5499% | 1.9524% | 0.2662% |

\* Understated — see side findings.

### Weights don't mean anything today, and the data proves it twice

**Stage 2, the same item on two monsters.** Sword of Medusa carries weight `1` on both the zombie and the snake:

| | table total weight | per-kill odds |
|---|---|---|
| Zombie | 100 | **1 in 2,000** |
| Amphis (snake) | 21,648 | **1 in 433,000** |

A 216× gap from identical authored numbers. The cause is that the snake's table also carries butcher-parts rows (Snake Meat 2,308, Skin 1,846, Teeth 1,846, Tongue 1,569) which crowd the uniques down; the zombie's table has none. **Adding a common item to a table silently made every unique in it 200× rarer.** Nobody chose either number.

**Stage 1, the same item across thirteen monsters.** Plate Mail (M) carries weight `50` in every table it appears in, and the resulting odds are flat:

| monster | grade | odds |
|---|---|---|
| Ice Golem | 2 | 1 in 3,391 |
| Liche | 3 | 1 in 3,220 |
| Dragon | 4 | 1 in 3,134 |

An Ice Golem and a Dragon drop Plate Mail within 8% of each other. There is no per-monster differentiation in the shipped data at all — not because it was tuned flat, but because nothing expresses it.

### The stage-1 structure, for the record

98 tables, 96 referenced, but only **47 are real monster tables**; the other 51 are 7-row boilerplate for guard towers, crusade structures and summons — 100% consumables, zero gear. **44 of those 47 carry a byte-identical 10,170-weight non-gear block.** So today's gear share is `G / (C + G)` with `C` an unnamed constant — 16 distinct values across 47 tables, band 4.69%–13.90%.

That invariant is why #68's premise ("an emergent ratio across 98 hand-weighted tables") overstated the problem, and also why it is fragile: it holds by authoring habit, nothing validates it, and the two failures above are what happens when it doesn't hold.

### Legacy is not currently faithful

Measured against the original (`m_iPrimaryDropRate` 6500, `m_iSecondaryDropRate` 9000, verified in both archived sources):

| outcome | original | ours, grades 1–4 |
|---|---|---|
| gold | 21% | 30% |
| consumable | 12.6% | ~6.3% |
| **equipment** | **1.4%** | **~0.65%** |
| nothing | 65% | ~63% |

ADR 0002 makes `legacy` the retail-faithful launch candidate. It is 43% richer in gold and roughly 2× stingier in both consumables and equipment than the game it reproduces.

## The decision

Every row in `drop_entries`, both stages, carries `drop_one_in` — the **`N` in "1 in N kills"** for that item from that monster — instead of a relative weight. Bigger is rarer. `350` is a common drop; `2000000` is a lifetime chase.

```
effective_one_in = drop_one_in / (global x stage x category x grade)
```

Multipliers are **generosity**: `1.0` ships the authored number, `2.0` makes every row twice as likely, shrinking the chance of nothing (see below). All four default to `1.0`, live in `gamedata.db`, and reload live. Categories are gear / consumable / gold / unique, so consumables tune without touching gear.

**The stage layer is not redundant with the others.** The two stages sit at completely different saturation points, so one lever cannot serve both:

| grade | stage-1 "something drops" | stage-2 item | s1 headroom | s2 headroom |
|---|---|---|---|---|
| 1 vermin | 12.95% | 1.43% | 7.7x | 70x |
| 2 standard | 37.00% | 1.36% | 2.7x | 74x |
| 3 veteran | 37.00% | 0.81% | 2.7x | 124x |
| 4 elite | 37.00% | 0.86% | 2.7x | 117x |
| 5 boss | 100.00% | 100.00% | none | none |

A global lever generous enough to move stage 2 meaningfully would saturate stage 1 several times over. "Stage 1 feels good, stage 2 feels stingy" is a sentence the data supports and the other three layers cannot express — category is orthogonal (stage 2 holds consumables and manuals as well as uniques; stage 1 holds gear), and grade cuts across both stages at once.

The server converts each row to an internal fixed-point probability at load (parts per billion, which holds the full authored range in a `uint32`). That is an implementation detail — `1 in N` is what the data says and what every tool reports.

- **One roll per stage**, one uniform draw; the row probabilities lay out cumulatively and anything past the last row is "nothing". At most one item, exactly as today. Explicit `item_id = 0` rows are deleted — "nothing" is the remainder, not a row.
- **Adding or removing a row cannot change any other row's odds.** This is the property that kills both failures above, at both stages, without splitting pools.
- **Multipliers preserve ratios by construction.** Pulling the global lever from 1.0 to 1.333 moves Medusa-from-a-snake from 1 in 2,000,000 to 1 in 1,500,000 and moves everything else by the same factor. Categories are gear / consumable / gold / unique, so consumables can be tuned without touching gear — the #67 failure, made structurally impossible rather than merely visible.
- **Saturation clamps proportionally, per stage.** If multipliers push one stage's summed probability past 1.0, that stage's table scales back to exactly 1.0 — ratios intact, "nothing" at zero — and the boot validator names it. Increasing a multiplier past that point is a no-op rather than a silent distortion. **Saturation is not automatically a defect**: the five bosses are deliberately saturated at stage 2 (`guaranteed_secondary` — a second item on every kill, which is the design) and wrongly saturated at stage 1 (#87). The validator reports it; whether it was intended is a data question.

### Stage 1 and stage 2 become separate tables

Today one `drop_tables` row bundles both stages, discriminated by `drop_entries.tier`. They split: `drop_tables` declares its own `stage`, `drop_entries` loses the discriminator entirely, and `npc_configs` carries `stage1_table_id` and `stage2_table_id` independently.

The shipped data argues for this on its own:

| | stage 1 | stage 2 |
|---|---|---|
| tables carrying rows | 98 | 37 |
| **distinct contents** | **23** | **36** |
| hand-duplicated row-sets | 75 | 1 |

**61 of 98 tables carry stage-1 rows and no stage-2 rows at all** — a dead half on every one of them. And stage 1 is overwhelmingly repetitive (98 row-sets, 23 distinct contents) while stage 2 is genuinely per-monster (37 row-sets, 36 distinct). The bundling is what forces the duplication: a monster that wants the standard potion block plus its own unique roster has to copy the entire potion block to get it. Split, those 75 duplicate stage-1 row-sets collapse into 13 shared definitions, and the potion block becomes one row-set that ~44 monsters reference.

`guaranteed_secondary` and `scatter_count` are stage-2 properties living on a bundled table; they move onto the stage-2 table, where they mean something. The §8 implementation note asking for the "tier 1 / tier 2" naming collision to be renamed to "stage" resolves itself — with two table references there is no stage discriminator left to name.

### One engine: "stage" is only which slot a monster references

The two stages must be **configurable in exactly the same ways**, so every behaviour that used to belong to "the second drop" becomes an ordinary property of a drop table, available to either slot. There is one table type, one roll routine, one set of multiplier layers.

A drop table carries:

| property | meaning | default |
|---|---|---|
| rows | `(item_id, drop_one_in, min_count, max_count)` | — |
| `roll_count_min` / `roll_count_max` | how many times the table is rolled | 1 / 1 |
| `placement` | single tile, or the fixed spiral spread | single |
| `delay` | on death, or on corpse decay | on death |
| rows summing to exactly 1.0 | "nothing" can never come up | not required |

`npc_configs` references a stage-1 table and a stage-2 table. **That reference is the only thing "stage" means.** Nothing in the roll, the rate model, the multipliers or the validator branches on it.

This retires three stage-2-only concepts by generalising them:

- **`guaranteed_secondary`** becomes "this table's rows sum to 1.0", validator-enforced. Any table may do it; the five bosses' stage-2 tables do.
- **`scatter_count`** becomes `roll_count_min` / `roll_count_max` plus `placement`. This also fixes a divergence: the original scattered a *guaranteed 5–15 items* for the Wyverns and 12–20 for Abaddon (`bGetMultipleItemNamesWhenDeleteNpc`, min/max parameters), whereas our port rolls a fixed 15/15/20 times against a table with a large "nothing" slot, so most rolls miss. Min/max counts express the original directly.
- **`base_secondary_drop_chance`** (the flat 5% gate) retires exactly as `first_drop_chance` does — each row states its own absolute rarity.

**The one deliberate asymmetry that stays** is not part of the drop calculation: §8 locks stage 2 out of tier-rolling, so gear selected by a stage-2 table would not get a tier. That is a rule about what happens to an item after it is chosen, and it is enforced today by the validator refusing tier-scope gear in stage-2 tables.

### What a multiplier means: less "nothing"

A multiplier answers exactly one question — *"I am not seeing drops often enough from this stage."* It scales **row rarity**, which shrinks the table’s "nothing" remainder. At `2.0` every row is twice as likely and the chance of an empty roll falls accordingly.

Three things follow, and all three are deliberate:

- **`roll_count` is never touched by a multiplier.** How many items a scatter boss spreads is an authored property of that table, not something a global lever moves. Turning stage 2 up must not make Abaddon scatter more items.
- **On a table whose rows already sum to 1.0, a multiplier is a no-op — and that is correct.** Something already drops on every roll; there is no "nothing" left to shrink. The five bosses’ guaranteed stage-2 tables are in exactly this state by design.
- **Ratios inside a table never change.** That is the invariant the whole model exists to protect.

The boot validator still *reports* every table a live multiplier cannot move, by name. The no-op is intended behaviour, not an error — but it should never be a surprise.

Worked, at `stage 2 x 2.0`: a Liche’s second drop moves 1 in 57 -> 1 in 29, while Abaddon, both Wyverns, Helclaw and Tiger Worm are unchanged, because their stage-2 tables already always yield something.

Stage 1 and stage 2 carry independent multipliers, so either can be moved without touching the other — which is the whole point of the stage layer.

### Why "1 in N" and not a chance-out-of-D

Both encode the same thing; the difference is entirely who does the arithmetic. An earlier draft of this ADR specified parts per billion, which made the snake’s Sword of Medusa the number `500` and Plate Mail from a Tiger Worm `2857142`. Owner review rejected it: rarity is reasoned about as "1 in N", so the stored number should be `N`. Writing `2000000` and `350` needs no conversion in either direction, and misreading it is nearly impossible — bigger is rarer, full stop.

The internal representation stays parts per billion, chosen for precision rather than authoring: at a 1-in-2,000,000 target a 10,000,000 denominator would quantise to the integer `5` with a 20% gap to `6`, while parts per billion resolves 0.2% steps there, floors at 1-in-a-billion, and fits a `uint32` alongside a table summing to full probability. All 1,851 currently-priced rows convert with nothing rounding below 10 ppb, so no existing drop loses resolution.

## Considered options

- **A — gold becomes a table row.** Rejected. Reproducing today's rates needs `first_drop_chance` at 37% for grades 1–4 while boss stays 100%, making the gold row's weight per-grade and forcing ~96 hand-authored rows carrying a *new* emergent ratio. G subsumes it: gold is simply a row with an absolute chance.
- **B — separate consumable and gear pools with a per-grade gear share.** Rejected. Fixes stage-1 crowding only, leaves stage-2 crowding (the 216× Medusa gap) untouched, and still expresses rarity as a slice of a pool.
- **D — per-grade outcome profile.** *Was this ADR's recommendation; now rejected.* It is a genuine improvement over today and would make legacy faithful, but it sets gear frequency per grade, which is precisely the control the owner needs per monster. Its per-grade profile survives inside G as the grade multiplier layer.
- **E — renormalise weights at load.** Rejected. With `C` already constant it is arithmetic nobody needs, priced in weights that no longer say what they mean.
- **F — change nothing structural.** Rejected. #66 makes today's system *visible*, and the `G / (C + G)` invariant makes it tunable by someone who knows it — but the Medusa and Plate Mail measurements show the authored numbers are already wrong by two orders of magnitude in places, and no amount of reporting fixes a data model where `1` means two different things in two tables.
- **G — absolute per-(item, monster) rarity ("1 in N") with multiplier layers.** Chosen.

### Content curation is not part of this change

Which items live in which stage is a **separate decision from how their rarity is encoded**, and the two must not ship together. The migration's only verification gate is that `dropodds` reports identical rates before and after; moving an item between stages changes its rate and destroys that gate.

Owner review has already identified intended stage-2 content that does not match the shipped data — the three stones and four Ancient Piece rows sit in all 98 stage-1 tables today, Super Power Green Potion (391) is absent from every drop table despite being droppable in the original, and item 90 (gold) is not a table row at all. Stage 2 additionally carries ~40 monster body-part rows, 9 protection necklaces and 7 magic manuals that no one has yet ruled in or out.

All of that is real work and none of it belongs here. It lands as its own ticket, on top of the new encoding, where each move is a deliberate rate change reviewed on its own terms.

## Migration — reproduce before retune

Mechanical, per table and stage:

```
drop_one_in(row) = round( table.total_weight / (gate(stage, monster) × row.weight) )
```

where `gate` is today's gating — for stage 1, `0.70 × first_drop_chance` (the gold preempt times the grade's chance); for stage 2, `0.05` for ordinary tables and `1.0` for the five `guaranteed_secondary` bosses. Gold becomes an ordinary row at `drop_one_in = 3` (today’s 30%). All multipliers start at 1.0.

This reproduces every current per-kill probability exactly. Verification is a single invariant: `dropodds` before and after must agree per grade to four decimals, and a per-row comparison must agree to within one part per billion — rounding to an integer `N` is the only permitted difference. **No retune happens in the same change as the migration** — today's rates land first, and every subsequent move is loosen-only under the ratchet law.

The 51 boilerplate tables and the 47 real ones convert identically; nothing is hand-authored.

## Consequences

- **The tuning surface inverts.** Today: six terms across three storage locations, three reload semantics, and ~1,900 weights that each mean something different. After: ~1,900 numbers that each read as "1 in N kills", plus three multiplier layers that scale them without bending them. That is more numbers and far less to know.
- **Legacy becomes faithful for the first time**: the original's 21% / 12.6% / 1.4% / 65% is directly expressible as row chances. This *changes* current legacy rates, toward the original — a fidelity fix, labelled as one, verified against `NpcDeadItemGenerator` rather than against today's behaviour.
- `loot_grades.first_drop_chance` retires, subsumed by the per-row chances. `drop_rates.primary` and `drop_rates.gold` in `server_config.json` retire, replaced by the multiplier layers, which live in the database and reload live. The restart-only reload semantics disappear entirely.
- The hardcoded gold preempt (`base_gold_drop_chance`, and the branch that makes a gold drop skip the item roll) is deleted; gold competes as a row like anything else.
- **#66's report becomes simpler and more useful.** Its job stops being "multiply a five-term chain" and becomes "print the stored number and the multiplier stack", plus a per-monster per-item rarity listing — which is the view the owner has been asking for and which no current tool produces.
- **Saturation headroom is now a real design quantity.** A normal monster today sits at 37% (30% gold + 7% item), leaving ~2.7× of global headroom. A boss sits at **100%** and has none — entirely because `first_drop_chance` is 10000 for grade 5. That value is itself disputed (see below), and fixing it restores boss headroom.
- **Stage 2 comes into scope**, unlike every other option. The second drop's *mechanics* are unchanged — timing, corpse-decay placement, `guaranteed_secondary`, scatter spread, the five bosses — but its rows convert to absolute chances like stage 1's. Scatter is unaffected: N independent rolls against a table whose remainder is "nothing" works exactly as it does now.
- The original's reputation modifier shifted the roll from consumables *toward equipment*. Under this model it becomes a per-player multiplier in the same stack, which is where it belongs. Deliberately **not** part of this decision — see the rep-inversion defect below.

## Related defects found during evaluation

Four, none in scope here.

- **Boss stage-1 fires 100% of the time.** `loot_grades` grade 5 carries `first_drop_chance = 10000`, and §8 states it deliberately. Owner review rejects this: stage *2* is the guaranteed venue for the five bosses (`guaranteed_secondary = 1`), and stage 1 was never intended to be certain. It yields boss tiered gear 1 in 11 kills and a boss Legendary 1 in 376, which the ratchet law makes unrevokable after launch. It also leaves bosses with zero multiplier headroom under G.
- **The reputation modifier is inverted.** `baseSecondary = 500 - rating × rep_drop_modifier` with `rep_drop_modifier = 5`, so a player at **rating 100 or above gets no stage-2 drop at all** — no uniques, ever. The original applied the same term to the consumable/equipment split, where good reputation was a *bonus*. Needs confirming against real player rating values.
- **`ItemId::ZemstoneofSacrifice` is 753**, but `items` puts Zemstone at 650 and **Wizard Hat (M) at 753**. `is_special_item(753)` is true in the running server, so Wizard Hat (M) never tier-rolls while Wizard Hat (W) does. Live in 5 drop tables — [#71](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/71).
- **`dropodds` applies the gold preempt to NPCs with no gold dice**, which fall through to a full-rate item roll. Grade-1 "any item" understated by 38.2% — [#72](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/72).
