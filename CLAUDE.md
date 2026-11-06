# crpp

C++ rewrite of the desktop client of `pb29/Cloudroom` (branch `dev`, readable
with `gh`). Priorities, in order: minimal CPU load, minimal memory, stability.
Minimal dependencies. Technical facts come from official references, developer
communities and upstream source, not from memory.

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
- Windows release builds use MSVC (`/std:c++latest` via CMake). GCC/Clang build
  and test the pure modules on every change to keep them portable.
- Build is CMake + Ninja only; no package manager. Third-party code, if ever
  needed, is vendored in `third_party/` at a pinned version.
- Tests use CTest with the tiny `tests/support/check.hpp`; no framework.
  Golden vectors are derived from upstream `server/src/main.rs`.
- Layout: `src/<module>/` (CMake static library) with `src/<module>/tests/`.
  Dependency direction: `ui -> session -> wire`; `transport` and `input` are
  reached from `session` only through interfaces.
- Audio is raw WASAPI with a hand-written WAV parser (no third party).
- `client-apk` and the upstream Rust `app` are out of scope.
- Goal: the same client as upstream (same screens, behavior, settings and
  cache file compatibility at `%LOCALAPPDATA%\Cloudroom\`), one instance at a
  time. Only features that are visible in upstream are implemented; pen input
  stays out until the server supports it.
- Sound engine: raw WASAPI, same sound packs. The spatial/reverb/resampling
  design is not copied from upstream; it is researched from public sources
  and may be better than upstream (see `docs/research/`).
- Every UI element is vector data (paths, SVG); no bitmaps. Animations follow
  upstream CSS durations and easings. App font: Pretendard GOV Variable.
- Distribution differs from upstream: GitHub Actions builds, the app checks the
  latest GitHub Release, and release notes are shown as the changelog. No
  custom installer first; update checking and download come before it.

## Build

```
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++
cmake --build build && ctest --test-dir build --output-on-failure
```

MSVC needs a developer environment (vcvars); `cl` is not on PATH by default.

## Module design

Logic modules are pure and I/O-free; a seam exists only where two adapters
exist (real + test).

- `wire`: parse/encode server and client messages and input frames. Pure.
  Frames are at most 1024 bytes and at most 32 input events; use fixed buffers.
- `session`: pure state machine `step(Event) -> Actions` (connect order,
  resume/ticket, seat grace, heartbeat, backoff, 100 ms input throttle).
- `transport`: WinHTTP WebSocket/HTTPS adapter with one receive thread; fake
  adapter for tests.
- `input`: Raw Input (`RIDEV_INPUTSINK`, no low-level hook) adapter; scripted
  adapter for tests.
- `sound`: pack loading and a pure `render(span<float>)` mixer; device adapter
  behind it.
- `store`: settings and chat cache, atomic write via temp file + rename.
- `update`: version check, download, SHA-256 (BCrypt) verify, installer launch.
- `ui`: view-model driven native rendering; redraw on demand only.

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

- Pen input via Raw Input needs device checks.
- Dead-peer detection (network cut) is untested.
- Confirm the toolchain (VS 2026 or clang-cl) installs on this Windows 10 LTSC
  2021 machine.
- Re-check C++26 status against WG21 primary records.
