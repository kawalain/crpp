#!/usr/bin/env bash
# Fills the official Repository (ADR 0010) in two folders:
#
# - <github>, a checkout of kawalain/crpp-repository, which the exe reads: this release's signed
#   catalog-<format>.txt and every file any served catalog lists, once, named by its SHA-256.
#   Files no catalog lists any more are removed; git keeps them in its history.
# - <out>, which wrangler.jsonc serves on the Worker: the same files under s/, its mirror, and
#   the frozen catalogs of the exes before ADR 0010 at the addresses they know, with a
#   _redirects rule per file that serves its path from s/ with status 200. Those exes refuse
#   redirects, so a rule must answer with the file itself.
#
#   .github/scripts/build-repository.sh <version> [--out dist/repository] [--github dist/github]
#       [--tool build-msvc/src/assets/crpp_catalog.exe] [--cache .cache/remote-assets]
#       [--key ~/.crpp-signing/crpp-update-signing.pem] [--public <hex>]
#
# --public names another key than the release key, with --key its private one, to try the result
# with a build of -DCRPP_UPDATE_TEST_KEY; the frozen catalogs stay signed by the release key.
#
# The third-party files come from the cache a development build fills. A file of a frozen catalog
# the checkout no longer has comes from the tag its catalog names, or from its own source. Each is
# checked against its hash.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
cd "$here/../.."

[[ $# -ge 1 ]] || { echo "usage: $0 <version> [options]" >&2; exit 2; }
version=$1
shift
out=dist/repository github=dist/github tool=build-msvc/src/assets/crpp_catalog.exe cache=.cache/remote-assets
key="$HOME/.crpp-signing/crpp-update-signing.pem" public=''
while [[ $# -gt 0 ]]; do
  case $1 in
    --out) out=$2 ;; --github) github=$2 ;; --tool) tool=$2 ;; --cache) cache=$2 ;; --key) key=$2 ;; --public) public=$2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift 2
done
fail() { echo "$*" >&2; exit 1; }

[[ $version =~ ^0\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]] || fail "not a 0.y.z version: $version"
[[ -n $public ]] || public=$(tr -d '\r\n' < src/update/release_public_key.txt)
[[ $public =~ ^[0-9a-f]{128}$ ]] || fail 'the public key is not 128 lowercase hex digits'
[[ -d $github/.git ]] || fail "$github is not a checkout of the Repository's GitHub repository"
# The Worker's copy of the GitHub folder, which every catalog names as its mirror. Also in
# src/assets/CMakeLists.txt.
mirror=https://crpp.kawala.in/s/

# The format this exe reads its packs in, and the name of its catalog.
format=$(sed -n 's/^inline constexpr int pack_format = \([0-9]*\);$/\1/p' src/skin/include/crpp/skin/pack.hpp)
[[ $format =~ ^[1-9][0-9]*$ ]] || fail 'pack_format was not found in src/skin/include/crpp/skin/pack.hpp'
catalog_name="catalog-$format.txt"

# The catalogs of the exes before ADR 0010, as they were signed: <folder on the Worker> <file>
# <sha256 of the file>. Their exes read them at those folders and nothing else.
frozen=(
  '. .github/frozen-catalogs/1.txt faeb2d6e6adca780416eb9687b41b80b53b4943271de00ab76452c23b62d1939'
  '2 .github/frozen-catalogs/2.txt 39c4cb6ab85454cd860a29de8c22a0ff6a8d3562070658045291237b141e0588'
)
for entry in "${frozen[@]}"; do
  read -r _ file hash <<< "$entry"
  got=$(sha256sum < "$file")
  [[ ${got%% *} == "$hash" ]] || fail "$file is not the catalog that was signed"
done

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
store="$scratch/store"

echo "Format $format, version $version"
"$tool" "$store" --remote cmake/remote_assets.txt --assets assets --skins skins --mirror "$mirror" \
  --default-skin cloud --key "$public" --copy "$cache"
mv "$store/catalog.txt" "$scratch/$catalog_name"
"$here/sign-catalog.sh" "$scratch/$catalog_name" "$key"
# As the program will check it.
"$tool" --verify "$scratch/$catalog_name" --key "$public"

# Every file of the frozen catalogs too: from the checkout, which has them after the first time,
# or else from the tag or the source each names.
check() { local got; got=$(sha256sum < "$1"); [[ ${got%% *} == "$2" ]]; }
for entry in "${frozen[@]}"; do
  read -r _ file _ <<< "$entry"
  declare -A sources=()
  while read -r kind name url; do sources[$name]=$url; done < <(grep '^source ' "$file" | tr -d '\r')
  while read -r _ _ path _ sha from source remote _; do
    target="$store/$sha"
    [[ -e $target ]] && continue
    if [[ -f $github/$sha ]] && check "$github/$sha" "$sha"; then
      cp "$github/$sha" "$target"
      continue
    fi
    [[ $from == from && -n ${sources[$source]:-} ]] || fail "$path of $file has nowhere to come from"
    url=${sources[$source]}
    if [[ $url =~ ^https://raw\.githubusercontent\.com/kawalain/crpp/(v[0-9.]+)/$ ]]; then
      git show "${BASH_REMATCH[1]}:$remote" > "$target"
    elif [[ -f $cache/$path ]] && check "$cache/$path" "$sha"; then
      cp "$cache/$path" "$target"
    else
      curl -fsS --max-time 120 "$url$remote" -o "$target" || fail "could not fetch $url$remote"
    fi
    check "$target" "$sha" || { rm -f "$target"; fail "$path of $file is not the file it lists"; }
  done < <(grep '^file ' "$file" | tr -d '\r')
  unset sources
done

# The GitHub folder: its catalog, the files and a word on what it is; what is no longer listed goes.
find "$github" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
cp "$store"/* "$github/"
cp "$scratch/$catalog_name" "$github/"
cat > "$github/README.md" << EOF
# Cloudroom++ Repository

The official Repository of [Cloudroom++](https://github.com/kawalain/crpp), written by its release
script. \`$catalog_name\` is the signed catalog of Skin Pack format $format; every file it or an older
catalog lists is here once, named by its SHA-256. Do not edit by hand: the next release replaces it.
EOF

# The Worker: the same folder as the mirror, and the frozen catalogs with their files' paths.
rm -rf "$out"
mkdir -p "$out/s"
find "$github" -mindepth 1 -maxdepth 1 ! -name .git -exec cp -r {} "$out/s/" \;
# The bare address, which someone may open in a browser, shows the source.
printf '/ https://github.com/kawalain/crpp 302\n' > "$out/_redirects"
rules=0
for entry in "${frozen[@]}"; do
  read -r folder file _ <<< "$entry"
  mkdir -p "$out/$folder"
  cp "$file" "$out/$folder/catalog.txt"
  prefix=/
  [[ $folder == . ]] || prefix="/$folder/"
  while read -r _ _ path _ sha _; do
    printf '%s%s /s/%s 200\n' "$prefix" "$path" "$sha" >> "$out/_redirects"
    rules=$((rules + 1))
  done < <(grep '^file ' "$file" | tr -d '\r')
done
# Cloudflare takes at most 2,000 static rules (developers.cloudflare.com/workers/static-assets/redirects/).
[[ $rules -lt 2000 ]] || fail "$rules rules in _redirects, more than Cloudflare takes"
echo "$(find "$github" -path "$github/.git" -prune -o -type f -print | wc -l) files in $github," \
  "$(du -sm --exclude=.git "$github" | cut -f1) MB; $rules rules in $out/_redirects"
