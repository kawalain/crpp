#!/usr/bin/env bash
# Fills a folder with the official Repository (ADR 0008): every file its catalog lists, at its
# installed path, the catalog signed with the update key, and the _redirects file. wrangler.jsonc
# serves that folder as it is.
#
#   .github/scripts/build-repository.sh <version> [--out dist/repository]
#       [--tool build-msvc/src/assets/crpp_catalog.exe] [--cache .cache/remote-assets]
#       [--ref v<version>] [--key ~/.crpp-signing/crpp-update-signing.pem]
#
# --ref is where raw.githubusercontent.com serves this repository's files as the second
# source; it defaults to the release's tag. The third-party files come from the cache a
# development build fills, and each is checked against its pinned hash.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
cd "$here/../.."

[[ $# -ge 1 ]] || { echo "usage: $0 <version> [options]" >&2; exit 2; }
version=$1
shift
out=dist/repository tool=build-msvc/src/assets/crpp_catalog.exe cache=.cache/remote-assets
ref="v$version" key="$HOME/.crpp-signing/crpp-update-signing.pem"
while [[ $# -gt 0 ]]; do
  case $1 in
    --out) out=$2 ;; --tool) tool=$2 ;; --cache) cache=$2 ;; --ref) ref=$2 ;; --key) key=$2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift 2
done

[[ $version =~ ^0\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]] || { echo "not a 0.y.z version: $version" >&2; exit 1; }
public=$(tr -d '\r\n' < src/update/release_public_key.txt)
[[ $public =~ ^[0-9a-f]{128}$ ]] || { echo 'the public key is not 128 lowercase hex digits' >&2; exit 1; }
# What raw.githubusercontent.com serves at the tag is what is committed, so the files here must be too.
dirty=$(git status --porcelain -- assets skins cmake/remote_assets.txt)
[[ -z $dirty ]] || { printf 'assets/, skins/ or cmake/remote_assets.txt differ from the commit:\n%s\n' "$dirty" >&2; exit 1; }

rm -rf "$out"
"$tool" "$out" --remote cmake/remote_assets.txt --assets assets --skins skins --ref "$ref" --default-skin cloud \
  --key "$public" --copy "$cache"
# The bare address, which someone may open in a browser, shows the source. Only that path: a rule
# here wins over a file at the same path, so a wider one would take the files with it.
printf '/ https://github.com/kawalain/crpp 302\n' > "$out/_redirects"

"$here/sign-catalog.sh" "$out/catalog.txt" "$key"
# As the program will check it.
"$tool" --verify "$out/catalog.txt" --key "$public"
echo "$(find "$out" -type f | wc -l) files, $(du -sm "$out" | cut -f1) MB in $out"
