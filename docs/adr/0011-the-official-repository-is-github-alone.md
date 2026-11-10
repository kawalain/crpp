# The official Repository is GitHub alone, and the Worker is frozen

This replaces, in ADR 0010, the Worker at `crpp.kawala.in` as the official Repository's mirror. Its other reasons hold:
files are served and installed by their hashes, and the official Repository is the GitHub repository
`kawalain/crpp-repository`.

From v0.4.0 the official catalogs name no mirror and the exe knows none: the official files come from GitHub, through
`raw.githubusercontent.com`, and a third-party file still falls back to its own source. The Worker is never deployed
again. It keeps serving what v0.3.0's release put there, the path-laid trees and catalogs that v0.2.x and v0.3.0
read, for as long as those versions are in use; they update to v0.4.0 from the GitHub Release, which the Worker has
no part in.

The code that built and deployed the Worker is gone from `main`: `wrangler.jsonc`, the frozen catalogs, the Worker
half of `build-repository.sh` and the deploy step of `release.sh`. Should the Worker ever have to be put back as it
is, check out `v0.3.0` (397241e) and run its `build-repository.sh 0.3.0` and `npx wrangler deploy` from there:
that tag built what is live.

## Considered options

- Keep the Worker as the mirror, as ADR 0010 planned: a second host to deploy, watch and pay attention to at every
  release, for the case where GitHub does not answer, which the app already reports as a network failure.
- Delete the Worker now: v0.2.x and v0.3.0 would find no files on a new install or a Skin change until they update.
- Keep the Worker's code in `main` unused: one command away from deploying a tree that no longer matches what is
  live, and a reader would think it part of the release.

## Consequences

- An official build has no mirror address. A catalog may still name a `mirror` (grammar 2), for any other
  Repository that wants one; the official one does not.
- When GitHub does not answer, the official files cannot be installed until it does; the app says so as it does for
  any network failure.
- A Release touches GitHub only: the tag, the Repository's commit and the Release.
- The GitHub repository keeps only the files of the catalogs it serves; the frozen trees live only on the Worker.
