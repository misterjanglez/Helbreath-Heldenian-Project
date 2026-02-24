## ========================== Helbreath 3.82 v0.2.11 — Update (Feb 24) ==========================

### Server
- Introduced a new expression-based formula engine — game formulas like weapon attack speed are now configurable in the database instead of being hardcoded
- Formulas are human-readable strings that can reference each other, and the server validates all of them on startup before accepting players
- Redesigned the server console with color-coded log output, grouped startup summaries, and a refreshed ASCII banner

### Bug Fixes
- Fixed armor break not properly reducing endurance for other players
- Restored the missing black fog visual effect when armor breaks
- Fixed an issue where arrows could be sold when they shouldn't be
- Fixed stats dialog alignment being off
- Fixed create food crashing the client
- Fixed player corpses disappearing before players could revive or restart
- Dead player corpses now display hover names when moused over
- Enabled the Pretend-Corpse skill to be activated directly from the skill dialog
- Improved click and drag detection so actions feel more responsive and less prone to misfires
- Widened the skill list click area and centered the skill-use status text on screen
- Fixed 27 guild-related bugs:
  - Guild name display and caching issues
  - Kennedy NPC interaction problems
  - Item transfer edge cases
  - Sell dialog showing double count prefixes
  - Unified maximum guild members to 128

### Visuals
- Added a Christmas holiday tile variant that swaps in automatically when the server enables seasonal weather
- Fixed gold not appearing immediately when dropped — it no longer plays invisible animation frames first
- Added a new animated ping-pong cursor effect when targeting items
- Added scrolling support to the graphics settings tab in the System Menu, with a custom scrollbar and mouse wheel support

### Performance
- Sprites now load their textures on demand when first drawn rather than all at once
- Unused sprite textures automatically release from memory after 30 seconds of inactivity, reducing overall memory footprint

### Platform
- Fixed Linux build compatibility issues with the server and client
