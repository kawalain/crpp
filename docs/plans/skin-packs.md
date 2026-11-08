# Plan: Skins as downloaded data

Agreed with the user on 2026-11-07. Delete this file when the work is done; what lasts goes to
`CLAUDE.md`, `GLOSSARY.md` and the ADRs. Decision: [ADR 0007](../adr/0007-skins-are-downloaded-data.md).

Goal: nobody downloads a look they do not use, and a Skin can be added or changed without a release.
Every part of a Skin, screens and cursors included, is data in a Skin Pack, and the exe holds no Skin.
A smaller exe and less memory are not goals.

## Decided

- **Data language.** JSONC with no scripts: nodes for boxes, text, vector pictures, fields, scroll areas and
  repeated lists; names of app state and commands to bind to; arithmetic with `min` and `max` for positions;
  rows and columns; conditions on state; transitions with a duration and a curve. What it cannot express
  becomes a new node in the engine, never a script. Where speed matters, as in the chat list, a row's layout
  is data while reuse and partial redraw stay in the engine.
- **Pack contents.** Themes, screens, control shapes, window frame, motion, cursors, fonts, pictures and
  notification and system sounds. Sound packs stay shared and are all fetched, because another member's
  choice must be heard. Emoji and word lists stay shared too.
- **First run.** The exe has one small screen in a system font: progress, failure, retry and exit. Nothing else
  works until one pack is installed; once it is, it keeps working. The default Cloud pack comes first, others
  when picked. Superseded in part on 2026-11-08 by Repositories (ADR 0008): the official one is on Cloudflare
  Workers, with `raw.githubusercontent.com` at the release tag as its second source. The screen counts files and
  bytes, reads 64 KB at a time so the bar moves, and retries a passing failure twice after a short wait.
- **Who publishes.** The official Repository, and any Repository a user adds whose catalog is signed (ADR 0008).
  Others may offer only Skin Packs and Sound packs. `--skin-dir` loads an unsigned folder for development.
- **Trust.** A signed catalog lists packs and file hashes, and each file is checked. The official one is signed with
  the update key (decided 2026-11-08, replacing a key of its own); another Repository's key is remembered when it is
  added, and a change refuses it. The verifying code is the update module's. Files are fetched one by one, with no
  archive format.
- **Compatibility.** A pack states a `format` number and the exe refuses any outside its range. The catalog offers
  a pack per format. A newly required screen raises the number and updates the official packs together.
- **Managing.** The display tab lists Themes not yet installed. Picking one shows progress and applies it when done;
  a failure stays on the current Skin and says why. Unused packs can be removed there, but not the one in use or
  its parents. Updates: the catalog is read
  once at start, files arrive behind the app and apply on the next start, with no polling.
- **Cursors.** Part of a pack, drawn as vectors, made into `HCURSOR` at run time with `SetCursor` only, so they
  stop at the app's windows. Seven: arrow, hand, I-beam and four for resizing. Cloud's pack has none and uses the
  system's. All of Retro shares one set, modelled on the white arrow of the time and drawn here, with an
  I-beam that has a white edge to show on any field. They scale with the window's DPI and follow Windows' pointer
  size. A pack asks for hard edges, and then the steps of the grid are part of the look. The shadow is left to the system, as no source says whether it adds one to such a cursor and drawing one could
  double it. A "테마 커서 쓰기" check in the display tab, on by default and shown only for a Theme
  with cursors, turns them off (`themeCursor`, a key of this client's own). OS menus and IME windows stay the
  system's. Whether resizing keeps them during the drag needs a real desktop.

- **What the program knows.** Decided on 2026-11-08. Generic nodes, the names of app state and commands, screen
  slots and wording keys. It does not know a pack's name, a Theme's id, or any colour, size, duration or curve: those
  are read from packs. What is hard to express as data, such as scrolling's settling or a partial redraw, stays a
  general feature of the engine, with its numbers from the pack. Which pack comes first is the catalog's to say.
- **Skins and Themes.** A user picks a Skin, then a Theme, then a dark mode: off, on or following the system. Dark is a
  variant of a Theme with its own Palette, and is locked while the Theme has none, without losing the choice. Changing
  Skin goes to that Skin's default Theme. These replace the lists of light and dark Themes, and the choice is kept under
  this client's own keys `skin`, `skinTheme` and `darkMode`. When they are absent upstream's `theme` is read once;
  upstream's keys are never written.
- **Packs.** `skins/cloud` has 구름 (dark variant 먹구름) and 파스텔 (no dark variant). `skins/retro` has `classic`, the
  default, then `luna_blue`, `luna_homestead` and `luna_metallic`, shown as 고전, 루나 블루, 루나 홈스테드 and 루나 메탈릭,
  all four with a dark variant. Classic is 98's look with its blue gradient caption. Colours come from public sources,
  and no file of Windows is copied.
- **Luna within Retro.** Luna differs in its frame, controls, fonts and sounds, not only colours, so `retro` holds an
  overlay `luna` that its Themes name. An overlay is merged by the same code as a parent pack.
- **Extending.** A pack may name a parent with `"extends"`. Constants, fonts, parts, cursors and screens are replaced
  whole by name, with no patches inside a node tree. Themes are inherited, and a child adds to them or replaces one by
  name. A dark variant inherits its Theme's colours by name. At most four levels; a cycle or another `format` is refused.
  A parent is a pack of its own, fetched with its child, and cannot be removed while a child uses it. No official pack
  extends another yet, so a test pack proves this.

## Limits on cost

Measured as before, against the build these steps start from. Going over stops the work and comes back to the user
with the numbers:

- Idle CPU stays 0.0%, and a still window draws nothing.
- Idle memory grows at most 5%.
- Scrolling CPU grows at most 20%.
- From reading a pack to the first screen: at most 100 ms.

## Measured

Step 1, Messenger's Home from data against its C++ layout, MSVC Release on a hidden desktop at 125%:

- 84 renders, both Palettes at 100%, 125% and 150% in both Locales, give the same hash.
- A pack changed on purpose gives other images, so the comparison is not comparing a thing with itself.
- Idle: 0.00% of one core, working set 47.4-47.9 MB either way. The pack holds about 12 KB.
- Reading the pack, then the first screen: 0.6 ms and 0.8 ms.
- The pointer moving between links, drawing as fast as the display takes: 3.8-6.3% (mean 5.24%) from
  C++, 4.4-6.1% (mean 5.22%) from data. Offscreen one frame costs 1.1% more. Scrolling is not comparable
  until Settings is data, in step 2.

Step 2, the options window of Messenger from data against its C++ layout, the same way, with the wheel posted
about 60 times a second and reversing every 20 notches, five rounds of each:

- Scrolling: 5.39-6.72% (mean 6.17%) from C++, 5.78-6.48% (mean 6.11%) from data.
- Idle 0.00% and afterwards 0.00%, working set 48.4-48.9 MB and 48.8-49.1 MB, private 62.6-64.7 MB and 63.1-64.5 MB.

Step 2 done, all of Messenger from data, on 26 screens and 595 nodes:

- 74 scenes in Korean at 100% and 150%, English at 125% and the dark Palette give the hash of the C++ layouts, but for the four
  of the display tab, which has the new cursor check. Those were equal before it, and were looked at since. Cloud is unchanged: 72 scenes, three sets.
- The same scrolling, five rounds again: 5.08-6.17% (mean 5.58%) from C++, 5.16-6.33% (mean 6.01%) from data, so +7.7%.
  Idle 0.00%, working set 48.5-49.0 MB and 48.6-49.0 MB.
- Reading the pack 3.1 ms and the first screen 0.85 ms. It holds about 155 KB, and adds 396 KB of private bytes. A frame offscreen: +3.5%.
- From the pack on a hidden desktop: the frame's hit tests, window sizes per screen and panel, a question holding the others off, and leaving for Cloud's
  Theme all behave. The cursors themselves need a real desktop, which is in Verification debt.

Steps 3 to 5, Cloud from its pack against the C++ layouts at 2680ddd, on 26 screens and 668 nodes, MSVC Release on a hidden desktop at 100%, settings open, the
wheel posted about 60 times a second and reversing every 20 notches, seven rounds of each taken in turn:

- 74 scenes in Korean at 100% and 150% and English at 100%, moments of motion included, give the hash of the C++ layouts. The exceptions are `luna-controls`, which
  is not a Cloud screen, and the display tab and `motion-tab`, which have the new rows. Three of those were looked at: `settings-display-tip`, `settings-display-dark` and `motion-tab`.
  `settings-display-dark` shows the mode as off, because the scene sets the Theme without the settings. Retro's Luna is unchanged but for its display tab.
- Idle CPU 0.00% in both. Tests show a Room, chat and settings ask for no frames once settled.
- Scrolling: 4.88-9.77% (mean 7.48%) before, 5.47-7.81% (mean 6.47%) now. Within the limit.
- Reading the pack 3.5 ms and the first screen 0.8 ms. A frame offscreen: +1.3%. The pack holds about 183 KB.
- **Idle memory is over the limit by its mean:** working set 45.7-51.6 MB (mean 47.6) before, 52.1-52.5 MB (mean 52.3) now, so +9.9%. Private 73.9 MB to 75.8 MB, +2.6%.
  The earlier build lands either near 46 or near 52, and the new one always at the top of that, so this may be which mode it lands in and not what it holds.
- **Decided on 2026-11-08: private bytes are what the limit counts**, as the working set depends on which mode a run lands in. Measured again,
  20 rounds of each taken in turn, settings open, MSVC Release on a hidden desktop at 100%, 2680ddd against 07df243: private 72.5-76.3 MB
  (mean 74.8) before and 73.9-76.1 MB (mean 75.2) now, so +0.5%, within the limit. The working set of the earlier build was near 46 MB in 4
  rounds and 51.1-52.3 MB in 16; the new one was 51.6-52.4 MB in all 20, +1.2% against the upper mode's median. Why it never lands lower was
  not looked into. Idle CPU 0.00% in every round.
- The fixes of the Open list, da84062 against 07df243, every scene from the packs: Cloud and Luna Blue give the same 74 hashes. Classic
  differs in 10, each by its background: a window of its own and the main window had Luna's beige behind them, and now its grey.

## Open

Reported by the user on 2026-11-08 after trying the build. The other three are fixed:

- Retro's cursors, the hand above all, differ somewhat from the real ones.

## Steps

Start from `main` with the Messenger work merged, after the user has seen its real windows. Steps 1 to 5 are done. The user's hands on step 4 are moved to
before step 7, which removes what Cloud's pack is compared with.

1. **Engine, proved on one screen.** A pure, portable module for parsing and layout, tested by CTest and GCC, and a
   painter on `Gfx` in `ui`. Move Messenger's Home to data beside the C++ one and measure the limits. Stop here if
   they do not hold.
2. **All of Messenger as data.** Screens, settings, panels' windows, frame, controls and cursors, including the
   cursor setting. It has no motion, so it proves layout first. Compared by eye at 100%, 125% and 150%.
   Then, on 2026-11-08, its pointers were redrawn: drawn with hard edges on the 32 by 32 grid, then enlarged by nearest
   neighbour, so the steps grow with the display's scale.
3. **Extending, and Retro.** The engine for `extends` and overlays. `skins/messenger` becomes `skins/retro` with the
   overlay `luna`, proved by the hashes: `luna_blue` and its dark variant against what `messenger` and `messengerdark`
   draw today. Then `classic` and the other Luna Themes, which have nothing to compare with and need the user's eyes.
4. **Cloud as data.** Value changes such as hover, toggles, tabs, dropdowns and dialogs state a duration and a curve in
   the pack. Scrolling, screen changes, speaking discs and reusing chat rows stay in the engine with numbers from the
   pack. Edge fades and layered shadows become nodes. Done when every scene in `tests/shots/scenes.txt`
   renders to the same hash in Korean at 100% and 150% and in English at 100%. That proves the look and not the feel,
   so motion scenes (`--at`) are compared as well, and the user tries scrolling and transitions by hand with
   `--skin-dir` before step 7.
5. **Choosing.** The Skin, Theme and dark mode rows, their keys, and the list of Themes read from packs, which needs
   Cloud's pack first.
6. **Repositories and download** (ADR 0008), in three commits' worth of parts, tested with a test key and a local
   server. A build with `skins/` beside it downloads nothing; an exe without one installs a pack first.
   - **6a.** The catalog for everything downloaded, its signature, the tool that writes and signs one, the official
     Repository and its second source, the first-run screen with the 0% fix, and keeping installed files whose hash
     matches. The font comes with the Cloud pack only. Then, after asking, a first `wrangler deploy` from here.
   - **6b.** The display tab lists Themes not yet installed; picking one shows progress and applies it. The system
     tab's "저장소" row adds and removes Repositories and shows their fingerprints. Details under "Downloading,
     decided" below. A Sound pack another member lacks already plays as the device's first pack.
   - **6c.** Removing unused packs, and updates fetched behind the app and applied on the next start.
   - **6d.** A parent in another Repository, and pinning it.
   - **6e.** Sound packs of other Repositories.
7. **Remove the C++.** Delete Luna's and Cloud's layouts and `Skin`, and fix ADR 0004 and the module design. Measure
   again.
8. **Release path.** `docs/releases/README.md` and its script fill `dist/repository`, sign the catalog with the
   update key and deploy it with `npx --yes wrangler@4.152.0 deploy` (`wrangler.jsonc`). A clean install is tried with
   a local server, then for real, and the second source once the tag is on GitHub.

## Downloading, decided

Agreed with the user on 2026-11-08, after 6a. Work that is not pushed needs no migration: the catalog grammar, keys
and paths below change in place until the push that publishes them.

Order: the documents below, then Retro's cursors, then the install queue, the pack language, 6b, a push (after asking), 6c, 6d, 6e, the
hands-on check of Cloud on a real desktop, 7 and 8.

- **Documents first**, each its own `docs:` commit: both READMEs describe Retro and Cloud and Repositories, not the old
  Messenger Themes; ADR 0005 and 0007, never pushed, are rewritten in place to say what holds now (one key, other
  Repositories, no jsDelivr, the pack language; 0005 loses "a new Skin is large, on purpose" and the per-Skin
  implementations); ADR 0004, pushed with v0.1.x, only marks each sentence ADR 0008 replaces (jsDelivr, "the retry
  screen is gone"); `CLAUDE.md` says the official Sound packs sit
  in `shared`, not one group each.
- **Retro's cursors.** Render the `cursors` sheet beside the real ones for the user to point at what differs.
- **Install queue**, a `fix:` before 6b. `start_install` while `assets_worker` is joinable assigns to a joinable
  `std::thread` and terminates. One queue runs one install at a time, always reports progress to the UI, and rereads
  the Skins when it ends.
- **Pack language.** JSONC is costly to write for people and agents alike: 13.6% of the packs' 181 KB is quotes,
  expressions hide in strings where nothing checks them, three sigils mix and `group` nests deeply. Reading costs
  little (3.5 ms, 183 KB), so only the front end changes: a small language of our own, with nodes, positional
  arguments, `key value`, braces for children, expressions as syntax, and units such as `150ms` and `40%`. The engine,
  the loaded tree and the compiled expressions stay. A Home screen written in it beside its JSONC comes first, for
  the user to judge (`pack-language-draft.md`, accepted with its three open points). Colours are `#rrggbb`. Syntax highlighting for Vim, Neovim and VS Code comes later, when the user asks.
  Names the exe defines carry `@`: app state, built-ins (`@dark`, `@hot`, `@w`) and wording (`@tr.home_create`), so a
  later exe cannot collide with a pack; a pack's own names (Palette, constants, parts, `let`) are bare, and a clash
  among them is an error. Files are `.cskin` (`pack.cskin`, `screens/home.cskin`). The JSON Schemas and
  `crpp_skin_schema` go; `crpp_skin check <dir>` runs the app's loader and prints `file:line:col`, with a guess at
  the name meant, and tests check the official packs with it. `keys.hpp` stays the list of what may be written. A
  one-off converter writes the packs from the loaded JSONC, comments before a node included; every scene must keep
  its hash (`render.sh`, `compare.sh`), then the JSONC loader and the converter go. Nothing of this was released, so
  ADR 0007 is edited in place to name the language; there is no superseding ADR.
- **Catalog.** The catalog tool copies each Theme from `pack.jsonc` into a `theme` line with its dark variant and its
  names per Locale, so Themes not installed can be listed. The parser skips lines it does not know and still refuses a
  known line that is wrong. `crpp-catalog 1` stays. The catalog is read behind the app at every start, files are not;
  if it cannot be read the saved one is used silently, the cause going to the diagnostic log. Fetching updates of
  installed packs stays in 6c.
- **Names.** A Theme or Skin name missing in the current Locale shows the English one, then the id. A test checks the
  official packs have Korean and English. A pack id is `a-z0-9_-`, at most 32 characters, checked by the tool and the
  app, so that with a Repository's prefix it stays within the server's 64 bytes for a Sound pack.
- **Picking a Theme not installed.** The row shows progress and a cancel; fetching goes on when settings close or the
  screen changes. When done it applies only if that Theme is still the last one picked, otherwise it stays installed.
  A failure keeps the current Skin and shows the reason and "다시 시도" in the row, and a toast if settings were
  closed. A cancel keeps the files already fetched, as their hashes match; 6c cleans them up.
- **Repositories.** Known by the key that signs the catalog: on disk and in keys a pack of the official Repository is
  its id (`cloud`, `retro`) and one of another is `<first 16 hex of the fingerprint>/<id>`. Two Repositories with one
  key offering one id offer the same pack. Adding one fetches its catalog, checks the signature and asks in a dialog
  showing the fingerprint, packs and sizes; "추가" remembers the key (trust on first use). An address is an HTTPS
  folder or `github:owner/repo[@ref]`, any ref, `HEAD` when left out (check that raw.githubusercontent.com takes it).
  A key that changes shows the warning of 6d; going ahead registers it as a new Repository and keeps packs fetched
  under the old key. A Repository with a pack in use cannot be removed and says why; its unused packs go with it.
- **6c.** Removing a pack is refused for the one in use and for its parents. (The rule about the system sync's target
  is gone: a dark variant is in the same Theme and pack.)
- **6d, a parent elsewhere.** A pack names its parent in `pack.jsonc`, the tool copies it into the catalog's `skin`
  line, so the app checks it before fetching. `"extends": "retro"` is a pack of the same or the official Repository;
  `"extends": {"repository", "key", "pack", "version"?, "sha256"?}` one of another. The key is always pinned; a pack
  gets a `version` (`0.y.z`), and `"0.3"` accepts 0.3.x, `"0.3.2"` only that; `sha256` is a digest of the parent's
  sorted file lines (path, size, hash) in its catalog. Both given, both must hold. A parent's Repository not yet added
  is asked for in the same dialog as adding one.
  When a pin fails, as a Repository serves only its latest files:
  - Another key or digest: a warning that the screens may break and the pack may be made by someone other than the one
    pinned. A pack holds no code, so it says no more than that. Cancel is the default; going ahead is not on Enter.
  - The same key and another version: a plain question, "update or hold" with hold as the default, or "install anyway".
  - Going ahead remembers the digest accepted, and asks again when the parent changes again. A held update stays held
    and the display tab says which child holds it back.
  - A pack that then fails to load keeps the current Skin, shows why, and logs the choice and the reason.
- **6e, Sound packs of other Repositories.** The official Repository's are all fetched as now; others only when picked
  in the sound tab, listed from `sound` name lines as Themes are. A member's pack another lacks plays silently as the
  device's first pack, with no mark on the profile card: only the fingerprint and id travel, not an address.
- **Open risk:** a pack per `format` in one catalog is designed when format 2 is needed. Until then a pack in a format
  the exe does not take is not updated and the installed one stays.

## Testing

- CTest: parser limits and bad input, arithmetic, layout, conditions, catalog signature, format refusal, removal rules.
- `ui_harness`: flows run through the same state and commands on data screens.
- `--shot` with `--skin-dir`; hashes for Cloud, eyes for Messenger.
- Hidden desktop, with a localhost `serverUrl` before any click. Real cursors and the first-run failure need a real
  desktop, which goes to Verification debt.

## Open risks

- Far the largest change so far: thousands of lines of screens move, and both forms live side by side until step 5.
- Cloud's speaking discs rely on partial redraw and its panels on layered shadows; if data cannot match them within
  the limits, the engine grows nodes for them.
- A network that blocks both sources cannot start the app at all on a first run. That was accepted.
- Text a pack brings of its own, such as a Theme's name, needs a name per Locale inside the pack. The rest stays in
  `src/locale`, so a pack refers to wording by key and a new key needs a release.
