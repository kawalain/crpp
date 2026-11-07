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
  when picked. GitHub is a second source for when jsDelivr is blocked.
- **Who publishes.** Only official packs through the app. `--skin-dir` loads an unsigned folder for development.
  The parser limits sizes and depth from the start, so opening this to others later stays possible.
- **Trust.** A signed catalog lists packs and file hashes, and each file is checked. It uses an ECDSA P-256 key of
  its own, so a leaked Skin key cannot sign an exe; the verifying code is the update module's. Files are fetched
  one by one, with no archive format.
- **Compatibility.** A pack states a `format` number and the exe refuses any outside its range. The catalog offers
  a pack per format. A newly required screen raises the number and updates the official packs together.
- **Managing.** The display tab lists Themes not yet installed. Picking one shows progress and applies it when done;
  a failure stays on the current Skin and says why. Unused packs can be removed there, but not the one in use or
  one the system sync points to. A sync target still fetching keeps the current Skin. Updates: the catalog is read
  once at start, files arrive behind the app and apply on the next start, with no polling.
- **Cursors.** Part of a pack, drawn as vectors, made into `HCURSOR` at run time with `SetCursor` only, so they
  stop at the app's windows. Seven: arrow, hand, I-beam and four for resizing. Cloud's pack has none and uses the
  system's. Both Messenger Themes share one set, modelled on the white arrow of the time and drawn here, with an
  I-beam that has a white edge to show on any field. They scale with the window's DPI and follow Windows' pointer
  size and shadow settings. A "테마 커서 사용" check in the display tab, on by default and shown only for a Theme
  with cursors, turns them off (`themeCursor`, a key of this client's own). OS menus and IME windows stay the
  system's. Whether resizing keeps them during the drag needs a real desktop.

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

## Steps

Start from `main` with the Messenger work merged, after the user has seen its real windows.

1. **Engine, proved on one screen.** A pure, portable module for parsing and layout, tested by CTest and GCC, and a
   painter on `Gfx` in `ui`. Move Messenger's Home to data beside the C++ one and measure the limits. Stop here if
   they do not hold.
2. **All of Messenger as data.** Screens, settings, panels' windows, frame, controls and cursors, including the
   cursor setting. It has no motion, so it proves layout first. Compared by eye at 100%, 125% and 150%.
3. **Cloud as data.** Add transitions and what its art needs. Done when every scene in `tests/shots/scenes.txt`
   renders to the same hash in Korean at 100% and 150% and in English at 100%. That proves the look and not the feel,
   so motion scenes (`--at`) are compared as well.
4. **Packs and download.** `skins/` in this repository, catalog and signature, the second key, the first-run screen,
   the second source, the display tab's list, removal and background updates. Generating the key is the user's step,
   as in `docs/signing-key.md`. The Cloud pack refers to Pretendard at its own pinned source.
5. **Remove the C++.** Delete Luna's and Cloud's layouts and `Skin`, and fix ADR 0004 and the module design. Measure
   again.
6. **Release path.** `docs/releases/README.md` and its script publish the catalog. A clean install is tried with a
   local server, then for real once the files are on GitHub.

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
