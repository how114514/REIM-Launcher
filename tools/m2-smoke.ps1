# REIM M2 smoke (S2): Catch2, headless, Preview and UE R0 from NativeBuild.
#
#   powershell -ExecutionPolicy Bypass -File tools\m2-smoke.ps1 `
#     -World "D:\Minecraft\.minecraft\saves\REIM-M1-26.3" `
#     -Generated "D:\EngineeringDocuments\REIM-Launcher\generated\26.3"
#
# UE runs only from the Converter-produced NativeBuild (M2-E9). Each step is
# executed and its exit code checked. Nothing is reported as passing unless it
# actually ran.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$World,
    [Parameter(Mandatory = $true)][string]$Generated,
    [string]$UeRoot = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { "D:\Program Files\Epic Games\UE_5.8" }),
    [string]$Dll = "",
    [string]$Instance = "",
    [string]$NativeBuild = "",
    [int]$RenderDistance = 6,
    [int]$UeSeconds = 10,
    [switch]$SkipTests,
    [switch]$SkipHeadless,
    [switch]$SkipPreview,
    [switch]$SkipUe
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
$Bin = Join-Path $Repo "build\m2\bin"
$Reports = Join-Path $Repo "build\m2\reports"
New-Item -ItemType Directory -Force -Path $Reports | Out-Null

if (-not $Dll) { $Dll = Join-Path $Bin "mc_26.3.dll" }
if (-not (Test-Path $Dll)) { throw "Runtime DLL not found: $Dll (build first)" }
if (-not (Test-Path $World)) { throw "World not found: $World" }
if (-not (Test-Path $Generated)) { throw "Generated data not found: $Generated" }
if (-not $NativeBuild) { $NativeBuild = Join-Path $Repo "build\native-build\26.3" }
$NativeVersion = Join-Path $NativeBuild "NativeVersions\26.3"
if (-not (Test-Path (Join-Path $NativeVersion "native_version.json"))) {
    throw "NativeVersion not found: $NativeVersion (run tools\m2-package.ps1 first)"
}

if (-not $Instance) {
    $Instance = Join-Path $Repo "build\m2\instance-smoke.json"
    @{
        world_path          = $World
        generated_path      = $Generated
        native_version_path = $NativeVersion
        render_distance     = $RenderDistance
        mouse_sensitivity   = 0.15
        invert_y            = $false
        render_level        = 0
        auto_start          = $true
    } | ConvertTo-Json | Set-Content -Encoding UTF8 $Instance
}

function Invoke-Step([string]$Name, [scriptblock]$Body) {
    Write-Host "==> $Name"
    & $Body
    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

if (-not $SkipTests) {
    $env:REIM_TEST_WORLD = $World
    $env:REIM_TEST_GENERATED = $Generated
    Invoke-Step "Catch2 m1-tests" { & (Join-Path $Bin "m1-tests.exe") }
}

if (-not $SkipHeadless) {
    Invoke-Step "headless 100 ticks" {
        & (Join-Path $Bin "reim-headless.exe") --headless --world $World `
            --generated $Generated --render-distance $RenderDistance --ticks 100 `
            --timeout 180 --dump (Join-Path $Reports "smoke-headless.json")
    }
}

if (-not $SkipPreview) {
    Invoke-Step "Preview hidden 150 frames" {
        & (Join-Path $Bin "REIMPreview.exe") --world $World --generated $Generated `
            --render-distance $RenderDistance --hidden --frames 150 `
            --screenshot (Join-Path $Reports "smoke-preview.png") `
            --report (Join-Path $Reports "smoke-preview.json")
    }
}

if (-not $SkipUe) {
    $exe = Join-Path $NativeBuild "REIMRuntime\REIMRuntime.exe"
    $shot = Join-Path $Reports "smoke-ue-r0.png"
    if (-not (Test-Path $exe)) { throw "NativeBuild UE Runtime not found: $exe" }
    $exeArgs = @("-reim-instance=$Instance", "-reim-shot=$shot", "-reim-exit-after=$UeSeconds",
              "-windowed", "-ResX=1280", "-ResY=720", "-log", "-unattended", "-reim-nodialog")
    Invoke-Step "UE R0 from NativeBuild ($([System.IO.Path]::GetFileName($exe)))" {
        # GUI-subsystem exe: wait explicitly and check the real exit code.
        $process = Start-Process -FilePath $exe -ArgumentList $exeArgs -PassThru -Wait
        if ($process.ExitCode -ne 0) {
            throw "UE exited with code $($process.ExitCode)"
        }
    }
    # Screenshot writes are flushed asynchronously around process exit.
    for ($i = 0; $i -lt 50 -and -not (Test-Path $shot); $i++) {
        Start-Sleep -Milliseconds 100
    }
    if (-not (Test-Path $shot)) { throw "UE R0 screenshot was not produced: $shot" }
}

Write-Host "m2-smoke: all requested steps succeeded"
