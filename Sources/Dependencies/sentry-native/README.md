# sentry-native (bundled)

Prebuilt [sentry-native](https://github.com/getsentry/sentry-native) 0.15.4 for Windows x64
(RelWithDebInfo, shared DLL, crashpad backend). Used by both client and server for crash
reporting and error monitoring via `Sources/Dependencies/Shared/Util/error_monitor.h`.

- `include/sentry.h` — SDK header
- `lib_x64/sentry.lib` — import library (linked by Client.vcxproj / Server.vcxproj)
- `lib_x64/sentry.dll`, `crashpad_handler.exe`, `crashpad_wer.dll` — runtime files; must sit
  next to the executable (copies live in `Binaries/Game/`, `Binaries/Server/`, `Sources/Debug/`)

One RelWithDebInfo build serves Debug and Release project configs — sentry exposes a C ABI,
so no CRT state crosses the DLL boundary.

Rebuild from source: `powershell -ExecutionPolicy Bypass -File Scripts\build_sentry.ps1`

Linux does not use these binaries — the CMake builds fetch and statically link sentry-native
via FetchContent (see `HB_ENABLE_SENTRY` in `Sources/{Server,Client}/CMakeLists.txt`).
