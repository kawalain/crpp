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
- Language baseline is C++23, the newest published ISO standard that both MSVC and GCC
  implement, as the global toolchain rule asks. Do not depend on C++26 (the DIS ballot was
  reported as failed; contracts, reflection and `std::execution` are not
  portable yet). Errors use `std::expected`, not exceptions.
- Upstream server code (`server/src/main.rs`) is the protocol spec. The
  client's `protocol.rs` is stale (it lacks kinds 11-13, 24-26, 49).

- First release targets Windows only, but the code is written to be
  cross-platform: pure modules are portable ISO C++23 with no platform
  headers; every OS API lives in an adapter behind a seam (`transport`,
  `input`, `AudioDevice`, `ui`, tray/notification). macOS/Linux adapters are
  added later without touching pure modules.
- Windows builds use MSVC from Build Tools 2026 (`/std:c++latest` via CMake), locally
  too, and the runner `windows-2025-vs2026`; Build Tools 2022 is not supported. MSVC's
  `/std:c++latest` also turns on parts of the C++26 draft, so only `portability-gcc`
  catches C++26 in pure modules. Once MSVC 14.52 ships `/std:c++23` and CMake maps
  C++23 to it, move to it. The CI job `portability-gcc` builds and tests the pure modules with GCC on a
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
  Dependency direction, as CMake links them: `session`, `input`, `sound -> wire`; `client -> session,
  transport`; `api -> store, locale`; `update -> store`; `skin -> store, locale`; `assets -> files, update, skin`;
  `ui -> session, update, locale, skin, files`; `sound -> files` too. `store` is also the JSON reader of `api`, `update` and `skin`.
  `transport` and `input` are reached from `session` only through interfaces, and only `apps/gui` links
  every adapter. Nothing lower may come to depend on `ui` or `assets`.
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
  chat rows) the durations are ours, listed in `docs/differences-from-upstream.md`. App font: Pretendard GOV Variable.
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

What this client does otherwise than upstream, and why, is listed in
`docs/differences-from-upstream.md`. Read it before changing what a user sees, and add to it in the
commit that makes a new difference. A setting this client adds is a key of its own beside
upstream's, which upstream may drop when it saves a Shared store.

## Build

```
.github/scripts/msvc-build.sh build-msvc
ctest --test-dir build-msvc --output-on-failure
```

The script imports the developer environment (vcvars) into bash; `cl` is not on PATH by default.
The app is a Windows-subsystem program with no console, which Git Bash waits for; read `--smoke`'s
result from the exit code. `build-msvc/apps/gui/crpp_hidden_run.exe <seconds> <exe> <args...>` runs
it on a hidden desktop, ends it after that many seconds with 124, and otherwise returns its code.
The desktop stays for two minutes after the last run, held by a windowless `crpp_hidden_run --hold`:
every desktop made and destroyed leaves composition objects in dwm.exe and the kernel that only a
restart frees, and one per scene slowed dwm.exe down over a day of renders.

An option with a value stands anywhere on the command line: `crpp --shot <png> <size> <hour> <scene>
--data <dir>`, or `--data <dir>` first. What says what to do keeps its order. A word with dashes that
is no option ends the start with 3 and no window, and says why in `<png>.error` or
`start-failure.txt` (`apps/gui/options.hpp`).
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

`-DCRPP_SANITIZE=ON` (`cmake/sanitize.cmake`) builds with AddressSanitizer, and with GCC also
UBSan; the GCC job runs the tests that way. With MSVC it is a diagnostic build in a scratch folder
outside the repository, deleted afterwards: `msvc-build.sh <scratch>/asan -DCRPP_SANITIZE=ON`, then
`ctest`, which finds the runtime by itself. The exe loads it from beside cl.exe, so copy
`clang_rt.asan_dynamic-x86_64.dll` next to it before `render.sh`, and give `ASAN_OPTIONS` a
`log_path` without a drive letter, as `:` separates its options.

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
so a run that clicks needs a `serverUrl` on localhost. A development build with an
`assets/` folder holding a `catalog.txt` beside it, as every build has, runs from that folder and
`skins/` and installs nothing; an official build never reads them, as nobody signed them. To test installing, build with `-DCRPP_UPDATE_TEST_KEY=<hex>`, fill
a folder that `git init` made with `.github/scripts/build-repository.sh <version> --github <folder> --out <worker>
--public <hex> --key <test key file>`, serve that folder on localhost and copy the exe elsewhere: `--asset-url
<folder>` reads its `catalog-<format>.txt`, signed with the build's key. `<worker>` is the Worker's half, which
`wrangler dev` serves. Rebuild with `-DCRPP_UPDATE_TEST_KEY=` afterwards.

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
- `winhttp`: what the WinHTTP adapters of `transport`, `api`, `assets` and `update` do the same way: a handle
  that closes itself, a session with one timeout and one rule for redirects, a status, a body up to a
  limit. A new request goes through it; an adapter keeps only what is its own. Windows only.
- `win`: what every Windows adapter owns, freed on every path: `ComPtr` for COM, and `Own` with the
  aliases of `handles.hpp` (`Handle`, `CoTaskMem`, `LocalMem`, `GdiObject`, `MemoryDc`, `Icon`, `Menu`)
  for handles and system memory. A new resource gets an alias there rather than a free call by hand.
  Header only; `own.hpp` and `com_ptr.hpp` include no Windows header, so their test runs anywhere.
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
  `keys.hpp` lists what a node may have, for the reader and the loader. `naming.hpp` says how a pack, a
  Theme and a version are written and what a shown name may hold, for the loader and for `assets`' catalog alike. `crpp_skin check <dir>` loads a pack as the
  program does and says its first mistake as `file:line`, and tests run it on the official packs; `crpp_skin names`
  lists what each screen offers.
  A panel with a window of its own reports its size, and `measure_screen` walks the screen without drawing to learn it. What the
  program itself must know of a look, as a frame's sizes and each window's, is in constants that `required_constants` lists; fonts and cursors have sections of their own.
- `files`: `Tree`, a path a catalog gives a file and where the file really is. Whatever reads a downloaded file asks
  one, as installed files are named by their hashes and a development build reads its source folders. Pure.
- `assets`: a Repository's signed catalog (`catalog.hpp`, ADR 0008) and the files it lists in groups:
  `shared`, which holds the official Sound packs, emoji and word lists, one `skin:<id>` per Skin Pack, and
  `sound:<id>`, which the official catalog does not use. `install` fetches the groups asked for from the
  Repository, or a file's second source when the Repository fails, verifies each and keeps what
  already matches. A Repository serves each file as `<folder><sha256>`, then its catalog's `mirror` does (ADR 0010);
  what is installed is kept once in the store `files/<sha256>` of the Protected store, which every Repository shares,
  and each Repository's folder lists its paths with their hashes (`installed_tree`). At start what an older exe
  installed at its paths moves into the store (`move_into_store`) and what no list names leaves it
  (`collect_garbage`). The exe holds only the official Repository's address, its mirror and the update key
  (`built_in.hpp`): `kawalain/crpp-repository` on GitHub, mirrored by the Worker at `crpp.kawala.in/s/`, with a
  `catalog-<format>.txt` per pack format. `crpp_catalog` writes a catalog, and with `--copy` the folder to serve,
  from `assets/`, `skins/`, another Repository's `--sounds` and `cmake/remote_assets.txt`;
  `.github/scripts/build-repository.sh` fills and signs the official one and the Worker's half, which keeps the
  frozen catalogs of v0.2.x and v0.3.0 (`.github/frozen-catalogs/`) answering from `s/`, and `sign-catalog.sh`
  prints its key's fingerprint. A Repository the user adds (`folder_of` reads
  its address) offers only packs, in folders of their own, and is trusted by the key its catalog states
  (`fetch_new_catalog`) once the user has seen the fingerprint; `apps/gui/area_repositories.cpp` keeps each
  in `repositories/<first 16 digits>/` of the Protected store. Behind the app at every start what is
  installed follows each Repository's latest catalog, used from the next start (`held` keeps a pack held back); `kept_groups` is what an added one keeps, and `uninstall` removes a pack. `pack_chain`
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
  `window_input.hpp` is the pointer, keys and IME of every window of the app, which the main window's and
  the panels' procedures both go through; `options.hpp` reads the command line; and
  `area::sound::tell_server_my_sounds` is the one place the message of my packs and emoji is made.
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
- The exe is built for size (`cmake/size.cmake`): `/O1`, whole-program optimisation, no RTTI and Windows' own
  UCRT took it from 2.21 to 1.54 MB, at no CPU cost that could be measured (`docs/research/binary-size.md`).
  Code that needs RTTI or `/O2` must show it in numbers first.
- Every build is hardened (`cmake/hardening.cmake`): the STL checks bounds (`_MSVC_STL_HARDENING`, with GCC
  `_GLIBCXX_ASSERTIONS`), Control Flow Guard and CET compatibility. It costs 41 KB and nothing measurable in
  CPU (`docs/research/hardening.md`). A hot loop that seems to pay for a check is measured, not exempted.
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

## Known limitations

- A Skin Pack that loads can still leave no way to the settings: it draws every screen, so one that
  leaves out the droplet or the menu, or draws nothing, cannot be changed from inside the app. The
  loader bounds its numbers (`skin::whole`, the ranges of constants), not what it chooses to draw.
  The way back is to delete `skin` and `skinTheme` from `settings.json` in the data folder, or the
  pack's folder. A start that ignores the chosen Skin, as with Shift held, is not built.
- A `chat.bin` that cannot be read is reported as the secrets are: the notice says they could not be
  read, though only the kept chat is gone.
- Sound is resampled from the thirteen rates `parse_wav` takes to whatever rate the device gives. A
  device at a rate that shares few factors with them, which no ordinary device has, would make a
  large resampler: the limit is on the recordings, not on the device.
- No screen reader or high contrast: there is no UI Automation provider (`WM_GETOBJECT`), and
  `SPI_GETHIGHCONTRAST` is not read. The keyboard works every control, but Narrator reads nothing.
- The keyboard cannot reach the Mini window: it never takes focus and has no taskbar button, so it is not
  in Alt+Tab. The tray menu and the mute shortcut do what it does.
- The DLL search order is the default, beyond `/DEPENDENTLOADFLAG` for the exe's own imports.
  `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)` would keep a DLL left beside a downloaded exe
  out of later loads, but Microsoft's pages imply a COM or IME DLL loaded later may then miss DLLs beside
  it, and they do not say which flags COM loads with. Adopt it only after a third-party IME has been
  watched with Process Monitor.

## Verification debt

What has only run in tests, in shots or on a hidden desktop, and still needs a real desktop, real
hardware, ears or a real server, is listed in `docs/verification-debt.md`. Add to it what a change
could not verify, and take out what someone has since seen.
