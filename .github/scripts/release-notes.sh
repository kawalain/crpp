#!/usr/bin/env bash
# Writes a release's notes: docs/releases/<tag>.md, whose "주요 변경 사항" comes first, then the
# commits since the previous version by kind. Stops if the most important changes are missing.
#   release-notes.sh --tag v0.1.0 --out dist/release-notes.md [--source <notes.md>]
# --tag may be HEAD with --source naming a notes file, to look at the result before tagging.
# --subjects names a file of commit subjects that stands in for the history, for the test.
set -euo pipefail

tag='' out='' source='' subjects=''
while [[ $# -gt 0 ]]; do
  case $1 in
    --tag) tag=$2 ;; --out) out=$2 ;; --source) source=$2 ;; --subjects) subjects=$2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift 2
done
[[ -n $tag && -n $out ]] || { echo "usage: $0 --tag <tag> --out <file> [--source <file>]" >&2; exit 2; }
[[ -n $source ]] || source="docs/releases/$tag.md"

highlights='## 주요 변경 사항'
# In the order they are shown. What matches none of them goes last.
kinds=(feat fix perf refactor docs)
titles=('새 기능' '버그 수정' '성능 개선' '코드 개선' '문서')
other='그 밖의 변경'

[[ -f $source ]] || { echo "$source is missing: its text opens the changelog" >&2; exit 1; }
mapfile -t written < <(sed 's/\r$//' "$source")
first_heading='' in_section=0 listed=0
for line in "${written[@]}"; do
  if [[ $line == '## '* ]]; then
    [[ -z $first_heading ]] && first_heading=$line
    if [[ $line == "$highlights" && $in_section == 0 && $listed == 0 ]]; then in_section=1; else in_section=2; fi
  elif [[ $in_section == 1 && $line =~ ^-\ [^[:space:]] ]]; then
    listed=1
  fi
done
[[ $first_heading == "$highlights" ]] || { echo "$source must start with \"$highlights\"" >&2; exit 1; }
[[ $listed == 1 ]] || { echo "$source lists nothing under \"$highlights\"" >&2; exit 1; }

if [[ -n $subjects ]]; then
  mapfile -t lines < <(sed 's/\r$//' "$subjects")
else
  previous=$(git describe --tags --abbrev=0 --match 'v0.*' "$tag^" 2>/dev/null || true)
  range=$tag
  [[ -n $previous ]] && range="$previous..$tag"
  mapfile -t lines < <(git -c i18n.logOutputEncoding=UTF-8 log --no-merges --format=%s "$range")
fi

declare -A groups=()
for line in "${lines[@]}"; do
  [[ -z $line ]] && continue
  title=$other item=$line
  if [[ $line =~ ^([a-z]+)(\(([^\)]+)\))?!?:\ (.+)$ ]]; then
    for i in "${!kinds[@]}"; do
      [[ ${BASH_REMATCH[1]} == "${kinds[$i]}" ]] && title=${titles[$i]}
    done
    item=${BASH_REMATCH[4]}
    [[ -n ${BASH_REMATCH[3]} ]] && item="**${BASH_REMATCH[3]}:** $item"
  fi
  groups[$title]+="- $item"$'\n'
done

mkdir -p "$(dirname "$out")"
{
  printf '%s\n' "${written[@]}"
  for title in "${titles[@]}" "$other"; do
    [[ -n ${groups[$title]:-} ]] || continue
    printf '\n## %s\n%s' "$title" "${groups[$title]}"
  done
} > "$out"
