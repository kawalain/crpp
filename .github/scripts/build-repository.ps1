# Fills a folder with the official Repository (ADR 0008): every file its catalog lists, at its
# installed path, and the catalog, signed with the update key. wrangler.jsonc serves that
# folder as it is. Needs PowerShell 7 and a build of crpp_catalog.
#
#   .github/scripts/build-repository.ps1 -Version 0.y.z [-Out dist/repository]
#       [-Tool build-msvc/src/assets/crpp_catalog.exe] [-Cache .cache/remote-assets]
#
# -Ref is where raw.githubusercontent.com serves this repository's files as the second
# source; it defaults to the release's tag. The third-party files come from the cache a
# development build fills, and each is checked against its pinned hash.
param(
  [Parameter(Mandatory)][string]$Version,
  [string]$Out = 'dist/repository',
  [string]$Tool = 'build-msvc/src/assets/crpp_catalog.exe',
  [string]$Cache = '.cache/remote-assets',
  [string]$Ref = "v$Version",
  [string]$KeyFile = (Join-Path $HOME '.crpp-signing/crpp-update-signing.pem'),
  [string]$PublicKey = (Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot '../../src/update/release_public_key.txt')).Trim()
)
$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '../..')
try {

if ($Version -notmatch '^0\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') { throw "not a 0.y.z version: $Version" }
if ($PublicKey -notmatch '^[0-9a-f]{128}$') { throw 'the public key is not 128 lowercase hex digits' }
# What raw.githubusercontent.com serves at the tag is what is committed, so the files here must be too.
$dirty = git status --porcelain -- assets skins cmake/remote_assets.txt
if ($dirty) { throw "assets/, skins/ or cmake/remote_assets.txt differ from the commit:`n$dirty" }

if (Test-Path -LiteralPath $Out) { Remove-Item -LiteralPath $Out -Recurse -Force }
& $Tool $Out --remote cmake/remote_assets.txt --assets assets --skins skins --ref $Ref --default-skin cloud `
  --key $PublicKey --copy $Cache
if ($LASTEXITCODE -ne 0) { throw "crpp_catalog failed ($LASTEXITCODE)" }

$env:CRPP_UPDATE_SIGNING_KEY = Get-Content -Raw -LiteralPath $KeyFile
try {
  & (Join-Path $PSScriptRoot 'sign-catalog.ps1') -Catalog (Join-Path $Out 'catalog.txt')
} finally {
  Remove-Item Env:CRPP_UPDATE_SIGNING_KEY
}
# As the program will check it.
& $Tool --verify (Join-Path $Out 'catalog.txt') --key $PublicKey
if ($LASTEXITCODE -ne 0) { throw 'the signed catalog does not verify' }
$files = Get-ChildItem -LiteralPath $Out -Recurse -File
"$($files.Count) files, $([math]::Round(($files | Measure-Object Length -Sum).Sum / 1MB, 1)) MB in $Out"
} finally {
  Pop-Location
}
