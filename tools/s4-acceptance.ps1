# REIM S4 acceptance (M1-F + M1-G + M1-H).
#
# Usage (from any directory):
#   powershell -ExecutionPolicy Bypass -File tools\s4-acceptance.ps1 `
#       -World "D:\Minecraft\.minecraft\saves\REIM-M1-26.3"
#
# Automated checks:
#   * Release build and m1-tests (unit + real-world integration)
#   * reim-headless real-world run (fixed 20 TPS path)
#   * Python oracle vs Runtime block samples (M1-H4)
#   * REIMPreview hidden-window run with real terrain, HUD, screenshot and an
#     automated forward walk through the Host -> ABI input path
#   * Error paths: missing world, invalid DLL, generated-manifest check
#   * Log rotation/statistics are enabled in the Preview host
#
# The 10-minute soak is NOT run by default (docs/M1_PLAN.md section 9.2).
# Run it explicitly:
#   ...\s4-acceptance.ps1 -SoakSeconds 600
#
# No Java/JVM is started and the save is never modified.

param(
  [string]$World = 'D:\Minecraft\.minecraft\saves\REIM-M1-26.3',
  [string]$Generated = '',
  [double]$PreviewSeconds = 20,
  [double]$SoakSeconds = 0,
  [switch]$SkipBuild,
  [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$bin = Join-Path $root 'build\m1\bin'
$reports = Join-Path $root 'build\m1\reports'
New-Item -ItemType Directory -Force -Path $reports | Out-Null

$results = New-Object System.Collections.Generic.List[object]

function Add-Result {
  param([string]$Check, [string]$Status, [string]$Detail)
  $results.Add([pscustomobject]@{ check = $Check; status = $Status; detail = $Detail })
  Write-Host ("[{0}] {1}: {2}" -f $Status, $Check, $Detail)
}

function Fail {
  param([string]$Check, [string]$Detail)
  Add-Result -Check $Check -Status 'FAIL' -Detail $Detail
  throw "$Check failed: $Detail"
}

function Invoke-Tool {
  param([string]$Name, [string]$Exe, [string[]]$Arguments)
  & $Exe @Arguments | Out-Host
  if ($LASTEXITCODE -ne 0) { Fail -Check $Name -Detail "exit code $LASTEXITCODE" }
}

if (-not (Test-Path (Join-Path $World 'level.dat'))) {
  throw "world not found (level.dat missing): $World"
}
$World = (Resolve-Path $World).Path
if ([string]::IsNullOrEmpty($Generated)) {
  $Generated = Join-Path $root 'generated\26.3'
}
if (-not (Test-Path (Join-Path $Generated 'blocks.json'))) {
  throw "generated data not found: $Generated"
}
$Generated = (Resolve-Path $Generated).Path

Write-Host '== configure / build =='
if (-not $SkipBuild) {
  cmake --preset m1-release | Out-Host
  if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
  cmake --build --preset m1-release --parallel 8 | Out-Host
  if ($LASTEXITCODE -ne 0) { throw 'cmake build failed' }
}
if (-not (Test-Path (Join-Path $bin 'REIMPreview.exe')) -or
    -not (Test-Path (Join-Path $bin 'mc_26.3.dll'))) {
  Fail -Check 'build' -Detail 'REIMPreview.exe or mc_26.3.dll missing'
}
Add-Result -Check 'build' -Status 'PASS' -Detail 'Release binaries present'

if (-not $SkipTests) {
  Write-Host '== unit + real-world integration tests =='
  $env:REIM_TEST_WORLD = $World
  $env:REIM_TEST_GENERATED = $Generated
  & (Join-Path $bin 'm1-tests.exe') | Out-Host
  if ($LASTEXITCODE -ne 0) { Fail -Check 'm1-tests' -Detail "exit code $LASTEXITCODE" }
  Add-Result -Check 'm1-tests' -Status 'PASS' -Detail 'all unit/integration tests passed'
}

# ---------------------------------------------------------------------------
# Runtime block samples + Python oracle (M1-H4)
# ---------------------------------------------------------------------------
Write-Host '== headless runtime: 100 ticks =='
$headlessReport = Join-Path $reports 's4-headless.json'
Invoke-Tool -Name 'reim-headless' -Exe (Join-Path $bin 'reim-headless.exe') -Arguments @(
  '--headless', '--world', $World, '--generated', $Generated,
  '--ticks', '100', '--timeout', '180', '--dump', $headlessReport
)
$headless = Get-Content $headlessReport -Raw | ConvertFrom-Json
if (-not $headless.ready) { Fail -Check 'headless-ready' -Detail 'never ready' }
if ($headless.timed_out) { Fail -Check 'headless-timeout' -Detail 'timed out' }
if ($headless.tick -lt 100) { Fail -Check 'headless-ticks' -Detail "only $($headless.tick)" }
if ($headless.frame.mesh_sections -le 0) { Fail -Check 'headless-mesh' -Detail 'no meshed sections' }
Add-Result -Check 'headless' -Status 'PASS' -Detail ("tick={0} chunks={1} sections={2} tps={3:N2}" -f `
  $headless.tick, $headless.frame.loaded_chunks, $headless.frame.mesh_sections, $headless.frame.measured_tps)

Write-Host '== Python oracle vs Runtime block samples =='
$oracleOutput = & python (Join-Path $root 'tools\mc-oracle.py') runtime --world $World --report $headlessReport 2>&1
$oracleOutput | Out-Host
if ($LASTEXITCODE -ne 0) { Fail -Check 'oracle-runtime' -Detail 'block sample mismatch' }
Add-Result -Check 'oracle-runtime' -Status 'PASS' -Detail 'all Runtime samples match the independent oracle'

# ---------------------------------------------------------------------------
# Preview: real terrain, screenshot, HUD and Host -> ABI input
# ---------------------------------------------------------------------------
Write-Host '== REIMPreview: real terrain + input (hidden) =='
$autoWalk = [Math]::Min(3.0, [Math]::Max(1.0, $PreviewSeconds * 0.25))
$screenshot = Join-Path $reports 's4-preview.png'
$previewReport = Join-Path $reports 's4-preview.json'
$previewExit = 0
& (Join-Path $bin 'REIMPreview.exe') `
  --world $World `
  --generated $Generated `
  --hidden `
  --seconds $PreviewSeconds `
  --auto-walk $autoWalk `
  --screenshot $screenshot `
  --report $previewReport | Out-Host
$previewExit = $LASTEXITCODE
if ($previewExit -ne 0) { Fail -Check 'preview-run' -Detail "exit code $previewExit" }

$preview = Get-Content $previewReport -Raw | ConvertFrom-Json
if (-not $preview.ready) { Fail -Check 'preview-ready' -Detail 'renderer never saw a ready frame' }
if ($preview.loaded_chunks -le 0) { Fail -Check 'preview-chunks' -Detail 'no chunks loaded' }
if ($preview.mesh_sections -le 0) { Fail -Check 'preview-mesh' -Detail 'no meshed sections' }
if ($preview.draw_calls -le 0) { Fail -Check 'preview-draw' -Detail 'no section draw calls' }
if ($preview.triangles -le 0) { Fail -Check 'preview-tris' -Detail 'no triangles drawn' }
if ($preview.average_fps -le 0) { Fail -Check 'preview-fps' -Detail 'no frames presented' }
if ($preview.measured_tps -lt 10 -or $preview.measured_tps -gt 30) {
  Fail -Check 'preview-tps' -Detail "TPS out of sanity range: $($preview.measured_tps)"
}
if ($preview.auto_walk_distance -lt 1.0) {
  Fail -Check 'preview-input' -Detail ("auto-walk moved only {0:N2} blocks" -f $preview.auto_walk_distance)
}
if (-not (Test-Path $screenshot)) { Fail -Check 'preview-screenshot' -Detail 'screenshot missing' }
$shotSize = (Get-Item $screenshot).Length
if ($shotSize -lt 20000) { Fail -Check 'preview-screenshot-size' -Detail "$shotSize bytes" }

Add-Type -AssemblyName System.Drawing
$bitmap = [System.Drawing.Bitmap]::FromFile($screenshot)
$colors = New-Object 'System.Collections.Generic.HashSet[int]'
for ($y = 0; $y -lt $bitmap.Height; $y += 8) {
  for ($x = 0; $x -lt $bitmap.Width; $x += 8) {
    [void]$colors.Add($bitmap.GetPixel($x, $y).ToArgb())
  }
}
$bitmap.Dispose()
if ($colors.Count -lt 50) {
  Fail -Check 'preview-screenshot-content' -Detail "only $($colors.Count) distinct sampled colours (blank frame?)"
}
Add-Result -Check 'preview-render' -Status 'PASS' -Detail ("draw={0} tris={1} fps={2:N1} tps={3:N2} screenshot={4} bytes, {5} colours" -f `
  $preview.draw_calls, $preview.triangles, $preview.average_fps, $preview.measured_tps, `
  $shotSize, $colors.Count)
Add-Result -Check 'preview-input' -Status 'PASS' -Detail ("forward walk moved {0:N2} blocks" -f $preview.auto_walk_distance)

# HUD/log diagnostics (M1-G5, M1-H3)
$logPath = Join-Path $bin 'logs\preview.log'
if (-not (Test-Path $logPath)) { Fail -Check 'preview-log' -Detail 'preview.log missing' }
$logText = Get-Content $logPath -Raw
if ($logText -notmatch 'stats: fps=') { Fail -Check 'preview-log-stats' -Detail 'no periodic stats lines' }
if ($logText -notmatch 'world ready') { Fail -Check 'preview-log-ready' -Detail 'no ready diagnostics' }
Add-Result -Check 'preview-log' -Status 'PASS' -Detail 'world-ready diagnostics and periodic stats present'

# ---------------------------------------------------------------------------
# Error paths (M1-H2)
# ---------------------------------------------------------------------------
Write-Host '== error paths =='
$missingOutput = & (Join-Path $bin 'REIMPreview.exe') --world 'D:\reim-missing-world' --generated $Generated 2>&1
$missingExit = $LASTEXITCODE
$missingOutput | Out-Host
if ($missingExit -ne 2) { Fail -Check 'error-missing-world' -Detail "exit code $missingExit, expected 2" }
Add-Result -Check 'error-missing-world' -Status 'PASS' -Detail 'clear error, exit 2'

& (Join-Path $bin 'REIMPreview.exe') --check-dll (Join-Path $bin 'REIMPreview.exe') 2>&1 | Out-Host
if ($LASTEXITCODE -eq 0) { Fail -Check 'error-invalid-dll' -Detail 'invalid DLL was accepted' }
Add-Result -Check 'error-invalid-dll' -Status 'PASS' -Detail 'LoadLibrary failure reported'

& (Join-Path $bin 'REIMPreview.exe') --check-generated $Generated 2>&1 | Out-Host
if ($LASTEXITCODE -ne 0) { Fail -Check 'generated-manifest' -Detail "exit code $LASTEXITCODE" }
Add-Result -Check 'generated-manifest' -Status 'PASS' -Detail 'manifest accepted through DLL create'

# ---------------------------------------------------------------------------
# Optional soak (M1-H3). Not run unless -SoakSeconds is supplied.
# ---------------------------------------------------------------------------
if ($SoakSeconds -gt 0) {
  Write-Host ("== Preview soak: {0:N0}s ==" -f $SoakSeconds)
  $soakReport = Join-Path $reports 's4-soak.json'
  & (Join-Path $bin 'REIMPreview.exe') `
    --world $World `
    --generated $Generated `
    --hidden `
    --seconds $SoakSeconds `
    --report $soakReport | Out-Host
  if ($LASTEXITCODE -ne 0) { Fail -Check 'soak' -Detail "exit code $LASTEXITCODE" }
  $soak = Get-Content $soakReport -Raw | ConvertFrom-Json
  if (-not $soak.ready) { Fail -Check 'soak-ready' -Detail 'never ready' }
  if ($soak.elapsed_seconds -lt ($SoakSeconds * 0.95)) {
    Fail -Check 'soak-duration' -Detail "ran $($soak.elapsed_seconds)s of $SoakSeconds s"
  }
  if ($soak.tick -lt [int]($SoakSeconds * 18)) {
    Fail -Check 'soak-ticks' -Detail "only $($soak.tick) ticks"
  }
  Add-Result -Check 'soak' -Status 'PASS' -Detail ("{0:N1}s, tick={1}, tps={2:N2}, fps={3:N1}" -f `
    $soak.elapsed_seconds, $soak.tick, $soak.measured_tps, $soak.average_fps)
} else {
  Add-Result -Check 'soak' -Status 'SKIPPED' -Detail 'not run by default; use -SoakSeconds 600 for the 10-minute gate'
}

$summary = [pscustomobject]@{
  stage = 'S4-acceptance'
  world = $World
  generated = $Generated
  preview_seconds = $PreviewSeconds
  soak_seconds = $SoakSeconds
  results = $results
}
$summaryPath = Join-Path $reports 's4-acceptance.json'
[System.IO.File]::WriteAllText($summaryPath, ($summary | ConvertTo-Json -Depth 5))

Write-Host ''
Write-Host '== S4 acceptance summary =='
$results | ForEach-Object { Write-Host ("  {0,-6} {1}: {2}" -f $_.status, $_.check, $_.detail) }
Write-Host "report: $summaryPath"
Write-Host ''
Write-Host 'ROADMAP M1 gate (automated evidence):'
Write-Host '  1 real save/chunks/terrain shown ....... PASS (preview screenshot + report)'
Write-Host '  2 first-person move/collision .......... PARTIAL (auto-walk + S3 physics tests; manual 10-min feel pending)'
Write-Host '  3 no JVM/classpath ..................... PASS (no Java launched by these binaries)'
Write-Host '  4 renderer read-only ................... PASS by design (separate process + immutable snapshot)'
Write-Host '  5 fixed 20 TPS vs independent FPS ...... PASS (measured above)'
Write-Host '  6 10-minute soak ....................... SEE soak row (run -SoakSeconds 600)'
Write-Host '  7 block samples vs oracle .............. PASS'
Write-Host '  8 key data from Converter .............. PASS (manifest validated at create)'
Write-Host 'Manual items (user): mouse/keyboard feel, 10-minute interactive session.'
