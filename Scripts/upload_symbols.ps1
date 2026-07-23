# Uploads native debug symbols (PDBs + binaries, with source context) to Sentry
# so crash stack traces symbolicate to function names and lines.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File Scripts\upload_symbols.ps1
#   powershell -ExecutionPolicy Bypass -File Scripts\upload_symbols.ps1 -Org myorg -Project myproject
#
# Auth (one-time setup):
#   1. Create an auth token: sentry.io -> Settings -> Auth Tokens (scopes:
#      project:read, project:releases, org:read) — or use the pre-scoped token
#      from Project Settings -> Debug Files.
#   2. Store it either as the SENTRY_AUTH_TOKEN environment variable, or run
#      Scripts\tools\sentry-cli.exe login   (writes %USERPROFILE%\.sentryclirc)
#   Org/project slugs (from the sentry.io URL) can likewise go in
#   .sentryclirc under [defaults], or be passed as parameters here.
#
# Run after building and BEFORE deploying a release, so the uploaded symbols
# match the shipped binaries (Sentry matches by PDB debug ID, so stale extra
# uploads are harmless — missing ones mean raw-address stacks).

param(
	[string]$Org = $env:SENTRY_ORG,
	[string]$Project = $env:SENTRY_PROJECT
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path $PSScriptRoot -Parent

# Locate sentry-cli: PATH first, then Scripts\tools (auto-downloaded)
$cli = $null
$pathCli = Get-Command sentry-cli -ErrorAction SilentlyContinue
if ($pathCli) { $cli = $pathCli.Source }
else {
	$cli = Join-Path $PSScriptRoot "tools\sentry-cli.exe"
	if (-not (Test-Path $cli)) {
		Write-Host "Downloading sentry-cli..."
		New-Item -ItemType Directory -Force (Split-Path $cli) | Out-Null
		Invoke-WebRequest -Uri "https://github.com/getsentry/sentry-cli/releases/latest/download/sentry-cli-Windows-x86_64.exe" -OutFile $cli -UseBasicParsing
	}
}

# Auth pre-check with actionable guidance
if (-not $env:SENTRY_AUTH_TOKEN -and -not (Test-Path "$env:USERPROFILE\.sentryclirc")) {
	Write-Host "ERROR: no Sentry auth configured." -ForegroundColor Red
	Write-Host "  Create an auth token at sentry.io (Settings -> Auth Tokens), then either:"
	Write-Host "    setx SENTRY_AUTH_TOKEN <token>     (new shells only)"
	Write-Host "  or run:"
	Write-Host "    $cli login"
	exit 1
}

# Every build-output dir that exists, plus the bundled sentry.dll symbols
$candidates = @(
	"Sources\Debug",
	"Sources\Release",
	"Sources\ReleaseDbg",
	"Sources\Dependencies\sentry-native\lib_x64"
)
$paths = $candidates | ForEach-Object { Join-Path $repoRoot $_ } | Where-Object { Test-Path $_ }
if (-not $paths) { throw "No build output directories found - build first." }

$cliArgs = @("debug-files", "upload", "--include-sources")
if ($Org) { $cliArgs += @("--org", $Org) }
if ($Project) { $cliArgs += @("--project", $Project) }
$cliArgs += $paths

Write-Host "Uploading symbols from:"
$paths | ForEach-Object { Write-Host "  $_" }
& $cli @cliArgs
if ($LASTEXITCODE -ne 0) { throw "sentry-cli exited with $LASTEXITCODE" }
Write-Host "Symbol upload complete." -ForegroundColor Green
