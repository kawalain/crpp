# Assets come through jsDelivr, and none of them is required

This replaces the source and the failure handling in ADR 0002; its reasons for fetching at first run still hold.

Twemoji and the font are third-party files that were copied into this repository only to be served from it, and they were 85% of what a first run fetched. They now come from their own repositories (`cmake/remote_assets.txt` pins them by size and SHA-256), and the rest from this repository, all through `cdn.jsdelivr.net/gh/`. A commit-pinned URL there is cached for good and needs no direct connection to GitHub. Pretendard is pinned by tag, because jsDelivr answers 403 for that large repository by commit; the hash pins it all the same. unpkg has the font but no current Twemoji SVGs, so it would be a fallback for part of the set only, and jsDelivr already fronts several CDNs, so there is one source and no fallback host.

With one host, its failure takes everything at once, so a failure of any asset no longer stops the app. The retry screen is gone: only a first start waits, on a progress screen, and later starts fetch what is missing behind the app, for the start after, because a network that blocks the host would otherwise delay every start by its time-outs.

## Consequences

- The system stands in for what is missing: Malgun Gothic for the font, Segoe UI Emoji for an emoji (or the default emoji where it has no single glyph, and such emoji leave the picker), and generated sounds for recordings.
- The manifest, not the disk, says what a pack contains, so a pack still fetching can be chosen and a sound a pack deliberately lacks stays silent.
- A notice appears once when the app starts relying on these, not at every start. The cause goes to the diagnostic log.
- A development build fetches the third-party files once into `.cache/remote-assets` at configure time, with the system's `curl`, and runs with the stand-ins if it cannot. Tests never need them.
- Files of this repository exist on jsDelivr only once their commit is on GitHub. Until then, a new install of a build from that commit runs on the stand-ins.
- If an upstream repository disappears, new installs lose its files until a release changes the source; installed ones keep working.
