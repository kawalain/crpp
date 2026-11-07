# The script's test, run by CTest as update.release_notes.
$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'release-notes.ps1'
$dir = Join-Path ([IO.Path]::GetTempPath()) "crpp-notes-test-$PID"
New-Item -ItemType Directory $dir | Out-Null
$failed = 0
function Check([bool]$ok, [string]$what) {
  if (-not $ok) { Write-Host "CHECK failed: $what"; $script:failed++ }
}
function Refused($source) {
  try {
    & $script -Tag v0.1.0 -Source $source -Out "$dir/never.md" -Subjects @('feat: x')
    return $false
  } catch { return $true }
}

try {
  $source = "$dir/v0.1.0.md"
  @('# 구름의 제목', '## 주요 변경 사항', '- 가장 중요한 수정', '', '직접 쓴 문단') | Set-Content -Encoding utf8NoBOM $source
  & $script -Tag v0.1.0 -Source $source -Out "$dir/out.md" -Subjects @(
    'fix(ui): second kind first in history',
    'feat(gui): a feature',
    'chore: a chore',
    'docs: a note',
    'perf(ui)!: faster',
    'feat: without a scope',
    'not conventional at all'
  )
  $out = Get-Content -Encoding utf8 "$dir/out.md"
  Check ($out[0] -eq '# 구름의 제목') 'the title stays first, for the release name'
  Check ($out[1] -eq '## 주요 변경 사항') 'the most important changes come first'
  $order = @($out | Where-Object { $_ -match '^## ' })
  Check (($order -join '|') -eq '## 주요 변경 사항|## 새 기능|## 버그 수정|## 성능 개선|## 문서|## 그 밖의 변경') "kinds in order: $($order -join '|')"
  Check ($out -contains '- **gui:** a feature') 'a scope is bold'
  Check ($out -contains '- without a scope') 'no scope, no prefix'
  Check ($out -contains '- **ui:** faster') 'a breaking mark does not hide the kind'
  Check ($out -contains '- a chore') 'other kinds go under other changes'
  Check ($out -contains '- not conventional at all') 'an odd subject is kept whole'
  Check (-not ($out -contains '## 코드 개선')) 'an empty kind is left out'

  & $script -Tag v0.1.0 -Source $source -Out "$dir/empty.md" -Subjects @()
  Check ((Get-Content -Encoding utf8 "$dir/empty.md") -contains '- 가장 중요한 수정') 'no commits still gives the notes'

  Check (Refused "$dir/missing.md") 'a missing file stops the release'
  @('# 제목', '## 변경 사항', '- 하나') | Set-Content -Encoding utf8NoBOM "$dir/wrong.md"
  Check (Refused "$dir/wrong.md") 'another first section stops the release'
  @('# 제목', '## 주요 변경 사항', '', '## 다른 절', '- 하나') | Set-Content -Encoding utf8NoBOM "$dir/empty-section.md"
  Check (Refused "$dir/empty-section.md") 'an empty section stops the release'
} finally {
  [IO.Directory]::Delete($dir, $true)
}
if ($failed -gt 0) { exit 1 }
Write-Host 'release notes: ok'
