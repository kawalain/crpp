#!/usr/bin/env bash
# Builds, signs and publishes a release from this machine, with the official Repository (ADR 0010).
# See docs/releases/README.md.
#   .github/scripts/release.sh <version> [--dry-run] [--notes <file>] [--key <key.pem>]
# --dry-run does everything up to the tag, and leaves the result in dist/. The private key is
# named by its file, never passed as text, and only the signing steps read it.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
cd "$here/../.."

[[ $# -ge 1 ]] || { echo "usage: $0 <version> [--dry-run] [--notes <file>] [--key <key.pem>]" >&2; exit 2; }
version=$1
shift
dry_run=0 notes='' key="$HOME/.crpp-signing/crpp-update-signing.pem"
while [[ $# -gt 0 ]]; do
  case $1 in
    --dry-run) dry_run=1; shift ;;
    --notes) notes=$2; shift 2 ;;
    --key) key=$2; shift 2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
fail() { echo "$*" >&2; exit 1; }
# Also named in wrangler.jsonc. No package.json pins it, as the build has no package manager.
wrangler=wrangler@4.159.0

tag="v$version"
[[ $version =~ ^0\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]] || fail "not a 0.y.z version: $version"
[[ -n $notes ]] || notes="docs/releases/$tag.md"
[[ -f $key ]] || fail "no signing key at $key; see docs/signing-key.md"
[[ $(tr -d '\r\n' < src/update/release_public_key.txt) != PLACEHOLDER ]] || fail 'the public key is still the placeholder'

echo '== Checking the tree'
[[ $(git branch --show-current) == main ]] || fail 'not on main'
[[ -z $(git status --porcelain) ]] || fail 'the tree is not clean, so the build would not match a commit'
git fetch --quiet origin main --tags || fail 'could not reach origin'
head=$(git rev-parse HEAD)
[[ $head == "$(git rev-parse origin/main)" ]] || fail 'HEAD is not origin/main: push first, so that the tag names a pushed commit'
tagged=$(git tag --list "$tag")
if [[ -n $tagged && $(git rev-parse "$tag^{commit}") != "$head" ]]; then fail "$tag already points at another commit"; fi
if gh release view "$tag" --json tagName >/dev/null 2>&1; then fail "$tag is already published"; fi

dist=dist
rm -rf "$dist"
"$here/release-notes.sh" --tag HEAD --source "$notes" --out "$dist/release-notes.md"

echo '== Building'
"$here/msvc-build.sh" build-release -DCRPP_FETCH_DEV_ASSETS=OFF -DCRPP_OFFICIAL_BUILD=ON "-DCRPP_VERSION=$version"

echo '== Testing'
# input.raw injects keys, so it fails whenever someone is using this machine's keyboard.
ctest --test-dir build-release --output-on-failure -E '^input\.raw$'

echo '== Signing'
built=build-release/apps/gui/crpp.exe
expected="$version+$(git rev-parse --short=7 HEAD)"
# The version resource holds it in UTF-16.
pattern=''
for byte in $(printf '%s' "$expected" | iconv -f UTF-8 -t UTF-16LE | od -An -v -tx1); do pattern+="\\x$byte"; done
LC_ALL=C grep -qaP "${pattern}\\x00\\x00" "$built" || fail "the exe does not say it is $expected"
cp "$built" "$dist/crpp.exe"
"$here/sign-release.sh" "$dist/crpp.exe" "$version" "$dist/crpp-update.manifest" "$key"

# Against the key built into this very build: a key file that does not match would otherwise
# strand every installed copy.
build-release/src/update/crpp_update_verify.exe "$dist/crpp-update.manifest" "$dist/crpp.exe" || fail 'the release does not verify'

echo '== Filling the Repository'
# The exe reads it from GitHub, and wrangler.jsonc serves dist/repository as its mirror.
repository_git=https://github.com/kawalain/crpp-repository
git clone --quiet --depth 1 "$repository_git" "$dist/github" ||
  fail "could not clone $repository_git; it must exist and be public"
"$here/build-repository.sh" "$version" --out "$dist/repository" --github "$dist/github"   --tool build-release/src/assets/crpp_catalog.exe --key "$key"

if [[ $dry_run == 1 ]]; then
  echo "== Dry run: nothing was tagged or published. The result is in $dist/."
  exit 0
fi

echo '== Publishing'
# The tag, then the Repository and its mirror, then the Release, so the new exe never starts against
# the files of the one before.
[[ -n $tagged ]] || git tag "$tag"
git push origin "$tag" || fail 'pushing the tag failed'
git -C "$dist/github" add -A
git -C "$dist/github" diff --cached --quiet || git -C "$dist/github" commit --quiet -m "chore: the Repository of $tag"
git -C "$dist/github" push --quiet origin HEAD:refs/heads/main || fail 'pushing the Repository failed; run this again to retry'
npx --yes "$wrangler" deploy || fail 'deploying the mirror failed; run this again to retry'
"$here/publish-release.sh" "$tag"
gh release view "$tag" --json url --jq .url
