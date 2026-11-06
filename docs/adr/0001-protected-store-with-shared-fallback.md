# Protected store with a shared fallback

Upstream keeps login tokens, guest IDs and resume keys in plain text and only obfuscates its chat cache, so we keep our own **Protected store** in `%LOCALAPPDATA%\crpp\` while still letting existing users share upstream's files. At startup, an existing Protected store always wins, even if upstream files exist; otherwise an existing `%LOCALAPPDATA%\Cloudroom\settings.json` selects the **Shared store**; otherwise a new Protected store is created.

The Protected store encrypts `token`, `gid`, `resumeKey`, `roomTicket`, `seatReturns` and the chat cache with DPAPI; other settings stay plain. This protects against other accounts and stolen disks, but not against malware running as the same Windows user. Data that fails to decrypt, such as a copy from another PC, counts as absent, so the user signs in again.

## Consequences

- In the Shared store, the file format follows upstream's and unknown keys are preserved.
- Every launch in the Shared store shows a non-blocking warning with a button that converts to the Protected store. Conversion copies the data, then separately asks whether to erase the secrets from upstream's file, which signs upstream's client out.
- The old Roaming location is not read.
