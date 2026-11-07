# Release notes

The GitHub Release's notes are the app's changelog, and the app opens them when it first
notices a new version. They have two parts:

- The file `v<version>.md` here, written by hand: the most important changes come first,
  under `## 주요 변경 사항` with at least one `-` item. The release workflow refuses a tag
  without it.
- The commits since the previous version's tag, by kind (`feat`, `fix`, `perf`, `refactor`,
  `docs`, the rest), added by `.github/scripts/release-notes.ps1`. A commit's subject is
  its line in the changelog, so write it for someone reading that.

Details:

- A first line `# Title` becomes the release's name, which the app shows large. Without
  it the name is the tag and only the date and version are shown.
- The app draws headings, `-`/`*`/numbered lists with nesting, paragraphs and `**bold**`.
  Italics, `code` and links show as plain text. Other Markdown shows as it is written.

Notes can be edited on GitHub later; the app picks that up at its next check.

To look at the result first, as the app will draw it:

```
.github/scripts/release-notes.ps1 -Tag HEAD -Source docs/releases/v0.y.z.md -Out out/notes.md
crpp --shot out/notes.png 951x692 12 changelog=out/notes.md --data out/data
```

To release: commit the notes to `main`, push, then run this from PowerShell 7:

```
.github/scripts/release.ps1 -Version 0.y.z
```

It refuses a tree that differs from `origin/main`, then builds an official exe in
`build-release`, tests it, signs it with the key in `~/.crpp-signing` (see
`docs/signing-key.md`), verifies the result against the key built into it, pushes the tag and
publishes. `-DryRun` stops before the tag and leaves the result in `dist/`.

GitHub Actions no longer runs by itself, as its minutes are limited. Nothing builds on a push
or a tag, so GCC's portability check runs only on `gh workflow run ci.yml`; do that before a
release that changed a pure module. `gh workflow run release.yml --ref v0.y.z` publishes an
existing tag from a runner instead.

## If Defender flags a release

The first v0.1.1 exe was flagged as `Trojan:Win32/Bearfoos.A!ml` the moment it started after
updating, and Defender removed it. From then on a plain scan flagged those exact bytes. 16
other builds passed, including the same commit, and two self-updates through a local server
did too. An exe rebuilt from the same tag then updated from v0.1.0 without a detection. So
the verdict followed that one file, not the code; what caused it is unknown, and a scan
before publishing does not predict it.

After publishing, update from the previous version once and watch for a minute. If it is
flagged, rebuild from the tag, sign and verify as `release.ps1` does, and replace the files
with `gh release upload v0.y.z dist/crpp.exe dist/crpp-update.manifest --clobber`. The new
exe's hash must differ. Until then an update can cost someone their copy of the app, because
the rollback ends once the new exe has started.
