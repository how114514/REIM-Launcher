# REIM M2-E9: prove the UE Host only starts from the Converter-produced
# NativeBuild and rejects tampered/manually assembled inputs.
#
#   powershell -ExecutionPolicy Bypass -File tools\m2-e9.ps1 `
#     -World D:\Minecraft\.minecraft\saves\REIM-M1-26.3 `
#     -Generated D:\EngineeringDocuments\REIM-Launcher\generated\26.3
#
# Every case is actually executed; the report records the real exit code and
# the error line from the Host log. Nothing is marked pass unless it ran.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$World,
    [Parameter(Mandatory = $true)][string]$Generated,
    [string]$NativeBuild = "",
    [int]$ValidSeconds = 5
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
$Reports = Join-Path $Repo "build\m2\reports"
$Work = Join-Path $Repo "build\m2\e9"
New-Item -ItemType Directory -Force -Path $Reports, $Work | Out-Null

if (-not $NativeBuild) { $NativeBuild = Join-Path $Repo "build\native-build\26.3" }
$UeExe = Join-Path $NativeBuild "REIMRuntime\REIMRuntime.exe"
$GoodNv = Join-Path $NativeBuild "NativeVersions\26.3"
if (-not (Test-Path $UeExe)) { throw "UE Runtime not found: $UeExe" }
if (-not (Test-Path (Join-Path $GoodNv "native_version.json"))) {
    throw "NativeVersion not found: $GoodNv"
}

function Write-Utf8([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText($Path, $Text,
        (New-Object System.Text.UTF8Encoding($false)))
}

function Copy-Tree([string]$From, [string]$To) {
    Remove-Item -Recurse -Force $To -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $To | Out-Null
    Copy-Item -Recurse -Force (Join-Path $From "*") $To
}

function New-Instance([string]$Path, [string]$WorldPath, [string]$GeneratedPath,
                      [string]$NativeVersionPath, [string]$DllPath = "") {
    $instance = [ordered]@{
        world_path        = $WorldPath
        generated_path    = $GeneratedPath
        render_distance   = 4
        mouse_sensitivity = 0.15
        invert_y          = $false
        render_level      = 0
        auto_start        = $true
    }
    if ($NativeVersionPath) { $instance.native_version_path = $NativeVersionPath }
    if ($DllPath) { $instance.dll_path = $DllPath }
    Write-Utf8 $Path ($instance | ConvertTo-Json)
}

function Invoke-Case([string]$Name, [string]$Instance, [bool]$ExpectSuccess,
                     [string]$ExpectedPattern) {
    $log = Join-Path $Work "$Name.log"
    Remove-Item -Force $log -ErrorAction SilentlyContinue
    # Shipping keeps its durable reasons in the Host log (no engine log).
    $hostLog = Join-Path $Work "logs\REIM-host.log"
    $hostOffset = if (Test-Path $hostLog) { [int64](Get-Item $hostLog).Length } else { 0 }
    $ueArgs = @("-reim-instance=$Instance", "-windowed", "-ResX=800", "-ResY=600",
                "-log", "-unattended", "-reim-nodialog", "-abslog=$log")
    if ($ExpectSuccess) {
        $ueArgs += @("-reim-exit-after=$ValidSeconds")
    } else {
        $ueArgs += @("-reim-exit-after=8")
    }
    $process = Start-Process -FilePath $UeExe -ArgumentList $ueArgs -PassThru -Wait
    $exitCode = $process.ExitCode
    $logText = ""
    if (Test-Path $log) { $logText = Get-Content -Raw -LiteralPath $log }
    $hostText = ""
    if (Test-Path $hostLog) {
        $bytes = [System.IO.File]::ReadAllBytes($hostLog)
        $start = [Math]::Min($hostOffset, [int64]$bytes.Length)
        $hostText = [System.Text.Encoding]::UTF8.GetString($bytes, [int]$start,
                                                           $bytes.Length - [int]$start)
    }
    $matched = (($logText + "`n" + $hostText) -match $ExpectedPattern)
    if ($ExpectSuccess) {
        $ok = $exitCode -eq 0
        $expected = "success"
    } else {
        $ok = ($exitCode -ne 0) -and $matched
        $expected = "rejected"
    }
    Write-Host ("{0}: exit={1} expected={2} pattern={3} -> {4}" -f `
        $Name, $exitCode, $expected, $ExpectedPattern, $(if ($ok) { "PASS" } else { "FAIL" }))
    return [ordered]@{
        case        = $Name
        expected    = $expected
        exit_code   = $exitCode
        pattern     = $ExpectedPattern
        matched     = $matched
        passed      = $ok
        log         = $log
        host_log    = $hostLog
    }
}

$cases = New-Object System.Collections.Generic.List[object]

# 0. Valid NativeBuild starts (baseline).
$validInstance = Join-Path $Work "valid.json"
New-Instance $validInstance $World $Generated $GoodNv
$cases.Add((Invoke-Case "valid-nativebuild" $validInstance $true "REIM.*world ready"))

# 1. Tampered DLL: same NativeVersion layout, one byte appended.
$badDllNv = Join-Path $Work "nv-bad-dll"
Copy-Tree $GoodNv $badDllNv
$badDll = Join-Path $badDllNv "mc_26.3.dll"
$stream = [System.IO.File]::Open($badDll, [System.IO.FileMode]::Append)
$stream.WriteByte(0)
$stream.Close()
$instance = Join-Path $Work "bad-dll.json"
New-Instance $instance $World $Generated $badDllNv
$cases.Add((Invoke-Case "tampered-dll" $instance $false "DLL hash mismatch"))

# 2. Edited adapter identity with a matching file hash in native_version.json:
#    the descriptor found on disk differs from the compiled adapter pin.
$badAdapterNv = Join-Path $Work "nv-bad-adapter"
Copy-Tree $GoodNv $badAdapterNv
$adapterPath = Join-Path $badAdapterNv "ue_adapter.json"
$adapter = Get-Content -Raw -LiteralPath $adapterPath | ConvertFrom-Json
$adapter.adapter_version = "9.9.9-forged"
Write-Utf8 $adapterPath ($adapter | ConvertTo-Json -Depth 20)
$newAdapterHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $adapterPath).Hash.ToLowerInvariant()
$nvPath = Join-Path $badAdapterNv "native_version.json"
$nv = Get-Content -Raw -LiteralPath $nvPath | ConvertFrom-Json
$nv.adapter.sha256 = $newAdapterHash
Write-Utf8 $nvPath ($nv | ConvertTo-Json -Depth 20)
$instance = Join-Path $Work "bad-adapter.json"
New-Instance $instance $World $Generated $badAdapterNv
$cases.Add((Invoke-Case "edited-adapter" $instance $false "adapter identity mismatch"))

# 3. Tampered generated manifest: the Runtime/NativeVersion hash no longer matches.
$badGenerated = Join-Path $Work "generated-bad"
Copy-Tree $Generated $badGenerated
$manifestPath = Join-Path $badGenerated "manifest.json"
$manifestText = Get-Content -Raw -LiteralPath $manifestPath
Write-Utf8 $manifestPath ($manifestText + "`n")
$instance = Join-Path $Work "bad-manifest.json"
New-Instance $instance $World $badGenerated $GoodNv
$cases.Add((Invoke-Case "tampered-manifest" $instance $false "generated manifest hash mismatch"))

# 4. Manual assembly: dll_path only, no NativeVersion.
$instance = Join-Path $Work "dev-dll-only.json"
New-Instance $instance $World $Generated "" (Join-Path $Repo "build\m2\bin\mc_26.3.dll")
$cases.Add((Invoke-Case "manual-dll-path" $instance $false "native_version_path is required"))

$failed = @($cases | Where-Object { -not $_.passed })
$report = [ordered]@{
    schema       = "reim.m2.e9"
    format       = 1
    native_build = $NativeBuild
    world        = $World
    generated    = $Generated
    cases        = $cases
    passed       = $failed.Count -eq 0
}
$reportPath = Join-Path $Reports "m2-e9.json"
$report | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $reportPath
Write-Host "E9 report: $reportPath"
if ($failed.Count -ne 0) {
    throw "E9 failed: $($failed.Count) case(s) did not behave as required"
}
Write-Host "m2-e9: valid NativeBuild starts; tampered DLL/manifest/adapter and manual assembly are rejected"
