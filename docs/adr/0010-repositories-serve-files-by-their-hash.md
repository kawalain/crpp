# Repositories serve files by their hash

This replaces, in ADR 0008, files served and installed at their paths, and in ADR 0009, a tree of files per catalog
address. Their other reasons hold: catalogs are signed, a Repository is known by its key, and each pack format of the
official Repository has a catalog of its own.

Each catalog address of the official Repository had a whole copy of the files, so every frozen format added one more
tree to upload, and a file that changed path was fetched again although its content had not. A catalog already gives
each file's SHA-256, so a file is now stored once under that hash, beside the catalogs that list it, and a file no
served catalog lists is not deployed. The official Repository serves files at `s/<sha256>` and its catalogs at
`s/catalog-<format>.txt`; any Repository serves `catalog.txt` and `<sha256>` files in one folder. The name is 64
lowercase hex digits, with no extension and no subfolders.

The app stores what it installs the same way: one folder of `<sha256>` files for every Repository, found through the
installed catalogs. Nothing installed is meant to be edited by hand, so nothing needs a readable name. Development
builds and `--skin-dir` still read the source trees by path, behind the same seam.

## Considered options

- 3xx redirects for the exes already out: their downloads refuse redirects (`WINHTTP_OPTION_REDIRECT_POLICY` set to
  never), so the frozen trees are served through `_redirects` rules with status 200, which Cloudflare's static assets
  answer with the target's content. One static rule per path of each frozen tree, generated from its catalog: about
  720 against a limit of 2,000, and later formats need none, as their catalogs are hash-laid already.
- Hash names on the server only, paths on disk: keeps the installed files readable, but every Repository's files stay
  apart and the index of installed hashes stays.
- Path-laid Repositories beside hash-laid ones: one less step for a pack author, two code paths for good. A Repository
  is built with `crpp_catalog` and can be pushed to a branch of its own, as a site is.
- Hashing every installed file at each start: a font alone is 13 MB of CPU work per start. Files are hashed when
  fetched; a start checks that each is there with the catalog's size.

## Consequences

- The catalog grammar is `crpp-catalog 2`. An exe that meets 1 at an added Repository says it must be rebuilt.
- The frozen catalogs (`catalog.txt` for format 1 and `2/catalog.txt` for v0.3.0) are kept as files, byte for byte,
  and their files count as used, so the exes that read them keep working, and get nothing new.
- A client that read a catalog one release ago may meet a removed file; it falls back to the second source, which is
  still each file's path at the release's tag, and follows the latest catalog at its next start.
- An update from a path-laid install moves each file whose size and hash match to its hash name once, so the font is
  not fetched again.
- Unused files are removed at start, before any is opened, by the union of every installed catalog, so a font held
  for the next start is simply a second hash until then.
