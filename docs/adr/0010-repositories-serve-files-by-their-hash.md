# Repositories serve files by their hash, and the official one is on GitHub

This replaces, in ADR 0008, files served and installed at their paths, the official Repository on Cloudflare Workers
and each file's second source at the release's tag; and in ADR 0009, a tree of files per catalog address. Their other
reasons hold: catalogs are signed, a Repository is known by its key, and each pack format of the official Repository
has a catalog of its own.

Each catalog address of the official Repository had a whole copy of the files, so every frozen format added one more
tree to upload, and a file that changed path was fetched again although its content had not. A catalog already gives
each file's SHA-256, so a file is now stored once under that hash, beside the catalogs that list it, and a file no
served catalog lists is not kept. Any Repository is one folder of `catalog.txt` and `<sha256>` files, 64 lowercase
hex digits with no extension; the official one has a catalog per pack format, `catalog-<format>.txt`.

From this version the official Repository is the public GitHub repository `kawalain/crpp-repository`, read through
`raw.githubusercontent.com`, which the release script commits to; git keeps each content once by itself. The Worker at
`crpp.kawala.in` stays for now as its mirror, the same folder at `s/`, and as the address the exes already out know.

The app stores what it installs the same way: one folder of `<sha256>` files for every Repository, found through each
Repository's list of installed paths and hashes. That list stays because a pack held back, a parent a pin refuses or
an unreadable format, keeps an older hash than the catalog. Nothing installed is meant to be edited by hand, so
nothing needs a readable name. Development builds and `--skin-dir` still read the source trees by path, behind the
same seam.

## Considered options

- 3xx redirects for the exes already out: their downloads refuse redirects (`WINHTTP_OPTION_REDIRECT_POLICY` set to
  never), so the Worker serves their frozen trees through `_redirects` rules with status 200, which Cloudflare's
  static assets answer with the target's content. One static rule per path of each frozen tree, generated from its
  catalog: about 720 against a limit of 2,000, and later formats need none, as their catalogs are hash-laid already.
- An orphan branch of `kawalain/crpp` for the official Repository: a plain clone of the source would fetch every
  font and sound the Repository ever had.
- Hash names on the server only, paths on disk: keeps the installed files readable, but every Repository's files stay
  apart.
- Path-laid Repositories beside hash-laid ones: one less step for a pack author, two code paths for good. A Repository
  is built with `crpp_catalog` and pushed to a repository or branch of its own, as a site is.
- Hashing every installed file at each start: a font alone is 13 MB of CPU work per start. Files are hashed when
  fetched; a start checks that each is there with the catalog's size.

## Consequences

- The catalog grammar is `crpp-catalog 2`. An exe that meets 1 at an added Repository says it must be rebuilt.
- A catalog may name a `mirror`, another folder with the same files; the official one names the Worker. A file is
  tried at the Repository, then the mirror, then a third-party file's own source. A per-file second source at the
  release's tag is gone, as it was the same host as the Repository now.
- The exe knows the GitHub repository's address, so moving it takes a release; the Worker's domain still serves the
  exes before this one.
- The frozen catalogs on the Worker (`catalog.txt` for format 1 and `2/catalog.txt` for v0.3.0) are kept byte for
  byte, and their files count as used, so the exes that read them keep working, and get nothing new.
- A client that read a catalog one release ago may meet a removed file, and follows the latest catalog at its next
  start.
- The repository's history grows with every changed file; only its tip is read, so it can be cut short and pushed
  again without any client noticing.
- An update from a path-laid install moves each file whose size and hash match to its hash name once, so the font is
  not fetched again.
- Unused files are removed at start, before any is opened, by the union of every Repository's installed list.
