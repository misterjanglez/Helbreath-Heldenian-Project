# Rebuilds the bundled sentry-native SDK (Sources/Dependencies/sentry-native).
# Mirrors Scripts\build_sfml.bat: source is fetched, built out-of-tree, and only
# the artifacts are copied into the repo.
#
# Usage: powershell -ExecutionPolicy Bypass -File Scripts\build_sentry.ps1
#
# Notes:
# - Uses the VS 2026 bundled CMake; the "Visual Studio 17 2022" generator fails
#   to locate an instance on this machine, so the VS18 generator is used. The
#   result is a C-ABI DLL, safe to link from the v143-built client/server.
# - Build happens in a SHORT path (%TEMP%\hbsentry) because crashpad's tlog
#   paths overflow MAX_PATH in deeper directories.
# - One RelWithDebInfo build serves both Debug and Release project configs
#   (C API DLL - no CRT mixing across the boundary).

$ErrorActionPreference = "Stop"

$version = "0.15.4"
$work = Join-Path $env:TEMP "hbsentry"
$repoRoot = Split-Path $PSScriptRoot -Parent
$dest = Join-Path $repoRoot "Sources\Dependencies\sentry-native"

# Locate the newest VS instance's bundled CMake via vswhere (same approach as
# Sources\build.ps1), falling back to the known VS 2026 Community path.
$cmake = $null
$generator = $null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $inst = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json | Select-Object -First 1
    if ($inst) {
        $candidate = Join-Path $inst.installationPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path $candidate) {
            $cmake = $candidate
            $major = [int]$inst.installationVersion.Split('.')[0]
            if ($major -ge 18) { $generator = "Visual Studio 18 2026" }
            elseif ($major -eq 17) { $generator = "Visual Studio 17 2022" }
        }
    }
}
if (-not $cmake) {
    $cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    $generator = "Visual Studio 18 2026"
}
if (-not (Test-Path $cmake)) { throw "CMake not found at $cmake" }
if (-not $generator) { throw "No supported Visual Studio generator found" }

Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $work | Out-Null

Write-Host "Downloading sentry-native $version..."
$zip = Join-Path $work "sentry-native.zip"
Invoke-WebRequest -Uri "https://github.com/getsentry/sentry-native/releases/download/$version/sentry-native.zip" -OutFile $zip -UseBasicParsing
Expand-Archive -Path $zip -DestinationPath (Join-Path $work "src") -Force

Write-Host "Configuring ($generator)..."
& $cmake -S (Join-Path $work "src") -B (Join-Path $work "build") -G $generator -A x64 `
    -DSENTRY_BUILD_SHARED_LIBS=ON -DSENTRY_BACKEND=crashpad `
    -DSENTRY_BUILD_EXAMPLES=OFF -DSENTRY_BUILD_TESTS=OFF
if ($LASTEXITCODE -ne 0) { throw "configure failed" }

Write-Host "Building (RelWithDebInfo)..."
& $cmake --build (Join-Path $work "build") --config RelWithDebInfo --parallel
if ($LASTEXITCODE -ne 0) { throw "build failed" }

& $cmake --install (Join-Path $work "build") --config RelWithDebInfo --prefix (Join-Path $work "install")
if ($LASTEXITCODE -ne 0) { throw "install failed" }

Write-Host "Copying artifacts into $dest..."
$i = Join-Path $work "install"
New-Item -ItemType Directory -Force "$dest\include", "$dest\lib_x64" | Out-Null
Copy-Item "$i\include\sentry.h" "$dest\include\"
Copy-Item "$i\lib\sentry.lib" "$dest\lib_x64\"
Copy-Item "$i\bin\sentry.dll", "$i\bin\sentry.pdb", "$i\bin\crashpad_handler.exe", "$i\bin\crashpad_wer.dll" "$dest\lib_x64\"

# Runtime copies next to the executables
foreach ($rt in @("Binaries\Game", "Binaries\Server", "Sources\Debug")) {
    $rtPath = Join-Path $repoRoot $rt
    if (Test-Path $rtPath) {
        Copy-Item "$i\bin\sentry.dll", "$i\bin\crashpad_handler.exe", "$i\bin\crashpad_wer.dll" $rtPath
    }
}

Write-Host "Done. sentry-native $version bundled."
