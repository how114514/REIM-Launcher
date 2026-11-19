# REIM S2 smoke test (M1-C: NBT / Anvil / world data).
#
# Usage (from any directory):
#   powershell -ExecutionPolicy Bypass -File tools\s2-smoke.ps1 `
#       -World "D:\Minecraft\.minecraft\saves\REIM-M1-26.3"
#
# Runs the C++ unit/integration tests, inspect-world twice (determinism),
# then compares the report against tools\mc-oracle.py, an independent Python
# implementation. No Java/JVM is started and the save is never modified.

param(
  [string]$World = 'D:\Minecraft\.minecraft\saves\REIM-M1-26.3',
  [string]$Python = 'python'
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
$oracle = Join-Path $root 'tools\mc-oracle.py'

Write-Host '== configure =='
cmake --preset m1-release | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }

Write-Host '== build =='
cmake --build --preset m1-release --parallel 8 | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'cmake build failed' }

Write-Host '== unit + real-world integration tests =='
$env:REIM_TEST_WORLD = $World
& (Join-Path $bin 'm1-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'm1-tests failed' }

Write-Host '== inspect-world =='
$report = Join-Path $reports 's2-inspect-world.json'
$rerun = Join-Path $reports 's2-inspect-world-rerun.json'
& (Join-Path $bin 'reim-convert.exe') inspect-world --world $World --out $report
if ($LASTEXITCODE -ne 0) { throw 'inspect-world failed' }
& (Join-Path $bin 'reim-convert.exe') inspect-world --world $World --out $rerun
if ($LASTEXITCODE -ne 0) { throw 'inspect-world rerun failed' }

$hash1 = (Get-FileHash -Algorithm SHA256 $report).Hash
$hash2 = (Get-FileHash -Algorithm SHA256 $rerun).Hash
if ($hash1 -ne $hash2) { throw "inspect-world output is not deterministic" }
Write-Host "inspect-world sha256: $hash1"

$parsed = Get-Content $report -Raw | ConvertFrom-Json
if ($parsed.summary.chunks_skipped -ne 0) {
  throw "inspect-world skipped $($parsed.summary.chunks_skipped) chunk(s)"
}
if ($parsed.summary.damaged_regions -ne 0) {
  throw "inspect-world found $($parsed.summary.damaged_regions) damaged region(s)"
}

Write-Host '== Python oracle: level and spawn chunk =='
$levelJson = & $Python $oracle level --world $World
if ($LASTEXITCODE -ne 0) { throw 'oracle level failed' }
$levelJson | Out-File -Encoding utf8 (Join-Path $reports 's2-oracle-level.json')

$chunkJson = & $Python $oracle chunk --world $World --x 1 --z 5
if ($LASTEXITCODE -ne 0) { throw 'oracle chunk failed' }
$chunkJson | Out-File -Encoding utf8 (Join-Path $reports 's2-oracle-chunk-1-5.json')

Write-Host '== C++ report vs Python oracle =='
& $Python $oracle compare --world $World --report $report --check-chunks
if ($LASTEXITCODE -ne 0) { throw 'oracle comparison failed' }

Write-Host "S2 smoke passed. Reports: $reports"
