# crpp

C++ rewrite of the desktop client of `pb29/Cloudroom` (private; branch `dev`,
readable with `gh`). Priorities, in order: minimal CPU load, minimal memory,
stability. Minimal dependencies. Technical facts come from official references
and developer communities, not from memory.

## Decisions

- UI is native Win32 + Direct2D/DirectWrite. No WebView2.
- Protocol: `cloudroom-v2` only. The v1 subprotocol is not implemented.
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
- Windows release builds use MSVC from Build Tools 2022 (`/std:c++latest` via
  CMake). GCC/Clang build and test the pure modules on every change to keep them
  portable.
- Build is CMake + Ninja only; no package manager. Third-party code, if ever
  needed, is vendored in `third_party/` at a pinned version.
- Tests use CTest with the tiny `tests/support/check.hpp`; no framework.
  Golden vectors are derived from upstream `server/src/main.rs`.
- Layout: `src/<module>/` (CMake static library) with `src/<module>/tests/`.
  Dependency direction: `ui -> session -> wire`; `transport` and `input` are
  reached from `session` only through interfaces.
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
- Assets: see `docs/adr/0002-download-assets-on-first-run.md`. Offline use is
  not a goal: without a server the app opens only to say so.
- Keystroke privacy: see `docs/adr/0003-presence-mode-by-default.md`.
- Sound engine: raw WASAPI, upstream's sound packs. The spatial/reverb/resampling
  design is researched from public sources (see `docs/research/`). Automated
  tests cover numbers and stability; a listening test tool lets the user judge
  quality by ear.
- Every UI element is vector data (paths, SVG); no bitmaps. Animations follow
  upstream CSS durations and easings. App font: Pretendard GOV Variable.
- Distribution differs from upstream: GitHub Actions builds, the app checks the
  latest GitHub Release, and release notes are shown as the changelog. A release
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
- "키보드 문자열 구분" is replaced by the Detail mode toggle: the row of a key is
  never sent (`docs/adr/0003-presence-mode-by-default.md`).
- No "최소화 시 절전 모드" toggle: it switches a WebView2 memory mode. Render
  resources are always released while the window is hidden.
- Escape closes every dialog that has a cancel action.
- Logging out inside a Room says it leaves the Room, then does so. Nickname and
  handle can be changed without leaving.
- The first-run prompt shows only to someone with no stored identity: a saved
  nickname, Guest ID or token goes straight to Home, and an account's details are
  refreshed in the background. Upstream waits for `api/me` before showing anything.
- Logging out, or a rejected token, also erases the Guest ID, so the next start asks
  again. A token refused on connecting leads to the sign-in prompt at once.
- HTTP redirects are not followed, so a password never goes to another address.
- The first time the window hides to the tray while in a Room, a notification says
  the Room is still connected, because input timing keeps being shared.
- Reachability is not polled in the background. `GET /health` runs at startup, after a
  connection that failed without reaching the server, and every 5 s while the offline
  popup is up. A server lost while on Home shows at the next attempt.
- Reconnecting backs off and gives up at the server's grace instead of retrying every
  3 s forever; it does not ask `/health` first. A connection error names no reason in
  parentheses, because the transport reports none.
- A Room that cannot be resumed at startup says so; upstream returns to Home silently.
- The changelog comes from GitHub release notes, so an empty one says it could not be
  loaded, and a first run ever does not open it.

## Build

```
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++
cmake --build build && ctest --test-dir build --output-on-failure
```

MSVC needs a developer environment (vcvars); `cl` is not on PATH by default.
Run GCC-built tests from PowerShell: Git Bash's PATH finds another
`libstdc++-6.dll` first and they die with `0xc0000139`.

`crpp --data <dir>` keeps a run's data out of the user's folders, and with no
`serverUrl` in it skips the startup `/health`, so scripted runs stay off the default
server; seed `serverUrl` in its `crpp/settings.json` to test the offline popup. An exe with an
`assets/` folder beside it, as every build has, runs from that folder; copy the
exe elsewhere to test fetching, with `--asset-url` for a local server.

Scripted runs of the app go on a hidden Win32 desktop (`CreateDesktop`,
`STARTUPINFO.lpDesktop`, `EnumDesktopWindows`), so no window reaches the user's
desktop. `PostMessage` still reaches it, but Raw Input arrives only on the input
desktop: drive a Room with a windowless second member instead of injected keys.

## Module design

Logic modules are pure and I/O-free; a seam exists only where two adapters
exist (real + test).

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
  behind it. The stream uses the default period (about 10 ms), not the smallest
  the research note suggests: CPU comes first and network delay dwarfs the gain.
- `store`: settings, Seats and chat cache in the Protected or Shared store; a
  flushed temp file + rename for every write. `Platform` is the seam for DPAPI
  and file replacement.
- `assets`: fetch and verify files against the manifest CMake builds into the
  exe, pinned to the commit that last changed `assets/`. That commit must be on
  GitHub before a release can install. `Platform` is the seam for WinHTTP and BCrypt.
- `api`: the account HTTP API as pure requests and responses, and `Flow`, the
  prompt's steps. `HttpClient` is the seam for WinHTTP; `Worker` keeps its blocking
  calls off the UI thread. `apps/gui/account_controller` joins it to `ui`.
- `update`: version check, download, signature and SHA-256 (BCrypt) verify,
  self-replace, Markdown of the release notes. Only official builds with a real key go
  online. Key: `docs/signing-key.md`. Releasing: `docs/releases/README.md`. To try the
  flow, build with `-DCRPP_UPDATE_TEST_KEY=<hex>` and run with `--update-url`; never
  start an official build in a test, as it asks GitHub.
- `ui`: every part of the screen is a `Surface` with its own file, hit ids, state
  and callbacks; `App` only routes input through the layers on screen. `controls.hpp`
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

## Performance rules

- Idle means blocked in `GetMessage`/socket receive; no polling timers
  (use device notifications instead of periodic device checks).
- No heap allocation on the input path.
- Resample WAV once at load to the device rate; load pack variants lazily.
- Mixer does no work when no voice is active.
- Measure idle memory/CPU of every UI change; do not trust third-party numbers.

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
- Confirm Raw Input still reports while a game or an elevated window has focus.
- IME composition and candidate windows at a scale above 100%: they get physical
  client pixels, which Microsoft's DPI pages do not state for the IME calls.
- Output device changes (plug, unplug, default change) were never exercised on real
  hardware: only listing and opening every device is. Nor has the idle mute run a
  full idle period in a real Room. It ends on a key or click, not on mouse movement
  alone: Raw Input only reports Input kinds.
- The sound of spatial audio and reverb needs ears: run
  `crpp_soundtest assets/sounds`, which walks every seat.
- Following the OS theme: `WM_SETTINGCHANGE` was only posted in a scripted run.
  Flipping the real Windows setting and seeing the dark title bar are unverified.
- The tray needs a real desktop: a hidden one has no taskbar, so the icon fails to
  add and a close exits. Icon, menu, notifications, flash, the Run key and a second
  start have only been tested as decisions, not seen.
- Dead-peer detection (network cut) is untested.
- Shared store compatibility is tested against upstream's format as read from
  its source, not yet against the upstream client itself.
- Accounts are tested with a fake HTTP seam and a scripted localhost server, never
  against a real `server/`.
- Re-check C++26 status against WG21 primary records.
- The workflows have never run. A first real release must also confirm GitHub's redirect
  for release downloads through WinHTTP, and a replacement under real antivirus.
