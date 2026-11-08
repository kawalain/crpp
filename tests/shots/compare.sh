#!/usr/bin/env bash
# Compares two folders from render.sh by file hash, and says which scenes differ.
#   tests/shots/compare.sh <before> <after>
set -euo pipefail
[[ $# == 2 ]] || { echo "usage: $0 <before> <after>" >&2; exit 2; }
before=$1 after=$2
scenes=0 different=0
for scene in "$before"/*.png; do
  [[ -e $scene ]] || continue
  name=$(basename "$scene")
  scenes=$((scenes + 1))
  if [[ ! -e $after/$name ]]; then
    echo "missing  $name"; different=$((different + 1))
  elif [[ $(sha256sum < "$scene") != $(sha256sum < "$after/$name") ]]; then
    echo "differs  $name"; different=$((different + 1))
  fi
done
echo "$scenes scenes, $different differ"
[[ $different == 0 ]]
