# Builds, signs and publishes a release from this machine. See docs/releases/README.md.
#   .github/scripts/release.ps1 -Version 0.1.1
# -DryRun does everything up to the tag, and leaves the result in dist/. The private key is
# read from -KeyFile, never from the command line, and is given only to the signing script.
param(
  [Parameter(Mandatory)][string]$Version,
  [string]$KeyFile = (Join-Path $HOME '.crpp-signing/crpp-update-signing.pem'),
  [string]$Notes,
  [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '../..')

$tag = "v$Version"
if ($Version -notmatch '^0\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') { throw "not a 0.y.z version: $Version" }
if (-not $Notes) { $Notes = "docs/releases/$tag.md" }
if (-not (Test-Path -LiteralPath $KeyFile)) { throw "no signing key at $KeyFile; see docs/signing-key.md" }
if ((Get-Content -Raw src/update/release_public_key.txt).Trim() -eq 'PLACEHOLDER') { throw 'the public key is still the placeholder' }

Write-Host '== Checking the tree'
if ((git branch --show-current) -ne 'main') { throw 'not on main' }
if (git status --porcelain) { throw 'the tree is not clean, so the build would not match a commit' }
git fetch --quiet origin main --tags
if ($LASTEXITCODE -ne 0) { throw 'could not reach origin' }
$head = git rev-parse HEAD
if ($head -ne (git rev-parse origin/main)) { throw 'HEAD is not origin/main: push first, as assets are pinned to pushed commits' }
$tagged = git tag --list $tag
if ($tagged -and (git rev-parse "$tag^{commit}") -ne $head) { throw "$tag already points at another commit" }
gh release view $tag --json tagName *> $null
if ($LASTEXITCODE -eq 0) { throw "$tag is already published" }
$global:LASTEXITCODE = 0

$dist = 'dist'
if (Test-Path $dist) { [IO.Directory]::Delete((Resolve-Path $dist).Path, $true) }
.github/scripts/release-notes.ps1 -Tag HEAD -Source $Notes -Out "$dist/release-notes.md"

Write-Host '== Building'
cmd /c ".github\scripts\msvc-build.cmd build-release `"-DCRPP_FETCH_DEV_ASSETS=OFF`" `"-DCRPP_OFFICIAL_BUILD=ON`" `"-DCRPP_VERSION=$Version`""
if ($LASTEXITCODE -ne 0) { throw 'the build failed' }

Write-Host '== Testing'
# input.raw injects keys, so it fails whenever someone is using this machine's keyboard.
ctest --test-dir build-release --output-on-failure -E '^input\.raw$'
if ($LASTEXITCODE -ne 0) { throw 'the tests failed' }

Write-Host '== Signing'
$built = 'build-release/apps/gui/crpp.exe'
$expected = "$Version+$((git rev-parse --short=7 HEAD))"
$says = (Get-Item $built).VersionInfo.ProductVersion
if ($says -ne $expected) { throw "the exe says $says, not $expected" }
Copy-Item $built "$dist/crpp.exe"
$env:CRPP_UPDATE_SIGNING_KEY = Get-Content -Raw -LiteralPath $KeyFile
try {
  .github/scripts/sign-release.ps1 -Exe "$dist/crpp.exe" -Version $Version -Out "$dist/crpp-update.manifest"
} finally {
  Remove-Item Env:CRPP_UPDATE_SIGNING_KEY
}

# Against the key built into this very build: a key file that does not match would otherwise
# strand every installed copy.
build-release/src/update/crpp_update_verify.exe "$dist/crpp-update.manifest" "$dist/crpp.exe"
if ($LASTEXITCODE -ne 0) { throw 'the release does not verify' }

if ($DryRun) {
  Write-Host "== Dry run: nothing was tagged or published. The result is in $dist/."
  return
}

Write-Host '== Publishing'
if (-not $tagged) { git tag $tag }
git push origin $tag
if ($LASTEXITCODE -ne 0) { throw 'pushing the tag failed' }
.github/scripts/publish-release.ps1 -Tag $tag
gh release view $tag --json url --jq .url
