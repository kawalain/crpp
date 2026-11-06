# Release notes

The file `v<version>.md` here becomes the GitHub Release's notes, and the app shows them
as the changelog. The release workflow refuses a tag without one.

- A first line `# Title` becomes the release's name, which the app shows large. Without
  it the name is the tag and only the date and version are shown.
- Upstream's sections fit: `## 변경 사항` and `## 버그 수정` with `-` items.
- The app draws headings, `-`/`*`/numbered lists with nesting, paragraphs and `**bold**`.
  Italics, `code` and links show as plain text. Other Markdown shows as it is written.

Notes can be edited on GitHub later; the app picks that up at its next check.

To release: commit the notes to `main`, push, then `git tag v0.y.z && git push origin v0.y.z`.
The first release needs the real key first: see `docs/signing-key.md`.
