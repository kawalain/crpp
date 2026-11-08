#!/usr/bin/env bash
# ECDSA P-256 with OpenSSL, in the form BCrypt checks: 128 lowercase hex digits.
#   p256.sh sign <key.pem> <file>   the signature of the file's bytes, over their SHA-256: r || s
#   p256.sh public <key.pem>        the public key: X || Y
# The private key is named by its file, never passed as text, so it stays out of process
# lists and logs. docs/signing-key.md says where it lives.
set -euo pipefail

hex_of() { od -An -v -tx1 | tr -d ' \n'; }

# A DER INTEGER may carry a leading zero byte or be shorter than 32 bytes.
as_32_bytes() {
  local value=${1,,}
  while [[ ${#value} -gt 64 && ${value:0:2} == 00 ]]; do value=${value:2}; done
  printf '%064s' "$value" | tr ' ' 0
}

check_key() {
  [[ -r $1 ]] || { echo "p256.sh: cannot read the key $1" >&2; exit 1; }
  openssl pkey -in "$1" -noout -text 2>/dev/null | grep -q 'prime256v1\|P-256' \
    || { echo "p256.sh: $1 is not a P-256 private key" >&2; exit 1; }
}

case ${1:-} in
  sign)
    [[ $# == 3 ]] || { echo "usage: $0 sign <key.pem> <file>" >&2; exit 2; }
    check_key "$2"
    mapfile -t parts < <(openssl dgst -sha256 -sign "$2" -binary "$3" | openssl asn1parse -inform DER | awk -F: '/INTEGER/ { print $NF }')
    [[ ${#parts[@]} == 2 ]] || { echo 'p256.sh: the signature is not two integers' >&2; exit 1; }
    echo "$(as_32_bytes "${parts[0]}")$(as_32_bytes "${parts[1]}")"
    ;;
  public)
    [[ $# == 2 ]] || { echo "usage: $0 public <key.pem>" >&2; exit 2; }
    check_key "$2"
    # The DER public key ends with the uncompressed point 04 || X || Y.
    openssl pkey -in "$2" -pubout -outform DER | tail -c 64 | hex_of
    echo
    ;;
  *)
    echo "usage: $0 sign <key.pem> <file> | public <key.pem>" >&2
    exit 2
    ;;
esac
