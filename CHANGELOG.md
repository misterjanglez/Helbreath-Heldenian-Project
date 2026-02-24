# Screen architecture migration — hotkeys, managers, and render helpers

Migrated all hotkey handling from CGame to Screen_OnGame as part of the screen decoupling effort. Added on_key_down and on_key_up virtual methods to IGameScreen so screens can handle their own keyboard input. CGame now just gates overlays and routes key events to the active screen. Trivial hotkeys are inlined directly in the switch, complex ones are named methods on Screen_OnGame, and Ctrl+letter combos register through HotkeyManager on screen initialization.

Moved fishing, crafting, and quest managers from CGame to Screen_OnGame. Network message wrappers now route through the active screen with null safety checks instead of accessing CGame members directly.

Moved seven draw helper methods from CGame to Screen_OnGame as part of Phase 5b. PlayerRenderer and NpcRenderer now hold a Screen_OnGame pointer to call draw methods directly, and RenderHelpers::draw_name takes a Screen_OnGame reference instead of CGame.
