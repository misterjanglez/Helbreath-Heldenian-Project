# Helbreath — Game Domain

Domain language for the Helbreath game (client, server, and shared protocol). Single context covering the whole game; terms are added as they are pinned down.

## Language

### Trading Post

**Trading Post**:
The escrow-based barter market players access through the Auctioneer. Items in it belong to no character until a Trade completes or they are returned.
_Avoid_: trading block, market, auction house (reserved for a possible future evolution)

**Auctioneer**:
The vendor NPC — named Vince in-game — present in every warehouse map, who opens the Trading Post.

**Seller**:
The character who owns a Listing.
_Avoid_: poster, lister

**Listing**:
A bundle of one or more items a Seller has placed on the Trading Post for barter, escrowed until its Trade completes or it is delisted.
_Avoid_: post, posting

**Offer**:
A bundle of items escrowed against a specific Listing by a character other than the Seller.
_Avoid_: bid

**Finalize**:
The Seller's acceptance of one Offer, which completes the Trade.
_Avoid_: accept, complete (as verbs for this action)

**Rescind**:
Withdrawal of an Offer by its offerer, allowed any time before the Trade completes.
_Avoid_: cancel, withdraw

**Delist**:
Removal of a Listing by its Seller, returning the escrowed item.

**Trade**:
The completed exchange of a Listing for an accepted Offer, with both sides' items delivered to Warehouses.

**Seeking note**:
The Seller's optional one-line, display-only text on a Listing describing what they want in exchange.

**Escrowed**:
Held by the Trading Post itself; absent from every character's inventory and Warehouse.

### Storage

**Warehouse**:
The per-character item storage kept by the Warehouse Keeper. Called the *bank* throughout the code — the two names are interchangeable: bank in code, Warehouse in player-facing text.
_Avoid_: storage, depot

**Exchange**:
The existing face-to-face trade window between two nearby players. Distinct from a Trading Post Trade.

### Dark Items (max-level progression)

**Dark Items**:
The Dark Knight / Dark Mage equipment claimable from the City Hall Officer at max level (180). Free, re-claimable, and always unique-owner bound — never tradeable. On official Helbreath USA a GM placed these on the character by email; the City Hall claim menu is this project's automation of that (primary source: helbreathusa.com/darkknightinfo.php, archived 2008).
_Avoid_: DK set (ambiguous with the evil-side Executor set in Snoopy 3.82)

**Majestic Points**:
Earned per level-up's worth of experience past max level (exp resets, point granted). Spend on stat refinement, Angel upgrades, or Dark weapon/wand upgrades. Code name: `gizon_item_upgrade_left`.

**Dark weapon/wand upgrade**:
+2 per upgrade, never fails, cost `x(x+6)/8 + 2` at current level x — reproducing the official HB USA table exactly: +0→+2 (2), +2→+4 (4), +4→+6 (7), +6→+8 (11), +8→+10 (16), +10→+12 (22), +12→+14 (29), +14→+15 (37); 128 points total.

**Evolution chains** (claim the base form; evolutions are earned — per player
clarification of official HB USA behavior, 2026-07-23):
- Dark Knight: Flameberge → Giant Sword (at +4) → Dark Knight Templar (at +8, not
  glowing) → the Templar starts glowing when maxed at +15.
- Dark Mage: Magic Staff → Magic Wand (at +4) → Dark Mage Dragon Wand (at +8, not
  glowing) → the Dragon Wand starts glowing when maxed at +15. (The Dragon Wand is
  legacy item 746 — BlackMageTemple in the CENTUU cfg, previously misnamed "Templar".)
- Great Sword (battle-mage weapon) and Rapier are standalone — upgradeable, never evolve.
- The +15 glow is instance color 9, which drives the client's pulsating `dk_glare`.
- The legacy 746→892 stage is dropped: item 892 exists in no known config.

**Claimable pieces**:
DK: Hauberk, Full-Helm, Leggings, Plate Mail, Flameberge, Great Sword, Rapier.
DM: Hauberk, Chain Mail, Leggings, Robe, Magic Staff. (Male-only oddities — Scale Mail, Leather Armor — are not offered. Sex variants resolved server-side.)

**Equip gates** (original-faithful, enforced at equip, not claim):
Dark Mage Robe requires MAG 100; weight rule requires STR ≥ weight/1000 (Dark Knight Plate Mail = STR 100); everything else has no stat requirement.

### Item Tiers (tiered modifier system)

Design locked 2026-07-27. Binding contract: `PLANS/ItemTiers_Plan.md`; pivot rationale: `docs/adr/0002-tiered-item-modifiers.md`.

**Tier**:
An item's rarity rung — Common / Rare / Epic / Legendary — which is purely its Modifier count: Common 1, Rare 2, Epic 3, Legendary 4. Tier never changes base stats (damage, defense, weight, durability) and never changes value legality (shared Bands); it selects the Tier curve rolls draw from and how many Modifiers the item carries. Stored as an explicit byte (0 = Untiered), never derived from count. (Map charting lock, 2026-07-26; Encoding decision, 2026-07-27.)
_Avoid_: grade (reserved for Loot grade), rarity (informal only)

**Chase item**:
Collective term for the pre-existing aspirational gear outside the tier system: named uniques (the `is_special_item` roster), Dark Items, and maxed angels. Under the tier pivot they sit below Legendary god-rolls in raw power, but Dark Items remain the free level-180 baseline and uniques keep exclusive Signature effects. (Power ceiling decision, 2026-07-26.)

**Signature effect**:
An effect that exists only on a named unique — Xelima HP drain, Merien activated immunity, Medusa on-hit hold, Sword of Ice Elemental on-hit freeze, and kin. The tier system's modifier catalog never rolls these; they are permanently unique-exclusive. (Marquee mechanics decision, 2026-07-27: on-hit freeze added after the code showed it is the SoIE's activated ability. Value model decision, 2026-07-27: Kloness rep damage removed — retail evidence showed common Righteous drops carried the same mechanic, so it fails the signature test; the Kloness niche is top base damage with the mechanic built in.)

**God-roll**:
An item whose modifiers all rolled at or near their designed Band ceilings. The perfect item is a designed quantity — bands, curves, and caps live in `gamedata.db` — never an emergent lottery outlier.

**Modifier**:
One rolled line on a tiered item: a catalog entry plus one rolled value (two for attribute pairs, which roll each half independently). Tier is purely how many modifiers an item carries — Common 1, Rare 2, Epic 3, Legendary 4. (Modifier catalog decision, 2026-07-27.)

**Bucket**:
The thematic group a modifier belongs to — DAMAGE, PRECISION, HANDLING, ECONOMY, SET-AXIS, COMBAT UTILITY, CASTING, ATTRIBUTES, MARQUEE. The bucket law: an item carries at most one modifier per bucket, order-free — which structurally forbids repeats and same-kind stacking. Membership, per-class eligibility, weights, and min-tiers all live in `gamedata.db`.
_Avoid_: group, pool (reserved for the legacy `attribute_pools` tables)

**Set axis**:
One of the eight classic armor lines players compile sets around: Defense Ratio, Physical Absorb, Magic Resist, Magic Absorb, HP/SP/MP Recovery, Poison Resist. All eight share one bucket, so every armor piece contributes exactly one set-axis line — set building stays an across-your-slots game.

**Marquee**:
The Legendary-only bucket of new exotic mechanics, with a per-class identity: weapons break the target down (Sunder, Bleed, MP drain, SP drain — one per item), wands cast faster (max 20% cast-time reduction), capes and leggings grant movement speed (max 10% per item, 20% aggregate hard-clamp). Each marquee modifier declares its eligible item classes in data and carries exactly one rolled value; none may replicate a Signature effect. (Marquee mechanics decision, 2026-07-27 — replaced the original "weapons freeze" leg, which fell to the signature test.)

**Sunder**:
Weapon marquee exotic: rolled proc chance (max 15%) that a landed hit breaks the target's armor — fixed −50 defense ratio for 5 s, re-proc refreshes. A debuff, not CC and not a damage scalar; works on players and NPCs; unresistable in v1.

**Bleed**:
Weapon marquee exotic: rolled proc chance (max 15%) that a landed hit opens a bleed — fixed 5 damage every 2 s for 8 s, re-proc refreshes. Deliberately distinct from Poisoning on every axis: fixed tick vs dice roll, refreshes vs blocked-while-poisoned, ignores poison resist, not Cure-curable. Can co-roll with Poisoning (different buckets).

**Band**:
A modifier's designed min–max value range, written in display units (what the player reads). One band per modifier; every tier rolls the full band by default — per-tier window deviations are data edits. The band's top is the modifier's designed ceiling: reachable at any tier, mythically rare at all of them. (Value model decision, 2026-07-27.)
_Avoid_: range, window (reserved for per-tier deviations)

**Tier curve**:
A named probability curve — Common, Rare, Epic, Legendary, extensible with future contexts (unique_forge, reroll) — defined over normalized Band position, selecting how lucky a roll is. Tier changes the curve, never value legality. Four global curves plus sparse per-modifier overrides, all in `gamedata.db`. (Value model decision, 2026-07-27.)

**Aggregate cap**:
The per-modifier ceiling on the summed value of same-modifier lines across a character's equipped items — sum, then hard-clamp, at equip recalc. Populated for every stackable modifier at launch; per the ratchet law caps only ever loosen. Angels, unique built-ins, and hero armor are exempt (non-tier systems never count against tier caps). (Value model decision, 2026-07-27.)
_Avoid_: set cap (retail-era term; kept only when citing retail sources)

**Perfect roll**:
A line that rolled the very top of its Band. Near-aspirational by contract: ≈1-in-25,000+ on a Legendary single line, ≈1-in-100,000 for a both-halves-perfect pair, rarer still at lower tiers. The chase's asymptote — legal, designed, almost never witnessed. (Value model decision, 2026-07-27.)

**Roll strategy**:
One of the two pluggable implementations of item attribute generation living behind a single seam in the codebase: `legacy` (the retail-faithful prefix + secondary roll) and `tiered` (the Common/Rare/Epic/Legendary modifier system). Both are data-driven and both are live launch candidates until the test verdict; after it, features target the chosen strategy by default and the other is frozen at its launch feature-set. (Configurability decision, 2026-07-27.)

**Item system mode**:
The world-defining switch selecting which Roll strategy a world runs — `item_system = legacy | tiered` in `gamedata.db` `meta`. Read once at server boot (restart-only, deliberately excluded from live `reload`), replicated to clients through the config cache. A world's mode travels with its database. (Configurability decision, 2026-07-27.)

**Unified modifier ID**:
The single flat byte namespace (1–255 usable, 0 = empty slot) every modifier type occupies in storage, on the wire, and in the DB — replacing the old positional prefix/secondary enum pair, renumbered from scratch. IDs are data (rows in the replicated modifier catalog); code keys behavior off each row's code-side effect id. Both Roll strategies emit unified IDs: strategies differ in what they roll, never in what they store. (Encoding decision, 2026-07-27.)

**Untiered**:
An item whose stored tier byte is 0 — rolled by the legacy strategy rather than the tier system. Distinguishes a legacy prefix+secondary roll from a genuine two-modifier Rare when both strategies' items share one world database. Tier is stored fact, never derived from modifier count. (Encoding decision, 2026-07-27.)

**Plain**:
Gear with no modifiers at all (tier byte 0, every modifier slot empty) — never rolled by either strategy. In tiered mode plain gear enters the world only through NPC shops, the blacksmith, and granted rewards (Hero's / Combatant Capes); monster-dropped gear is always tiered, Common floor. Player rule: shop and granted gear is plain, hunted gear is tiered. (Drop economics decision, 2026-07-27.)
_Avoid_: clean, blank

**Drop stage (First drop / Second drop)**:
The two rungs of the existing drop pipeline, retained by the tier system. The First drop rolls the instant a monster dies; the Second drop is rolled at death but appears when the corpse decays — the venue for named uniques and boss guaranteed/scatter loot. **The tier roll lives in the First drop only, bosses included**: a tiered item is at most one per kill, at death. Second drops never tier-roll and are mechanically unchanged; in tiered mode the boot validator rejects tier-eligible gear in any stage-2 table. The first-drop chance is a per-Loot-grade data knob (grades 1–4 keep today's 10%, bosses 100%). (Spec-assembly review clarification, 2026-07-27.)
_Avoid_: drop-table tier (the code's legacy name for stages — collides with item Tier; rename to stage at implementation)

**Loot grade**:
A monster's rung on the five-step drop ladder — vermin / standard / veteran / elite / boss — stored as a column on `npc_configs`, each grade mapping to one live-reloadable tier-weight row in `gamedata.db`. The sole determinant of an item's tier odds at drop time: no zone layer, no per-monster override. The staircase access rule is the hard gate — Rare needs standard+, Epic needs veteran+, Legendary needs elite+ — and per the ratchet law a grade's tier access only ever widens post-launch. (Drop economics decision, 2026-07-27.)
_Avoid_: monster tier (collides with drop-table tiers and item Tiers)

**Enchant (+N)**:
The stone-upgrade bonus — the `enchant_bonus` byte, shown as a "+N" name suffix — earned via Xelima (weapons) / Merien (armor) stones. Fully orthogonal to Tiers and Modifiers: works identically in both Roll strategies and on any tier ("+7 Legendary" is legal), never counts against buckets or aggregate caps. The whole table is data-driven in `gamedata.db` (per-step success %, per-step destroy flag, per-category caps). Launch values: +7 cap for weapons/shields/armor/wands (custom-made weapons +10), safe through +3 — failures attempting +1..+3 eat only the stone, +4 and above destroy the item. Majestic upgrade lines (angels +10, DK weapons +15) are a separate system on untiered items. (System interactions decision, 2026-07-27.)
_Avoid_: upgrade level (collides with majestic upgrades)

**Tier presentation**:
How tier reads to the player. The tier word leads the item name ("Legendary Chain Mail" — continuing the prefix-word tradition) and the whole name renders in the tier color everywhere names appear (tooltip, pickup chat line). Palette is Helbreath-native, from the game's own dye table: Common white, Rare green rgb(128,192,128), Epic blue rgb(150,160,225), Legendary gold rgb(255,176,16) — no imported MMO purple/orange. Tier names Common/Rare/Epic/Legendary are final and player-facing. Tooltip layout is otherwise today's exactly (gray labels, green values, one line per modifier). Sprites are never tinted in tiered mode — tier reads from name color, tooltip, and beam; legacy mode keeps its prefix dye-tints. Every tiered ground drop emits a light-beam in its tier color (Common's a faint neutral). No server broadcasts, no map banners. Names, colors, and the name template live in the tier-presentation table replicated via the config cache. (Presentation decision, 2026-07-27.)

### Provenance Ledger (item identity & audit)

Design locked 2026-07-29. Binding contract: `PLANS/ItemLedger_Plan.md`; decision records: `docs/adr/0003-item-provenance-ledger.md` (Serials + ledger), `docs/adr/0004-single-game-db.md` (Game DB consolidation).

**Serial**:
The permanent server-global identity of one Instanced item — a monotonic int64 minted once at creation, never reused, never sent to the client. Serials are server + database facts only; a tampered client cannot observe the tracking, and no Compatibility bump is ever involved.

**Provenance Ledger**:
The append-only `itemledger.db` recording every Instanced item's creation (`item_instances`) and every custody/state transition it undergoes (`item_events`), plus aggregate flows for Counted items. A passive audit trail: the in-RAM world and snapshot saves stay authoritative.

**Instanced / Counted**:
The two tracking granularities. An item type is Instanced iff non-stackable (gear, named uniques, angels, Dark Items — Commons included); stackables (gold, potions, ammo) are Counted via daily flow counters, because stack merge/split dissolves identity.
_Avoid_: tracked/untracked (everything is tracked; the tiers differ in granularity)

**Mint**:
Creating an item instance and assigning its Serial. All creation funnels through the minting factory in `ItemManager` — a raw `new CItem` in game code is a review defect. (Aligns with the existing GmMint action and the tiers GM creator, #56.)

**Biography**:
One Serial's ordered event history — the query that settles ownership disputes and feeds the legal-transition replay.

**Reconciliation**:
The comparison of snapshot state (inventories, Warehouse) against the ledger's derived current-holder state. Mismatches — one Serial in two inventories, a holder the ledger never saw — are the dupe detector.

**Transition machine**:
The legal-move rules a Serial's Biography is replayed through (`ItemBiography.h`, table in `PLANS/ItemLedger_Plan.md`). Places: unborn, unplaced, inventory, warehouse, escrow, ground, gone. Where reconciliation needs both records, this checks the ledger against itself — an item cannot be picked up twice with no drop between, and nothing happens to it after it left the world.
_Note_: an item minted and never placed may legally go anywhere (creation venues do not record placement), and `Sell`/`Deplete` are not exits (the exit is the `destroyed` that follows them).

**Game DB**:
The single consolidated account/character store (`game.db`) replacing the per-account `accounts/*.db` files: `UNIQUE` character names, one persistent WAL connection, atomic multi-account transactions (Exchange, Trading Post custody). `gamedata.db`, `MapInfo.db`, and `itemledger.db` remain separate.

### Guilds

Design locked 2026-08-07 (session #93). Binding contract: `PLANS/CoreSystems_Restoration_Plan.md` §3.1/§3.1.1; decision records: `docs/adr/0006-guild-rank-permission-bitmask.md`, `docs/adr/0007-guild-burn-economy-title-slots.md`.

**Guild**:
A town-bound organization of citizens of one side, led by a Guildmaster.

**Rank**:
A member's position inside a Guild — Guildmaster, Officer, or Guildsman — determining which Permissions they hold. Never the guild's progression rung (that is Guild Level).
_Avoid_: role, grade

**Officer**:
The delegated middle Rank, able to approve Join Requests, kick, and coordinate in the Council. Distinct from the Guildhall Officer NPC.
_Avoid_: elder

**Guildhall Officer**:
The guildhall NPC — named Kennedy in-game — who handles guild-name applications and self-secession.
_Avoid_: Officer (bare, which means the Rank)

**Permission**:
A capability a Rank grants its holder (approving joins, kicking, claiming Commander, withdrawing from the Treasury, speaking in the Council).

**Guild Level**:
The guild-wide progression rung earned by Donations, which unlocks Title slots, member capacity, and perks.
_Avoid_: guild rank

**Donation**:
The irreversible sacrifice of a member's own gold, enemy kills, or contribution to feed the guild's progression. Donated resources cease to exist for the donor.
_Avoid_: deposit (which is reversible and feeds nothing)

**Treasury**:
The guild's shared gold wallet — deposited by any member, withdrawn by Officers and above. Storage only; never feeds progression.
_Avoid_: bank (reserved for the Warehouse), vault (reserved for possible future item storage)

**Title**:
A claimable, slot-limited distinction — Commander, Huntmaster, or Raidmaster — whose slot count grows with Guild Level and whose bonuses apply only while held. First come, first served; leaders may re-assign; absence releases it.

**Commander**:
The war-command Title: eligibility to take the Commander duty in a Crusade. Officer-and-above may claim.

**Huntmaster**:
The PvE-bonus Title.

**Raidmaster**:
The PvP-bonus Title.

**Council**:
The per-town chat channel shared by the leaders (Officer and above) of all that town's guilds.
_Avoid_: command channel, officer chat

**Roster**:
The guild's member list — names, Ranks, Titles, online status — and the surface for moderation actions.

**Join Request**:
A pending application to a guild, opened by handing an Admission Ticket to an approver or filing at the Guildhall Officer; it waits in queue until an approver decides.

**Admission Ticket / Secession Ticket**:
The purchasable papers (items 88/89) that open a Join Request or a leave request. Consumed on filing, whatever the outcome.

### War Events (crusade, heldenian, apocalypse)

**War event**:
One of the three server-wide wars — Crusade, Heldenian, Apocalypse. At most one runs at a time (the mutual-exclusion rule, enforced by the start funnels in `WarManager`).

**Event schedule**:
The `event_schedule` table in `gamedata.db` — one row per scheduled start (and, for heldenian/apocalypse, end) with a day-of-week, times, an `is_active` switch, and for heldenian rows a Battle type. The event scheduler (5 s tick in `WarManager`, #97) drives wars from active rows; inactive rows are loaded but never fire.

**Battle type**:
Which Heldenian is fought: `1` battlefield (btfield towers) or `2` castle siege (hrampart gates). Named in `EventSchedule.h` (`heldenian_battle`).

**Once-per-day guard**:
The scheduler starts each event at most once per calendar day (yyyymmdd stamp, not day-of-week — a long-running server must not block next week's same weekday). GM `/begin*` commands bypass the schedule and the guard by design; a GM war neither consumes nor is blocked by the day's scheduled one.

**Scheduled end**:
A row's end time ends its event (heldenian/apocalypse) whenever that event is running at that minute, regardless of who started it. Crusade has no clock end — it ends through its own war flow or `/endcrusade`.
