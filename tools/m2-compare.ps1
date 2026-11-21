# REIM M2-G1: same save/spawn/input in Preview and UE, then compare the
# exported Runtime state (player, chunks, sections, block samples).
#
#   powershell -ExecutionPolicy Bypass -File tools\m2-compare.ps1 `
#     -World D:\Minecraft\.minecraft\saves\REIM-M1-26.3 `
#     -Generated D:\EngineeringDocuments\REIM-Launcher\generated\26.3
#
# UE runs only from the Converter-produced NativeBuild. Nothing is reported as
# passing unless both processes actually ran and produced reports.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$World,
    [Parameter(Mandatory = $true)][string]$Generated,
    [string]$NativeBuild = "",
    [int]$Tick = 200,
    [int]$RenderDistance = 6,
    [int]$AutoWalkTicks = 0,
    [float[]]$Spawn = @(),
    [float]$SpawnYaw = 0.0,
    [float]$SpawnPitch = 0.0,
    [switch]$SkipPreview,
    [switch]$SkipUe
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
$Bin = Join-Path $Repo "build\m2\bin"
$Reports = Join-Path $Repo "build\m2\reports"
$Work = Join-Path $Repo "build\m2\compare"
New-Item -ItemType Directory -Force -Path $Reports, $Work | Out-Null

if (-not $NativeBuild) { $NativeBuild = Join-Path $Repo "build\native-build\26.3" }
$UeExe = Join-Path $NativeBuild "REIMRuntime\REIMRuntime.exe"
$NativeVersion = Join-Path $NativeBuild "NativeVersions\26.3"
$PreviewExe = Join-Path $Bin "REIMPreview.exe"
$PreviewReport = Join-Path $Work "preview-state.json"
$UeReport = Join-Path $Work "ue-state.json"
$Instance = Join-Path $Work "instance.json"
# Never compare a stale report from an earlier run.
Remove-Item -Force $PreviewReport, $UeReport -ErrorAction SilentlyContinue

foreach ($path in @($World, $Generated)) {
    if (-not (Test-Path $path)) { throw "not found: $path" }
}

$hasSpawn = $Spawn.Count -eq 3
$spawnArgs = @()
if ($hasSpawn) { $spawnArgs = @("--spawn", "$($Spawn[0])", "$($Spawn[1])", "$($Spawn[2])",
                                "--spawn-yaw", "$SpawnYaw", "--spawn-pitch", "$SpawnPitch") }

function Invoke-Step([string]$Name, [scriptblock]$Body) {
    Write-Host "==> $Name"
    & $Body
    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

if (-not $SkipPreview) {
    if (-not (Test-Path $PreviewExe)) { throw "Preview not built: $PreviewExe" }
    $previewArgs = @("--world", $World, "--generated", $Generated,
                     "--render-distance", "$RenderDistance", "--hidden",
                     "--exit-tick", "$Tick", "--report", $PreviewReport) + $spawnArgs
    if ($AutoWalkTicks -gt 0) { $previewArgs += @("--auto-walk-ticks", "$AutoWalkTicks") }
    Invoke-Step "Preview (hidden, exit tick $Tick)" { & $PreviewExe @previewArgs }
}

if (-not $SkipUe) {
    if (-not (Test-Path $UeExe)) {
        throw "UE Runtime not found: $UeExe (run tools\m2-package.ps1 first)"
    }
    if (-not (Test-Path (Join-Path $NativeVersion "native_version.json"))) {
        throw "NativeVersion not found: $NativeVersion"
    }
    $inst = [ordered]@{
        world_path          = $World
        generated_path      = $Generated
        native_version_path = $NativeVersion
        render_distance     = $RenderDistance
        mouse_sensitivity   = 0.15
        invert_y            = $false
        render_level        = 0
        auto_start          = $true
    }
    if ($hasSpawn) {
        $inst.spawn_x = $Spawn[0]
        $inst.spawn_y = $Spawn[1]
        $inst.spawn_z = $Spawn[2]
        $inst.spawn_yaw = $SpawnYaw
        $inst.spawn_pitch = $SpawnPitch
    }
    $inst | ConvertTo-Json | Set-Content -Encoding UTF8 $Instance

    $ueArgs = @("-reim-instance=$Instance", "-reim-state-report=$UeReport",
                "-reim-state-tick=$Tick", "-reim-exit-tick=$Tick",
                "-windowed", "-ResX=1280", "-ResY=720", "-log", "-unattended",
                "-reim-nodialog")
    if ($AutoWalkTicks -gt 0) { $ueArgs += "-reim-auto-walk-ticks=$AutoWalkTicks" }
    # UE is a GUI-subsystem executable: callers must wait explicitly, otherwise
    # the report is read while the process is still running.
    Invoke-Step "UE from NativeBuild (exit tick $Tick)" {
        $process = Start-Process -FilePath $UeExe -ArgumentList $ueArgs -PassThru -Wait
        if ($process.ExitCode -ne 0) { throw "UE exited with code $($process.ExitCode)" }
    }
    if (-not (Test-Path $UeReport)) { throw "UE state report was not produced: $UeReport" }
}

if (-not (Test-Path $PreviewReport) -or -not (Test-Path $UeReport)) {
    throw "both reports are required for comparison (Preview=$PreviewReport UE=$UeReport)"
}

$preview = Get-Content -Raw -LiteralPath $PreviewReport | ConvertFrom-Json
$ue = Get-Content -Raw -LiteralPath $UeReport | ConvertFrom-Json
$pStatus = $preview.status
$uStatus = $ue.status

function Num($value) { return [double]$value }
function PlayerField($obj, $name) { return [double]$obj.player.$name }

$tolerance = if ($AutoWalkTicks -gt 0) { 1.0 } else { 0.001 }
$diffs = New-Object System.Collections.Generic.List[string]

if ([int]$preview.tick -ne [int]$ue.tick) {
    $diffs.Add("tick preview=$($preview.tick) ue=$($ue.tick)")
}
if ([int]$preview.loaded_chunks -ne [int]$ue.loaded_chunks) {
    $diffs.Add("loaded_chunks preview=$($preview.loaded_chunks) ue=$($ue.loaded_chunks)")
}
if ([int]$preview.mesh_sections -ne [int]$ue.mesh_sections) {
    $diffs.Add("mesh_sections preview=$($preview.mesh_sections) ue=$($ue.mesh_sections)")
}
foreach ($axis in @("x", "y", "z")) {
    $delta = [math]::Abs((PlayerField $preview $axis) - (PlayerField $ue $axis))
    if ($delta -gt $tolerance) {
        $diffs.Add("player.$axis delta=$delta preview=$(PlayerField $preview $axis) ue=$(PlayerField $ue $axis)")
    }
}
foreach ($axis in @("yaw", "pitch")) {
    $delta = [math]::Abs((PlayerField $preview $axis) - (PlayerField $ue $axis))
    if ($delta -gt $tolerance) {
        $diffs.Add("player.$axis delta=$delta")
    }
}

$sampleDiffs = 0
$pSamples = @($pStatus.samples)
$uSamples = @($uStatus.samples)
if ($pSamples.Count -ne $uSamples.Count) {
    $diffs.Add("sample_count preview=$($pSamples.Count) ue=$($uSamples.Count)")
} else {
    for ($i = 0; $i -lt $pSamples.Count; $i++) {
        $a = $pSamples[$i]
        $b = $uSamples[$i]
        if ($a.x -ne $b.x -or $a.y -ne $b.y -or $a.z -ne $b.z -or $a.block -ne $b.block) {
            $sampleDiffs++
            if ($sampleDiffs -le 8) {
                $diffs.Add("sample[$i] preview=($($a.x),$($a.y),$($a.z)):$($a.block) ue=($($b.x),$($b.y),$($b.z)):$($b.block)")
            }
        }
    }
}

$result = if ($diffs.Count -eq 0) { "match" } else { "mismatch" }
$report = [ordered]@{
    schema             = "reim.m2.g1_dual_backend"
    format             = 1
    world              = $World
    generated          = $Generated
    native_build       = $NativeBuild
    tick_requested     = $Tick
    auto_walk_ticks    = $AutoWalkTicks
    tolerance          = $tolerance
    preview            = @{ report = $PreviewReport; tick = $preview.tick;
                            player = $preview.player; loaded_chunks = $preview.loaded_chunks;
                            mesh_sections = $preview.mesh_sections }
    ue                 = @{ report = $UeReport; tick = $ue.tick;
                            player = $ue.player; loaded_chunks = $ue.loaded_chunks;
                            mesh_sections = $ue.mesh_sections;
                            native_version = $ue.native_version; adapter = $ue.adapter }
    sample_count       = $pSamples.Count
    sample_differences = $sampleDiffs
    differences        = @($diffs)
    result             = $result
}
$reportPath = Join-Path $Reports "g1-dual-backend.json"
$report | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $reportPath
Write-Host "G1 report: $reportPath"

if ($diffs.Count -ne 0) {
    $diffs | ForEach-Object { Write-Host "  DIFF: $_" }
    throw "dual-backend comparison failed ($($diffs.Count) difference(s))"
}
Write-Host "dual-backend state matches at tick $($preview.tick): player, chunks, sections, $($pSamples.Count) block samples"
