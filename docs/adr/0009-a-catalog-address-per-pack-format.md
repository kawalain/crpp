# A catalog address per pack format, and the old one frozen

Skin Pack format 2 adds names that format 1 does not have, such as the `focused` state and the `context_menu` screen.
An exe of format 1 refuses a whole pack that uses a name it does not know, and v0.2.0 also fetches whatever the
official catalog lists for an installed pack without checking its format, overwriting the pack it can read. Every
client reads the latest official catalog at each start, so deploying format-2 packs at the current address would leave
every v0.2.0 user with no Skin at their next start.

So each format of the official Repository has its own catalog address. The current address stays the format-1
Repository, frozen at the last format-1 release (v0.2.x), and the release script keeps deploying it, rebuilt from that
tag, beside the new one. An exe of format 2 reads its catalog at an address of its own (`2/` under the official one).
A v0.2.0 user loses nothing, and moves to format 2 by updating the exe through GitHub Releases as before.

## Considered options

- One catalog with an entry per format: v0.2.0's parser refuses a catalog that lists an id twice, so it would stop
  receiving every update, the shared files included.
- Adding format 2's names inside format 1: the `focused` state has to appear in screens and parts that v0.2.0 reads,
  so it would refuse the official packs anyway.
- Accepting the breakage: v0.2.0 users would be left at the first-run screen with packs they cannot read.

## Consequences

- An exe reads a range of formats, not one: format 2 reads 1 and 2. A format-1 pack has `focused` always false and no
  context menu; keys still work. Packs of added Repositories, which are format 1 today, keep working.
- An exe never fetches a pack in a format it does not read; the installed one stays. This guards format 3 and later;
  it cannot reach the v0.2.0 already installed, which is why the old address is frozen.
- The frozen format-1 tree gets no new files: no new emoji, word lists or sounds reach v0.2.x.
- An added Repository has a single address. Its catalog may list packs of any format, and an exe takes those it reads.
