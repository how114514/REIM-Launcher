# REIM M2-D host checks (S2): lifecycle loop, settings persistence, menu.
#
#   powershell -ExecutionPolicy Bypass -File tools\m2-d-host.ps1 `
#     -World D:\Minecraft\.minecraft\saves\REIM-M1-26.3 `
#     -Generated D:\EngineeringDocuments\REIM-Launcher\generated\26.3
#
# Every case actually runs. Nothing is reported as passing unless it did.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$World,
    [Parameter(Mandatory = $true)][string]$Generated,
    [string]$NativeBuild = "",
    [int]$Iterations = 20,
    [int]$ExitSeconds = 3
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
$Reports = Join-Path $Repo "build\m2\reports"
$Work = Join-Path $Repo "build\m2\host-checks"
New-Item -ItemType Directory -Force -Path $Reports, $Work | Out-Null

if (-not $NativeBuild) { $NativeBuild = Join-Path $Repo "build\native-build\26.3" }
$UeExe = Join-Path $NativeBuild "REIMRuntime\REIMRuntime.exe"
$NativeVersion = Join-Path $NativeBuild "NativeVersions\26.3"
if (-not (Test-Path $UeExe)) { throw "UE Runtime not found: $UeExe" }
if (-not (Test-Path (Join-Path $NativeVersion "native_version.json"))) {
    throw "NativeVersion not found: $NativeVersion"
}

function Write-Utf8([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText($Path, $Text,
        (New-Object System.Text.UTF8Encoding($false)))
}

function New-Instance([string]$Path, [int]$RenderDistance = 4,
                      [bool]$VSync = $false, [int]$MaxFps = 0,
                      [float]$Sensitivity = 0.15, [bool]$InvertY = $false) {
    $instance = [ordered]@{
        world_path          = $World
        generated_path      = $Generated
        native_version_path = $NativeVersion
        render_distance     = $RenderDistance
        mouse_sensitivity   = $Sensitivity
        invert_y            = $InvertY
        vsync               = $VSync
        frame_rate_limit    = $MaxFps
        render_level        = 0
        auto_start          = $true
    }
    Write-Utf8 $Path ($instance | ConvertTo-Json)
    return $instance
}

function Invoke-Ue([string]$Instance, [string[]]$Extra, [string]$LogPath) {
    Remove-Item -Force $LogPath -ErrorAction SilentlyContinue
    $ueArgs = @("-reim-instance=$Instance", "-windowed", "-ResX=800", "-ResY=600",
                "-log", "-unattended", "-reim-nodialog", "-abslog=$LogPath") + $Extra
    $process = Start-Process -FilePath $UeExe -ArgumentList $ueArgs -PassThru -Wait
    $text = ""
    if (Test-Path $LogPath) { $text = Get-Content -Raw -LiteralPath $LogPath }
    return @{ ExitCode = $process.ExitCode; Log = $text }
}

# ---- D2: repeated start/exit -------------------------------------------------
$instancePath = Join-Path $Work "instance.json"
New-Instance $instancePath | Out-Null
$failures = New-Object System.Collections.Generic.List[string]
for ($i = 1; $i -le $Iterations; $i++) {
    $log = Join-Path $Work ("lifecycle-{0:D2}.log" -f $i)
    $run = Invoke-Ue $instancePath @("-reim-exit-after=$ExitSeconds") $log
    $ready = $run.Log -match "REIM.*world ready"
    Write-Host ("lifecycle {0}/{1}: exit={2} ready={3}" -f $i, $Iterations, $run.ExitCode, $ready)
    if ($run.ExitCode -ne 0 -or -not $ready) {
        $failures.Add("lifecycle ${i}: exit=$($run.ExitCode) ready=$ready")
    }
}

# ---- D4: settings persistence ------------------------------------------------
$settingsPath = Join-Path $Work "instance-settings.json"
New-Instance $settingsPath 4 $false 0 0.15 $false | Out-Null
$log = Join-Path $Work "settings.log"
$run = Invoke-Ue $settingsPath @("-reim-test-save-settings", "-reim-exit-after=2") $log
$saved = $null
if (Test-Path $settingsPath) { $saved = Get-Content -Raw -LiteralPath $settingsPath | ConvertFrom-Json }
$settingsOk = $run.ExitCode -eq 0 -and $saved -ne $null -and
    [math]::Abs([double]$saved.mouse_sensitivity - 0.2) -lt 0.0001 -and
    $saved.invert_y -eq $true -and $saved.vsync -eq $true -and
    [int]$saved.frame_rate_limit -eq 60 -and [int]$saved.render_distance -eq 5
Write-Host ("settings persistence: exit={0} saved={1}" -f $run.ExitCode, $settingsOk)
if (-not $settingsOk) { $failures.Add("settings persistence failed") }

# ---- D5: missing world / missing atlas are fatal, not a fake world -----------
$errorCases = New-Object System.Collections.Generic.List[object]

$missingWorld = Join-Path $Work "world-does-not-exist"
$instancePath = Join-Path $Work "instance-missing-world.json"
$instance = [ordered]@{
    world_path          = $missingWorld
    generated_path      = $Generated
    native_version_path = $NativeVersion
    render_distance     = 4
    render_level        = 0
    auto_start          = $true
}
Write-Utf8 $instancePath ($instance | ConvertTo-Json)
$log = Join-Path $Work "missing-world.log"
$run = Invoke-Ue $instancePath @("-reim-exit-after=6") $log
$ok = $run.ExitCode -ne 0 -and ($run.Log -match "world not found|Runtime create failed")
$errorCases.Add([ordered]@{ case = "missing-world"; exit_code = $run.ExitCode;
                            passed = $ok })
Write-Host ("missing world: exit={0} passed={1}" -f $run.ExitCode, $ok)
if (-not $ok) { $failures.Add("missing world was not a fatal error") }

$badGenerated = Join-Path $Work "generated-no-atlas"
Remove-Item -Recurse -Force $badGenerated -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $badGenerated | Out-Null
Copy-Item -Recurse -Force (Join-Path $Generated "*") $badGenerated
Remove-Item -Force (Join-Path $badGenerated "atlas.png") -ErrorAction SilentlyContinue
$instancePath = Join-Path $Work "instance-missing-atlas.json"
$instance = [ordered]@{
    world_path          = $World
    generated_path      = $badGenerated
    native_version_path = $NativeVersion
    render_distance     = 4
    render_level        = 0
    auto_start          = $true
}
Write-Utf8 $instancePath ($instance | ConvertTo-Json)
$log = Join-Path $Work "missing-atlas.log"
$run = Invoke-Ue $instancePath @("-reim-exit-after=6") $log
$ok = $run.ExitCode -ne 0 -and ($run.Log -match "generated data rejected|Runtime create failed|atlas")
$errorCases.Add([ordered]@{ case = "missing-atlas"; exit_code = $run.ExitCode;
                            passed = $ok })
Write-Host ("missing atlas: exit={0} passed={1}" -f $run.ExitCode, $ok)
if (-not $ok) { $failures.Add("missing atlas was not a fatal error") }

# ---- D3: menu screenshot -----------------------------------------------------
$menuPath = Join-Path $Work "instance-menu.json"
New-Instance $menuPath | Out-Null
$menuLog = Join-Path $Work "menu.log"
$menuShot = Join-Path $Reports "s2-menu.png"
Remove-Item -Force $menuShot -ErrorAction SilentlyContinue
$run = Invoke-Ue $menuPath @("-reim-menu", "-reim-shot=$menuShot",
                             "-reim-exit-after=4") $menuLog
for ($i = 0; $i -lt 50 -and -not (Test-Path $menuShot); $i++) {
    Start-Sleep -Milliseconds 100
}
$menuOk = $run.ExitCode -eq 0 -and (Test-Path $menuShot) -and
    ($run.Log -match "menu opened")
Write-Host ("menu screenshot: exit={0} shot={1} opened={2}" -f `
    $run.ExitCode, (Test-Path $menuShot), ($run.Log -match "menu opened"))
if (-not $menuOk) { $failures.Add("menu screenshot failed") }

$report = [ordered]@{
    schema       = "reim.m2.d_host"
    format       = 1
    native_build = $NativeBuild
    iterations   = $Iterations
    lifecycle_failures = @($failures | Where-Object { $_ -like "lifecycle*" })
    settings_persisted = $settingsOk
    error_paths        = $errorCases
    menu_screenshot    = $menuShot
    menu_ok            = $menuOk
    failures           = @($failures)
    passed             = $failures.Count -eq 0
}
$reportPath = Join-Path $Reports "m2-d-host.json"
$report | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $reportPath
Write-Host "D-host report: $reportPath"
if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Host "  FAIL: $_" }
    throw "M2-D host checks failed"
}
Write-Host "m2-d-host: $Iterations start/exit cycles, settings persistence and menu screenshot passed"
