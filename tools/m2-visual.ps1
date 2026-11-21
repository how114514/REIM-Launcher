# REIM M2-I7 (S4): fixed-camera visual assertions for Preview and UE.
#
#   powershell -ExecutionPolicy Bypass -File tools\m2-visual.ps1 `
#     -PreviewExe build\m2\bin\REIMPreview.exe -World <save> -Generated generated\26.3
#
#   powershell -ExecutionPolicy Bypass -File tools\m2-visual.ps1 `
#     -UeExe build\native-build\26.3\REIMRuntime\REIMRuntime.exe `
#     -UeInstance build\m2\acceptance\instance-g2.json
#
# Checks (per image, no pixel is "passed" without reading it):
#   - no magenta missing-texture checker (S4 DoD)
#   - snowy ground: a meaningful share of bright, low-saturation pixels
#   - vegetation: grass/leaf green pixels exist (tint reached the vertices)
#   - foliage: some dark green pixels (spruce tint is not white)
# Writes build/m2/reports/m2-visual.json; exit code 1 when any check fails.
[CmdletBinding()]
param(
    [string]$PreviewExe = "",
    [string]$World = "",
    [string]$Generated = "",
    [string]$PreviewImage = "",
    [string]$UeExe = "",
    [string]$UeInstance = "",
    [string]$UeImage = "",
    [string]$OutDir = "",
    [int]$RenderDistance = 6,
    [double]$SpawnX = 16.0,
    [double]$SpawnY = 65.0,
    [double]$SpawnZ = 80.0,
    [double]$SpawnYaw = 0.0,
    [double]$SpawnPitch = 0.0
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) { $OutDir = Join-Path $Repo "build\m2\reports\s4" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$Report = Join-Path $Repo "build\m2\reports\m2-visual.json"

function Invoke-Preview([string]$OutImage) {
    if (-not $PreviewExe) { $PreviewExe = Join-Path $Repo "build\m2\bin\REIMPreview.exe" }
    if (-not $Generated) { $Generated = Join-Path $Repo "generated\26.3" }
    if (-not $World) { throw "-World is required to run the Preview screenshot" }
    $args = @(
        "--world", $World, "--generated", $Generated,
        "--render-distance", "$RenderDistance", "--hidden", "--frames", "150",
        "--spawn", "$SpawnX", "$SpawnY", "$SpawnZ",
        "--spawn-yaw", "$SpawnYaw", "--spawn-pitch", "$SpawnPitch",
        "--screenshot", $OutImage
    )
    $p = Start-Process -FilePath $PreviewExe -ArgumentList $args -PassThru -Wait
    if ($p.ExitCode -ne 0) { throw "Preview screenshot failed with exit $($p.ExitCode)" }
}

function Invoke-Ue([string]$OutImage) {
    if (-not $UeExe) {
        $UeExe = Join-Path $Repo "build\native-build\26.3\REIMRuntime\REIMRuntime.exe"
    }
    if (-not $UeInstance) {
        $UeInstance = Join-Path $Repo "build\m2\acceptance\instance-g2.json"
    }
    Remove-Item -Force $OutImage -ErrorAction SilentlyContinue
    $args = @(
        "-reim-instance=$UeInstance", "-reim-shot=$OutImage",
        "-reim-shot-delay=6", "-reim-exit-after=12", "-reim-nohud",
        "-windowed", "-ResX=1280", "-ResY=720", "-unattended", "-reim-nodialog"
    )
    $p = Start-Process -FilePath $UeExe -ArgumentList $args -PassThru -Wait
    for ($i = 0; $i -lt 50 -and -not (Test-Path $OutImage); $i++) {
        Start-Sleep -Milliseconds 100
    }
    if ($p.ExitCode -ne 0 -or -not (Test-Path $OutImage)) {
        throw "UE screenshot failed (exit $($p.ExitCode), shot exists=$([bool](Test-Path $OutImage)))"
    }
}

function Measure-Image([string]$Path) {
    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap($Path)
    $w = $bmp.Width; $h = $bmp.Height
    $rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bytes = New-Object byte[] ($data.Stride * $h)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $bmp.UnlockBits($data)
    $bmp.Dispose()

    $magenta = 0
    $snow = 0; $lower = 0
    $green = 0; $darkGreen = 0
    for ($y = 0; $y -lt $h; $y += 2) {
        $row = $y * $data.Stride
        for ($x = 0; $x -lt $w; $x += 2) {
            $i = $row + $x * 4
            $b = $bytes[$i]; $g = $bytes[$i + 1]; $r = $bytes[$i + 2]
            if ($r -gt 200 -and $g -lt 48 -and $b -gt 200) { $magenta++ }
            if ($y -gt ($h * 0.25)) {
                $lower++
                $mx = [Math]::Max($r, [Math]::Max($g, $b))
                $mn = [Math]::Min($r, [Math]::Min($g, $b))
                if (($mx - $mn) -le 14 -and $mx -ge 185) { $snow++ }
            }
            if ($g -gt ($r + 8) -and $g -gt ($b + 8) -and $g -gt 50) {
                $green++
                if ($g -lt 170) { $darkGreen++ }
            }
        }
    }
    return [ordered]@{
        file            = $Path
        width           = $w
        height          = $h
        sampled_pixels  = ($lower + 0)
        magenta_pixels  = $magenta
        snow_pixels     = $snow
        snow_ratio      = if ($lower -gt 0) { [Math]::Round($snow / $lower, 4) } else { 0 }
        green_pixels    = $green
        dark_green_pixels = $darkGreen
    }
}

function Test-Image($m) {
    $checks = [ordered]@{
        no_magenta_checker = ($m.magenta_pixels -le 20)
        snowy_ground       = ($m.snow_ratio -ge 0.03)
        vegetation_tinted  = ($m.green_pixels -ge 300)
        foliage_dark_green = ($m.dark_green_pixels -ge 100)
    }
    $ok = -not ($checks.Values -contains $false)
    return [ordered]@{ pass = $ok; checks = $checks }
}

$results = [ordered]@{ schema = "reim.m2.visual"; format = 1; images = @() }
$allOk = $true

$images = @()
if ($PreviewImage) { $images += @{ name = "preview"; path = $PreviewImage; produced = $false } }
if ($UeImage) { $images += @{ name = "ue"; path = $UeImage; produced = $false } }
if ($PreviewExe -or $World) {
    $path = Join-Path $OutDir "s4-visual-preview.png"
    Invoke-Preview $path
    $images += @{ name = "preview"; path = $path; produced = $true }
}
if ($UeExe -or $UeInstance) {
    $path = Join-Path $OutDir "s4-visual-ue.png"
    Invoke-Ue $path
    $images += @{ name = "ue"; path = $path; produced = $true }
}
if ($images.Count -eq 0) {
    throw "No image to analyse: pass -PreviewImage/-UeImage or a runner (-World / -UeInstance)"
}

foreach ($item in $images) {
    if (-not (Test-Path $item.path)) { throw "image not found: $($item.path)" }
    $metrics = Measure-Image $item.path
    $verdict = Test-Image $metrics
    $results.images += [ordered]@{
        name     = $item.name
        produced = $item.produced
        metrics  = $metrics
        verdict  = $verdict
    }
    if (-not $verdict.pass) { $allOk = $false }
}

$results.pass = $allOk
$results | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $Report
$results | ConvertTo-Json -Depth 8
if (-not $allOk) { exit 1 }
