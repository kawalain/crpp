# Signs a Repository's catalog in place: appends the line whose signature covers every byte
# before it. See src/assets/include/crpp/assets/catalog.hpp for the format. The private key
# is a PEM read from the environment, never from the command line, so it stays out of
# process lists and logs, and it must be the key the catalog names. Needs PowerShell 7.
param(
  [Parameter(Mandatory)][string]$Catalog
)
$ErrorActionPreference = 'Stop'

$pem = $env:CRPP_UPDATE_SIGNING_KEY
if ([string]::IsNullOrWhiteSpace($pem)) { throw 'CRPP_UPDATE_SIGNING_KEY is not set' }

$path = (Resolve-Path -LiteralPath $Catalog).Path
$bytes = [System.IO.File]::ReadAllBytes($path)
$text = [System.Text.Encoding]::UTF8.GetString($bytes)
if (-not $text.StartsWith("crpp-catalog 1`n")) { throw "$Catalog is not a catalog of format 1" }
if ($text.Contains("`nsignature ")) { throw "$Catalog is signed already" }
$named = ($text -split "`n")[1]

$ecdsa = [System.Security.Cryptography.ECDsa]::Create()
try {
  $ecdsa.ImportFromPem($pem)
  if ($ecdsa.KeySize -ne 256) { throw "the key has $($ecdsa.KeySize) bits, not 256" }
  $point = $ecdsa.ExportParameters($false).Q
  $public = [System.Convert]::ToHexString([byte[]]($point.X + $point.Y)).ToLowerInvariant()
  if ($named -ne "key $public") { throw 'the catalog names another key than the one signing it' }
  # .NET's default format here is r || s, which is what BCrypt verifies.
  $signature = $ecdsa.SignData($bytes, [System.Security.Cryptography.HashAlgorithmName]::SHA256)
} finally {
  $ecdsa.Dispose()
}
$hex = [System.Convert]::ToHexString($signature).ToLowerInvariant()
[System.IO.File]::AppendAllText($path, "signature $hex`n", [System.Text.UTF8Encoding]::new($false))
