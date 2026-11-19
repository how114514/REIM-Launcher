# REIM S1 smoke test (M1-A + M1-B).
#
# Usage (from any directory):
#   powershell -ExecutionPolicy Bypass -File tools\s1-smoke.ps1 `
#       -MinecraftDir "D:\Minecraft\.minecraft" `
#       -ServerJar "D:\Minecraft\reim-work\26.3\server.jar"
#
# ServerJar is optional and is only recorded as an input; this script never
# starts the Minecraft data generator or the Java game. The offline
# reim-analyze.jar is the only Java tool it runs.

param(
  [Parameter(Mandatory = $true)][string]$MinecraftDir,
  [string]$Version = '26.3',
  [string]$ServerJar = ''
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$bin = Join-Path $root 'build\m1\bin'
$work = Join-Path $root "work\$Version"
$generated = Join-Path $root "generated\$Version"
$reports = Join-Path $root 'build\m1\reports'
New-Item -ItemType Directory -Force -Path $reports | Out-Null

Write-Host "== configure =="
cmake --preset m1-release | Out-Host

Write-Host "== build =="
cmake --build --preset m1-release --parallel 8 | Out-Host

Write-Host "== unit tests =="
& (Join-Path $bin 'm1-tests.exe')
if ($LASTEXITCODE -ne 0) { throw "m1-tests failed" }

Write-Host "== prepare =="
$prepare = @('prepare', '--minecraft-dir', $MinecraftDir, '--version', $Version,
             '--out', $work)
if ($ServerJar) { $prepare += @('--server-jar', $ServerJar) }
& (Join-Path $bin 'reim-convert.exe') @prepare
if ($LASTEXITCODE -ne 0) { throw "prepare failed" }

Write-Host "== analyze (offline JDK tool; not the Minecraft JVM) =="
& (Join-Path $bin 'reim-convert.exe') analyze --profile $Version --out $work
if ($LASTEXITCODE -ne 0) { throw "analyze failed" }

Write-Host "== extract =="
& (Join-Path $bin 'reim-convert.exe') extract --profile $Version --out $generated
if ($LASTEXITCODE -ne 0) { throw "extract failed" }

Write-Host "== verify Converter -> Runtime manifest =="
& (Join-Path $bin 'reim-convert.exe') verify --generated $generated --profile $Version
if ($LASTEXITCODE -ne 0) { throw "verify failed" }

Write-Host "== DLL ABI / manifest contract =="
& (Join-Path $bin 'REIMPreview.exe') --check-dll (Join-Path $bin 'mc_26.3.dll')
if ($LASTEXITCODE -ne 0) { throw "DLL ABI check failed" }
& (Join-Path $bin 'REIMPreview.exe') --check-generated $generated
if ($LASTEXITCODE -ne 0) { throw "DLL manifest check failed" }

Write-Host "== empty window =="
& (Join-Path $bin 'REIMPreview.exe') --frames 60 `
    --screenshot (Join-Path $reports 's1-empty-window.png')
if ($LASTEXITCODE -ne 0) { throw "preview failed" }

Write-Host "S1 smoke passed. Reports: $reports"
