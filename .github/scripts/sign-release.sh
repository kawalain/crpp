#!/usr/bin/env bash
# Writes the signed update manifest for one exe. See src/update/include/crpp/update/update.hpp
# for the format.
#   .github/scripts/sign-release.sh <crpp.exe> <version> <out manifest> <key.pem>
set -euo pipefail
here=$(dirname "$0")
[[ $# == 4 ]] || { echo "usage: $0 <exe> <version> <out> <key.pem>" >&2; exit 2; }
exe=$1 version=$2 out=$3 key=$4

[[ $version =~ ^0\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]] || { echo "not a 0.y.z version: $version" >&2; exit 1; }
hash=$(sha256sum < "$exe" | cut -d' ' -f1)
size=$(stat -c %s "$exe")
printf 'crpp-update 1\nversion %s\nasset %s\nsize %s\nsha256 %s\n' "$version" "$(basename "$exe")" "$size" "$hash" > "$out"
signature=$("$here/p256.sh" sign "$key" "$out")
printf 'signature %s\n' "$signature" >> "$out"
