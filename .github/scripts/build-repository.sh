#!/usr/bin/env bash
# Fills the official Repository (ADR 0010, ADR 0011) in <github>, a checkout of
# kawalain/crpp-repository, which the exe reads: this release's signed catalog-<format>.txt and every
# file it lists, once, named by its SHA-256. Files it no longer lists are removed; git keeps them in
# its history.
#
#   .github/scripts/build-repository.sh <version> [--github dist/github]
#       [--tool build-msvc/src/assets/crpp_catalog.exe] [--cache .cache/remote-assets]
#       [--key ~/.crpp-signing/crpp-update-signing.pem] [--public <hex>]
#
# --public names another key than the release key, with --key its private one, to try the result
# with a build of -DCRPP_UPDATE_TEST_KEY.
#
# The third-party files come from the cache a development build fills, each checked against its hash.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
cd "$here/../.."

[[ $# -ge 1 ]] || { echo "usage: $0 <version> [options]" >&2; exit 2; }
version=$1
shift
github=dist/github tool=build-msvc/src/assets/crpp_catalog.exe cache=.cache/remote-assets
key="$HOME/.crpp-signing/crpp-update-signing.pem" public=''
while [[ $# -gt 0 ]]; do
  case $1 in
    --github) github=$2 ;; --tool) tool=$2 ;; --cache) cache=$2 ;; --key) key=$2 ;; --public) public=$2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift 2
done
fail() { echo "$*" >&2; exit 1; }

[[ $version =~ ^0\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]] || fail "not a 0.y.z version: $version"
[[ -n $public ]] || public=$(tr -d '\r\n' < src/update/release_public_key.txt)
[[ $public =~ ^[0-9a-f]{128}$ ]] || fail 'the public key is not 128 lowercase hex digits'
[[ -d $github/.git ]] || fail "$github is not a checkout of the Repository's GitHub repository"
# The format this exe reads its packs in, and the name of its catalog.
format=$(sed -n 's/^inline constexpr int pack_format = \([0-9]*\);$/\1/p' src/skin/include/crpp/skin/pack.hpp)
[[ $format =~ ^[1-9][0-9]*$ ]] || fail 'pack_format was not found in src/skin/include/crpp/skin/pack.hpp'
catalog_name="catalog-$format.txt"

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
store="$scratch/store"

echo "Format $format, version $version"
"$tool" "$store" --remote cmake/remote_assets.txt --assets assets --skins skins \
  --default-skin cloud --key "$public" --copy "$cache"
mv "$store/catalog.txt" "$scratch/$catalog_name"
"$here/sign-catalog.sh" "$scratch/$catalog_name" "$key"
# As the program will check it.
"$tool" --verify "$scratch/$catalog_name" --key "$public"

# The GitHub folder: its catalog, the files and a word on what it is; what is no longer listed goes.
find "$github" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
cp "$store"/* "$github/"
cp "$scratch/$catalog_name" "$github/"
cat > "$github/README.md" << EOF
# Cloudroom++ Repository

The official Repository of [Cloudroom++](https://github.com/kawalain/crpp), written by its release
script. \`$catalog_name\` is the signed catalog of Skin Pack format $format; every file it lists is
here once, named by its SHA-256. Do not edit by hand: the next release replaces it.
EOF

echo "$(find "$github" -path "$github/.git" -prune -o -type f -print | wc -l) files in $github," \
  "$(du -sm --exclude=.git "$github" | cut -f1) MB"
