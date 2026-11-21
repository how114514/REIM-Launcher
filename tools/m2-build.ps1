# REIM M2 build driver (S3: CMake + UE 5.8.4 host + pinned UE adapter +
# Development/Shipping BuildCookRun).
#
#   powershell -ExecutionPolicy Bypass -File tools\m2-build.ps1 -Config Release
#   powershell -ExecutionPolicy Bypass -File tools\m2-build.ps1 -Package -PackageConfig Development
#   powershell -ExecutionPolicy Bypass -File tools\m2-build.ps1 -Package -PackageConfig Shipping
#
# Does not create or modify Minecraft data. Fails loudly; never prints a pass
# for a step that did not run.
[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [string]$UeRoot = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { "D:\Program Files\Epic Games\UE_5.8" }),
    [string]$Target = "REIMRuntime",
    [switch]$SkipNative,
    [switch]$SkipUe,
    [switch]$SkipAssets,
    [switch]$SkipAdapter,
    [string]$Python = "python",
    [switch]$Package,
    [ValidateSet("Development", "Shipping")]
    [string]$PackageConfig = "Development"
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Repo "runtime\host\REIM.uproject"
$BuildRoot = Join-Path $Repo "build\m2"
$LogRoot = Join-Path $BuildRoot "logs"
New-Item -ItemType Directory -Force -Path $BuildRoot, $LogRoot | Out-Null

function Invoke-Step([string]$Name, [scriptblock]$Body) {
    Write-Host "==> $Name"
    & $Body
    if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

if (-not $SkipNative) {
    $preset = "m2-" + $Config.ToLowerInvariant()
    Invoke-Step "CMake configure ($preset)" { cmake --preset $preset }
    Invoke-Step "CMake build ($preset)" { cmake --build --preset $preset --parallel 8 }
}

if (-not $SkipUe) {
    $buildBat = Join-Path $UeRoot "Engine\Build\BatchFiles\Build.bat"
    if (-not (Test-Path $buildBat)) {
        throw "UE 5.8.4 not found at '$UeRoot' (set UE_ROOT or -UeRoot)"
    }
    $env:UE_ROOT = $UeRoot
    [System.Environment]::SetEnvironmentVariable("UE-LocalDataCachePath", "D:\UE-DDC")
    Invoke-Step "UBT REIMEditor Win64 Development" {
        & $buildBat REIMEditor Win64 Development -project="$Project" -waitmutex
    }
    Invoke-Step "UBT $Target Win64 Development" {
        & $buildBat $Target Win64 Development -project="$Project" -waitmutex
    }

    if (-not $SkipAssets) {
        $map = Join-Path $Repo "runtime\host\Content\Maps\R0.umap"
        $sky = Join-Path $Repo "runtime\host\Content\R0\M_Sky.uasset"
        $player = Join-Path $Repo "runtime\host\Content\R0\M_DebugPlayer.uasset"
        $generator = Join-Path $Repo "runtime\host\Tools\generate_r0_assets.py"
        if (-not (Test-Path $map) -or -not (Test-Path $sky) -or -not (Test-Path $player)) {
            $cmd = Join-Path $UeRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
            Invoke-Step "Generate R0 map/materials" {
                & $cmd "$Project" -run=pythonscript -script="$generator" `
                    -unattended -nosplash -nop4 -EnablePlugins=PythonScriptPlugin -stdout
            }
        } else {
            Write-Host "R0 assets present; skipping generator"
        }
    }

    # Adapter hashes must describe the final assets, so generate the descriptor
    # after the R0 asset step.
    if (-not $SkipAdapter) {
        $adapterScript = Join-Path $Repo "converter\adapters\ue\generate_ue_adapter.py"
        $adapterOut = Join-Path $Repo "runtime\host\Build\Adapter"
        Invoke-Step "Generate ue_adapter.json (pinned adapter)" {
            & $Python $adapterScript --repo $Repo --out $adapterOut
        }
    }
}

if ($Package) {
    $uat = Join-Path $UeRoot "Engine\Build\BatchFiles\RunUAT.bat"
    $archiveName = if ($PackageConfig -eq "Development") {
        "ue-dev-package"
    } else {
        "ue-shipping-package"
    }
    $archive = Join-Path $BuildRoot $archiveName
    # UE 5.8 no longer feeds ProjectPackagingSettings.IncludeAppLocalPrerequisites
    # into BuildCookRun (only Turnkey reads it), so pass the engine's app-local
    # dependency folder explicitly (M2-F2/F6). Stage it to a path without spaces
    # first: RunUAT re-expands the argument through cmd and splits on spaces.
    $engineAppLocal = Join-Path $UeRoot "Engine\Binaries\ThirdParty\AppLocalDependencies"
    if (-not (Test-Path $engineAppLocal)) {
        throw "app-local dependencies not found: $engineAppLocal"
    }
    $appLocal = Join-Path $BuildRoot "applocal"
    Remove-Item -Recurse -Force $appLocal -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $appLocal | Out-Null
    Copy-Item -Recurse -Force (Join-Path $engineAppLocal "*") $appLocal
    Invoke-Step "BuildCookRun $PackageConfig" {
        & $uat BuildCookRun "-project=$Project" "-target=$Target" -noP4 `
            "-platform=Win64" "-clientconfig=$PackageConfig" -build -cook -stage -pak `
            -archive "-archivedirectory=$archive" `
            "-applocaldirectory=$appLocal" -nodebuginfo -unattended -utf8output
    }
    $exe = Join-Path $archive "Windows\$Target.exe"
    if (-not (Test-Path $exe)) {
        throw "BuildCookRun $PackageConfig did not produce $exe"
    }
    Write-Host "package ($PackageConfig): $archive"
}

Write-Host "m2-build: all requested steps succeeded"
