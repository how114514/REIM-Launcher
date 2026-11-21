# REIM M2-G5: run the UE automation tests against the Converter-produced
# Development NativeBuild (build/native-build-dev/26.3). Shipping has no
# automation code; the final Shipping NativeBuild lives at build/native-build/26.3.
#
#   powershell -ExecutionPolicy Bypass -File tools\m2-automation.ps1 `
#     -World D:\Minecraft\.minecraft\saves\REIM-M1-26.3 `
#     -Generated D:\EngineeringDocuments\REIM-Launcher\generated\26.3
#
# Each run is an actual process. A test is marked passed only when the log
# contains a Success result for it and the process exited 0.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$World,
    [Parameter(Mandatory = $true)][string]$Generated,
    [string]$NativeBuild = "",
    [string]$Exe = "",
    [int]$BridgeTimeoutSeconds = 180,
    [int]$R0TimeoutSeconds = 120,
    [switch]$SkipR0
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
$Reports = Join-Path $Repo "build\m2\reports"
$Work = Join-Path $Repo "build\m2\automation"
New-Item -ItemType Directory -Force -Path $Reports, $Work | Out-Null

if (-not $NativeBuild) { $NativeBuild = Join-Path $Repo "build\native-build-dev\26.3" }
$NativeVersion = Join-Path $NativeBuild "NativeVersions\26.3"
if (-not $Exe) { $Exe = Join-Path $NativeBuild "REIMRuntime\REIMRuntime.exe" }
$Dll = Join-Path $NativeVersion "mc_26.3.dll"
foreach ($path in @($World, $Generated, $Exe, $Dll,
                    (Join-Path $NativeVersion "native_version.json"))) {
    if (-not (Test-Path $path)) { throw "not found: $path" }
}

$Instance = Join-Path $Work "instance.json"
@{
    world_path          = $World
    generated_path      = $Generated
    native_version_path = $NativeVersion
    render_distance     = 4
    mouse_sensitivity   = 0.15
    invert_y            = $false
    render_level        = 0
    auto_start          = $true
} | ConvertTo-Json | Set-Content -Encoding UTF8 $Instance

function Invoke-AutomationRun([string]$Name, [string]$Filter, [string[]]$ExtraArgs,
                              [int]$TimeoutSeconds, [string[]]$ExpectedTests) {
    $log = Join-Path $Work "$Name.log"
    Remove-Item -Force $log -ErrorAction SilentlyContinue
    $common = @(
        "-reim-instance=`"$Instance`"",
        "-reim-test-dll=`"$Dll`"",
        "-reim-test-world=`"$World`"",
        "-reim-test-generated=`"$Generated`"",
        "-log",
        "-abslog=`"$log`"",
        "-unattended",
        "-nopencils",
        "-nosplash"
    )
    $args = $common + $ExtraArgs
    Write-Host "==> $Name ($Filter)"
    $process = Start-Process -FilePath $Exe -ArgumentList $args -PassThru
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $process.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        $process.Refresh()
    }
    $timedOut = -not $process.HasExited
    if ($timedOut) {
        Stop-Process -Id $process.Id -Force
        $exitCode = -1
    } else {
        $exitCode = $process.ExitCode
    }
    $text = if (Test-Path $log) { Get-Content -Raw -LiteralPath $log } else { "" }
    $results = @()
    foreach ($test in $ExpectedTests) {
        $escaped = [regex]::Escape($test)
        $success = $text -match ('Result=\{Success\} Name=\{[^}]+\} Path=\{' + $escaped + '\}')
        $failure = $text -match ('Result=\{Fail\} Name=\{[^}]+\} Path=\{' + $escaped + '\}')
        $results += [ordered]@{
            test    = $test
            success = [bool]$success
            failure = [bool]$failure
        }
    }
    $queueEmpty = $text -match "Automation Test Queue Empty"
    $critical = $text -match "Fatal error|=== Critical error"
    $passed = (-not $timedOut) -and ($exitCode -eq 0) -and $queueEmpty -and
              (-not $critical) -and
              (@($results | Where-Object { -not $_.success -or $_.failure }).Count -eq 0)
    Write-Host ("{0}: exit={1} timeout={2} queueEmpty={3} -> {4}" -f `
        $Name, $exitCode, $timedOut, $queueEmpty, $(if ($passed) { "PASS" } else { "FAIL" }))
    return [ordered]@{
        name        = $Name
        filter      = $Filter
        exit_code   = $exitCode
        timed_out   = $timedOut
        queue_empty = [bool]$queueEmpty
        critical    = [bool]$critical
        tests       = $results
        passed      = [bool]$passed
        log         = $log
    }
}

$cases = New-Object System.Collections.Generic.List[object]

$cases.Add((Invoke-AutomationRun "nullrhi" "REIM.Bridge" @(
        "-nullrhi",
        "-ExecCmds=`"Automation RunTests REIM.Bridge`"",
        "-TestExit=`"Automation Test Queue Empty`"",
        "-reim-exit-after=$BridgeTimeoutSeconds"
    ) $BridgeTimeoutSeconds @("REIM.Bridge.NullRHI", "REIM.Bridge.Errors")))

if (-not $SkipR0) {
    $shot = Join-Path $Reports "m2-automation-r0.png"
    $cases.Add((Invoke-AutomationRun "r0" "REIM.R0" @(
            "-windowed",
            "-ResX=1280",
            "-ResY=720",
            "-ExecCmds=`"Automation RunTests REIM.R0`"",
            "-TestExit=`"Automation Test Queue Empty`"",
            "-reim-shot=`"$shot`"",
            "-reim-shot-delay=3",
            "-reim-exit-after=$R0TimeoutSeconds"
        ) $R0TimeoutSeconds @("REIM.R0.Smoke")))
}

$failed = @($cases | Where-Object { -not $_.passed })
$report = [ordered]@{
    schema = "reim.m2.automation"
    format = 1
    exe    = $Exe
    world  = $World
    cases  = $cases
    passed = ($failed.Count -eq 0)
}
$reportPath = Join-Path $Reports "m2-automation.json"
$report | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $reportPath
Write-Host "automation report: $reportPath"
if ($failed.Count -ne 0) {
    throw "automation failed: $($failed.Count) run(s) did not pass"
}
Write-Host "m2-automation: all automation runs passed"
