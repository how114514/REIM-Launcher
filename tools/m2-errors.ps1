# REIM M2-H2: error matrix against the Shipping NativeBuild.
#
#   powershell -ExecutionPolicy Bypass -File tools\m2-errors.ps1 `
#     -World D:\Minecraft\.minecraft\saves\REIM-M1-26.3 `
#     -Generated D:\EngineeringDocuments\REIM-Launcher\generated\26.3
#
# Every case is actually executed against build/native-build/26.3. Shipping
# keeps its reasons in the Host log (build/m2/errors/<case>/REIM-host.log),
# not in an engine log. A case passes only when the real exit code and log
# match the expectation. GPU device loss is not scriptable and is reported as
# not constructed rather than passed.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$World,
    [Parameter(Mandatory = $true)][string]$Generated,
    [string]$NativeBuild = "",
    [switch]$SkipMinimize
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
$Reports = Join-Path $Repo "build\m2\reports"
$Work = Join-Path $Repo "build\m2\errors"
New-Item -ItemType Directory -Force -Path $Reports, $Work | Out-Null

if (-not $NativeBuild) { $NativeBuild = Join-Path $Repo "build\native-build\26.3" }
$UeExe = Join-Path $NativeBuild "REIMRuntime\REIMRuntime.exe"
$GoodNv = Join-Path $NativeBuild "NativeVersions\26.3"
if (-not (Test-Path $UeExe)) { throw "UE Runtime not found: $UeExe" }
if (-not (Test-Path (Join-Path $GoodNv "native_version.json"))) {
    throw "NativeVersion not found: $GoodNv"
}
foreach ($path in @($World, $Generated)) {
    if (-not (Test-Path $path)) { throw "not found: $path" }
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

function New-Instance([string]$Path, [string]$CaseDir, [string]$WorldPath,
                      [string]$GeneratedPath, [string]$NativeVersionPath) {
    $instance = [ordered]@{
        world_path        = $WorldPath
        generated_path    = $GeneratedPath
        render_distance   = 2
        mouse_sensitivity = 0.15
        invert_y          = $false
        render_level      = 0
        auto_start        = $true
        log_dir           = (Join-Path $CaseDir "logs")
    }
    if ($NativeVersionPath) { $instance.native_version_path = $NativeVersionPath }
    Write-Utf8 $Path (($instance | ConvertTo-Json -Depth 4))
}

function Invoke-Case([string]$Name, [string]$Instance, [bool]$ExpectSuccess,
                     [string]$ExpectedPattern, [string]$CaseDir) {
    $log = Join-Path $CaseDir "ue.log"
    $hostLog = Join-Path $CaseDir "logs\REIM-host.log"
    Remove-Item -Force $log, $hostLog -ErrorAction SilentlyContinue
    $args = @("-reim-instance=$Instance", "-windowed", "-ResX=800", "-ResY=600",
              "-log", "-unattended", "-reim-nodialog", "-reim-exit-after=8",
              "-abslog=$log")
    $process = Start-Process -FilePath $UeExe -ArgumentList $args -PassThru -Wait
    $exitCode = $process.ExitCode
    $text = ""
    foreach ($p in @($hostLog, $log)) {
        if (Test-Path $p) { $text += "`n" + (Get-Content -Raw -LiteralPath $p) }
    }
    $matched = $text -match $ExpectedPattern
    $ok = if ($ExpectSuccess) { $exitCode -eq 0 -and $matched }
          else { $exitCode -ne 0 -and $matched }
    Write-Host ("{0}: exit={1} expected={2} matched={3} -> {4}" -f `
        $Name, $exitCode, $(if ($ExpectSuccess) { "success" } else { "rejected" }),
        $matched, $(if ($ok) { "PASS" } else { "FAIL" }))
    return [ordered]@{
        case       = $Name
        expected   = $(if ($ExpectSuccess) { "success" } else { "rejected" })
        exit_code  = $exitCode
        pattern    = $ExpectedPattern
        matched    = [bool]$matched
        passed     = [bool]$ok
        host_log   = $hostLog
        ue_log     = $log
    }
}

$cases = New-Object System.Collections.Generic.List[object]
$CaseBase = Join-Path $Work "cases"
Remove-Item -Recurse -Force $CaseBase -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $CaseBase | Out-Null

# 0. valid baseline
$dir = Join-Path $CaseBase "valid"; New-Item -ItemType Directory -Force -Path $dir | Out-Null
$inst = Join-Path $dir "instance.json"
New-Instance $inst $dir $World $Generated $GoodNv
$cases.Add((Invoke-Case "valid" $inst $true "world ready" $dir))

# 1. no NativeVersion at all
$dir = Join-Path $CaseBase "missing-native-version"; New-Item -ItemType Directory -Force -Path $dir | Out-Null
$inst = Join-Path $dir "instance.json"
New-Instance $inst $dir $World $Generated ""
$cases.Add((Invoke-Case "missing-native-version" $inst $false "native_version_path is required" $dir))

# 2. NativeVersion without the DLL
$dir = Join-Path $CaseBase "missing-dll"; New-Item -ItemType Directory -Force -Path $dir | Out-Null
$nv = Join-Path $dir "nv"; Copy-Tree $GoodNv $nv; Remove-Item -Force (Join-Path $nv "mc_26.3.dll")
$inst = Join-Path $dir "instance.json"
New-Instance $inst $dir $World $Generated $nv
$cases.Add((Invoke-Case "missing-dll" $inst $false "NativeVersion DLL missing" $dir))

# 3. NativeVersion declares an older ABI
$dir = Join-Path $CaseBase "native-version-abi"; New-Item -ItemType Directory -Force -Path $dir | Out-Null
$nv = Join-Path $dir "nv"; Copy-Tree $GoodNv $nv
$nvPath = Join-Path $nv "native_version.json"
$json = Get-Content -Raw -LiteralPath $nvPath | ConvertFrom-Json
$json.abi_version = 1
Write-Utf8 $nvPath ($json | ConvertTo-Json -Depth 20)
$inst = Join-Path $dir "instance.json"
New-Instance $inst $dir $World $Generated $nv
$cases.Add((Invoke-Case "native-version-abi" $inst $false "native_version.json rejected" $dir))

# 4. NativeVersion declares the wrong profile
$dir = Join-Path $CaseBase "native-version-profile"; New-Item -ItemType Directory -Force -Path $dir | Out-Null
$nv = Join-Path $dir "nv"; Copy-Tree $GoodNv $nv
$nvPath = Join-Path $nv "native_version.json"
$json = Get-Content -Raw -LiteralPath $nvPath | ConvertFrom-Json
$json.profile = "26.4"
Write-Utf8 $nvPath ($json | ConvertTo-Json -Depth 20)
$inst = Join-Path $dir "instance.json"
New-Instance $inst $dir $World $Generated $nv
$cases.Add((Invoke-Case "native-version-profile" $inst $false "native_version.json rejected" $dir))

# 5. generated data without its manifest
$dir = Join-Path $CaseBase "missing-manifest"; New-Item -ItemType Directory -Force -Path $dir | Out-Null
$gen = Join-Path $dir "generated"; Copy-Tree $Generated $gen; Remove-Item -Force (Join-Path $gen "manifest.json")
$inst = Join-Path $dir "instance.json"
New-Instance $inst $dir $World $gen $GoodNv
$cases.Add((Invoke-Case "missing-manifest" $inst $false "generated manifest hash mismatch" $dir))

# 6. generated data without the atlas PNG
$dir = Join-Path $CaseBase "missing-atlas"; New-Item -ItemType Directory -Force -Path $dir | Out-Null
$gen = Join-Path $dir "generated"; Copy-Tree $Generated $gen; Remove-Item -Force (Join-Path $gen "atlas.png")
$inst = Join-Path $dir "instance.json"
New-Instance $inst $dir $World $gen $GoodNv
$cases.Add((Invoke-Case "missing-atlas" $inst $false "create failed|atlas|generated" $dir))

# 7. world directory missing level.dat
$dir = Join-Path $CaseBase "missing-world"; New-Item -ItemType Directory -Force -Path $dir | Out-Null
$inst = Join-Path $dir "instance.json"
New-Instance $inst $dir (Join-Path $dir "no-such-world") $Generated $GoodNv
$cases.Add((Invoke-Case "missing-world" $inst $false "level.dat|world not found|create failed" $dir))

# 8. corrupt level.dat (truncated copy of the real save)
$dir = Join-Path $CaseBase "corrupt-leveldat"; New-Item -ItemType Directory -Force -Path $dir | Out-Null
$save = Join-Path $dir "save"; Copy-Tree $World $save
$level = Join-Path $save "level.dat"
$bytes = [System.IO.File]::ReadAllBytes($level)
$keep = [Math]::Max(8, [int]($bytes.Length / 3))
$fs = [System.IO.File]::Open($level, 'Open', 'Write')
$fs.SetLength($keep)
$fs.Close()
$inst = Join-Path $dir "instance.json"
New-Instance $inst $dir $save $Generated $GoodNv
$cases.Add((Invoke-Case "corrupt-leveldat" $inst $false "level.dat|world|create failed|corrupt|invalid" $dir))

# 9. no -reim-instance in Shipping
$dir = Join-Path $CaseBase "missing-instance"; New-Item -ItemType Directory -Force -Path $dir | Out-Null
# ProjectSavedDir in a packaged game is %LOCALAPPDATA%/<ProjectName>/Saved.
$savedLogs = @(
    (Join-Path $env:LOCALAPPDATA "REIM\Saved\Logs\REIM-host.log"),
    (Join-Path $NativeBuild "REIMRuntime\REIM\Saved\Logs\REIM-host.log")
)
$savedLog = $savedLogs[0]
foreach ($p in $savedLogs) { Remove-Item -Force $p -ErrorAction SilentlyContinue }
$args = @("-windowed", "-ResX=800", "-ResY=600", "-log", "-unattended",
          "-reim-nodialog", "-reim-exit-after=8")
$process = Start-Process -FilePath $UeExe -ArgumentList $args -PassThru -Wait
$text = ""
foreach ($p in $savedLogs) {
    if (Test-Path $p) { $text += "`n" + (Get-Content -Raw -LiteralPath $p); $savedLog = $p }
}
$matched = $text -match "no -reim-instance"
$ok = $process.ExitCode -ne 0 -and $matched
Write-Host ("missing-instance: exit={0} matched={1} -> {2}" -f `
    $process.ExitCode, $matched, $(if ($ok) { "PASS" } else { "FAIL" }))
$cases.Add([ordered]@{
    case      = "missing-instance"
    expected  = "rejected"
    exit_code = $process.ExitCode
    pattern   = "no -reim-instance"
    matched   = [bool]$matched
    passed    = [bool]$ok
    host_log  = $savedLog
})

# 10. minimize/restore while running a valid world
if (-not $SkipMinimize) {
    $dir = Join-Path $CaseBase "minimize"; New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $inst = Join-Path $dir "instance.json"
    New-Instance $inst $dir $World $Generated $GoodNv
    $hostLog = Join-Path $dir "logs\REIM-host.log"
    Remove-Item -Force $hostLog -ErrorAction SilentlyContinue
    Add-Type -Namespace Reim -Name Win32 -MemberDefinition @'
public delegate bool EnumWindowsProc(System.IntPtr hWnd, System.IntPtr lParam);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool EnumWindows(EnumWindowsProc callback, System.IntPtr lParam);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern uint GetWindowThreadProcessId(System.IntPtr hWnd, out uint pid);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool IsWindowVisible(System.IntPtr hWnd);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool ShowWindowAsync(System.IntPtr hWnd, int nCmdShow);
'@
    $args = @("-reim-instance=$inst", "-windowed", "-ResX=800", "-ResY=600",
              "-log", "-unattended", "-reim-nodialog", "-reim-exit-after=12")
    $process = Start-Process -FilePath $UeExe -ArgumentList $args -PassThru
    # The archive root executable is UE's bootstrap; the visible window belongs
    # to the child REIMRuntime process it launches and waits for. MainWindowHandle
    # is often 0 for Slate windows, so enumerate top-level windows by PID.
    $child = $null
    for ($i = 0; $i -lt 120 -and $null -eq $child; $i++) {
        Start-Sleep -Milliseconds 250
        $child = Get-Process -ErrorAction SilentlyContinue |
            Where-Object { $_.ProcessName -like "REIMRuntime*" -and
                           $_.Id -ne $process.Id -and $_.Path -and
                           $_.Path -like "$NativeBuild*" } |
            Sort-Object WorkingSet64 -Descending | Select-Object -First 1
    }
    $minimized = $false
    $windowFound = $false
    if ($null -ne $child) {
        $found = New-Object System.Collections.Generic.List[System.IntPtr]
        $targetPid = [uint32]$child.Id
        $callback = [Reim.Win32+EnumWindowsProc]{
            param([System.IntPtr]$hWnd, [System.IntPtr]$lParam)
            [uint32]$owner = 0
            [Reim.Win32]::GetWindowThreadProcessId($hWnd, [ref]$owner) | Out-Null
            if ($owner -eq $targetPid -and [Reim.Win32]::IsWindowVisible($hWnd)) {
                $found.Add($hWnd)
            }
            return $true
        }
        [Reim.Win32]::EnumWindows($callback, [System.IntPtr]::Zero) | Out-Null
        if ($found.Count -gt 0) {
            $windowFound = $true
            [Reim.Win32]::ShowWindowAsync($found[0], 6) | Out-Null
            Start-Sleep -Seconds 2
            [Reim.Win32]::ShowWindowAsync($found[0], 9) | Out-Null
            $minimized = $true
        }
    }
    $process.WaitForExit(120000) | Out-Null
    $text = if (Test-Path $hostLog) { Get-Content -Raw -LiteralPath $hostLog } else { "" }
    $ready = $text -match "world ready"
    # Stability is the pass condition; minimize is recorded separately because
    # this automation session may have no interactive desktop/window at all.
    $ok = $process.ExitCode -eq 0 -and $ready
    Write-Host ("minimize: exit={0} window={1} minimized={2} ready={3} -> {4}" -f `
        $process.ExitCode, $windowFound, $minimized, $ready, $(if ($ok) { "PASS" } else { "FAIL" }))
    $cases.Add([ordered]@{
        case              = "minimize-restore"
        expected          = "success"
        exit_code         = $process.ExitCode
        window_found      = $windowFound
        minimized         = $minimized
        matched           = [bool]$ready
        passed            = [bool]$ok
        notes             = $(if ($windowFound) { "minimize/restore exercised" }
                              else { "no top-level window in this automation session; minimize not exercised" })
        host_log          = $hostLog
    })
}

$failed = @($cases | Where-Object { -not $_.passed })
$unverified = @("GPU device lost: not constructed (engine RHI handles it; no scripted case in M2)")
$minimizeCase = $cases | Where-Object { $_.case -eq "minimize-restore" } | Select-Object -First 1
if ($null -ne $minimizeCase -and -not $minimizeCase.minimized) {
    $unverified += "minimize/restore: no top-level window in this automation session"
}
$report = [ordered]@{
    schema  = "reim.m2.errors"
    format  = 1
    exe     = $UeExe
    world   = $World
    generated = $Generated
    cases   = $cases
    unverified = $unverified
    passed  = ($failed.Count -eq 0)
}
$reportPath = Join-Path $Reports "m2-errors.json"
$report | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $reportPath
Write-Host "error matrix report: $reportPath"
if ($failed.Count -ne 0) {
    throw "error matrix failed: $($failed.Count) case(s)"
}
Write-Host "m2-errors: all fabricated error cases behaved as required"
