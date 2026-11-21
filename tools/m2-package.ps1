# REIM M2-E: generate the pinned UE adapter descriptor, package a
# NativeVersion and assemble a NativeBuild (S2).
#
#   powershell -ExecutionPolicy Bypass -File tools\m2-package.ps1 `
#     -Generated D:\EngineeringDocuments\REIM-Launcher\generated\26.3 `
#     -VerifyRepro
#
# Each step is executed and its exit code checked. Nothing is reported as
# passing unless it actually ran.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Generated,
    [string]$Python = "python",
    [string]$Dll = "",
    [string]$RuntimePackage = "",
    [string]$AdapterOut = "",
    [string]$NativeVersionOut = "",
    [string]$NativeBuildOut = "",
    [switch]$SkipAssemble,
    [switch]$VerifyRepro
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
$Convert = Join-Path $Repo "build\m2\bin\reim-convert.exe"
$Reports = Join-Path $Repo "build\m2\reports"
New-Item -ItemType Directory -Force -Path $Reports | Out-Null

if (-not (Test-Path $Convert)) { throw "reim-convert not built: $Convert" }
if (-not (Test-Path $Generated)) { throw "generated data not found: $Generated" }
if (-not $Dll) { $Dll = Join-Path $Repo "build\m2\bin\mc_26.3.dll" }
if (-not (Test-Path $Dll)) { throw "Runtime DLL not found: $Dll (build first)" }
if (-not $AdapterOut) { $AdapterOut = Join-Path $Repo "runtime\host\Build\Adapter" }
if (-not $NativeVersionOut) { $NativeVersionOut = Join-Path $Repo "build\native-version\26.3" }
if (-not $NativeBuildOut) { $NativeBuildOut = Join-Path $Repo "build\native-build\26.3" }
if (-not $RuntimePackage) {
    $RuntimePackage = Join-Path $Repo "build\m2\ue-dev-package\Windows"
}

function Invoke-Step([string]$Name, [scriptblock]$Body) {
    Write-Host "==> $Name"
    & $Body
    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

function Invoke-Package([string]$OutDir) {
    & $Convert package --profile 26.3 --generated $Generated --dll $Dll `
        --ue-adapter $AdapterOut --out $OutDir
}

function Invoke-Assemble([string]$NvDir, [string]$OutDir) {
    & $Convert assemble --native-version $NvDir --runtime-package $RuntimePackage --out $OutDir
}

function File-Hash([string]$Path) {
    if (-not (Test-Path $Path)) { return $null }
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

$adapterScript = Join-Path $Repo "converter\adapters\ue\generate_ue_adapter.py"
Invoke-Step "Generate ue_adapter.json (pinned adapter)" {
    & $Python $adapterScript --repo $Repo --out $AdapterOut
}
Invoke-Step "Verify ue_adapter.json is current" {
    & $Python $adapterScript --repo $Repo --out $AdapterOut --check
}

# A stale file in the output directory would enter the NativeBuild digest.
Remove-Item -Recurse -Force $NativeVersionOut -ErrorAction SilentlyContinue
Invoke-Step "Package NativeVersion" { Invoke-Package $NativeVersionOut }

if (-not $SkipAssemble) {
    if (-not (Test-Path $RuntimePackage)) {
        throw "UE Runtime package not found: $RuntimePackage (run m2-build.ps1 -Package first)"
    }
    Invoke-Step "Assemble NativeBuild" { Invoke-Assemble $NativeVersionOut $NativeBuildOut }
}

if ($VerifyRepro) {
    $repro = Join-Path $Repo "build\m2\repro"
    $nvA = Join-Path $repro "nv-a"
    $nvB = Join-Path $repro "nv-b"
    $nbA = Join-Path $repro "nb-a"
    $nbB = Join-Path $repro "nb-b"
    Remove-Item -Recurse -Force $repro -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $repro | Out-Null

    Invoke-Step "Repro package A" { Invoke-Package $nvA }
    Invoke-Step "Repro package B" { Invoke-Package $nvB }
    $nvFiles = @("native_version.json", "verification.json", "ue_adapter.json",
                 "instance.json.template", "mc_26.3.dll")
    $nvDiffs = @()
    foreach ($name in $nvFiles) {
        $a = File-Hash (Join-Path $nvA $name)
        $b = File-Hash (Join-Path $nvB $name)
        if ($a -ne $b) { $nvDiffs += "native-version/$name" }
    }

    $nbDiffs = @()
    if (-not $SkipAssemble) {
        Invoke-Step "Repro assemble A" { Invoke-Assemble $nvA $nbA }
        Invoke-Step "Repro assemble B" { Invoke-Assemble $nvB $nbB }
        $a = File-Hash (Join-Path $nbA "native_build.json")
        $b = File-Hash (Join-Path $nbB "native_build.json")
        if ($a -ne $b) { $nbDiffs += "native-build/native_build.json" }
    }

    $report = [ordered]@{
        schema            = "reim.m2.repro"
        format            = 1
        generated         = $Generated
        dll               = $Dll
        native_version    = $NativeVersionOut
        native_build      = $NativeBuildOut
        package_identical = ($nvDiffs.Count -eq 0)
        assemble_identical = ($nbDiffs.Count -eq 0)
        differences       = @($nvDiffs + $nbDiffs)
    }
    $reportPath = Join-Path $Reports "m2-repro.json"
    $report | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 $reportPath
    Write-Host "repro report: $reportPath"
    if ($nvDiffs.Count -ne 0 -or $nbDiffs.Count -ne 0) {
        throw "reproducibility check failed: $($nvDiffs + $nbDiffs -join ', ')"
    }
    Write-Host "reproducibility: package and assemble outputs are byte identical"
}

Write-Host "m2-package: all requested steps succeeded"
