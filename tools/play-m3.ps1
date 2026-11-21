# REIM M3 playable entry (UE Host).
#
#   powershell -ExecutionPolicy Bypass -File tools\play-m3.ps1
#   powershell -ExecutionPolicy Bypass -File tools\play-m3.ps1 -Config Development
#   powershell -ExecutionPolicy Bypass -File tools\play-m3.ps1 -Seconds 5   # smoke
#
# Starts the UE Zen server if 8558 is not listening, writes a play instance
# (world copy + generated data + packaged NativeVersion), and launches the UE
# Host. This is the player-facing entry: the Runtime owns the game; UE only
# consumes Contract v3. R0 currently renders the world; UI/entity/audio Host
# integration is not implemented yet.
[CmdletBinding()]
param(
    [string]$World = "D:\Minecraft\reim-work\m3\worlds\REIM-M3-26.3-work",
    [string]$Generated = "",
    [ValidateSet("Shipping", "Development")]
    [string]$Config = "Shipping",
    [int]$Seconds = 0
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
if (-not $Generated) { $Generated = Join-Path $Repo "generated\26.3" }

$ShippingExe = Join-Path $Repo "build\native-build\26.3\REIMRuntime\REIMRuntime.exe"
$DevExe = Join-Path $Repo "runtime\host\Binaries\Win64\REIMRuntime.exe"
if ($Config -eq "Shipping") {
    $Exe = $ShippingExe
    $NativeVersion = Join-Path $Repo "build\native-build\26.3\NativeVersions\26.3"
} else {
    $Exe = $DevExe
    $NativeVersion = Join-Path $Repo "build\native-version\26.3"
}

foreach ($path in @($Exe, $World, $Generated, $NativeVersion)) {
    if (-not (Test-Path $path)) { throw "not found: $path" }
}

$zenUp = $false
try {
    $zenUp = [bool](Get-NetTCPConnection -LocalPort 8558 -State Listen -ErrorAction SilentlyContinue)
} catch { $zenUp = $false }
if (-not $zenUp) {
    $zen = "D:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\ZenLaunch.exe"
    if (-not (Test-Path $zen)) { throw "ZenLaunch.exe not found: $zen" }
    Write-Host "starting UE Zen server..."
    & $zen -Sponsor=None | Out-Null
    Start-Sleep -Seconds 3
}

$Instance = Join-Path $Repo "build\m3\reports\instance-m3-play.json"
$InstanceJson = [ordered]@{
    world_path          = $World
    generated_path      = $Generated
    native_version_path = $NativeVersion
    minecraft_dir       = "D:\Minecraft\.minecraft"
    render_distance     = 6
    render_level        = 0
    auto_start          = $true
    ui_scale            = 2
    log_dir             = (Join-Path $Repo "build\m3\reports\logs")
} | ConvertTo-Json
[System.IO.File]::WriteAllText($Instance, $InstanceJson,
    (New-Object System.Text.UTF8Encoding($false)))

$HostArgs = @(
    "-game", "-windowed", "-ResX=1600", "-ResY=900",
    "-unattended", "-nosplash",
    "-reim-instance=$Instance"
)
if ($Seconds -gt 0) { $HostArgs += "-reim-exit-after=$Seconds" }

Write-Host "REIM M3 ($Config)"
Write-Host "  runtime : $Exe"
Write-Host "  world   : $World"
Write-Host "  data    : $Generated"
Write-Host "  version : $NativeVersion"
Write-Host "  instance: $Instance"
Write-Host "Note: R0 world rendering only; UE UI/entity/audio Host integration is not implemented."
Start-Process -FilePath $Exe -ArgumentList $HostArgs | Out-Null
