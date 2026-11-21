# REIM M3 acceptance driver (stage 5B / M3-N8 + M3-O).
#
#   powershell -ExecutionPolicy Bypass -File tools\m3-acceptance.ps1 `
#     -World D:\Minecraft\reim-work\m3\worlds\REIM-M3-26.3-work `
#     -Generated generated\26.3 -UeSeconds 20 -SoakSeconds 0
#
# Runs only what this machine can run. UE/Cook/packaging require the designated
# development machine; a clean machine is never simulated. Every result is an
# actual process result; unexecuted checks are written as `not_run` and never
# as `pass`. The JSON report is build\m3\reports\m3-acceptance.json.
#
# Exit status: 0 when the report was produced and every *executed* check passed;
# 1 when an executed check failed. G3 status is in the report and may be false
# because required items are `not_run` even when the script exits 0.
[CmdletBinding()]
param(
    [string]$World = $env:REIM_TEST_WORLD,
    [string]$Generated = "",
    [switch]$SkipBuild,
    [switch]$SkipUe,
    [switch]$SkipUeAutomation,
    [switch]$SkipScenarios,
    [int]$UeSeconds = 20,
    [int]$SoakSeconds = 0,
    [string]$SoakReport = "",
    [string]$Out = ""
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
$Bin = Join-Path $Repo "build\m3\bin"
$Reports = Join-Path $Repo "build\m3\reports"
$NativeVersion = Join-Path $Repo "build\native-version\26.3"
$UeExe = Join-Path $Repo "runtime\host\Binaries\Win64\REIMRuntime.exe"
if (-not $Generated) { $Generated = Join-Path $Repo "generated\26.3" }
if (-not $Out) { $Out = Join-Path $Reports "m3-acceptance.json" }
New-Item -ItemType Directory -Force -Path $Reports | Out-Null

$steps = New-Object System.Collections.Generic.List[object]
function Add-Step([string]$Name, [string]$Status, [string]$Evidence, [string]$Note) {
    $steps.Add([ordered]@{ name = $Name; status = $Status; evidence = $Evidence; note = $Note })
    Write-Host ("{0,-28} {1,-9} {2}" -f $Name, $Status.ToUpperInvariant(), $Note)
}
function Run-Exe([string]$Exe, [string[]]$ArgList, [string]$LogPath) {
    $dir = Split-Path -Parent $LogPath
    if ($dir) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    # Negative tests intentionally write to stderr; keep those as captured text
    # instead of letting NativeCommandError terminate the acceptance run.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $out = & $Exe @ArgList 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    $text = ($out | ForEach-Object { "$_" }) -join "`n"
    $text | Set-Content -Encoding UTF8 $LogPath
    return @{ code = $code; text = $text }
}
function Run-Ue([string[]]$ArgList, [string]$LogPath) {
    # UE is a GUI-subsystem executable; PowerShell's call operator does not wait
    # for it. Start-Process -Wait does. The engine log is written by -abslog.
    $dir = Split-Path -Parent $LogPath
    if ($dir) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    $p = Start-Process -FilePath $UeExe -ArgumentList $ArgList -PassThru -Wait
    $text = if (Test-Path $LogPath) { Get-Content -Raw -LiteralPath $LogPath } else { "" }
    return @{ code = $p.ExitCode; text = $text }
}

# 1. Native build
if ($SkipBuild) {
    Add-Step "native-build" "not_run" "" "skipped by -SkipBuild"
} else {
    $r = Run-Exe "cmake" @("--build", "--preset", "m3-release", "--parallel", "8") (Join-Path $Reports "m3-s5-native-build.log")
    Add-Step "native-build" $(if ($r.code -eq 0) { "pass" } else { "fail" }) (Join-Path $Reports "m3-s5-native-build.log") "cmake --build --preset m3-release"
}

# 2. Unit/integration tests
$testsLog = Join-Path $Reports "m3-s5-tests.log"
$r = Run-Exe (Join-Path $Bin "m1-tests.exe") @() $testsLog
$testsPass = ($r.code -eq 0) -and ($r.text -match "All tests passed \((\d+) assertions in (\d+) test cases\)")
Add-Step "unit-tests" $(if ($testsPass) { "pass" } else { "fail" }) $testsLog $(if ($r.text -match "All tests passed \(([^)]+)\)") { $Matches[1] } else { "see log" })

# 3. Scenario suites
$ScenarioOut = Join-Path $Reports "scenarios-s5"
if ($SkipScenarios -or -not $World) {
    Add-Step "scenarios" "not_run" "" $(if (-not $World) { "no -World/REIM_TEST_WORLD" } else { "skipped by -SkipScenarios" })
} else {
    Remove-Item -Recurse -Force $ScenarioOut -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $ScenarioOut | Out-Null
    $suitePass = $true
    $suiteNotes = @()
    foreach ($dir in Get-ChildItem (Join-Path $Repo "tests\scenarios") -Directory | Where-Object { $_.Name -like "m3*" } | Sort-Object Name) {
        $env:REIM_TEST_WORLD = $World
        $log = Join-Path $ScenarioOut ("{0}.log" -f $dir.Name)
        $r = Run-Exe (Join-Path $Bin "reim-scenario.exe") @(
            "--suite", $dir.FullName, "--out", (Join-Path $ScenarioOut $dir.Name),
            "--generated", $Generated, "--dll", (Join-Path $Bin "mc_26.3.dll")) $log
        $m = [regex]::Match($r.text, "scenarios: (\d+) pass, (\d+) fail, (\d+) not_run")
        $line = $m.Value
        $suiteNotes += "$($dir.Name): $line"
        if ($r.code -ne 0 -or -not $m.Success -or [int]$m.Groups[2].Value -ne 0 -or [int]$m.Groups[3].Value -ne 0) { $suitePass = $false }
    }
    Add-Step "scenarios" $(if ($suitePass) { "pass" } else { "fail" }) $ScenarioOut ($suiteNotes -join "; ")

    $accountingJson = Join-Path $Reports "scenario-accounting.json"
    $r = Run-Exe "python" @((Join-Path $Repo "tools\m3-scenario-accounting.py"),
        "--reports", $ScenarioOut, "--json", $accountingJson) (Join-Path $Reports "scenario-accounting.log")
    $acct = if (Test-Path $accountingJson) { Get-Content -Raw -LiteralPath $accountingJson | ConvertFrom-Json } else { $null }
    if ($acct) {
        Add-Step "scenario-gate" $(if ($acct.gate_pass) { "pass" } else { "fail" }) $accountingJson (
            "required {0}/{1} = {2:P1}, not_run {3}" -f $acct.pass, $acct.denominator, $acct.pass_rate, $acct.not_run)
    } else {
        Add-Step "scenario-gate" "fail" $accountingJson "accounting report missing"
    }
}

# 4. Visual regression / audits
if ($World) {
    $r = Run-Exe "python" @((Join-Path $Repo "tools\m3-visual-regress.py"), "--world", $World) (Join-Path $Reports "visual-regress-s5.log")
    Add-Step "visual-regress" $(if ($r.code -eq 0) { "pass" } else { "fail" }) (Join-Path $Reports "visual-regress-s5.log") "Preview day/night/rain"
} else {
    Add-Step "visual-regress" "not_run" "" "no world"
}
$audits = @(
    @("asset-audit", "tools\m3-asset-audit.py", @(), "asset-audit.json"),
    @("render-audit", "tools\m3-render-audit.py", @(), "render-audit.json"),
    @("ui-audit", "tools\m3-ui-audit.py", @("--root", "runtime\host\Source", "--json", (Join-Path $Reports "ui-audit.json")), "ui-audit.json"),
    @("jrt-audit", "tools\m3-jrt-audit.py", @("--json", (Join-Path $Reports "jrt-audit.json")), "jrt-audit.json")
)
foreach ($a in $audits) {
    $log = Join-Path $Reports ("{0}-s5.log" -f $a[0])
    $r = Run-Exe "python" (@((Join-Path $Repo $a[1])) + $a[2]) $log
    Add-Step $a[0] $(if ($r.code -eq 0) { "pass" } else { "fail" }) (Join-Path $Reports $a[3]) "see log"
}

# 5. Converter mechanisms
$conv = Join-Path $Bin "reim-convert.exe"
$r = Run-Exe $conv @("verify", "--generated", $Generated, "--profile", "26.3") (Join-Path $Reports "convert-verify-s5.log")
Add-Step "convert-verify" $(if ($r.code -eq 0) { "pass" } else { "fail" }) (Join-Path $Reports "convert-verify-s5.log") "schema/ABI/manifest"
$r = Run-Exe $conv @("ledger", "--profile", "26.3", "--out", (Join-Path $Repo "work\26.3"), "--check") (Join-Path $Reports "ledger-check-s5.log")
Add-Step "port-ledger" $(if ($r.code -eq 0) { "pass" } else { "fail" }) (Join-Path $Reports "ledger-check-s5.log") "ledger --check"
$diffOut = Join-Path $Reports "diff-s5"
Remove-Item -Recurse -Force $diffOut -ErrorAction SilentlyContinue
$r = Run-Exe $conv @("diff", "--old", $Generated, "--new", $Generated, "--out", $diffOut) (Join-Path $Reports "diff-same-s5.log")
Add-Step "diff-mechanism" $(if ($r.code -eq 0) { "pass" } else { "fail" }) (Join-Path $Reports "diff-same-s5.log") "same-input diff (0 work items)"
$r = Run-Exe $conv @("patch", "verify", "--set", (Join-Path $Repo "converter\patches\26.3"), "--out", (Join-Path $Reports "patch-verify-s5.json")) (Join-Path $Reports "patch-verify-s5.log")
Add-Step "patch-mechanism" $(if ($r.code -eq 0) { "pass" } else { "fail" }) (Join-Path $Reports "patch-verify-s5.json") "replay/merge/conflict"

# 6. Toolchain reproducibility: package twice, compare deterministic files
$reproRoot = Join-Path $Reports "repro"
Remove-Item -Recurse -Force $reproRoot -ErrorAction SilentlyContinue
$reproOk = $true
foreach ($n in @("a", "b")) {
    $outDir = Join-Path $reproRoot $n
    $r = Run-Exe $conv @("package", "--profile", "26.3", "--generated", $Generated,
        "--dll", (Join-Path $Bin "mc_26.3.dll"), "--ue-adapter",
        (Join-Path $Repo "runtime\host\Build\Adapter"), "--out", $outDir) (Join-Path $reproRoot "$n.log")
    if ($r.code -ne 0) { $reproOk = $false }
}
foreach ($file in @("native_version.json", "verification.json", "ue_adapter.json", "mc_26.3.dll")) {
    $a = Join-Path $reproRoot "a\$file"; $b = Join-Path $reproRoot "b\$file"
    if (-not (Test-Path $a) -or -not (Test-Path $b)) { $reproOk = $false; continue }
    if ((Get-FileHash $a -Algorithm SHA256).Hash -ne (Get-FileHash $b -Algorithm SHA256).Hash) { $reproOk = $false }
}
Add-Step "toolchain-repro" $(if ($reproOk) { "pass" } else { "fail" }) $reproRoot "two package runs byte-identical (canonical files)"

# 6b. Package/license/import audit (O3)
$pkgRoot = Join-Path $Repo "build\m3\ue-shipping-package\Windows"
$nbRoot = Join-Path $Repo "build\native-build\26.3"
if ((Test-Path $pkgRoot) -and (Test-Path $nbRoot)) {
    $pkgJson = Join-Path $Reports "package-audit.json"
    $r = Run-Exe "python" @((Join-Path $Repo "tools\m3-package-audit.py"),
        "--package", $pkgRoot, "--native-build", $nbRoot, "--json", $pkgJson) (Join-Path $Reports "package-audit-s5.log")
    Add-Step "package-audit" $(if ($r.code -eq 0) { "pass" } else { "fail" }) $pkgJson "no Mojang/JVM content; licenses; import audit"
} else {
    Add-Step "package-audit" "not_run" "" "Shipping package/NativeBuild not present"
}

# 7. UE R0 + Bridge/R0 automation + three-host consistency
if ($SkipUe) {
    Add-Step "ue-r0" "not_run" "" "skipped by -SkipUe"
} elseif (-not (Test-Path $UeExe)) {
    Add-Step "ue-r0" "not_run" "" "UE Runtime not built on this machine"
} else {
    $zenUp = $false
    try { $zenUp = [bool](Get-NetTCPConnection -LocalPort 8558 -State Listen -ErrorAction SilentlyContinue) } catch { $zenUp = $false }
    if (-not $zenUp) {
        Add-Step "ue-r0" "not_run" "" "Zen storage server not listening on 8558 (start ZenLaunch.exe -Sponsor=None)"
    } else {
        if (-not (Test-Path (Join-Path $NativeVersion "native_version.json"))) {
            Add-Step "ue-r0" "not_run" $NativeVersion "NativeVersion missing; run reim-convert package"
        } else {
            $ueDir = Join-Path $Reports "visual"
            New-Item -ItemType Directory -Force -Path $ueDir | Out-Null
            $instance = Join-Path $Reports "instance-m3-acceptance.json"
            $instanceJson = [ordered]@{
                world_path = $World; generated_path = $Generated
                native_version_path = $NativeVersion; minecraft_dir = ""
                render_distance = 4; render_level = 0; auto_start = $true
                ui_scale = 2; log_dir = (Join-Path $Reports "logs")
            } | ConvertTo-Json
            [System.IO.File]::WriteAllText($instance, $instanceJson,
                (New-Object System.Text.UTF8Encoding($false)))
            $shot = Join-Path $ueDir "m3-s5-ue-world.png"
            $state = Join-Path $ueDir "m3-s5-ue-state.json"
            $perf = Join-Path $ueDir "m3-s5-ue-perf.json"
            Remove-Item -Force $shot, $state, $perf -ErrorAction SilentlyContinue
            $ueLog = Join-Path $ueDir "m3-s5-ue-world.log"
            $ueArgs = @("-game", "-windowed", "-ResX=1280", "-ResY=720", "-unattended", "-nosplash",
                "-log", "-abslog=$ueLog",
                "-reim-instance=$instance", "-reim-shot=$shot", "-reim-shot-delay=8",
                "-reim-exit-after=$UeSeconds", "-reim-state-tick=61",
                "-reim-state-report=$state", "-reim-perf-report=$perf")
            $r = Run-Ue $ueArgs $ueLog
            $uePass = ($r.code -eq 0) -and (Test-Path $shot) -and (Test-Path $state)
            Add-Step "ue-r0" $(if ($uePass) { "pass" } else { "fail" }) $ueLog "world ready + screenshot + state report"

            if (-not $SkipUeAutomation) {
                $bridgeLog = Join-Path $Reports "m3-s5-ue-nullrhi.log"
                $r = Run-Ue @("-game", "-nullrhi", "-unattended", "-nosplash",
                    "-log", "-abslog=$bridgeLog", "-reim-instance=$instance",
                    "-reim-test-dll=$(Join-Path $NativeVersion 'mc_26.3.dll')",
                    "-reim-test-world=$World", "-reim-test-generated=$Generated",
                    '-ExecCmds="Automation RunTests REIM.Bridge"',
                    '-TestExit="Automation Test Queue Empty"', "-reim-exit-after=180") $bridgeLog
                $bridgeText = if (Test-Path $bridgeLog) { Get-Content -Raw -LiteralPath $bridgeLog } else { "" }
                $bridgePass = ($r.code -eq 0) -and ($bridgeText -match "Result=\{Success\} Name=\{Errors\}") -and ($bridgeText -match "Result=\{Success\} Name=\{NullRHI\}") -and ($bridgeText -match "Automation Test Queue Empty")
                Add-Step "ue-bridge-automation" $(if ($bridgePass) { "pass" } else { "fail" }) $bridgeLog "REIM.Bridge.Errors + NullRHI"
                $r0Log = Join-Path $Reports "m3-s5-ue-r0.log"
                $r = Run-Ue @("-game", "-windowed", "-ResX=1280", "-ResY=720",
                    "-unattended", "-nosplash", "-log", "-abslog=$r0Log",
                    "-reim-instance=$instance",
                    "-reim-test-dll=$(Join-Path $NativeVersion 'mc_26.3.dll')",
                    "-reim-test-world=$World", "-reim-test-generated=$Generated",
                    '-ExecCmds="Automation RunTests REIM.R0"',
                    '-TestExit="Automation Test Queue Empty"', "-reim-exit-after=120") $r0Log
                $r0Text = if (Test-Path $r0Log) { Get-Content -Raw -LiteralPath $r0Log } else { "" }
                $r0Pass = ($r.code -eq 0) -and ($r0Text -match "Result=\{Success\} Name=\{Smoke\}") -and ($r0Text -match "Automation Test Queue Empty")
                Add-Step "ue-r0-automation" $(if ($r0Pass) { "pass" } else { "fail" }) $r0Log "REIM.R0.Smoke"
            }

            if ($World) {
                $headless = Join-Path $ueDir "m3-s5-headless.json"
                $preview = Join-Path $ueDir "m3-s5-preview.json"
                $r = Run-Exe (Join-Path $Bin "reim-headless.exe") @("--world", $World,
                    "--generated", $Generated, "--dll", (Join-Path $Bin "mc_26.3.dll"),
                    "--render-distance", "4", "--ticks", "61", "--dump", $headless) (Join-Path $ueDir "m3-s5-headless.log")
                $r = Run-Exe (Join-Path $Bin "REIMPreview.exe") @("--world", $World,
                    "--generated", $Generated, "--dll", (Join-Path $Bin "mc_26.3.dll"),
                    "--render-distance", "4", "--exit-tick", "60", "--no-vsync",
                    "--screenshot", (Join-Path $ueDir "m3-s5-preview.png"),
                    "--report", $preview) (Join-Path $ueDir "m3-s5-preview.log")
                $consistency = Join-Path $Reports "host-consistency.json"
                $r = Run-Exe "python" @((Join-Path $Repo "tools\m3-host-consistency.py"),
                    "--preview", $preview, "--headless", $headless, "--ue", $state,
                    "--json", $consistency) (Join-Path $Reports "host-consistency.log")
                Add-Step "three-host-consistency" $(if ($r.code -eq 0) { "pass" } else { "fail" }) $consistency "Runtime state at tick 61; R0 UE"

                $rcShot = Join-Path $ueDir "m3-s5-rc-world.png"
                if ((Test-Path $shot) -and (Test-Path $rcShot)) {
                    $imageJson = Join-Path $Reports "ue-r0-visual.json"
                    $r = Run-Exe "python" @((Join-Path $Repo "tools\m3-image-compare.py"),
                        "--baseline", $shot, "--current", $rcShot, "--json", $imageJson) (Join-Path $Reports "ue-r0-visual.log")
                    Add-Step "ue-r0-visual" $(if ($r.code -eq 0) { "pass" } else { "fail" }) $imageJson "Dev vs Shipping R0 screenshot"
                } else {
                    Add-Step "ue-r0-visual" "not_run" "" "Dev/Shipping screenshots not both present"
                }
            }
        }
    }
}

# 8. Checks this machine cannot run (explicit, never pass)
Add-Step "save-roundtrip" "not_run" "" "M3-B1 WorldWriter not implemented; SAV-01..06 not authored"
Add-Step "vanilla-oracle-diff" "not_run" "" "starting a Minecraft/Java game process is forbidden; server oracle not run"
Add-Step "version-upgrade" "not_run" "" "no adjacent second version; synthetic delta not approved"
Add-Step "clean-machine" "not_run" "" "no clean machine supplied"

# 9. Soak
if ($SoakSeconds -gt 0) {
    if (-not $World) {
        Add-Step "soak" "not_run" "" "no world"
    } else {
        $soakLog = Join-Path $Reports "m3-s5-soak.log"
        $soakJson = Join-Path $Reports "m3-s5-soak.json"
        $r = Run-Exe (Join-Path $Bin "reim-headless.exe") @("--world", $World,
            "--generated", $Generated, "--dll", (Join-Path $Bin "mc_26.3.dll"),
            "--render-distance", "4", "--seconds", "$SoakSeconds", "--dump", $soakJson) $soakLog
        Add-Step "soak" $(if ($r.code -eq 0) { "pass" } else { "fail" }) $soakLog "$SoakSeconds s headless"
    }
} elseif ($SoakReport -and (Test-Path $SoakReport)) {
    $soakData = Get-Content -Raw -LiteralPath $SoakReport | ConvertFrom-Json
    $soakOk = (-not $soakData.timed_out) -and (-not $soakData.failed)
    Add-Step "soak" $(if ($soakOk) { "pass" } else { "fail" }) $SoakReport (
        "pre-run report: tick {0}, timed_out={1}, failed={2}" -f $soakData.tick, $soakData.timed_out, $soakData.failed)
} else {
    Add-Step "soak" "not_run" "" "pass -SoakSeconds 1800 or -SoakReport for the 30-minute gate"
}

$executed = @($steps | Where-Object { $_.status -ne "not_run" })
$executedFail = @($executed | Where-Object { $_.status -eq "fail" })
$g3Required = @("scenario-gate", "save-roundtrip", "vanilla-oracle-diff",
    "version-upgrade", "clean-machine", "soak", "toolchain-repro",
    "three-host-consistency")
$g3Missing = @($steps | Where-Object {
    $g3Required -contains $_.name -and $_.status -ne "pass"
} | ForEach-Object { "$($_.name)=$($_.status)" })
$g3Pass = $g3Missing.Count -eq 0
$report = [ordered]@{
    schema = "reim.m3.acceptance"
    format = 1
    generated_at = (Get-Date).ToString("o")
    repo = $Repo
    world = $World
    generated = $Generated
    ue_exe = $UeExe
    native_version = $NativeVersion
    steps = $steps
    executed = $executed.Count
    executed_fail = $executedFail.Count
    not_run = @($steps | Where-Object { $_.status -eq "not_run" }).Count
    g3_pass = $g3Pass
    g3_missing = $g3Missing
    g3_note = "G3 requires scenario 62 gate, SAV roundtrip, oracle diff, clean machine, version upgrade, soak, reproducibility and three-Host consistency; unexecuted items stay not_run."
}
$json = $report | ConvertTo-Json -Depth 8
[System.IO.File]::WriteAllText($Out, $json, (New-Object System.Text.UTF8Encoding($false)))
Write-Host ""
Write-Host ("acceptance report: {0} (executed {1}, fail {2}, not_run {3})" -f $Out, $report.executed, $report.executed_fail, $report.not_run)
Write-Host ("G3: {0} {1}" -f $(if ($g3Pass) { "PASS" } else { "FAIL" }), ($g3Missing -join " "))
exit $(if ($executedFail.Count -eq 0) { 0 } else { 1 })
