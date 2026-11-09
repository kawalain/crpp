#!/usr/bin/env bash
# Signs a Repository's catalog in place: appends the line whose signature covers every byte
# before it. See src/assets/include/crpp/assets/catalog.hpp for the format. The key must be
# the one the catalog names. It prints the key's fingerprint, which the app shows to a user
# adding the Repository.
#   .github/scripts/sign-catalog.sh <catalog.txt> <key.pem>
set -euo pipefail
here=$(dirname "$0")
[[ $# == 2 ]] || { echo "usage: $0 <catalog.txt> <key.pem>" >&2; exit 2; }
catalog=$1 key=$2

[[ $(head -n 1 "$catalog") == 'crpp-catalog 2' ]] || { echo "$catalog is not a catalog of format 2" >&2; exit 1; }
if grep -q '^signature ' "$catalog"; then echo "$catalog is signed already" >&2; exit 1; fi
named=$(sed -n 2p "$catalog")
[[ $named == "key $("$here/p256.sh" public "$key")" ]] || { echo 'the catalog names another key than the one signing it' >&2; exit 1; }
signature=$("$here/p256.sh" sign "$key" "$catalog")
printf 'signature %s\n' "$signature" >> "$catalog"
# What the app shows a user who adds this Repository, to compare with what its host publishes.
fingerprint=$(printf '%s' "${named#key }" | xxd -r -p | sha256sum)
printf 'fingerprint %s\n' "${fingerprint%% *}"
