<#
Compares two folders from render.ps1 by file hash, and says which scenes differ.

  tests/shots/compare.ps1 <before> <after>
#>
param(
  [Parameter(Mandatory)][string]$Before,
  [Parameter(Mandatory)][string]$After
)
$different = 0
$scenes = Get-ChildItem $Before -Filter *.png
foreach ($scene in $scenes) {
  $other = Join-Path $After $scene.Name
  if (-not (Test-Path $other)) { Write-Output "missing  $($scene.Name)"; $different++; continue }
  if ((Get-FileHash $scene.FullName).Hash -ne (Get-FileHash $other).Hash) { Write-Output "differs  $($scene.Name)"; $different++ }
}
Write-Output "$($scenes.Count) scenes, $different differ"
if ($different) { exit 1 }
