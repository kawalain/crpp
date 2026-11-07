# Publishes the exe, manifest and notes in dist/ as a GitHub Release of an existing tag. The
# first line of the notes, if it is "# Title", becomes the release's name.
param([Parameter(Mandatory)][string]$Tag)
$ErrorActionPreference = 'Stop'

$lines = Get-Content -Encoding utf8 dist/release-notes.md
$title = $Tag
if ($lines.Count -gt 0 -and $lines[0] -match '^# (.+)$') {
  $title = $Matches[1]
  $lines = $lines | Select-Object -Skip 1
}
$lines | Set-Content -Encoding utf8NoBOM dist/notes.md
gh release create $Tag dist/crpp.exe dist/crpp-update.manifest --verify-tag --latest --title $title --notes-file dist/notes.md
if ($LASTEXITCODE -ne 0) { throw 'publishing failed' }
