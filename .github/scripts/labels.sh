#!/usr/bin/env bash
# Sets the repository's issue labels to the list below: creates or updates each, and removes
# GitHub's defaults that this project does not use. Labels made by hand and not listed are left.
#   .github/scripts/labels.sh [owner/repo]
set -euo pipefail
repo=${1:-kawalain/crpp}

# name|colour|description
labels=(
  'crash|b60205|앱이 비정상 종료함 (The app ended abnormally)'
  'bug|d73a4a|잘못 동작함 (Something does not work as it should)'
  'enhancement|a2eeef|새 기능이나 개선 제안 (A new feature or a change)'
  'question|d876e3|질문 (A question)'
  'area: room|1d76db|방, 멤버, 채팅 (Rooms, members, chat)'
  'area: sound|1d76db|입력 소리, 사운드 팩, 장치 (Input sounds, packs, devices)'
  'area: skin|1d76db|스킨, 테마, 화면 (Skins, Themes, screens)'
  'area: update|1d76db|업데이트와 릴리스 (Updates and Releases)'
  'area: account|1d76db|계정, 게스트, 서버 선택 (Accounts, guests, servers)'
  'area: install|1d76db|저장소와 다운로드 (Repositories and downloads)'
  'needs-info|fbca04|더 알려 주셔야 해요 (Waiting for more information)'
  'upstream|c5def5|원본 서비스나 서버의 문제 (A matter of the upstream service or server)'
)
# GitHub's defaults that are not in the list above.
unused=('documentation' 'duplicate' 'good first issue' 'help wanted' 'invalid' 'wontfix')

for entry in "${labels[@]}"; do
  IFS='|' read -r name colour description <<< "$entry"
  gh label create "$name" --repo "$repo" --color "$colour" --description "$description" --force
done
existing=$(gh label list --repo "$repo" --limit 200 --json name --jq '.[].name')
for name in "${unused[@]}"; do
  if grep -qxF "$name" <<< "$existing"; then gh label delete "$name" --repo "$repo" --yes; fi
done
