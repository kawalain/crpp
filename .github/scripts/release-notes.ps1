# Writes a release's notes: docs/releases/<tag>.md, whose "주요 변경 사항" comes first, then the
# commits since the previous version by kind. Stops if the most important changes are missing.
#   release-notes.ps1 -Tag v0.1.0 -Out dist/release-notes.md
# -Tag may be HEAD with -Source naming a notes file, to look at the result before tagging.
# -Subjects stands in for the history, for the script's test.
param(
  [Parameter(Mandatory)][string]$Tag,
  [Parameter(Mandatory)][string]$Out,
  [string]$Source = "docs/releases/$Tag.md",
  [string[]]$Subjects
)
$ErrorActionPreference = 'Stop'

$highlights_heading = '## 주요 변경 사항'
# In the order they are shown. What matches none of them goes last.
$kinds = [ordered]@{
  feat = '새 기능'
  fix = '버그 수정'
  perf = '성능 개선'
  refactor = '코드 개선'
  docs = '문서'
}
$other = '그 밖의 변경'

function Read-Subjects {
  [Console]::OutputEncoding = [Text.Encoding]::UTF8
  $previous = git describe --tags --abbrev=0 --match 'v0.*' "$Tag^" 2>$null
  $range = if ($LASTEXITCODE -eq 0 -and $previous) { "$previous..$Tag" } else { $Tag }
  $global:LASTEXITCODE = 0
  $lines = git -c i18n.logOutputEncoding=UTF-8 log --no-merges --format=%s $range
  if ($LASTEXITCODE -ne 0) { throw "git log failed for $range" }
  return @($lines)
}

function Format-Subjects([string[]]$lines) {
  $groups = [ordered]@{}
  foreach ($title in $kinds.Values) { $groups[$title] = [Collections.Generic.List[string]]::new() }
  $groups[$other] = [Collections.Generic.List[string]]::new()
  foreach ($line in $lines) {
    if ($line -match '^(?<kind>[a-z]+)(\((?<scope>[^)]+)\))?!?: (?<text>.+)$') {
      $title = if ($kinds.Contains($Matches.kind)) { $kinds[$Matches.kind] } else { $other }
      $item = if ($Matches.scope) { "**$($Matches.scope):** $($Matches.text)" } else { $Matches.text }
    } else {
      $title = $other
      $item = $line
    }
    $groups[$title].Add("- $item")
  }
  $text = [Collections.Generic.List[string]]::new()
  foreach ($entry in $groups.GetEnumerator()) {
    if ($entry.Value.Count -eq 0) { continue }
    $text.Add('')
    $text.Add("## $($entry.Key)")
    $text.AddRange($entry.Value)
  }
  return $text
}

if (-not (Test-Path -LiteralPath $Source)) { throw "$Source is missing: its text opens the changelog" }
$written = @(Get-Content -LiteralPath $Source -Encoding utf8)
$first_heading = @($written | Where-Object { $_ -match '^## ' })[0]
if ($first_heading -ne $highlights_heading) { throw "$Source must start with `"$highlights_heading`"" }
$at = [array]::IndexOf($written, $highlights_heading)
$next = @($written | Select-Object -Skip ($at + 1))
$section = @($next | ForEach-Object -Begin { $done = $false } -Process {
  if ($_ -match '^## ') { $done = $true }
  if (-not $done) { $_ }
})
if (-not ($section | Where-Object { $_ -match '^- \S' })) { throw "$Source lists nothing under `"$highlights_heading`"" }

$lines = if ($PSBoundParameters.ContainsKey('Subjects')) { $Subjects } else { Read-Subjects }
$body = @($written) + @(Format-Subjects $lines)

$folder = Split-Path -Parent $Out
if ($folder) { New-Item -ItemType Directory -Force $folder | Out-Null }
($body -join "`n") + "`n" | Set-Content -NoNewline -Encoding utf8NoBOM -LiteralPath $Out
