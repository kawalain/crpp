# Downloads come from Repositories, signed catalogs anyone can host

This replaces where files come from in ADR 0002 and ADR 0004, and the list of files built into the exe. Their other
reasons hold: files are fetched one by one at first run, each is checked by its hash, and nothing but a Skin Pack is
required (ADR 0007). It also changes two points of ADR 0007: the official catalog is signed with the update key, and
packs may come from Repositories other than the official one.

A first run stood at 0% for a long time: this repository's files were pinned to a commit that was not on GitHub yet,
so jsDelivr answered 404 for each of them before the font, 85% of the bytes, started. A list built into the exe ties
every download to a commit and every change to a release. Now everything the program downloads, Skin Packs, Sound
packs, emoji, word lists and fonts, is listed in the signed catalog of a Repository, and the exe holds only the
official Repository's address and key.

The official Repository is static files on Cloudflare Workers at `crpp.kawala.in`, a custom domain (a two-line Worker
sends any path that is no file to the source repository),
deployed with Wrangler from the developer's machine by the release script, which also signs the catalog. Requests to
static files there are free and unlimited, and a file may be up to 25 MiB. When it is blocked, the same files come from
`raw.githubusercontent.com` at the release's tag; the repository is public.

A user may add other Repositories in the system tab, by an HTTPS address of a folder holding `catalog.txt` or by
`github:owner/repo@ref`. Their catalogs must be signed too: the app shows the fingerprint of the key a catalog states,
remembers it, and refuses the Repository if it changes. Unsigned ones are refused, and a small tool writes and signs a
catalog for anyone who hosts one.

## Considered options

- Keep jsDelivr in front of GitHub: free and cached for good, but only for pushed commits, and still a list in the exe.
- A separate key for catalogs, as ADR 0007 had it: a leaked catalog key could not sign an exe. Both keys would sit on
  the same machine, signed by the same script, so the user chose one key.
- Unsigned or hash-only Repositories: simpler to host, but anyone between the user and the host could change a pack.
- Exe updates through the Repository too: they stay on GitHub Releases, whose notes are the changelog.

## Consequences

- A Repository other than the official one may offer only Skin Packs and Sound packs. Emoji, word lists and the shared
  fonts come from the official one, so the members of a Room see the same pictures.
- Packs are named by Repository and name, so two Repositories never collide. A member who lacks another's Sound pack
  hears the default pack for that device in its place. The official Repository comes first and cannot be removed.
- Files already installed whose hash matches the catalog are kept, so an update of v0.1.x fetches only what changed.
- The Pretendard and Twemoji licence texts travel with their files. The sounds keep the status they have in the public
  repository today.
- The program knows the custom domain, not the Worker, so the host can change without a release; the domain itself
  cannot.
- The release script, not GitHub, now decides what a first run fetches, so a first install works before a push. The
  second source needs the release tag on GitHub.
