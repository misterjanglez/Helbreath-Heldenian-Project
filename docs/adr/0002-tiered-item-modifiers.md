# Item drops pivot from retail prefix+secondary rolls to a four-tier modifier system, behind a dual-strategy seam

This project's standing mission is faithfulness to original Helbreath mechanics — and this decision is a recorded, deliberate exception. Dropped gear moves from the retail roll (one guaranteed prefix, 40% chance of one secondary) to a tier system: Common/Rare/Epic/Legendary, where tier is purely modifier count (1–4), modifiers come from a thematic-bucket catalog, and Legendary god-rolls become the game's power apex above Dark Items and named uniques. The pivot is intentional: the retail item chase dead-ends at level 180 (Dark Items are free and re-claimable, uniques are a bounded lottery), while a tiered roll system gives hunting an unbounded endgame. Design contract: `PLANS/ItemTiers_Plan.md`; the full decision trail is the wayfinder map ([issue #22](https://github.com/misterjanglez/Helbreath-Heldenian-Project/issues/22)).

Because it is an experiment against the mission, the pivot ships **reversibly**: both roll strategies live in the codebase behind a config switch (`item_system = legacy | tiered` in `gamedata.db` meta), and the retail-faithful `legacy` strategy remains a genuine launch candidate until play-testing renders a verdict. Where original behavior matters as a reference, retail-site archive evidence outranks the leaked source lineage — this effort restored the Righteous prefix's retail rep-damage mechanic, which the leaked code never implemented.

## Considered options

- **Stay retail-faithful** (prefix + secondary only) — rejected as the *only* mode: post-180 progression collapses to majestic grinding; the drop chase has no ceiling worth farming. Retained as the `legacy` strategy.
- **Extend the retail system in place** (more prefixes, higher values) — rejected: keeps the one-prefix structural limit, inherits the positional prefix/secondary encoding, and blurs what is retail and what is not.
- **Tiered modifier system as the only mode** — rejected: the system is unproven; deleting the faithful path would make a failed experiment unrecoverable.
- **Dual-strategy seam** (chosen) — both strategies data-driven, one world-defining switch, one shared superset encoding. The game launches permanently in ONE mode after the test verdict.

## Consequences

- The item encoding generalizes for both modes: `item_instance_data` becomes a 15-byte POD (tier byte + four type/value/value2 modifier slots), a single flat modifier-ID namespace replaces the positional prefix/secondary enums, and ~11 item-carrying packets reshape under one atomic Compatibility bump (0.7.x → 0.8.0).
- The world database becomes mode-defining: items record their origin (tier 0 = untiered/legacy roll), so both strategies' items can coexist in one DB during testing, and a world's mode travels with it.
- Post-verdict, the unchosen strategy is frozen at its launch feature-set — compiling and switchable, but unsupported; new features target the chosen mode by default.
- The ratchet law governs live tuning: every balance knob launches strict and only ever loosens, because rolled items are permanent world facts — a too-generous launch could only be corrected by confiscation.
- Balance data (catalog, bands, curves, caps, drop weights) lives in `gamedata.db`, live-reloadable, validated fail-fast at boot for **both** strategies — the inactive candidate must stay launch-ready.
- A fresh-start world is required; nothing migrates. Named uniques, necklaces/rings, angels, Dark Items, and crafted/reward gear stay outside the tier system, so their code paths are untouched by the pivot.
