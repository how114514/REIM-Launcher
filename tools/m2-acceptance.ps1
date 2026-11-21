# REIM M2-H3: one-command M2 acceptance run.
#
#   powershell -ExecutionPolicy Bypass -File tools\m2-acceptance.ps1 `
#     -World D:\Minecraft\.minecraft\saves\REIM-M1-26.3 `
#     -Generated D:\EngineeringDocuments\REIM-Launcher\generated\26.3 `
#     -UeSeconds 60
#
# Runs the existing checks (Catch2/headless/Preview/UE smoke, UE automation,
# E9 toolchain cases, error matrix), then the G2 60 s Shipping run, JVM check,
# dependency audit and fixed-camera screenshot baseline. Writes
# build/m2/reports/m2-acceptance.json. Items that were not executed are marked
# "not_run" - never "pass".
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$World,
    [Parameter(Mandatory = $true)][string]$Generated,
    [string]$ShippingNativeBuild = "",
    [string]$DevNativeBuild = "",
    [int]$UeSeconds = 60,
    [string]$CleanMachineEvidence = "",
    [switch]$SkipSmoke,
    [switch]$SkipAutomation,
    [switch]$SkipE9,
    [switch]$SkipErrors,
    [switch]$SkipPerf
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
$Bin = Join-Path $Repo "build\m2\bin"
$Reports = Join-Path $Repo "build\m2\reports"
$Work = Join-Path $Repo "build\m2\acceptance"
New-Item -ItemType Directory -Force -Path $Reports, $Work | Out-Null

if (-not $ShippingNativeBuild) { $ShippingNativeBuild = Join-Path $Repo "build\native-build\26.3" }
if (-not $DevNativeBuild) { $DevNativeBuild = Join-Path $Repo "build\native-build-dev\26.3" }
$ShipExe = Join-Path $ShippingNativeBuild "REIMRuntime\REIMRuntime.exe"
$ShipBin = Join-Path $ShippingNativeBuild "REIMRuntime\REIM\Binaries\Win64"
$ShipRealExe = Get-ChildItem $ShipBin -Filter "REIMRuntime-Win64-*.exe" -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $ShipRealExe) { $ShipRealExe = $ShipExe }
$ShipNv = Join-Path $ShippingNativeBuild "NativeVersions\26.3"
$DevExe = Join-Path $DevNativeBuild "REIMRuntime\REIMRuntime.exe"

$checks = New-Object System.Collections.Generic.List[object]
function Add-Check([string]$Id, [string]$Status, $Details) {
    $checks.Add([ordered]@{ id = $Id; status = $Status; details = $Details })
    Write-Host ("[{0}] {1}" -f $Status.ToUpperInvariant(), $Id)
}

function Write-Utf8([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText($Path, $Text,
        (New-Object System.Text.UTF8Encoding($false)))
}

# ---------------------------------------------------------------------------
# 0. Artifact and environment facts
# ---------------------------------------------------------------------------
$missing = @()
foreach ($path in @($World, $Generated, $ShipExe, $ShipNv, $DevExe,
                    (Join-Path $Bin "mc_26.3.dll"), (Join-Path $Bin "REIMPreview.exe"))) {
    if (-not (Test-Path $path)) { $missing += $path }
}
if ($missing.Count -ne 0) {
    Add-Check "artifacts" "fail" @{ missing = $missing }
    $checks | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 `
        (Join-Path $Reports "m2-acceptance.json")
    throw "required artifacts missing: $($missing -join ', ')"
}
$envFacts = [ordered]@{
    os            = (Get-CimInstance Win32_OperatingSystem).Caption
    gpu           = (Get-CimInstance Win32_VideoController | Select-Object -First 1).Name
    driver        = (Get-CimInstance Win32_VideoController | Select-Object -First 1).DriverVersion
    ue_root       = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { "D:\Program Files\Epic Games\UE_5.8" })
    shipping_build = $ShippingNativeBuild
    development_build = $DevNativeBuild
    world         = $World
    generated     = $Generated
}
Add-Check "artifacts" "pass" $envFacts

# ---------------------------------------------------------------------------
# 1. Existing suites
# ---------------------------------------------------------------------------
if (-not $SkipSmoke) {
    Write-Host "==> Catch2 + headless + Preview + Shipping UE smoke"
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "m2-smoke.ps1") `
        -World $World -Generated $Generated -NativeBuild $ShippingNativeBuild `
        -UeSeconds 10
    if ($LASTEXITCODE -ne 0) { Add-Check "smoke" "fail" @{ exit = $LASTEXITCODE } }
    else { Add-Check "smoke" "pass" @{ report = (Join-Path $Reports "smoke-ue-r0.png") } }
}

if (-not $SkipAutomation) {
    Write-Host "==> UE automation (Development NativeBuild)"
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "m2-automation.ps1") `
        -World $World -Generated $Generated -NativeBuild $DevNativeBuild
    if ($LASTEXITCODE -ne 0) { Add-Check "ue_automation" "fail" @{ exit = $LASTEXITCODE } }
    else { Add-Check "ue_automation" "pass" @{ report = (Join-Path $Reports "m2-automation.json") } }
}

if (-not $SkipE9) {
    Write-Host "==> E9 toolchain accept/reject (Shipping NativeBuild)"
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "m2-e9.ps1") `
        -World $World -Generated $Generated -NativeBuild $ShippingNativeBuild
    if ($LASTEXITCODE -ne 0) { Add-Check "e9_toolchain" "fail" @{ exit = $LASTEXITCODE } }
    else { Add-Check "e9_toolchain" "pass" @{ report = (Join-Path $Reports "m2-e9.json") } }
}

if (-not $SkipErrors) {
    Write-Host "==> Error matrix (Shipping NativeBuild)"
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "m2-errors.ps1") `
        -World $World -Generated $Generated -NativeBuild $ShippingNativeBuild
    if ($LASTEXITCODE -ne 0) { Add-Check "error_matrix" "fail" @{ exit = $LASTEXITCODE } }
    else { Add-Check "error_matrix" "pass" @{ report = (Join-Path $Reports "m2-errors.json") } }
}

# ---------------------------------------------------------------------------
# 2. G2 60 s Shipping run + G3 budgets + G4 JVM check
# ---------------------------------------------------------------------------
if (-not $SkipPerf) {
    $instance = Join-Path $Work "instance-g2.json"
    $perf = Join-Path $Reports "m2-perf-shipping.json"
    $shot = Join-Path $Reports "m2-shipping-g2.png"
    $hostLogDir = Join-Path $Work "g2"
    @{
        world_path          = $World
        generated_path      = $Generated
        native_version_path = $ShipNv
        render_distance     = 6
        mouse_sensitivity   = 0.15
        invert_y            = $false
        render_level        = 0
        auto_start          = $true
        log_dir             = (Join-Path $hostLogDir "logs")
    } | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $instance
    Remove-Item -Force $perf, $shot -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $hostLogDir -ErrorAction SilentlyContinue
    $hostLog = Join-Path $hostLogDir "logs\REIM-host.log"

    $args = @("-reim-instance=$instance", "-reim-perf-report=$perf",
              "-reim-shot=$shot", "-reim-shot-delay=5",
              "-reim-exit-after=$UeSeconds", "-windowed", "-ResX=1280", "-ResY=720",
              "-unattended", "-reim-nodialog")
    $started = Get-Date
    $process = Start-Process -FilePath $ShipExe -ArgumentList $args -PassThru
    Start-Sleep -Seconds 10
    $jvm = @{
        java_processes = @(Get-Process java, javaw -ErrorAction SilentlyContinue |
            Select-Object Id, ProcessName, Path)
        classpath_cmdlines = @(Get-CimInstance Win32_Process |
            Where-Object { $_.CommandLine -match "classpath|javaw|-Xmx" } |
            Select-Object ProcessId, Name, CommandLine)
        ue_cmdline = (Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)" |
            Select-Object -ExpandProperty CommandLine)
    }
    $process.WaitForExit([Math]::Max(120, $UeSeconds * 3) * 1000) | Out-Null
    $wall = ((Get-Date) - $started).TotalSeconds
    $g2 = [ordered]@{
        exit_code = $process.ExitCode
        wall_s    = [Math]::Round($wall, 1)
        perf      = $(if (Test-Path $perf) {
            Get-Content -Raw -LiteralPath $perf | ConvertFrom-Json } else { $null })
        screenshot = $(if (Test-Path $shot) { $shot } else { $null })
        host_log  = $(if (Test-Path $hostLog) { $hostLog } else { $null })
    }
    $ok = $process.ExitCode -eq 0 -and $null -ne $g2.perf -and
          $g2.perf.ready_at_s -ge 0 -and -not $g2.perf.fatal -and
          $g2.perf.tps.min -ge 19.5 -and $g2.perf.tps.max -le 20.5 -and
          $g2.perf.frame_blobs.busy_frames -eq 0 -and
          $g2.perf.bridge_fetch_ms.p99 -le 2.0 -and
          $g2.perf.rt_upload.steady_avg_ms -le 4.0 -and
          $g2.perf.draw_calls_max -gt 0
    Add-Check "g2_60s_shipping" $(if ($ok) { "pass" } else { "fail" }) $g2

    $jvmOk = $jvm.java_processes.Count -eq 0 -and $jvm.classpath_cmdlines.Count -eq 0
    Add-Check "g4_no_jvm" $(if ($jvmOk) { "pass" } else { "fail" }) $jvm
}

# ---------------------------------------------------------------------------
# 3. F6 dependency audit
# ---------------------------------------------------------------------------
$dumpbin = Get-ChildItem "C:\Program Files (x86)\Microsoft Visual Studio\2022" `
    -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "Hostx64\\x64" } |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $dumpbin) {
    Add-Check "f6_dependency_audit" "not_run" @{ reason = "dumpbin.exe not found" }
} else {
    $exeDeps = & $dumpbin /nologo /dependents $ShipRealExe 2>&1
    $bootDeps = & $dumpbin /nologo /dependents $ShipExe 2>&1
    $dllDeps = & $dumpbin /nologo /dependents (Join-Path $ShipNv "mc_26.3.dll") 2>&1
    $appLocal = @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll",
                  "msvcp140_1.dll", "msvcp140_2.dll", "ucrtbase.dll")
    $present = @($appLocal | Where-Object {
        Test-Path (Join-Path (Split-Path $ShipRealExe) $_) })
    $forbidden = @("jvm.dll", "java.dll", "UnrealEditor")
    $text = ($exeDeps + $bootDeps + $dllDeps) -join "`n"
    $bad = @($forbidden | Where-Object { $text -match [regex]::Escape($_) })
    $ok = $present.Count -eq $appLocal.Count -and $bad.Count -eq 0
    Add-Check "f6_dependency_audit" $(if ($ok) { "pass" } else { "fail" }) ([ordered]@{
        dumpbin     = $dumpbin
        exe         = $ShipRealExe
        dll         = (Join-Path $ShipNv "mc_26.3.dll")
        app_local   = $present
        missing     = @($appLocal | Where-Object { $present -notcontains $_ })
        forbidden   = $bad
        exe_deps    = @($exeDeps | Select-String -Pattern "\.dll" | ForEach-Object { $_.Line.Trim() })
        bootstrap_deps = @($bootDeps | Select-String -Pattern "\.dll" | ForEach-Object { $_.Line.Trim() })
        dll_deps    = @($dllDeps | Select-String -Pattern "\.dll" | ForEach-Object { $_.Line.Trim() })
    })
}

# ---------------------------------------------------------------------------
# 4. F2 package content audit
# ---------------------------------------------------------------------------
$package = Join-Path $ShippingNativeBuild "REIMRuntime"
$forbiddenFiles = @(Get-ChildItem $package -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match "^(mc_26\.3\.dll|manifest\.json|atlas\.png)$" -or
                   $_.Extension -eq ".pdb" } | Select-Object -ExpandProperty FullName)
$textHits = @(Get-ChildItem $package -Recurse -File -Include *.json,*.ini,*.txt |
    Select-String -SimpleMatch "EngineeringDocuments" -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty Path -Unique)
$size = (Get-ChildItem $package -Recurse -File | Measure-Object Length -Sum).Sum
$pkgOk = $forbiddenFiles.Count -eq 0 -and $textHits.Count -eq 0
Add-Check "f2_package_contents" $(if ($pkgOk) { "pass" } else { "fail" }) ([ordered]@{
    package_dir    = $package
    size_bytes     = $size
    forbidden      = $forbiddenFiles
    dev_path_hits  = $textHits
})

# ---------------------------------------------------------------------------
# 5. G7 fixed-camera screenshot baseline (two runs)
# ---------------------------------------------------------------------------
$shots = @()
$diff = $null
foreach ($n in @(1, 2)) {
    $instance = Join-Path $Work "instance-shot.json"
    $shot = Join-Path $Reports "m2-r0-fixed-$n.png"
    Remove-Item -Force $shot -ErrorAction SilentlyContinue
    @{
        world_path          = $World
        generated_path      = $Generated
        native_version_path = $ShipNv
        render_distance     = 6
        mouse_sensitivity   = 0.15
        invert_y            = $false
        render_level        = 0
        auto_start          = $true
        spawn_x             = 16.0
        spawn_y             = 65.0
        spawn_z             = 80.0
        spawn_yaw           = 45.0
        spawn_pitch         = 10.0
    } | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $instance
    $args = @("-reim-instance=$instance", "-reim-shot=$shot", "-reim-shot-delay=4",
              "-reim-exit-after=8", "-reim-nohud", "-windowed", "-ResX=1280",
              "-ResY=720", "-unattended", "-reim-nodialog")
    $p = Start-Process -FilePath $ShipExe -ArgumentList $args -PassThru -Wait
    for ($i = 0; $i -lt 50 -and -not (Test-Path $shot); $i++) { Start-Sleep -Milliseconds 100 }
    $shots += [ordered]@{ file = $shot; exit = $p.ExitCode; exists = (Test-Path $shot) }
}
if ($shots[0].exists -and $shots[1].exists) {
    Add-Type -AssemblyName System.Drawing
    $a = New-Object System.Drawing.Bitmap($shots[0].file)
    $b = New-Object System.Drawing.Bitmap($shots[1].file)
    $max = 0; $sum = 0.0; $count = 0
    for ($y = 0; $y -lt $a.Height; $y += 8) {
        for ($x = 0; $x -lt $a.Width; $x += 8) {
            $ca = $a.GetPixel($x, $y); $cb = $b.GetPixel($x, $y)
            $d = [Math]::Max([Math]::Abs($ca.R - $cb.R),
                 [Math]::Max([Math]::Abs($ca.G - $cb.G), [Math]::Abs($ca.B - $cb.B)))
            if ($d -gt $max) { $max = $d }
            $sum += $d; $count++
        }
    }
    $a.Dispose(); $b.Dispose()
    $diff = [ordered]@{
        max_channel_diff = $max
        mean_channel_diff = [Math]::Round($sum / [Math]::Max($count, 1), 3)
        sampled_pixels = $count
    }
}
$shotOk = $shots.Count -eq 2 -and $shots[0].exists -and $shots[1].exists -and
          $shots[0].exit -eq 0 -and $shots[1].exit -eq 0
Add-Check "g7_screenshot_baseline" $(if ($shotOk) { "pass" } else { "fail" }) ([ordered]@{
    shots = $shots
    diff  = $diff
})

# ---------------------------------------------------------------------------
# 5b. S4 visual assertions on the fixed-camera Shipping screenshot
# ---------------------------------------------------------------------------
if ($shots[0].exists) {
    $visualScript = Join-Path $Repo "tools\m2-visual.ps1"
    $visualOut = Join-Path $Reports "m2-visual.json"
    Remove-Item -Force $visualOut -ErrorAction SilentlyContinue
    & powershell -ExecutionPolicy Bypass -File $visualScript -UeImage $shots[0].file `
        -OutDir $Reports | Out-Null
    $visualExit = $LASTEXITCODE
    $visualDetail = if (Test-Path $visualOut) {
        Get-Content $visualOut -Raw | ConvertFrom-Json
    } else { $null }
    Add-Check "s4_visual_colors" $(if ($visualExit -eq 0) { "pass" } else { "fail" }) ([ordered]@{
        screenshot = $shots[0].file
        report     = $visualOut
        detail     = $visualDetail
    })
} else {
    Add-Check "s4_visual_colors" "not_run" @{ reason = "fixed-camera screenshot missing" }
}

# ---------------------------------------------------------------------------
# 6. G6 "UE has no game logic" source review
# ---------------------------------------------------------------------------
$sourceRoots = @((Join-Path $Repo "runtime\host\Source"),
                 (Join-Path $Repo "runtime\host\Plugins"))
$sourceFiles = @(Get-ChildItem $sourceRoots -Recurse -File -Include *.cpp,*.h,*.cs |
    Where-Object { $_.FullName -notmatch "\\Intermediate\\|\\Binaries\\" })
$forbiddenPatterns = @("level.dat", "NBT", "region/", "blocks.json", "textures.json",
                       "atlas.png", "mesher", "mesh generation", "terrain generator",
                       "minecraft:block")
$logicHits = @()
foreach ($pattern in $forbiddenPatterns) {
    $logicHits += @($sourceFiles | Select-String -SimpleMatch $pattern -ErrorAction SilentlyContinue |
        ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" })
}
# Only REIMBridge may load the DLL; the automation error test is test-only.
$abiFiles = @($sourceFiles | Select-String -SimpleMatch "reim_get_api" |
    ForEach-Object { $_.Path } | Sort-Object -Unique)
$abiViolations = @($abiFiles | Where-Object {
    $_ -notmatch "REIMBridge" -and $_ -notmatch "ReimAutomationTests\.cpp" })
$g6Ok = $logicHits.Count -eq 0 -and $abiViolations.Count -eq 0
$g6 = [ordered]@{
    files_scanned = $sourceFiles.Count
    forbidden_hits = $logicHits
    abi_callers = $abiFiles
    abi_violations = $abiViolations
    modules = @(
        "REIMGame: lifecycle/map/HUD wiring only",
        "REIMBridge: DLL load, NativeVersion/Adapter/generated hash validation, frame packets, input forwarding",
        "REIMRenderer: GPU section table and draw only, consumes Contract",
        "REIMUI: debug HUD/pause menu only",
        "REIMInput: key/mouse -> ReimInputEvent only"
    )
}
$g6 | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 `
    (Join-Path $Reports "m2-no-game-logic.json")
Add-Check "g6_no_game_logic" $(if ($g6Ok) { "pass" } else { "fail" }) $g6

# ---------------------------------------------------------------------------
# 7. F5 clean machine: not run unless evidence is provided
# ---------------------------------------------------------------------------
if ($CleanMachineEvidence -and (Test-Path $CleanMachineEvidence)) {
    Add-Check "f5_clean_machine" "pass" @{ evidence = $CleanMachineEvidence }
} else {
    Add-Check "f5_clean_machine" "not_run" @{
        reason = "no clean Windows machine/evidence provided to this session"
    }
}

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
$failed = @($checks | Where-Object { $_.status -eq "fail" })
$notRun = @($checks | Where-Object { $_.status -eq "not_run" })
$report = [ordered]@{
    schema  = "reim.m2.acceptance"
    format  = 1
    generated_at = (Get-Date).ToString("o")
    environment = $envFacts
    checks  = $checks
    failed  = $failed.Count
    not_run = @($notRun | ForEach-Object { $_.id })
    result  = $(if ($failed.Count -eq 0) { "executed checks passed" } else { "failures present" })
}
$reportPath = Join-Path $Reports "m2-acceptance.json"
$report | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 $reportPath
Write-Host "acceptance report: $reportPath"
Write-Host ("checks: {0} pass, {1} fail, {2} not_run" -f
    @($checks | Where-Object { $_.status -eq "pass" }).Count, $failed.Count, $notRun.Count)
if ($failed.Count -ne 0) {
    throw "acceptance failed: $($failed.id -join ', ')"
}
