#!/usr/bin/env bash
# Publishes the exe, manifest and notes in dist/ as a GitHub Release of an existing tag. The
# first line of the notes, if it is "# Title", becomes the release's name.
#   .github/scripts/publish-release.sh <tag>
set -euo pipefail
[[ $# == 1 ]] || { echo "usage: $0 <tag>" >&2; exit 2; }
tag=$1
title=$tag
first=$(head -n 1 dist/release-notes.md)
if [[ $first =~ ^#\ (.+)$ ]]; then
  title=${BASH_REMATCH[1]}
  tail -n +2 dist/release-notes.md > dist/notes.md
else
  cp dist/release-notes.md dist/notes.md
fi
gh release create "$tag" dist/crpp.exe dist/crpp-update.manifest --verify-tag --latest --title "$title" --notes-file dist/notes.md
