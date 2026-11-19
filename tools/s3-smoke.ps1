# REIM S3 smoke test (M1-D + M1-E: runtime core, 20 TPS, meshes, C ABI).
#
# Usage (from any directory):
#   powershell -ExecutionPolicy Bypass -File tools\s3-smoke.ps1 `
#       -World "D:\Minecraft\.minecraft\saves\REIM-M1-26.3"
#
# Builds Release, runs the unit/integration tests, then runs reim-headless for
# 100 fixed ticks through the C ABI against the real world and validates the
# JSON report. No Java/JVM is started and the save is never modified.

param(
  [string]$World = 'D:\Minecraft\.minecraft\saves\REIM-M1-26.3',
  [string]$Generated = '',
  [int]$Ticks = 100
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$bin = Join-Path $root 'build\m1\bin'
$reports = Join-Path $root 'build\m1\reports'
New-Item -ItemType Directory -Force -Path $reports | Out-Null

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

Write-Host '== configure =='
cmake --preset m1-release | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }

Write-Host '== build =='
cmake --build --preset m1-release --parallel 8 | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'cmake build failed' }

Write-Host '== unit + real-world integration tests =='
$env:REIM_TEST_WORLD = $World
$env:REIM_TEST_GENERATED = $Generated
& (Join-Path $bin 'm1-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'm1-tests failed' }

Write-Host "== headless runtime: $Ticks ticks =="
$report = Join-Path $reports 's3-headless.json'
& (Join-Path $bin 'reim-headless.exe') --headless `
  --world $World `
  --generated $Generated `
  --ticks $Ticks `
  --timeout 180 `
  --dump $report
if ($LASTEXITCODE -ne 0) { throw "reim-headless failed ($LASTEXITCODE)" }

$parsed = Get-Content $report -Raw | ConvertFrom-Json
if (-not $parsed.ready) { throw 'runtime never became ready' }
if ($parsed.timed_out) { throw 'runtime timed out' }
if ($parsed.tick -lt $Ticks) { throw "only $($parsed.tick) ticks ran, expected $Ticks" }
# Renderer frames come from fetch polling; simulation ticks are fixed at 20 TPS.
if ($parsed.frames -le $parsed.tick) {
  throw "frames ($($parsed.frames)) did not outpace ticks ($($parsed.tick))"
}
if ($parsed.frame.loaded_chunks -le 0) { throw 'no chunks loaded' }
if ($parsed.frame.mesh_sections -le 0) { throw 'no sections meshed' }
if ($parsed.updates_seen -le 0) { throw 'no mesh updates were produced' }
if ($parsed.vertices_seen -le 0) { throw 'mesh updates carried no vertices' }
$tps = [double]$parsed.frame.measured_tps
if ($tps -lt 10.0 -or $tps -gt 30.0) {
  throw "measured TPS out of sanity range: $tps"
}
if ($null -eq $parsed.status.samples -or $parsed.status.samples.Count -eq 0) {
  throw 'status report has no block samples'
}

Write-Host ("tick={0} loaded_chunks={1} mesh_sections={2} vertices={3} tps={4}" -f `
  $parsed.tick, $parsed.frame.loaded_chunks, $parsed.frame.mesh_sections, `
  $parsed.vertices_seen, $parsed.frame.measured_tps)
Write-Host 'block samples:'
$parsed.status.samples | ForEach-Object {
  Write-Host ("  ({0},{1},{2}) {3}" -f $_.x, $_.y, $_.z, $_.block)
}
Write-Host "S3 smoke passed. Report: $report"
