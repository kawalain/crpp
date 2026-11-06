# Writes the signed update manifest for one exe. See src/update/include/crpp/update/update.hpp
# for the format. The private key is a PEM read from the environment, never from the
# command line, so it stays out of process lists and logs. Needs PowerShell 7 (.NET 5+).
param(
  [Parameter(Mandatory)][string]$Exe,
  [Parameter(Mandatory)][string]$Version,
  [Parameter(Mandatory)][string]$Out
)
$ErrorActionPreference = 'Stop'

if ($Version -notmatch '^0\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') { throw "not a 0.y.z version: $Version" }
$pem = $env:CRPP_UPDATE_SIGNING_KEY
if ([string]::IsNullOrWhiteSpace($pem)) { throw 'CRPP_UPDATE_SIGNING_KEY is not set' }

$file = Get-Item -LiteralPath $Exe
$hash = (Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash.ToLowerInvariant()
$body = "crpp-update 1`nversion $Version`nasset $($file.Name)`nsize $($file.Length)`nsha256 $hash`n"

$ecdsa = [System.Security.Cryptography.ECDsa]::Create()
try {
  $ecdsa.ImportFromPem($pem)
  if ($ecdsa.KeySize -ne 256) { throw "the key has $($ecdsa.KeySize) bits, not 256" }
  # .NET's default format here is r || s, which is what BCrypt verifies.
  $signature = $ecdsa.SignData([System.Text.Encoding]::UTF8.GetBytes($body), [System.Security.Cryptography.HashAlgorithmName]::SHA256)
} finally {
  $ecdsa.Dispose()
}
$hex = [System.Convert]::ToHexString($signature).ToLowerInvariant()
[System.IO.File]::WriteAllText($Out, "$($body)signature $hex`n", [System.Text.UTF8Encoding]::new($false))
