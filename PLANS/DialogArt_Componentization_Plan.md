# Dialog Art Componentization Plan

Rebuilding the `game_dialogs` family (11 sheets, 239 frames) from a small set of
reusable components instead of 239 hand-generated images.

Status: **proposed** — art pipeline only, no gameplay change except §6.

---

## 1. Why componentize

`game_dialogs.pak` holds 239 frames, but they are not 239 distinct designs. Grouped
by size and role they collapse to about a dozen archetypes:

| Archetype | Component | Frames it rebuilds | Source sheets |
|---|---:|---:|---|
| Panel body | `panel_9slice` + watermark | ~24 | 0,1,2,3,4,6,7 |
| Title strip | `title_3slice` + text | 22 | 9 |
| Standard button | `btn_74x20` × 2 states | **64** | 10 |
| Wide button | `btn_176x24` × 2 states | 10 | 10 |
| Recessed field | `field_9slice` | ~20 | 0,1,9 |
| Icon button | `iconbtn_36x44` + glyph | 10 | 5 |
| Micro icon | `glyph_12` set | 22 | 1,3,4 |
| Scrollbar | track / thumb / arrows | ~8 | 1,10 |
| Heraldry tile | `crest_46x52` set | 32 | 4 |
| Character card | `card_107x187` × 2 | 2 | 10 |
| HUD bar | composite from parts | 2 | 5 |
| Cursors | — leave as-is — | 13 | 8 |

**~226 of 239 frames come from ~12 components plus three glyph libraries.**

Sheet 10 is the clearest case: 64 of its 83 frames are the *same* 74×20 button — 32
labels × 2 states (even = idle grey, odd = hover gold). One component, two
generations, done.

### The alignment rule

Frame rects in the pak are fixed. Anything the C++ writes at a hardcoded offset must
land where the art puts it. So every component is classified:

- **Unconstrained** — background, frames, ornament, watermarks. Generate freely.
- **Constrained** — recessed fields, button faces, icon wells. Position is owned by
  the assembler script, never by the generator.

Generate only unconstrained art. Let the script place everything constrained. This is
the whole point: it makes "pixel-perfect" a property of the build, not of the prompt.

---

## 2. Component set — literal dimensions

`slice` = 9-slice/3-slice insets in final pixels (corner size / border thickness).
`gen` = canvas to generate at, before downscaling.

| # | Component | Final size | Slice | Gen canvas | States |
|---|---|---|---|---|---|
| 1 | `parchment_tile` | 256×256 seamless | tile | 1024×1024 | 1 |
| 2 | `panel_9slice` | any | 24 / 12 | 1024×1024 | 1 |
| 3 | `field_9slice` | any | 8 / 4 | 1024×256 | 1 |
| 4 | `btn_74x20` | 74×20 | 3-slice, 10 | 1536×416 | 2 |
| 5 | `btn_176x24` | 176×24 | 3-slice, 16 | 1536×208 | 2 |
| 6 | `title_3slice` | any × 31 | 3-slice, 40 | 1536×256 | 1 |
| 7 | `corner_boss` | 26×26 | fixed | 512×512 | 1 |
| 8 | `divider_flourish` | 30×13 | fixed, mirrored | 512×256 | 1 |
| 9 | `iconbtn_36x44` | 36×44 | fixed well | 512×640 | 2 |
| 10 | `scroll_set` | 23×N track, 15×15 arrows | 3-slice | 768×1024 | 2 |
| 11 | `card_107x187` | 107×187 | 9-slice | 768×1024 | 2 |
| 12 | `pull_tab` | 18×64 | fixed | 384×1024 | 2 |

### Glyph libraries

| Library | Cell | Count | Used by |
|---|---|---:|---|
| `glyph_12` | 12×12 | 22 | +, −, ▲, ▼, checkbox, radio … |
| `icon_36` | 28×36 inside the 36×44 well | 10 | HUD action bar |
| `crest_46` | 46×52 | 32 | guild / crusade heraldry |
| `watermark` | varies | ~15 | one per panel theme |
| `status_24` | 24×24 | ~20 | buffs/debuffs (§6) |

### Panel sizes to render `panel_9slice` at

```
523×356  311×373  338×202  334×256  331×303  304×328  279×280  270×381
270×357  270×104  258×339  258×328  237×170  226×188  226×138  215×87
197×213  180×183  142×93   137×159  104×96   90×73    81×115   74×88   67×74
```

### Title strip widths to render `title_3slice` at (all height 29–36)

```
258×31 ×12   310×34 ×3   270×31 ×2   331×31   340×34   334×31   304×36   361×29
```

---

## 3. Two techniques that matter more than the prompts

**Batch related pieces into one canvas.** Image models hold style far better *within*
a single image than *across* separate generations. Ask for the idle and hover button
side by side in one image, or the frame with its corner boss beside it, then slice
them apart in the script. This is the single biggest lever on congruence.

**Chain reference images.** When generating component N, attach components 1…N−1 and
say "match the material, palette, bevel depth and wear of the attached pieces
exactly." Never generate a component cold after the first one.

Supporting rules:

- **Generate large, downscale last.** Never ask for a 74×20 image. Get 1536px wide and
  Lanczos down. Direct small generations come back as mush.
- **Flat magenta `#FF00FF` key, not "transparent."** Models fake alpha badly.
- **Verify 9-slice pieces at both extremes** before committing — render `field_9slice`
  at 78×19 *and* 232×40. Any ornament in the middle band ruins the wide one.
- **Re-roll cheaply.** Unconstrained art has no measuring step. Take 4, pick 1.

---

## 4. Style block

Prepend verbatim to every prompt. Do not paraphrase it between components.

The palette below is **measured from the reference panel**, not invented — sampled
from the 40 flattest 28×14 patches (for the field) and by luminance percentile over
the border ring (for the metal). An earlier draft of this plan carried guessed hex
values that were ~70 luminance units too bright; anything generated against those
will not sit with the rest.

```
STYLE: 2D hand-painted fantasy RPG interface art, late-1990s isometric MMO
(Helbreath / Diablo II / Baldur's Gate era). Aged parchment and dark bronze with
heavy patina in the recesses.
PALETTE — match these measured values:
   parchment field  #AF854C     <- the LIGHTEST large area
   gold highlight   #AE8950
   brass body       #684923
   patina midtone   #362513
   deep shadow      #0D0602
CRITICAL: the metal frame is DARKER than the field it surrounds — a dark bronze
border around a lighter tan field, with gold catching only on raised edges. Do
NOT make the parchment bright yellow or cream; it is a muted tan.
Painterly, subtly worn, NOT clean vector, NOT glossy, NOT chrome, NOT neon.
Flat orthographic front view, zero perspective, zero cast shadow outside the
object's own edge. Even lighting from top-left. No text, no letters, no numerals
anywhere. Object centered and isolated on flat pure magenta #FF00FF with
generous margin.
```

### QA gate — run on every component before accepting it

`Scripts/tools/check_component.py` (see §7). Thresholds that matter:

| Check | Pass | Why |
|---|---|---|
| Wrap-seam ratio (tiles) | < 1.6 | wrap diff vs interior adjacent-line diff |
| Low-freq spread | < 18 | catches an unwanted focal blotch |
| Patch stddev | 4–6 | grain energy must match the reference panel's 4.4 |
| Mean vs palette | ±8 per channel | keeps the family together |

Measure grain on **flat** patches only — sort candidate patches by stddev and use
the flattest 40. Sampling blind picks up ornament and text and reads ~30 instead
of ~4.

Generator output has consistently needed a post-pass; budget for it rather than
re-rolling. Seam heal (cross-fade the wrap band) and a per-channel gain onto the
target mean both preserve grain and fix the two failure modes seen so far.

---

## 4b. Superseding decision — harvest, don't generate

**Steps 1–12 below are now the fallback, not the primary route.**

The reference panel (1063×1480, aspect 0.718 = exactly 270/376, a clean 3.937×) is
already the target style at better-than-native resolution. Cutting components out of
it beats generating them: no palette drift, no seam healing, no slice-plane guessing,
no per-piece QA — and every piece is guaranteed congruent because it came from one
image.

Harvested to `Sprite Work/components/` (game-size PNG + `@hi` master + `components.json`
recording every source rect):

| Component | Game size | Role |
|---|---|---|
| `frame_corner_tl` | 41×41 | rotate/flip for all four corners |
| `frame_edge_top` / `_bottom` | 25×14 | stretch horizontally |
| `frame_edge_left` / `_right` | 14×25 | stretch vertically |
| `title_plate_blank` | 183×34 | 3-slice, any width 258–361 |
| `field_box` | 71×22 | 9-slice — every input field in the game |
| `row_medallion` | 20×19 | diamond beside each value row |
| `divider_bar` | 241×20 | 3-slice section rule |
| `divider_medallion` | 53×20 | stamped at the rule's midpoint |
| `well_corner_tl` | 32×28 | portrait-well filigree |
| `stat_box` | 62×23 | bottom stat field |
| `fleur_sep` | 13×37 | column separator |
| `griffin_watermark` | 127×185 | panel emblem |
| `parchment_tile_256` | 256×256 | seamless field |

**Verified.** The frame reassembles cleanly at 270×376, 523×356, 362×164 **and
90×73** — the small size that destroyed the generated frame. `title_plate_blank`
3-slices correctly at all eight widths sheet 9 needs.

Two facts worth keeping:

- `field_box` lands at game x **180.1…250.7**. `DialogBox_Character.cpp` already
  writes at `sX+180, sX+250`. The horizontal axis needs no code change at all.
- The reference's eight value rows are a **uniform 22.6px pitch** (game y 104.9,
  127.5, 150.4, 173.0, 195.6, 218.2, 240.8, 263.4) against the code's grouped
  19/17/31/18/17/32/17. Rows 1, 4 and 7 coincide; the rest do not. Adopting the
  reference layout means changing the eight `sY +` constants once — see §1.

Generation is still the route for anything the reference doesn't contain: buttons,
icon wells, scrollbars, glyph libraries, the remaining watermarks. Use the prompts
below for those, with the harvested pieces attached as reference images.

Open item: `title_plate_blank`'s stretched middle is sampled from a 24px text-free
strip, so very wide instances show slight horizontal streaking. Mirror-tiling the
strip instead of stretching it would fix it if it ever reads badly in game.

## 5. Build order and prompts

Ordered so each step locks in vocabulary the next step inherits. Do not reorder —
steps 1–3 establish the material language everything else references.

### Step 1 · `parchment_tile` — the base

```
[STYLE]  ...except: fill the entire canvas edge to edge, no magenta, no margin.
A SEAMLESSLY TILEABLE aged parchment texture. Subtle fiber grain, faint mottling,
gentle age-staining. Low contrast — this sits behind UI elements and must never
compete with them. CRITICAL: top edge must tile perfectly against bottom, left
against right, no visible seam, no feature that reads as a corner or centerpiece.
Uniform density across the whole field.
```

### Step 2 · frame kit — `corner_tile` + `edge_strip` + `edge_medallion`

> **Do not ask for an assembled frame.** The first attempt did, and it could not be
> sliced: the generator drew the corners at 18.5% of the frame width (49px on a
> 270px panel, 98px of 270 consumed), gave them a scalloped inner profile so the
> content area came out notched instead of rectangular, and parked a diamond
> ornament in the middle of each edge run where the stretch band lands. At 90×73 a
> single corner filled the whole panel. A model drawing "a frame" composes it as a
> picture — ornament wherever it looks good, corners scaled to the whole. Slice
> planes cannot be reverse-engineered out of that.
>
> Generate the **pieces**. Then the corner is a square tile at a size you chose, the
> edge is uniform because it was drawn as a strip, and the medallion is a stamp.

Hard constraints, measured from the reference panel:

| | target | first attempt |
|---|---|---|
| Border thickness @270 wide | **14px** | 18–19px |
| Corner tile @270 wide | **≤24px** | 49px |
| Smallest panel it must survive | 67×74 | failed |

24px is not a preference. The smallest panels in the family are 67×74, 74×88 and
90×73, so `2 × corner` has to clear 67.

```
[STYLE]   Reference: attached parchment tile and corner boss.
THREE SEPARATE OBJECTS on one canvas, clearly spaced apart, NOT assembled into a
frame. Do not draw a picture frame.

OBJECT 1 — CORNER TILE. A perfectly SQUARE tile containing a single ornate
top-left corner fitting of a metal frame. The metal runs along the TOP edge and
the LEFT edge of the tile, meeting in a decorative corner boss. The metal must
reach exactly to the top and left edges of the tile and STOP exactly at the right
and bottom edges, where a straight border strip will butt against it. CRITICAL:
the INNER profile of the metal forms a CLEAN 90-DEGREE RIGHT ANGLE — no scallops,
no curves, no flourishes intruding diagonally into the empty area. The empty area
is magenta and rectangular. Ornament is compact and legible at 24 pixels square.

OBJECT 2 — EDGE STRIP. A long straight horizontal band of the same metal, as if a
short length of the frame's top edge were cut out and laid flat. Very wide and
thin, roughly 12:1. UNIFORM along its entire length — a pure horizontal
extrusion, no ornament, no medallion, no centrepiece, no variation, no taper. Its
thickness matches the metal band in OBJECT 1 exactly. It must read correctly when
stretched to any length.

OBJECT 3 — EDGE MEDALLION. A single small diamond/lozenge ornament that will be
stamped onto the centre of an edge strip later. Compact, symmetric on both axes,
sized to sit within the strip's thickness.
```

Assembly: corners at the four corners, `edge_strip` stretched between them,
`edge_medallion` stamped at each edge's midpoint. The medallion stays centred at
every panel size, which is what you want visually anyway — a stretched one does not.

Verify with `slice9.py` at **90×73** first. If the smallest size works the rest will.

### Step 3 · `field_9slice` — the piece that broke frame 0

```
[STYLE]   Reference: attached panel frame — same brass, same patina, same bevel depth.
A horizontally elongated SUNKEN INPUT FIELD: an inset well with a thin brass bevel
around a darker recessed interior. Interior EMPTY and uniform. Deep shadow along
the top and left inner edge, warm highlight along bottom and right, so it clearly
reads as pressed INTO the panel. Wide letterbox canvas, roughly 4:1.
CRITICAL for 9-slicing: left and right end caps are exact mirror images occupying
narrow equal-width zones. Everything between them is a PERFECTLY UNIFORM
horizontal extrusion — no ornament, no centre medallion, no variation — so it
stretches to any width or height without artifacts.
```

### Step 4 · `btn_74x20` — both states, one canvas (rebuilds 64 frames)

```
[STYLE]   Reference: attached frame + field — same material family.
TWO BUTTONS stacked vertically on one canvas, identical geometry, different state.
Each is a small horizontal button plate with a raised bevel, a narrow flat face
where a label would sit (leave it EMPTY — no text), and a tiny decorative notch
at each end. Aspect roughly 3.7:1.
TOP  = IDLE: cool desaturated grey-brown stone/pewter face, restrained.
BOTTOM = HOVER: identical geometry, but the face is warm glowing amber-gold, lit
from within, clearly the same button energised. Geometry must be PIXEL-IDENTICAL
between the two — only the colour and glow change.
CRITICAL for 3-slicing: end caps mirror each other; the middle is a uniform
horizontal extrusion.
```

### Step 5 · `btn_176x24` — wide variant, both states

```
[STYLE]   Reference: attached 74×20 buttons — this is the SAME button, wider and taller.
TWO BUTTONS stacked vertically, idle grey-brown on top, hover amber-gold below,
geometry pixel-identical between them. Same bevel profile, same end notches, same
material as the reference — only the proportions change. Aspect roughly 7.3:1.
Empty label face, no text. 3-sliceable: mirrored end caps, uniform middle.
```

### Step 6 · `title_3slice` (rebuilds 22 frames)

```
[STYLE]   Reference: attached panel frame and buttons.
A long horizontal ornate BANNER PLATE for a dialog title bar. Raised brass
cartouche with a slightly darker inset field across the middle where a title would
sit — leave that field completely EMPTY, no text. Small decorative finials at each
end. Very wide letterbox, roughly 8:1.
CRITICAL for 3-slicing: left and right end treatments mirror each other in narrow
equal zones; the entire middle is a uniform horizontal extrusion stretchable to
any width from 258 to 361 pixels.
```

### Step 7 · `divider_flourish`

```
[STYLE]   Reference: attached set.
A single small ornamental flourish used as a section-header divider: compact
horizontal scrollwork with a small round centrepiece and tapering tendrils
sweeping outward, like wrought-brass filigree. It will be MIRRORED to make a
matched left/right pair, so design it as the LEFT piece with tendrils sweeping
right. Delicate but still legible at 30 pixels wide.
```

### Step 8 · `iconbtn_36x44` — both states

```
[STYLE]   Reference: attached set.
TWO ICON BUTTONS side by side, identical geometry. A small vertical plate with a
raised brass rim around a deep EMPTY square recess where an icon will be placed —
leave that recess flat and empty, it will be filled programmatically. Portrait
aspect roughly 4:5.
LEFT = IDLE grey-brown. RIGHT = HOVER warm amber, same geometry.
```

### Step 9 · `scroll_set`

```
[STYLE]   Reference: attached set.
A UI SCROLLBAR KIT, pieces laid out separated on one canvas:
(a) a tall narrow vertical TRACK — a recessed channel, uniform along its length,
    3-sliceable to any height;
(b) a short vertical THUMB / grip that rides in that track, raised with a small
    centre ridge;
(c) an UP ARROW button and (d) a DOWN ARROW button, small squares with raised
    bevel and an engraved brass triangle.
Render each of (c) and (d) TWICE — idle grey-brown and hover amber.
```

### Step 10 · `pull_tab` — the side-sheet handle (§6)

```
[STYLE]   Reference: attached set.
TWO SMALL VERTICAL PULL-TABS side by side, identical geometry. Each is a narrow
brass tab shaped to protrude from the right edge of a panel — flat on the left
where it meets the panel, rounded and bevelled on the right, with a subtle
engraved chevron pointing RIGHT. Tall narrow aspect, roughly 1:3.5.
LEFT = IDLE grey-brown. RIGHT = HOVER amber-gold.
```

### Step 11 · `card_107x187`

```
[STYLE]   Reference: attached set.
TWO CHARACTER-SELECT CARDS side by side, identical geometry. A tall panel with a
large EMPTY oval portrait recess in the upper two thirds and three EMPTY narrow
horizontal fields below it. No text, no labels. Aspect roughly 4:7.
LEFT = unselected, cool grey-brown parchment.
RIGHT = selected, warm glowing gold parchment. Geometry pixel-identical.
```

### Step 12 · watermark library — one prompt per theme, batch 4 per canvas

```
[STYLE]  ...except: monochrome warm sepia only, no saturated colour, soft edges,
LOW contrast — these are faint tonal ghosts sitting behind text at low opacity
and must never compete with it.
FOUR WATERMARK EMBLEMS on one canvas in a 2×2 grid, evenly spaced:
  1. <theme A>   2. <theme B>   3. <theme C>   4. <theme D>
```

Themes to cover, matching the panels they sit behind:

| Panel | Theme |
|---|---|
| Character info | griffin rampant on a kite shield |
| Magic list | hexagram / arcane circle |
| Skill | crossed tools over an anvil |
| Inventory | leather satchel and buckles |
| Bank | strongbox with iron bands |
| Shop / sell | balance scales and coin |
| Manufacture | anvil and smith's hammer |
| Guild | crossed banners |
| Crusade / war | siege field silhouette |
| Party | linked rings |
| Quest | rolled scroll with seal |
| System menu | astrolabe / gearwork |

### Step 13 · glyph libraries

```
[STYLE]  ...except: render as a GRID of separate small icons on one canvas,
evenly spaced, each isolated, engraved brass on a dark recessed disc.
A grid of simple UI glyphs, one per cell, no text:
plus, minus, up chevron, down chevron, left chevron, right chevron, check mark,
cross, empty checkbox, filled checkbox, radio empty, radio filled, lock, unlock,
sort ascending, sort descending.
```

Repeat for `icon_36` (HUD actions: sword, shield, spellbook, hexagram, hand,
scroll, floppy/save, chat, map, settings) and `status_24` (§6).

---

## 6. Character detail side sheet

### Layout

A slide-out pane hinged to the right edge of the Character Info panel. Same height
so it reads as one object.

```
Character panel   270×376  at (m_x, m_y)
Pull tab           18×64   at (m_x + 270, m_y + 150)     ← the call to action
Side sheet        220×376  at (m_x + 270, m_y)           ← slides out from the tab
```

Side sheet internals (offsets relative to the sheet's own origin):

| Element | x | y | w | h |
|---|---:|---:|---:|---:|
| Title plate | 10 | 8 | 200 | 26 |
| Section header + flourishes | 14 | 42 | 192 | 14 |
| Stat row label | 14 | — | 100 | 15 |
| Stat row `field_9slice` | 122 | — | 84 | 19 |
| Stat row pitch | | | | 22 |
| Buff grid cell `status_24` | 14 + 26·col | — | 24 | 24 |
| Buff grid | 6 cols × 3 rows, 2px gutter | | 156 | 78 |

Four sections at 22px row pitch, headers at 20px:

- **Offense** — Attack Ratio, Damage, Crit Chance
- **Defense** — Defense Ratio, Physical Absorb, Magic Resist, Magic Absorb
- **Recovery** — HP / SP / MP Recovery, Poison Resist
- **Active Effects** — the 6×3 status icon grid

### Art components needed

Nothing new except `pull_tab` (step 10) and the `status_24` glyph library. The sheet
body is `panel_9slice` at 220×376, rows are `field_9slice` at 84×19, header uses
`title_3slice` at 200×26 and `divider_flourish`.

### ⚠ This needs protocol work — the client does not have the data

Verified against the current tree:

- `Client/Player.h` carries only `m_str/dex/int/vit/mag/charisma`, the four
  `m_angelic_*`, and `m_gear_attribute[6]` — the six primary attributes, nothing else.
- Defense ratio and friends live server-side on `Server/Client.h`
  (`m_defense_ratio:138`, `m_add_defense_ratio` / `m_add_attack_ratio` /
  `m_add_poison_resistance:338`). None of it is transmitted.
- `Shared/Item/ModifierIds.h` defines the full vocabulary the pane would show —
  `defense_ratio:65`, `physical_absorb:66`, `magic_resist:67`, `magic_absorb:68`,
  `hp/sp/mp_recovery:69-71`, `poison_resist:72`, `crit_chance:77`, `move_speed:101` —
  but these are item modifier IDs, not player state the client receives.
- Buffs: the client knows only `m_is_poisoned`, `m_is_confusion`, `m_paralyze`
  (`Client/Player.h:143`). There is no active-effect list with durations.

The clean extension point is `PacketNotifyGearStats`, already handled at
`Client/NetworkMessages_Stats.cpp:190`. It carries `add_attribute[]` today; add a
derived-combat block beside it (or a sibling `PacketNotifyCombatStats`).

Work required, in order:

1. Server: aggregate the derived stats where `m_add_*` are already computed.
2. Shared: extend the packet struct.
3. Client: parse into new `Player` fields.
4. **Compatibility version bump** — the client needs a code change to handle it,
   which is the bump test in `CLAUDE.md`.
5. Buffs/durations: needs a server-side active-effect table first. Larger than the
   rest; worth splitting into its own issue and shipping the pane without the grid.

Recommendation: ship the art and the four stat sections first, leave the Active
Effects grid stubbed, and take the effect table as a follow-up.

---

## 7. Assembler

`Scripts/build_dialog_sheet.py` — chroma-key, slice, stamp per a JSON layout,
repack into `game_dialogs.pak` **with the existing frame rects preserved**.

The frame rects in the pak are already correct and must not change; the assembler
renders into each rect rather than repacking the atlas. The layout JSON doubles as
the runtime layout file if the hardcoded offsets in `DialogBox_*.cpp` are ever
lifted out of C++ — the same numbers drive both.

Loop becomes: drop in new component PNGs → run the script → launch.
