# Unified Build Script for Helbreath 3.82
# Usage: build.ps1 [-Target <Game|Server|All>] [-Config <Debug|Release>] [-UploadSymbols]
#
# Examples:
#   build.ps1                           # Build Game with Debug-SFML x64 (default)
#   build.ps1 -Target Server            # Build Server
#   build.ps1 -Target Game -Config Release
#   build.ps1 -Target All               # Build all projects
#   build.ps1 -Target All -Config Release -UploadSymbols   # Release + Sentry symbol upload
param(
    [ValidateSet("Game", "Server", "All")]
    [string]$Target = "Game",

    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    # Upload PDBs to Sentry after a successful build (Scripts\upload_symbols.ps1).
    # Use when building binaries that will actually ship/deploy.
    [switch]$UploadSymbols
)

$Renderer = "SFML"
$Platform = "x64"

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
# Locate MSBuild via vswhere (works across VS versions/editions); fall back to a known path
$msbuildPath = $null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere)
{
    $msbuildPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
}
if (-not $msbuildPath -or -not (Test-Path $msbuildPath))
{
    $msbuildPath = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
}
if (-not (Test-Path $msbuildPath)) { Write-Error "MSBuild not found. Install Visual Studio or Build Tools with the MSBuild component."; exit 1 }
$solutionPath = Join-Path $scriptDir "Helbreath.sln"

# Configuration string matches the solution configs (Debug|x64, Release|x64)
$configString = $Config
switch ($Target) {
    "Server" { $logFile = Join-Path $scriptDir "build_server.log" }
    "All"    { $logFile = Join-Path $scriptDir "build_all.log" }
    default  { $logFile = Join-Path $scriptDir "build_game.log" }
}

# Delete old log
if (Test-Path $logFile) { Remove-Item $logFile -Force }

Write-Host "============================================"
Write-Host "Building: $Target | Config: $configString | Platform: $Platform"
Write-Host "============================================"

# Generate version header (resolve python from PATH; fall back to py launcher)
$pythonExe = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $pythonExe) { $pythonExe = (Get-Command py -ErrorAction SilentlyContinue).Source }
if (-not $pythonExe) { Write-Error "Python not found on PATH. Install Python 3 and ensure 'python' resolves."; exit 1 }
& $pythonExe "$scriptDir\version_gen.py"

# Determine MSBuild target
$msbuildTarget = if ($Target -eq "All") { "" } else { "/t:$Target" }

# Run MSBuild
$msbuildArgs = @(
    $solutionPath
    "/p:Configuration=$configString"
    "/p:Platform=$Platform"
    "/p:SkipVersionIncrement=true"
    "/nologo"
    "/v:minimal"
    "/consoleloggerparameters:Summary;ErrorsOnly;WarningsOnly"
    "/fileLogger"
    "/fileloggerparameters:LogFile=$logFile;Verbosity=normal;Encoding=UTF-8"
)
if ($msbuildTarget) { $msbuildArgs += $msbuildTarget }

& $msbuildPath @msbuildArgs
$exitCode = $LASTEXITCODE

# Show summary
Write-Host ""
if ($exitCode -eq 0) {
    Write-Host "BUILD SUCCEEDED" -ForegroundColor Green
    if ($UploadSymbols) {
        Write-Host ""
        & powershell -ExecutionPolicy Bypass -File (Join-Path (Split-Path $scriptDir -Parent) "Scripts\upload_symbols.ps1")
        if ($LASTEXITCODE -ne 0) { Write-Host "Symbol upload FAILED (build itself succeeded)" -ForegroundColor Yellow }
    }
} else {
    Write-Host "BUILD FAILED" -ForegroundColor Red
    Write-Host "Check log: $logFile"
    # Show errors from log
    if (Test-Path $logFile) {
        Write-Host ""
        Write-Host "=== Errors ===" -ForegroundColor Red
        Get-Content $logFile | Select-String "error" | Select-Object -First 20
    }
}

exit $exitCode
