# Hotkey system migrated to screen architecture

Migrated all hotkey handling from CGame to Screen_OnGame as part of the screen decoupling effort. Added on_key_down and on_key_up virtual methods to IGameScreen so screens can handle their own keyboard input. CGame now just gates overlays and routes key events to the active screen. Trivial hotkeys are inlined directly in the switch, complex ones are named methods on Screen_OnGame, and Ctrl+letter combos register through HotkeyManager on screen initialization.
