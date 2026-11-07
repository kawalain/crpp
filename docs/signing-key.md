# The update signing key

A release is `crpp.exe` plus `crpp-update.manifest`, which names the version, size and
SHA-256 of the exe and is signed with ECDSA P-256. The app holds only the public key and
verifies with BCrypt, so whoever controls the download cannot change what gets installed.

Until a real key exists, the repository holds `PLACEHOLDER` in
`src/update/release_public_key.txt`. Such an app refuses all updates, and an official
build (`-DCRPP_OFFICIAL_BUILD=ON`) fails to configure.

## Setting it up

Run the wizard from the repository root in Git Bash. It stops for a yes before each step
that changes something and touches GitHub only in the step that says so:

```
bash docs/signing-key-wizard.sh
```

The same steps by hand:

1. Make the key, outside the repository, and back it up offline:
   `openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out crpp-update-signing.pem`
2. Give it to GitHub: `gh secret set CRPP_UPDATE_SIGNING_KEY < crpp-update-signing.pem`
3. Put the public key in the app. It is the last 64 bytes of the DER public key, as hex:
   `openssl pkey -in crpp-update-signing.pem -pubout -outform DER | tail -c 64 | od -An -tx1 | tr -d ' \n' > src/update/release_public_key.txt`
4. Commit that file and push. Commits before it can never verify a release.

## Rules

- The private key never enters the repository, a log, or a chat.
- `.github/scripts/release.ps1` signs with the key in `~/.crpp-signing`, where the wizard
  puts it; the release workflow signs with the secret. Both verify each signed manifest
  against the key built into that build before publishing, so a key that does not match
  stops the release.
- `CRPP_UPDATE_TEST_KEY` exists for trying the flow on a local server. An official build
  refuses it.

## If the key is lost or leaks

The app trusts exactly one key. Installed copies can move to a new key only through a
release signed with the old one, so:

- **Lost:** installed copies can no longer update themselves. Make a new key, and users
  must download the new exe by hand once.
- **Leaked:** anyone holding it and able to serve GitHub's responses can install code. If
  you still have it, ship a release with the new public key at once, signed with the old
  key; then replace the secret.
