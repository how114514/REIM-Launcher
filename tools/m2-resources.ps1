# REIM M2-F7: package/RAM/VRAM baseline sample during a Shipping run.
#
#   powershell -ExecutionPolicy Bypass -File tools\m2-resources.ps1 `
#     -World <save> -Generated <generated\26.3> -Seconds 30
#
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$World,
    [Parameter(Mandatory = $true)][string]$Generated,
    [string]$NativeBuild = "",
    [int]$Seconds = 30,
    [int]$RenderDistance = 6
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
$Reports = Join-Path $Repo "build\m2\reports"
$Work = Join-Path $Repo "build\m2\resources"
New-Item -ItemType Directory -Force -Path $Reports, $Work | Out-Null

if (-not $NativeBuild) { $NativeBuild = Join-Path $Repo "build\native-build\26.3" }
$Exe = Join-Path $NativeBuild "REIMRuntime\REIMRuntime.exe"
$Nv = Join-Path $NativeBuild "NativeVersions\26.3"
if (-not (Test-Path $Exe)) { throw "UE Runtime not found: $Exe" }

$instance = Join-Path $Work "instance.json"
@{
    world_path          = $World
    generated_path      = $Generated
    native_version_path = $Nv
    render_distance     = $RenderDistance
    mouse_sensitivity   = 0.15
    invert_y            = $false
    render_level        = 0
    auto_start          = $true
    log_dir             = (Join-Path $Work "logs")
} | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $instance

function GpuUsed {
    try {
        $value = & nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits 2>$null |
            Select-Object -First 1
        return [int]$value.Trim()
    } catch { return $null }
}

function Sample([string]$Label) {
    # The archive root exe is UE's bootstrap; the real game process is
    # REIMRuntime-Win64-<Config>.exe inside the package.
    $proc = Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $_.ProcessName -like "REIMRuntime*" -and
                       $_.Path -and $_.Path -like "$NativeBuild*" } |
        Sort-Object WorkingSet64 -Descending | Select-Object -First 1
    return [ordered]@{
        label          = $Label
        working_set_mb = $(if ($proc) { [Math]::Round($proc.WorkingSet64 / 1MB, 1) } else { $null })
        private_mb     = $(if ($proc) { [Math]::Round($proc.PrivateMemorySize64 / 1MB, 1) } else { $null })
        gpu_used_mb    = (GpuUsed)
        cpu_s          = $(if ($proc) { [Math]::Round($proc.CPU, 1) } else { $null })
    }
}

$gpuBefore = GpuUsed
$process = Start-Process -FilePath $Exe -ArgumentList @(
    "-reim-instance=$instance", "-reim-exit-after=$Seconds",
    "-windowed", "-ResX=1280", "-ResY=720", "-unattended", "-reim-nodialog") -PassThru
Start-Sleep -Seconds 10
$early = Sample "10s"
Start-Sleep -Seconds ([Math]::Max(5, $Seconds - 15))
$late = Sample ("{0}s" -f $Seconds)
$process.WaitForExit(120000) | Out-Null
Start-Sleep -Seconds 2
$gpuAfter = GpuUsed

$package = Join-Path $NativeBuild "REIMRuntime"
$packageBytes = (Get-ChildItem $package -Recurse -File | Measure-Object Length -Sum).Sum
$hostLog = Join-Path $Work "logs\REIM-host.log"
$ready = $(if (Test-Path $hostLog) {
    (Get-Content -Raw -LiteralPath $hostLog) -match "world ready"
} else { $false })

$report = [ordered]@{
    schema          = "reim.m2.resources"
    format          = 1
    exe             = $Exe
    render_distance = $RenderDistance
    seconds         = $Seconds
    exit_code       = $process.ExitCode
    world_ready     = [bool]$ready
    package_bytes   = $packageBytes
    package_mib     = [Math]::Round($packageBytes / 1MB, 1)
    gpu_before_mb   = $gpuBefore
    gpu_after_mb    = $gpuAfter
    samples         = @($early, $late)
    host_log        = $hostLog
}
$path = Join-Path $Reports "m2-resources.json"
$report | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 $path
Write-Host "resource report: $path"
$report | ConvertTo-Json -Depth 6
if ($process.ExitCode -ne 0 -or -not $ready) {
    throw "resource run did not reach the world / exit 0"
}
