# REIM S4 quick smoke. Thin wrapper around s4-acceptance.ps1 with a short
# Preview run; the optional soak stays off.
#
#   powershell -ExecutionPolicy Bypass -File tools\s4-smoke.ps1 `
#       -World "D:\Minecraft\.minecraft\saves\REIM-M1-26.3"

param(
  [string]$World = 'D:\Minecraft\.minecraft\saves\REIM-M1-26.3',
  [string]$Generated = '',
  [double]$PreviewSeconds = 15
)

$ErrorActionPreference = 'Stop'
$parameters = @{
  World = $World
  PreviewSeconds = $PreviewSeconds
}
if (-not [string]::IsNullOrEmpty($Generated)) {
  $parameters.Generated = $Generated
}
try {
  & (Join-Path $PSScriptRoot 's4-acceptance.ps1') @parameters
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} catch {
  Write-Error $_
  exit 1
}
exit 0
