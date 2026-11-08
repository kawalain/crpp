#!/usr/bin/env bash
# Fills a folder with the official Repository (ADR 0008): every file its catalog lists, at its
# installed path, the catalog signed with the update key, and the _redirects file. wrangler.jsonc
# serves that folder as it is.
#
# Each pack format has a catalog of its own (ADR 0009). The one the exe is built for goes in
# <out>/<format>/, or <out> itself for format 1. Every older format is rebuilt from the last tag
# that wrote it, as exes of that format still read it, and must give the catalog it gave then.
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

# The format this exe reads its packs in, and where its catalog is served.
format=$(sed -n 's/^inline constexpr int pack_format = \([0-9]*\);$/\1/p' src/skin/include/crpp/skin/pack.hpp)
[[ $format =~ ^[1-9][0-9]*$ ]] || { echo 'pack_format was not found in src/skin/include/crpp/skin/pack.hpp' >&2; exit 1; }
folder_of() { if [[ $1 == 1 ]]; then echo "$out"; else echo "$out/$1"; fi; }

# The older formats: <format> <last tag of it> <sha256 of its catalog before the signature>.
# A rebuild that gives another catalog would hand those exes files they never had.
frozen=(
  '1 v0.2.0 992c51d3372b0ca5361144f54a4ed6d526dfa0609b33f822993c3a3b46fa01e6'
)

rm -rf "$out"
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
folders=()
# The older formats first, so nothing of this one's is rebuilt over.
for entry in "${frozen[@]}"; do
  read -r old_format old_tag old_hash <<< "$entry"
  [[ $old_format != "$format" ]] || { echo "format $format is frozen at $old_tag, but this exe writes it" >&2; exit 1; }
  echo "Format $old_format, frozen at $old_tag"
  rm -rf "$scratch/tree" && mkdir -p "$scratch/tree"
  git archive "$old_tag" assets skins cmake/remote_assets.txt | tar -x -C "$scratch/tree"
  [[ $(git show "$old_tag:src/update/release_public_key.txt" | tr -d '\r\n') == "$public" ]] ||
    { echo "$old_tag was signed with another key" >&2; exit 1; }
  dest=$(folder_of "$old_format")
  "$tool" "$dest" --remote "$scratch/tree/cmake/remote_assets.txt" --assets "$scratch/tree/assets" \
    --skins "$scratch/tree/skins" --ref "$old_tag" --default-skin cloud --key "$public" --copy "$cache"
  got=$(sha256sum < "$dest/catalog.txt")
  [[ ${got%% *} == "$old_hash" ]] || { echo "format $old_format rebuilt from $old_tag gives another catalog" >&2; exit 1; }
  folders+=("$dest")
done
echo "Format $format, at $ref"
"$tool" "$(folder_of "$format")" --remote cmake/remote_assets.txt --assets assets --skins skins --ref "$ref" \
  --default-skin cloud --key "$public" --copy "$cache"
folders+=("$(folder_of "$format")")

# Signed only once every catalog is as it should be.
for folder in "${folders[@]}"; do
  "$here/sign-catalog.sh" "$folder/catalog.txt" "$key"
  # As the program will check it.
  "$tool" --verify "$folder/catalog.txt" --key "$public"
done

# The bare address, which someone may open in a browser, shows the source. Only that path: a rule
# here wins over a file at the same path, so a wider one would take the files with it.
printf '/ https://github.com/kawalain/crpp 302\n' > "$out/_redirects"
echo "$(find "$out" -type f | wc -l) files, $(du -sm "$out" | cut -f1) MB in $out"
