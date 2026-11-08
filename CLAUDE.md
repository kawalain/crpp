# crpp

C++ rewrite of the desktop client of `pb29/Cloudroom` (private; branch `dev`,
readable with `gh`). Priorities, in order: minimal CPU load, minimal memory,
stability. Minimal dependencies. Technical facts come from official references
and developer communities, not from memory.

## Decisions

- UI is native Win32 + Direct2D/DirectWrite. No WebView2.
- Protocol: `cloudroom-v2` only. The v1 subprotocol is not implemented.
- Wording lives in `src/locale`, one table per Locale, compiled in; see
  `docs/adr/0006-locales-compiled-into-the-exe.md`. Korean and English ship.
- Language baseline is C++23. Do not depend on C++26 (the DIS ballot was
  reported as failed; contracts, reflection and `std::execution` are not
  portable yet). Errors use `std::expected`, not exceptions.
- Upstream server code (`server/src/main.rs`) is the protocol spec. The
  client's `protocol.rs` is stale (it lacks kinds 11-13, 24-26, 49).

- First release targets Windows only, but the code is written to be
  cross-platform: pure modules are portable ISO C++23 with no platform
  headers; every OS API lives in an adapter behind a seam (`transport`,
  `input`, `AudioDevice`, `ui`, tray/notification). macOS/Linux adapters are
  added later without touching pure modules.
- Windows builds use MSVC from Build Tools 2022 (`/std:c++latest` via CMake), locally
  too. The CI job `portability-gcc` builds and tests the pure modules with GCC on a
  Linux runner, run by hand with `gh workflow run ci.yml`, only to keep them portable;
  no Linux build of the app is produced or supported.
- Build is CMake + Ninja only; no package manager. Third-party code, if ever
  needed, is vendored in `third_party/` at a pinned version.
- Every script is bash, run by Git Bash on Windows (never WSL's) and with `shell: bash` in
  workflows; no PowerShell or `.cmd`. What bash cannot reach, such as a hidden desktop, is a
  small C++ tool (`crpp_hidden_run`), and signing uses OpenSSL with a key file.
- Tests use CTest with the tiny `tests/support/check.hpp`; no framework.
  Golden vectors are derived from upstream `server/src/main.rs`.
- Layout: `src/<module>/` (CMake static library) with `src/<module>/tests/`.
  Dependency direction: `ui -> session -> wire`, and `ui -> skin`; `transport` and `input`
  are reached from `session` only through interfaces.
- Audio is raw WASAPI with a hand-written WAV parser (no third party).
- `client-apk` and the upstream Rust `app` are out of scope.
- Goal: the same features and screens as upstream, one instance at a time.
  Upstream defines the features, screens, visuals and (server only) the
  protocol. Design every implementation independently from its required result
  and public sources; upstream client code is never a design input. Behavior may
  differ where upstream is broken or a change is clearly better; record the
  difference in the commit or docs.
- Features visible in upstream are in scope, plus Pen input, which upstream
  hides because it does not work there. Pen ships hidden until verified on a
  real device.
- Data files: see `docs/adr/0001-protected-store-with-shared-fallback.md`.
- Assets: see `docs/adr/0002-download-assets-on-first-run.md` and
  `docs/adr/0004-assets-through-jsdelivr-and-never-required.md`. Offline use is
  not a goal: without a server the app opens only to say so.
- Keystroke privacy: see `docs/adr/0003-presence-mode-by-default.md`.
- Sound engine: raw WASAPI, upstream's sound packs. The spatial/reverb/resampling
  design is researched from public sources (see `docs/research/`). Automated
  tests cover numbers and stability; a listening test tool lets the user judge
  quality by ear.
- Every UI element is vector data (paths, SVG); no bitmaps. Animations follow
  upstream CSS durations and easings; where upstream has none (scrolling, screens, tabs,
  chat rows) the durations are ours, listed under Differences from upstream. App font: Pretendard GOV Variable.
- Distribution differs from upstream: releases are built and published from a developer's
  machine (`docs/releases/README.md`), the app checks the
  latest GitHub Release, and release notes are shown as the changelog. GitHub Actions runs
  only by hand, to save its limited minutes. A release
  is one exe plus a manifest signed with ECDSA P-256 (key in GitHub Secrets,
  public key in the app, verified with BCrypt). The app replaces itself and
  restarts; keep the apply step replaceable so an installer can take over later.

## Roadmap

The goal is every user-visible interface of upstream's `client/` on Windows.
macOS-only UI, `installer/` and developer-build tools are out of scope for now.

0. Foundation: one file per screen, shared controls, dialogs, toasts, settings keys.
1. Feedback and reconnect: failure reasons, offline popup.
2. The rest of the room and home screens: dialogs, profile card, member menu,
   emoji picker, public room search, return-to-Seat dialog.
3. Settings: sound, display, notification and system tabs, and the guest part of
   the account tab. Pen volume and pack controls are built but stay hidden.
4. OS integration: tray, chat notifications, taskbar flash, autostart, reopening
   the running instance.
5. Accounts: HTTP API, first-run flow, handle change, logout.
6. CI, Releases, `update`, update popup, changelog viewer.
7. Pen input, as its own step, verified on a real device.

Steps 1-6 run as parallel subagents after step 0; the numbers are the merge order.
Pull requests are disabled on this repository: each subagent commits to its own
worktree branch, and the main agent merges it into `main` with `--no-ff` after a
full build and test run.

Done means: state logic has CTest coverage, `--shot` output has been compared
with upstream by eye (pixel equality is not required), a scripted run passes on a
hidden desktop, and idle memory and CPU have been measured.

Scripted runs and tests stay off the network or use a scripted localhost server, and
never reach production. Never create accounts on production. No part of this has run
against a real `server/` yet; do that once a local build of it is available.

## Differences from upstream

- No input log. The advanced settings keep a diagnostic log without input data.
- "키보드 문자열 구분" is replaced by the Detail mode toggle, off by default: only with it on
  are the row of a key and the special keys sent
  (`docs/adr/0003-presence-mode-by-default.md`). The row is the key's place on the keyboard,
  so another layout than QWERTY can give another row than upstream's.
- No "최소화 시 절전 모드" toggle: it switches a WebView2 memory mode. Render
  resources are always released while the window is hidden, and after 30 s hidden so is
  the GPU driver's memory; see `docs/research/memory-floor.md`.
- Escape closes every dialog that has a cancel action.
- More text can be selected and copied: besides chat messages and the fields, the changelog and a Repository's key
  fingerprint, and a question's words with Ctrl+C, as in a Windows message box. Fields select by word and paragraph
  with a double or triple click and move and delete by word with Ctrl.
- Text has a context menu, where upstream suppresses every one: by a right-click, the menu key or Shift+F10, drawn
  from the Skin Pack. A field offers Cut, Copy, Paste, Delete and Select all, read-only text Copy and Select all, and
  a link in chat its address first. What cannot be done is greyed. Cloud's fades in over 120 ms.
- The keyboard alone works the controls, where upstream has no keyboard navigation. Tab and Shift+Tab go from control
  to control, staying inside what is open on top, and Enter and Space act. The arrows, Page keys, Home and End move
  sliders, dropdowns, tabs and the emoji grid, and Ctrl+Tab changes the settings tab, as in Windows. Where the keyboard
  is shows after Tab, an arrow or Alt, drawn by the Skin Pack. A layer that opens takes the keyboard and gives it back
  when it closes; a question that cannot be undone starts on Cancel.
- A link in chat has a colour of its own, the Palette's `link`, where upstream's is the text's and shows only by
  an underline under the pointer.
- The channel balance is explained: "자리에 따라 좌우로 듣기" and "좌우 차이", in words
  instead of a ratio, and locked while virtual surround is off, when it changes nothing.
  Its settings keys are upstream's, but a value means more: straight to the side, 60 is 6 dB
  between the ears and 90 is 19 dB, so the default can be heard. The same value sounds weaker in
  upstream.
- The sound is built for working side by side, on panning rather than echo. Members sit
  far apart left to right while Desks are free, not at random. Reverb is a list of its own,
  "울림": 끔, 사무실 (the default, short and quiet), 넓은 공간 (the old one). It is kept under a
  key of this client's own and does not depend on virtual surround.
- Logging out inside a Room says it leaves the Room, then does so. Nickname and
  handle can be changed without leaving.
- The first-run prompt shows only to someone with no stored identity: a saved
  nickname, Guest ID or token goes straight to Home, and an account's details are
  refreshed in the background. Upstream waits for `api/me` before showing anything.
- Logging out, or a rejected token, also erases the Guest ID, so the next start asks
  again. A token refused on connecting leads to the sign-in prompt at once.
- HTTP redirects are not followed, so a password never goes to another address.
- The emoji picker from the account tab dims the whole window, not only the settings
  panel, so the droplet is not usable under it.
- The first time the window hides to the tray while in a Room, a notification says
  the Room is still connected, because input timing keeps being shared.
- Reachability is not polled in the background. `GET /health` runs at startup, after a
  connection that failed without reaching the server, and every 5 s while the offline
  popup is up. A server lost while on Home shows at the next attempt.
- Reconnecting backs off and gives up at the server's grace instead of retrying every
  3 s forever; it does not ask `/health` first. A connection error names no reason in
  parentheses, because the transport reports none.
- A speaking disc costs less than upstream's: held up by someone who keeps typing it draws
  no frame, and otherwise only the avatars are drawn.
- A Room that cannot be resumed at startup says so; upstream returns to Home silently.
- The program calls itself Cloudroom++, so its autostart entry does not collide with upstream's.
- The system tab's advanced section has "뒤에 있을 때 메모리 비우기" (at once, 10 s, 1 min, never), kept
  under a key of this client's own. Behind another program, outside a Room, the window lets go of its
  graphics device after that wait and makes it again when it next draws.
- Assets may be missing without stopping the app; see ADR 0004.
- Motion upstream does not have: wheel scrolling settles over 160 ms, the ends stretch up
  to 48 px and return over 240 ms, screens cross-fade over 180 ms with an 8 px rise,
  settings tabs over 140 ms, the settings panel and emoji picker over 160 ms with 6 px,
  a dropdown fades in over 120 ms, and a new chat message rises 8 px over 300 ms. Scrolled
  areas fade at the edge they cut, by up to 27 px. All of it follows the system's "Show
  animations" setting; there is no toggle of its own.
- The app is named Cloudroom++, and its data stays in the same folders as upstream's.
- Every JSON file this client reads may hold comments and a comma after the last item. It writes plain
  JSON, so upstream reads what it saved, but not a file someone commented by hand. Saving drops comments.
- English besides Korean, chosen in a section of its own; it follows the OS language until then.
  Other words for the same things: 구름 is "Room", 사용자명 is "handle".
- The wording is gentler than upstream's: Korean speaks in ~해요 and ~할게요, with plainer names (외관 for
  디스플레이, 대화 for 채팅, 나가기 for 떠나기), and English says "we" and "please". "진정하세요" stays as it is,
  as the app's own catchphrase.
- The time section (24시간제, 자리비움으로 바뀌는 시간) and the language section are on the display tab, after the
  chat size, where upstream has time on the system tab. They are about what the screen shows.
- The changelog comes from GitHub release notes, so an empty one says it could not be
  loaded, and a first run ever does not open it. A new version's notes open when the app
  first notices it, not after the update, and not again once it is installed. They are
  the release's most important changes, then its commits by kind.
- A user picks a Theme and a dark mode (off, on, following the system) in the display tab, where
  upstream has a toggle and lists of light and dark Themes. One list holds every Skin's Themes, each Skin's
  under a heading with its name between lines, so a Theme brings its Skin. They are read from the packs in
  `skins/` beside the exe and kept under `skin`, `skinTheme` and `darkMode`; until those are set upstream's
  `theme`, `systemTheme` and `lightTheme` are read, and the choice never writes them. The dark mode is
  locked while a Theme has no dark variant, keeping its value. A Theme of a pack not yet installed is in the
  list with its size, and is fetched when picked, with progress and a cancel under the list.
- Skins come from Repositories (ADR 0008), listed in the system tab: the official one, which stays, and those
  the user adds by an HTTPS address or `github:owner/repo[@ref]`, after a dialog with the key's fingerprint.
  A pack of an added one is `<first 16 digits of the fingerprint>/<id>`. A pack may extend one of another
  Repository, pinned by its key's fingerprint and maybe a version and digest; an update it was not made for
  waits in the system tab until the user takes it. An added Repository may offer Sound packs too, which the sound
  tab fetches when picked. Upstream has none of this.
- Upstream's Theme 구름 is 뭉게구름 ("Cumulus"), as the Skin it belongs to is 구름 too. Its id is `cumulus`;
  upstream's keys keep upstream's `cloudroom`, which is read as `cumulus` wherever it is found.
- The create cloud on Home really grows under the pointer, as upstream's does.
- A second Skin upstream does not have, Retro, in the look of the desktops of the 1990s and early 2000s
  (`docs/research/retro-messenger-look.md`, `docs/research/retro-themes.md`). Values and shapes are re-created
  as vectors, and nothing is taken from Windows or the old programs: no logo, person icon, sound or font of
  theirs. 구름 stays the default. Under Retro:
  - The app draws its window frame, with minimize and close only. Those two are ordinary hits in
    the client area, not caption buttons.
  - The window has a size per screen, remembered per screen under `window<Screen>Width` and
    `Height`, so it shrinks to Home and grows to a Room. Cloud's saved size is left alone.
  - Options, What's new, a profile, the emoji picker, the offline message and every question are
    real windows, owned by the main one and without a taskbar button. A question holds the others
    off until it is answered. Only messages inside the main window, menus and popups stay in it.
  - Home lists the Seats to return to, where Cloud asks once in a dialog.
  - A menu bar holds only commands that work, by mouse and Escape; there are no Alt keys yet.
  - Only links show the hand cursor, and none of the motion listed above runs.
  - Text is in the fonts of the time on this system, not Pretendard.
  - A message and someone entering sound as tones made in `synth`, not the pack's recordings: plain tones
    under classic, and bells under Luna.
  - The pointer is drawn here too, and only in the app's windows: seven vector cursors of the Skin Pack, made into
    system cursors when first needed. "테마 커서 쓰기" in the display tab, on by default and shown only for a Theme
    with cursors, gives the system's back (`themeCursor`, a key of this client's own).

## Build

```
.github/scripts/msvc-build.sh build-msvc
ctest --test-dir build-msvc --output-on-failure
```

The script imports the developer environment (vcvars) into bash; `cl` is not on PATH by default.
The app is a Windows-subsystem program with no console, which Git Bash waits for; read `--smoke`'s
result from the exit code. `build-msvc/apps/gui/crpp_hidden_run.exe <seconds> <exe> <args...>` runs
it on a hidden desktop, ends it after that many seconds with 124, and otherwise returns its code.

Options are read from the end of the command line: `crpp --shot <png> <size> <hour> <scene>
--data <dir>`. Put first, `--data` is ignored and the real app opens on the user's desktop.
A shot is in Korean and in the Theme of its scene unless `--locale <id>` or
`--theme <id>` says otherwise. `auto` for the size renders the window size of the Skin's screen.
A scene that opens a panel with a window of its own renders that window. Under `--skin-dir`, the scene `cursors`, which
is not in `scenes.txt`, is a sheet of the pack's cursors.

A normal run offers every pack in `skins/` beside the exe; with none, it shows only the first-run screen. A shot
without `--skin-dir` draws from the pack beside the exe that has its `--theme`, or from the default one.
`--skin-dir <dir>` draws from the unsigned Skin Pack in that folder and offers only that pack.

Nothing may wait for a person in a scripted run. On a desktop nobody sees the app shows no dialog: when
it cannot start, as when it refuses a pack, it exits with 3 and leaves the reason in `<png>.error`, or
in `start-failure.txt` under `--data`. `render.sh` stops there and at the first scene that takes 30 s.

A change that must leave the screen as it was can prove it: `tests/shots/render.sh` renders
every scene in `scenes.txt` on a hidden desktop, before and after, and `compare.sh` compares
the folders by hash. A shot holds the clock, so the same scene always gives the same file.

A development build, any build without `CRPP_OFFICIAL_BUILD`, runs beside whatever is already
running, the user's app included: every start opens a new window. Without `--data` it keeps its
data in `data/` beside the exe, so each build folder has its own and the user's stays untouched.
It never writes the Run key, and its title and tray tip say `(dev)`. A second start bringing the
first forward, and the real Run key, therefore happen only in an official build. It still talks to
the default server. A running exe cannot be linked over (LNK1104): build into another folder.

`crpp --data <dir>` keeps a run's data in `<dir>`, and with no
`serverUrl` in it skips the startup `/health`, so scripted runs stay off the default
server; seed `serverUrl` in its `crpp/settings.json` to test the offline popup. That covers only
`/health`: a click on creating or joining a Room still goes to the default server, which is production,
so a run that clicks needs a `serverUrl` on localhost. An exe with an
`assets/` folder holding a `catalog.txt` beside it, as every build has, runs from that folder and
`skins/` and installs nothing. To test installing, build with `-DCRPP_UPDATE_TEST_KEY=<hex>`, write a
Repository with `crpp_catalog`, sign it with `.github/scripts/sign-catalog.sh` and the test key's file, serve it
on localhost and copy the exe elsewhere: `--asset-url <folder>` uses it, signed with the build's key.
Rebuild without the test key afterwards.

Scripted runs of the app go on a hidden Win32 desktop (`CreateDesktop`,
`STARTUPINFO.lpDesktop`, `EnumDesktopWindows`), so no window reaches the user's
desktop. `PostMessage` still reaches it, but Raw Input arrives only on the input
desktop: drive a Room with a windowless second member instead of injected keys.

## Module design

Logic modules are pure and I/O-free; a seam exists only where two adapters
exist (real + test).

- `locale`: `tr()` is the table of the current Locale, plain `const char*` fields; `fill` puts
  values into `{0}` and so on. A Locale also decides its line breaks and word lists. To add one,
  copy `en.cpp` and list it in `locale.cpp`; a test finds what is missing. A new field also needs
  a line in `keys.cpp`, the names a Skin Pack refers to wording by; the build says so. Pure.
- `wire`: parse/encode server and client messages and input frames. Pure.
  Frames are at most 1024 bytes and at most 32 input events; use fixed buffers.
- `session`: pure state machine `step(Event) -> Actions` (connect order,
  resume/ticket, seat grace, heartbeat, backoff, 100 ms input throttle).
  Reconnect waits 1 s doubling to 30 s with jitter, and stops at the server's
  15-minute `OFFLINE_GRACE`, on an auth failure, or when the Room is closed.
- `transport`: WinHTTP WebSocket/HTTPS adapter with one receive thread; fake
  adapter for tests.
- `input`: Raw Input (`RIDEV_INPUTSINK`) adapter on its own thread with a
  message-only window; scripted adapter for tests. This is the lowest level
  reachable without a driver; drivers and low-level hooks are rejected. It runs
  only while in a Room. The raw buffer is wiped, held-key bits are the only key
  state, and only `Kind` crosses the seam. Coordinates are never read; Pen reads
  only the tip and in-range bits. There is no input log. Reports are read in
  batches: the first event after a pause at once, then every 10 ms at most. A
  mouse move costs about 8,700 cycles this way, against 50,000 read one by one.
- `sound`: pack loading and a pure `render(span<float>)` mixer; device adapter
  behind it. Packs are read from the official folder first, then each added Repository's (`PackRoot`), so a
  member's pack this client lacks plays the device's first official one. `synth` makes a short stand-in for a recording that could not be fetched. The stream uses the default period (about 10 ms), not the smallest
  the research note suggests: CPU comes first and network delay dwarfs the gain.
- `store`: settings, Seats and chat cache in the Protected or Shared store; a
  flushed temp file + rename for every write. `Platform` is the seam for DPAPI
  and file replacement.
- `skin`: Skin Packs (`docs/adr/0007-skins-are-downloaded-data.md`). `read_cskin` reads a pack's files, in the pack
  language of `skins/README.md`, into a tree, and `load_pack` turns that into a compact one and checks every name in it, under limits on size and depth, so that drawing has
  nothing left to refuse. `draw` walks a screen; `Canvas` is the seam for drawing and `Source` for what
  the screen shows. `pack.hpp` describes the format. Expressions are compiled to a short program, and
  running one allocates nothing. Pure. A screen says what a pack can name in a `ScreenSchema`, keeps
  its state, hit ids and callbacks, and `SkinCanvas` in `ui` draws on a `Painter`. `skins/` holds the packs, and how to write one in its README.
  `keys.hpp` lists what a node may have, for the reader and the loader. `crpp_skin check <dir>` loads a pack as the
  program does and says its first mistake as `file:line`, and tests run it on the official packs; `crpp_skin names`
  lists what each screen offers.
  A panel with a window of its own reports its size, and `measure_screen` walks the screen without drawing to learn it. What the
  program itself must know of a look, as a frame's sizes and each window's, is in constants that `required_constants` lists; fonts and cursors have sections of their own.
- `assets`: a Repository's signed catalog (`catalog.hpp`, ADR 0008) and the files it lists in groups:
  `shared`, which holds the official Sound packs, emoji and word lists, one `skin:<id>` per Skin Pack, and
  `sound:<id>`, which the official catalog does not use. `install` fetches the groups asked for from the
  Repository, or a file's second source when the Repository fails, verifies each and keeps what
  already matches. The exe holds only the official Repository's address and the update key
  (`built_in.hpp`); `crpp_catalog` writes a catalog from `assets/`, `skins/`, another Repository's `--sounds` and
  `cmake/remote_assets.txt`, and `.github/scripts/build-repository.sh` fills and signs the official
  one, and `sign-catalog.sh` prints its key's fingerprint. A Repository the user adds (`folder_of` reads
  its address) offers only packs, in folders of their own, and is trusted by the key its catalog states
  (`fetch_new_catalog`) once the user has seen the fingerprint; `apps/gui/area_repositories.cpp` keeps each
  in `repositories/<first 16 digits>/` of the Protected store. Behind the app at every start what is
  installed follows each Repository's latest catalog, used from the next start (`held` keeps an open font for
  then, or a pack held back); `kept_groups` is what an added one keeps, and `uninstall` removes a pack. `pack_chain`
  follows a pack to its parents across Repositories, and `check_pin` tells whether a parent still holds to a pin
  (`own_digest`, which `crpp_catalog --digest` prints). `Platform` is the seam for
  WinHTTP and BCrypt.
- `api`: the account HTTP API as pure requests and responses, and `Flow`, the
  prompt's steps. `HttpClient` is the seam for WinHTTP; `Worker` keeps its blocking
  calls off the UI thread. `apps/gui/account_controller` joins it to `ui`.
- `update`: version check, download, signature and SHA-256 (BCrypt) verify,
  self-replace, Markdown of the release notes. Only official builds with a real key go
  online. Key: `docs/signing-key.md`. Releasing: `docs/releases/README.md`. To try the
  flow, build with `-DCRPP_UPDATE_TEST_KEY=<hex>` and run with `--update-url`; never
  start an official build in a test, as it asks GitHub.
- `ui`: every part of the screen is a `Surface` with its own file, hit ids, state
  and callbacks; `App` only routes input through the layers on screen. A click runs when the button
  comes up on the hit it went down on; `Surface::acts_on_press` names what cannot wait: what is dragged
  or scrolled by holding, a field and what opens a list, and `repeats` what goes on acting while held,
  on a timer that runs only until the button comes up. `controls.hpp`
  has the shared controls, `Dialogs` the modal question, `Toasts` the messages that
  expire, `Prefs` the typed settings keys. `src/ui/tests/ui_harness.hpp` drives an
  `App` without a window. `apps/gui/area_*.cpp` is the shell's side of each feature:
  wiring, `--shot` scenes, and reactions to changed settings.
  Native rendering; redraw on demand only. Per-Monitor
  v2, set by manifest: `App` works entirely in DIPs and the render target's DPI
  maps them, so only the shell in `apps/gui` converts pixels. The saved window
  size is in DIPs. `--shot ... --dpi <n>` renders any scale. `WM_DPICHANGED`
  cannot be sent from another process; test it by moving the window between
  monitors of different DPI.
  Geometry that lasts beyond a frame lives in `Gfx::path`: anything made from the factory
  keeps it, and the factory keeps the GPU device, so `App::release_graphics` must be able
  to let go of them all. A test counts the factory's references to prove it.
  Motion runs on `clock_ms()` (QPC). Anything still moving says so with `Ui::more_frames`
  while painting or with `animating()`; only then does the shell draw the next frame,
  paced by the display, so a still window draws nothing. The render target retains its frame,
  and a frame clips to what `WM_PAINT` was asked to draw: speaking discs invalidate only
  themselves, with `App::only_moving_area`. The whole frame is still walked for its hits. `Scroll` in `controls.hpp` is
  every scroll area's wheel, stretch and edge fade. A motion's first frame has made no
  progress yet, so it must ask for the next frame from its start. `--shot ...
  motion-<name> --at <ms>` renders a moment of a motion; `apps/gui/shot.cpp` names them.
  Every screen, control and window frame is drawn from the Skin Pack in use, through `SkinCanvas`
  (`docs/adr/0005-skins-own-shape-and-layout.md`, ADR 0007); a surface keeps its state, hit ids and
  callbacks whatever the pack. The program has one look of its own, the first-run screen's plain one in
  `setup_screen.cpp`, for before any pack is there. What it reads of a pack's whole look is
  `Ui::scales_stage`, `Ui::moves` and its constants, and of a Theme only `Theme`'s dark and sky. `window_frame.cpp` says what each
  point of an own-drawn frame is, and the shell answers `WM_NCHITTEST` from it. `App::screen_size` names
  the size of the current screen, and the shell fits the window to it when a frame starts.
  With an own frame `App::open_panes` says which panels should have a window, and `pane_window.cpp`
  makes that true on a posted message, so a window is never destroyed under its own click. There is one
  `Ui`: what differs per window is `WindowState`, swapped by `App::use_pane`, and a window's messages run
  inside a `PaneScope`. Tests go there with `UiHarness::on_pane`. A pack with its own frame draws boxes
  and lines on whole pixels through `Painter`, so equal sizes stay equal at any scale; a part of several
  lines counts from `Painter::snap` in `Painter::line`.

## Performance rules

- Idle means blocked in `GetMessage`/socket receive; no polling timers
  (use device notifications instead of periodic device checks).
- No heap allocation on the input path.
- Resample WAV once at load to the device rate; load pack variants lazily.
- Mixer does no work when no voice is active.
- Measure idle memory/CPU of every UI change; do not trust third-party numbers.
- Nearly all memory while visible is the GPU driver's: an empty hardware Direct2D window is
  already 58 MB private against the app's 63. Software rendering is not the way down: it
  takes about 10 times the CPU to scroll, and owning the D3D11 device saves nothing. Findings:
  `docs/research/memory-floor.md`.
- Outside a Room a shown window gives its pages back 10 s after its last still frame: Task
  Manager's Memory falls from about 17 MB to under 1 MB, and a hover brings about 6 back. Commit
  stays. Behind another program and not under the pointer it also lets go of the graphics device,
  at once by default (`releaseGraphicsAfter`): commit 65 to 12 MB, and each return costs about
  140 ms of CPU once.
- Frames have no cap of their own. Measured with an MSVC Release build at 125% on a hidden
  desktop, settings open: idle 0.0% of one core and about 47 MB working set; wheel
  scrolling without pause 0.6-1.9% at about 73 frames/s (a counter confirmed the frames),
  back to 0.0% afterwards. A real desktop adds composition, which this does not show.
- Retro, the same way with the wheel posted about 60 times a second, reversing every
  20 notches, on the options window: idle 0.0%, about 43 MB working set and 59 MB private; scrolling
  1.3-3.4%; 0.0% afterwards. With the options window open the second render target costs about 8 MB of
  working set (about 52 MB), until it closes. Cloud under the same harder wheel: idle 0.0% and about 45 MB; scrolling
  5.9-12.5%, where a build from before Skins gave 8.4-10.6%, so that is the stimulus, not a regression.

## Transport facts (verified on Windows 10 19044)

- WinHTTP answers server pings itself, never reports them and cannot send
  one. `WinHttpTransport::capabilities()` says so and `client::Client` then
  disables Session's ping and liveness timers. WinHTTP's own keepalive is a
  15 s unsolicited pong, so dead-peer detection relies on TCP failing.
- Aborting a blocked receive by closing the handles from another thread works
  here but is outside the documented rules; the transport tests pin it.
- `crpp_cli` against the production server (wss) created a private room and
  got its own chat message back, built with both MSVC and GCC.

## Verification debt

- Pen input via Raw Input needs device checks, including devices that expose no
  HID digitizer collection. If a Pen tap also arrives as a mouse click on a real
  device, suppress clicks briefly after tip activity; add this only once
  reproduced.
- Raw Input reports nothing while an elevated window has focus, as seen with Task Manager, which
  elevates without a prompt for an administrator (High integrity against the app's Medium). Microsoft
  lists reading raw input "for all integrity levels" among what only uiAccess grants, and uiAccess
  needs a code signature and an install under Program Files. Keyboard sound apps and Discord document
  the same limit and ask to run as administrator. The limit is accepted: no elevation, no uiAccess
  until the exe is signed and installed. Confirm a game that is not elevated still reports.
- IME composition and candidate windows at a scale above 100%: they get physical
  client pixels, which Microsoft's DPI pages do not state for the IME calls.
- Output device changes (plug, unplug, default change) were never exercised on real
  hardware: only listing and opening every device is. Nor has the idle mute run a
  full idle period in a real Room. It ends on a key or click, not on mouse movement
  alone: Raw Input only reports Input kinds.
- The sound of spatial audio and reverb needs ears: run
  `crpp_soundtest assets/sounds`, which walks every Desk; `--balance` and `--reverb` compare
  values. The left-right difference and Office's level and decay are starting points until then. `--synth` plays the generated
  stand-ins instead, which also need ears.
- Following the OS theme: `WM_SETTINGCHANGE` was only posted in a scripted run.
  Flipping the real Windows setting and seeing the dark title bar are unverified.
- The tray needs a real desktop: a hidden one has no taskbar, so the icon fails to
  add and a close exits. A chat notification was seen on one Windows 10 PC, with a window
  of a fake Room behind another. Upstream's developer saw none, on a machine not yet looked
  at; a notification respects Focus Assist. Menu, flash, the Run key and a second start have
  only been tested as decisions, not seen.
- Partial frames (a speaking disc over a retained frame) were measured on a hidden desktop,
  where one person typing went from 16.7% GPU to 0.0% and five at once from 17.5% to 10.7%.
  A real display adds composition, and Intel and AMD drivers have not run them.
- Letting go of the device while shown was seen to come back quickly on one Windows 10 PC with an
  NVIDIA card. Intel, AMD and hybrid laptops have not run it, and the pointer guard, which a hidden
  desktop cannot exercise, has not been watched. The trim when quiet has not run under memory
  pressure, where its pages may come back from disk.
- Coming back from a hidden window that gave up its graphics has only run on a hidden
  desktop, on one NVIDIA card. See it on a real desktop, and on Intel and AMD drivers. The
  same goes for a chat notification drawn in that state.
- The own-drawn window frame and the panels' windows have only run on a hidden desktop, where hit
  tests, the region, sizes, ownership and a question holding the others off were checked. A window
  corner is cut by a region, which has no partial pixels: those it only partly covers are blended
  with a grey, not with what is behind. See on a real desktop: focus and z-order between the windows, Snap, resizing by each edge, minimize and
  restore, Alt+Space, a right-click on the caption, moving between monitors of different DPI, the
  taskbar title, and what the region does to the shadow. Windows 11 has not run it at all, nor have
  Intel and AMD drivers, and a Room with real members has not been seen under these Themes.
- Following the OS theme with a Theme of another Skin on the other side has only run in tests: the
  frame and window size should change with it.
- The tones of Retro's Themes need ears: `crpp_soundtest --chimes`, bells and then plain tones.
- Retro's colours for Homestead, Metallic and the dark variants are drawn here (`docs/research/retro-themes.md`),
  and only some of their screens have been seen, on a hidden desktop. Choosing a Skin and going from one to
  another has run in tests and on a hidden desktop only: see it on a real one.
- Cloud from its pack gave the old C++ layout's image in every scene, moments of motion included, which proves the
  look and not the feel. Try scrolling, hovering and every transition by hand.
- The pack's cursors have only been drawn into bitmaps, with `DrawIconEx` for the blending. See on a real desktop: their edges,
  whether a resize keeps them during the drag, and whether the system puts its pointer shadow on them; Microsoft does not say,
  so they draw none. `CursorBaseSize` for "Pointer size" is undocumented and taken from what Qt does; try other sizes, and whether
  changing it sends `WM_SETTINGCHANGE`. Microsoft's sample calls a cursor's pixels premultiplied, while `DrawIconEx` blends them as straight.
- Dead-peer detection (network cut) is untested.
- Shared store compatibility is tested against upstream's format as read from
  its source, not yet against the upstream client itself.
- Accounts are tested with a fake HTTP seam and a scripted localhost server, never
  against a real `server/`.
- Re-check C++26 status against WG21 primary records.
- Motion has been compared frame by frame and measured, not felt: how scrolling and
  transitions feel needs the user's own hands, on a real display.
- The generated stand-in sounds need ears: `crpp_soundtest --synth`.
- Installing from a Repository has run on a hidden desktop only: against a local server a first start,
  a second that fetched nothing, a failure with no server, and "다시 시도" once it was back; against the
  official Repository (deployed 2026-11-08, on `https://crpp.kawala.in/` since) a first start that took
  14 s for 15.3 MB on one connection, and again after v0.2.0's release script deployed it. The second
  source ran once: with only v0.2.0's catalog served on localhost, all 327 files came from
  raw.githubusercontent.com, at the tag and at the third-party pins. A release of the exe from GitHub has not
  been installed by updating from v0.1.1 yet.
- Adding a Repository, fetching a Theme of it and removing it, updates behind the app and removing a
  downloaded Skin have run on a hidden desktop against localhost only, driven by a throwaway build, not by
  hand. So have a parent in another Repository, a pin that asks first or holds an update back, adding a
  parent's Repository with its child, a key that changed, and picking, failing and removing another
  Repository's Sound pack; a member playing one in a Room has not run. An update of the font itself, which waits a start, has only run in tests. A `github:` address has not been added from a real
  repository (raw.githubusercontent.com answered `HEAD` and a tag without a redirect), and the field, the
  dialog's long fingerprint and a pack's progress have not been seen on a real desktop.
- v0.1.0 updated itself to v0.1.1 from GitHub on one Windows 10 PC with Defender. The cause
  of the detection in `docs/releases/README.md` is unknown, and it may come back with any
  release. The exe has no code signature, so try another machine and SmartScreen.
