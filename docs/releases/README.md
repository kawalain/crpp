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

To release: commit the notes to `main`, push, then `git tag v0.y.z && git push origin v0.y.z`.
The key is described in `docs/signing-key.md`.
