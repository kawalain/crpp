# Download assets on first run

Shipping the font (13.4 MB), sound packs (2.1 MB) and emoji (0.3 MB) in every release is wasteful and the app is useless offline anyway, so a release contains only the exe, and the app fetches assets from `kawalain/crpp` at a pinned commit into `%LOCALAPPDATA%\crpp\assets\`. Upstream's repository is private, so it cannot be the source. Files are fetched one by one over HTTPS instead of as an archive, which avoids an archive parser, and each is checked against a list of path, size and SHA-256 built into the exe, so tampering with the repository cannot change what the app loads.

## Consequences

- The first run fetches every asset, because another member's Sound pack must be audible at once. Later versions fetch only files whose hash changed.
- Until the font arrives, the progress screen uses a system font. UI icons stay in the exe.
- A failed download shows a retry screen. With assets in place and no server, the app still opens and reports the connection failure.
