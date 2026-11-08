# A Skin is data, downloaded as a Skin Pack

ADR 0005 says what a Skin owns; this says how one is made. ADR 0008 says where packs come from.

A Skin was C++ compiled into the exe, so a new look needed a release and a first run fetched the Cloud font even for someone using another Skin. Screens, control shapes, motion and cursors now become data in a Skin Pack, drawn by one engine in the exe, and the exe holds no Skin's look. The goal is that nobody downloads a look they do not use, and that a Skin can be added or changed without a release.

Smaller memory and a smaller exe are not the goal. Measured before this decision, the Skin code added at most 205 KB to a 1.65 MB exe, idle memory did not grow beyond measuring error, and the Skin in use is the only one whose code runs. The largest part of a first download was a 13.4 MB font only Cloud uses.

A pack is written in a small language of its own, in `.cskin` files: nodes with positional arguments, `key value`
pairs and children in braces, with expressions and units such as `150ms` as part of the syntax. Names the exe defines
carry `@`, as its state, its built-ins and its wording (`@tr.home_create`) do, so a later exe adds names without breaking a pack; a pack's
own names are bare, and `#rrggbb` is a colour. `crpp_skin check` reports a mistake by file and line with the app's own loader. Only the
front end is the language's: what it reads is the tree the engine draws, with expressions compiled as before.

## Considered options

- JSONC, as the packs were first written: a parser exists and editors check it against JSON Schemas, but 13.6% of the
  packs were quotes, expressions sat in strings where nothing checked them, three sigils mixed and children nested in
  `group` arrays. People and agents both found it hard to write.
- An existing format: TOML and YAML suit deep trees badly, and YAML's implicit types trip people and agents alike.
  KDL fits nodes, but expressions would stay strings or need its syntax bent, and its parser would be written here
  all the same.

- Data only for colours, fonts, sounds and pictures, with layouts still in C++: far less work, but a new layout would still need a release.
- Skins as DLLs: would run downloaded code, raising the stakes of signing and of the antivirus detection in `docs/releases/README.md`, and C++ objects do not cross a DLL boundary safely with the static runtime a release uses.
- A plain Skin built into the exe, so that a failed download still leaves a usable app: every screen would be kept twice, and a Skin would not stand on its own.
- Scripts in a pack: more freedom, but that is downloaded code again. What the data cannot express becomes a new node in the engine.

## Consequences

- The screens are rewritten as data, and the engine is new code that must keep the performance rules: idle means blocked, and a still window draws nothing. Going over the limits in `docs/plans/skin-packs.md` stops the work.
- Unlike the assets of ADR 0004, a Skin Pack is required: until one is installed the exe shows only a small screen of its own for progress, failure and retry. The official Repository has a second source on GitHub (ADR 0008), but a network blocking both stops a first run.
- A signed catalog lists the packs and their hashes, so a Skin needs no release. Packs come from the official Repository and from any other a user adds (ADR 0008); a development build can load a folder.
- A pack states the format it is written in, and the exe refuses others, so screens an exe needs and a pack lacks are caught before anything is drawn.
- State, hit handling and features stay in the exe, whatever the Skin. A Skin Pack holds no code.
- Sound packs stay outside Skin Packs: another member's choice must be heard whichever Skin is in use.
- The exe knows no pack: not a name, a Theme's id, a colour or a duration. It knows generic nodes, the names of its own state and commands, its screen slots and its wording keys. What is hard to state as data stays a general feature of the engine, with its numbers from the pack, never a feature of one Skin.
- A pack may extend another by name, replacing whole items, as a look of Windows was written over the one before it. A parent may be in another Repository, pinned by its key and optionally by version or content (ADR 0008). A child therefore does not stand on its own: its parent is fetched with it and is kept while it is in use. Patching inside a node tree was rejected: replacing a whole part or screen is simple to reason about and to check.
