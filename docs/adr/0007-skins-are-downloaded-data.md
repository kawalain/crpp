# A Skin is data, downloaded as a Skin Pack

This replaces how ADR 0005 makes a Skin; what a Skin owns stays as that ADR says.

A Skin was C++ compiled into the exe, so a new look needed a release and a first run fetched the Cloud font even for someone using another Skin. Screens, control shapes, motion and cursors now become data in a Skin Pack, drawn by one engine in the exe, and the exe holds no Skin's look. The goal is that nobody downloads a look they do not use, and that a Skin can be added or changed without a release.

Smaller memory and a smaller exe are not the goal. Measured before this decision, the Skin code added at most 205 KB to a 1.65 MB exe, idle memory did not grow beyond measuring error, and the Skin in use is the only one whose code runs. The largest part of a first download was a 13.4 MB font only Cloud uses.

## Considered options

- Data only for colours, fonts, sounds and pictures, with layouts still in C++: far less work, but a new layout would still need a release, and a Skin would not stand on its own.
- Skins as DLLs: would run downloaded code, raising the stakes of signing and of the antivirus detection in `docs/releases/README.md`, and C++ objects do not cross a DLL boundary safely with the static runtime a release uses.
- A plain Skin built into the exe, so that a failed download still leaves a usable app: every screen would be kept twice, and a Skin would not stand on its own.
- Scripts in a pack: more freedom, but that is downloaded code again. What the data cannot express becomes a new node in the engine.

## Consequences

- The screens are rewritten as data, and the engine is new code that must keep the performance rules: idle means blocked, and a still window draws nothing. Going over the limits in `docs/plans/skin-packs.md` stops the work.
- Unlike the assets of ADR 0004, a Skin Pack is required: until one is installed the exe shows only a small screen of its own for progress, failure and retry. GitHub is a second source, so that a blocked jsDelivr does not stop a first run, but a network blocking both does.
- A signed catalog lists the packs and their hashes, so a Skin needs no release. Its key is not the update key, so a leaked one cannot sign an exe. Only official packs are offered; a development build can load a folder.
- A pack states the format it is written in, and the exe refuses others, so screens an exe needs and a pack lacks are caught before anything is drawn.
- State, hit handling and features stay in the exe, whatever the Skin. A Skin Pack holds no code.
- Sound packs stay outside Skin Packs: another member's choice must be heard whichever Skin is in use.
- ADR 0005's "a new Skin is large, on purpose" no longer holds, and neither do its implementations per Skin.
