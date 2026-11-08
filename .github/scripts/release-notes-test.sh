#!/usr/bin/env bash
# The test of release-notes.sh, run by CTest as update.release_notes.
set -euo pipefail
script="$(cd "$(dirname "$0")" && pwd)/release-notes.sh"
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
failed=0
check() {  # check <what> <command...>
  local what=$1
  shift
  if ! "$@"; then echo "CHECK failed: $what"; failed=$((failed + 1)); fi
}
has_line() { grep -qxF -- "$2" "$1"; }
lacks_line() { ! has_line "$@"; }
refused() { ! "$script" --tag v0.1.0 --source "$1" --out "$dir/never.md" --subjects "$dir/one.txt" 2>/dev/null; }

printf '%s\n' '# 구름의 제목' '## 주요 변경 사항' '- 가장 중요한 수정' '' '직접 쓴 문단' > "$dir/v0.1.0.md"
printf '%s\n' 'fix(ui): second kind first in history' 'feat(gui): a feature' 'chore: a chore' 'docs: a note' \
  'perf(ui)!: faster' 'feat: without a scope' 'not conventional at all' 'refactor(ui): tidied' \
  'feat(skin): an engine part' 'fix(assets): an installer fix' > "$dir/subjects.txt"
printf 'feat: x\n' > "$dir/one.txt"
: > "$dir/none.txt"

"$script" --tag v0.1.0 --source "$dir/v0.1.0.md" --out "$dir/out.md" --subjects "$dir/subjects.txt"
check 'the title stays first, for the release name' test "$(sed -n 1p "$dir/out.md")" == '# 구름의 제목'
check 'the most important changes come first' test "$(sed -n 2p "$dir/out.md")" == '## 주요 변경 사항'
order=$(grep '^## ' "$dir/out.md" | paste -sd'|')
check "kinds in order: $order" test "$order" == '## 주요 변경 사항|## 새 기능|## 버그 수정|## 성능 개선'
check 'a scope is bold' has_line "$dir/out.md" '- **gui:** a feature'
check 'no scope, no prefix' has_line "$dir/out.md" '- without a scope'
check 'a breaking mark does not hide the kind' has_line "$dir/out.md" '- **ui:** faster'
check 'other kinds are left out' lacks_line "$dir/out.md" '- a chore'
check 'a refactor is left out' lacks_line "$dir/out.md" '- **ui:** tidied'
check 'an odd subject is left out' lacks_line "$dir/out.md" '- not conventional at all'
check 'a scope of building and delivering is left out' lacks_line "$dir/out.md" '- **skin:** an engine part'
check 'and its fixes too' lacks_line "$dir/out.md" '- **assets:** an installer fix'

"$script" --tag v0.1.0 --source "$dir/v0.1.0.md" --out "$dir/empty.md" --subjects "$dir/none.txt"
check 'no commits still gives the notes' has_line "$dir/empty.md" '- 가장 중요한 수정'

check 'a missing file stops the release' refused "$dir/missing.md"
printf '%s\n' '# 제목' '## 변경 사항' '- 하나' > "$dir/wrong.md"
check 'another first section stops the release' refused "$dir/wrong.md"
printf '%s\n' '# 제목' '## 주요 변경 사항' '' '## 다른 절' '- 하나' > "$dir/empty-section.md"
check 'an empty section stops the release' refused "$dir/empty-section.md"

[[ $failed == 0 ]] || exit 1
echo 'release notes: ok'
