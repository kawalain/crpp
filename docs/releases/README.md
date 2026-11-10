# Release notes

The GitHub Release's notes are the app's changelog, and the app opens them when it first
notices a new version. They have two parts:

- The file `v<version>.md` here, written by hand: the most important changes come first,
  under `## 주요 변경 사항` with at least one `-` item. The release workflow refuses a tag
  without it.
- The commits since the previous version's tag that someone using the app can notice, by kind
  (`feat`, `fix`, `perf`), added by `.github/scripts/release-notes.sh`. Other kinds are left
  out, and so are the scopes of building and delivering the app (`skin`, `assets`,
  `repository`, `release` and the like; the script lists them): what those bring a user goes
  under the most important changes. A commit's subject is its line in the changelog, so write
  it for someone reading that.

Details:

- A first line `# Title` becomes the release's name, which the app shows large. Without
  it the name is the tag and only the date and version are shown.
- The app draws headings, `-`/`*`/numbered lists with nesting, paragraphs and `**bold**`.
  Italics, `code` and links show as plain text. Other Markdown shows as it is written.

Notes can be edited on GitHub later; the app picks that up at its next check.

To look at the result first, as the app will draw it:

```bash
.github/scripts/release-notes.sh --tag HEAD --source docs/releases/v0.y.z.md --out out/notes.md
crpp --shot out/notes.png 951x692 12 changelog=out/notes.md --data out/data
```

Before a release, check that every tool is on its latest stable release: MSVC Build Tools,
CMake, Ninja, the GCC image and runner labels in `.github/workflows/`, `actions/checkout`,
Wrangler in `release.sh`, and the third-party files in `cmake/remote_assets.txt`. A release
candidate, snapshot or preview does not count.

To release: commit the notes to `main`, push, then run this in Git Bash:

```bash
.github/scripts/release.sh 0.y.z
```

It refuses a tree that differs from `origin/main`, then builds an official exe in
`build-release`, tests it, signs it with the key in `~/.crpp-signing` (see
`docs/signing-key.md`) and verifies the result against the key built into it. It clones the
official Repository, the public GitHub repository `kawalain/crpp-repository`, into `dist/github`
and fills it through `.github/scripts/build-repository.sh` (ADR 0010): this release's
`catalog-<format>.txt`, signed with the same key, and every file a served catalog lists, once,
named by its SHA-256, from `assets/`, `skins/` and the third-party files a development build
caches in `.cache/remote-assets`. `dist/repository` is the Worker's half: the same files under
`s/`, its mirror, and the catalogs that v0.2.x and v0.3.0 read, kept in `.github/frozen-catalogs/`
as they were signed, answered from `s/` by `_redirects`. Each pack format has its own catalog (ADR
0009); when a release raises `pack_format`, its catalog gets the new number and the old one
stays only while `build-repository.sh` keeps listing its files. Then it pushes the tag, commits
and pushes `dist/github`, deploys the mirror with `npx --yes wrangler@4.159.0 deploy`
(`wrangler.jsonc`, your own `CLOUDFLARE_API_TOKEN` or `wrangler login`) and publishes the
Release. `--dry-run` stops before the tag and leaves the result in `dist/`. A run that fails
after the tag can be run again: it rebuilds, deploys and publishes what is missing.

GitHub Actions no longer runs by itself, as its minutes are limited. Nothing builds on a push
or a tag, so GCC's portability check runs only on `gh workflow run ci.yml`; do that before a
release that changed a pure module. `gh workflow run release.yml --ref v0.y.z` publishes an
existing tag from a runner instead; it does not deploy the Repository.

## If Defender flags a release

The first v0.1.1 exe was flagged as `Trojan:Win32/Bearfoos.A!ml` the moment it started after
updating, and Defender removed it. From then on a plain scan flagged those exact bytes. 16
other builds passed, including the same commit, and two self-updates through a local server
did too. An exe rebuilt from the same tag then updated from v0.1.0 without a detection. So
the verdict followed that one file, not the code; what caused it is unknown, and a scan
before publishing does not predict it.

After publishing, update from the previous version once and watch for a minute. If it is
flagged, rebuild from the tag, sign and verify as `release.sh` does, and replace the files
with `gh release upload v0.y.z dist/crpp.exe dist/crpp-update.manifest --clobber`. The new
exe's hash must differ. Until then an update can cost someone their copy of the app, because
the rollback ends once the new exe has started.
