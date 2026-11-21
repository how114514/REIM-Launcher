# REIM M3 packaging driver (stage 5B / M3-O1).
#
#   powershell -ExecutionPolicy Bypass -File tools\m3-package.ps1 -Config Development
#   powershell -ExecutionPolicy Bypass -File tools\m3-package.ps1 -Config Shipping
#
# Builds, cooks, stages and paks the UE host into build\m3\ue-<config>-package.
# Does not create or modify Minecraft data; generated data and worlds stay
# outside the package (they are user inputs, see M3_PLAN §4.1/§O3).
# Every step is an actual process; a step is reported only when it ran.
[CmdletBinding()]
param(
    [ValidateSet("Development", "Shipping")]
    [string]$Config = "Shipping",
    [string]$UeRoot = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { "D:\Program Files\Epic Games\UE_5.8" }),
    [string]$Target = "REIMRuntime",
    [switch]$SkipAppLocalCopy
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Repo "runtime\host\REIM.uproject"
$BuildRoot = Join-Path $Repo "build\m3"
$ArchiveName = if ($Config -eq "Development") { "ue-dev-package" } else { "ue-shipping-package" }
$Archive = Join-Path $BuildRoot $ArchiveName
$Uat = Join-Path $UeRoot "Engine\Build\BatchFiles\RunUAT.bat"
if (-not (Test-Path $Uat)) {
    throw "UE 5.8.4 RunUAT not found at '$Uat' (set UE_ROOT or -UeRoot)"
}
if (-not (Test-Path $Project)) { throw "project not found: $Project" }

New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null

$EngineAppLocal = Join-Path $UeRoot "Engine\Binaries\ThirdParty\AppLocalDependencies"
if (-not (Test-Path $EngineAppLocal)) {
    throw "app-local dependencies not found: $EngineAppLocal"
}
$AppLocal = Join-Path $BuildRoot "applocal"
if (-not $SkipAppLocalCopy) {
    Remove-Item -Recurse -Force $AppLocal -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $AppLocal | Out-Null
    Copy-Item -Recurse -Force (Join-Path $EngineAppLocal "*") $AppLocal
}

Write-Host "==> BuildCookRun $Config -> $Archive"
Remove-Item -Recurse -Force $Archive -ErrorAction SilentlyContinue
& $Uat BuildCookRun "-project=$Project" "-target=$Target" -noP4 `
    "-platform=Win64" "-clientconfig=$Config" -build -cook -stage -pak `
    -archive "-archivedirectory=$Archive" `
    "-applocaldirectory=$AppLocal" -nodebuginfo -unattended -utf8output
if ($LASTEXITCODE -ne 0) { throw "BuildCookRun $Config failed with exit code $LASTEXITCODE" }

$Exe = Join-Path $Archive "Windows\$Target.exe"
if (-not (Test-Path $Exe)) { throw "packaging did not produce $Exe" }
Write-Host "m3-package: $Config package at $Archive"
