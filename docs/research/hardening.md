# Hardening

Measured on 2026-11-10 with MSVC 14.51 (Build Tools 2026), Release builds of `crpp` at commit 72480fe with
the RAII change before it, on one Windows 10 PC with an NVIDIA card. Settings are in `cmake/hardening.cmake`.

## What is on

- `_MSVC_STL_HARDENING=1`: the STL checks `operator[]`, `front()`, `back()` and the like of `vector`, `array`,
  `span`, `string_view`, `optional` and others, and ends the process at a bad one instead of reading past the end.
  MSVC 14.51 defines it as 0 by default (`yvals.h`).
  A failed check calls `abort()` rather than `__fastfail(5)` (`_MSVC_STL_USE_ABORT_AS_DOOM_FUNCTION`,
  `__msvc_doom_core.hpp`), so that the crash handler of `src/crash` writes a report of it; `__fastfail` ends the
  process before any handler in it runs. The check comes before the bad access, so what the report reads is whole.
  It made the exe 11,264 bytes larger (1,690,624 to 1,701,888): each failed check is a call now, not one instruction.
- `/guard:cf`: Control Flow Guard checks the target of every indirect call.
- `/CETCOMPAT`: the exe says it works with CET shadow stacks, so Windows can enforce them where the CPU has them.
- With GCC, `_GLIBCXX_ASSERTIONS`, libstdc++'s counterpart of the STL checks; the portability job builds with it.

`/DYNAMICBASE`, `/HIGHENTROPYVA` and `/NXCOMPAT` were already on, as MSVC's linker defaults. `dumpbin /headers`
shows "Control Flow Guard" and "CET compatible" only in the hardened exe.

## What it costs

| | crpp.exe | Mixer, 200 s of audio | Settings scrolled 600 frames, Cloud | The same, Retro |
| --- | ---: | ---: | ---: | ---: |
| Off | 1,688,064 | 920-967 ms | 8.4-12.1 s | 0.56-1.05 s |
| `/guard:cf` and `/CETCOMPAT` | 1,713,664 | 916-948 ms | 8.1-11.2 s | 0.69-1.25 s |
| All of it | 1,729,536 (+2.5%) | 944-1003 ms | 8.0-11.8 s | 0.56-1.06 s |

- Mixer: 16 Desks triggered in turn with the Office reverb, 10 ms periods at 48 kHz, 13 to 18 runs each, interleaved.
  The STL checks add about 3% at the median, inside the spread of the runs without them (5%). The mixer takes about
  0.5% of one core while sounds play either way, and none while it is idle.
- Scrolling: the settings panel driven through `UiHarness` on a software render target, CPU of the process. Drawing
  is Direct2D's and does not change; the runs differ by more than any setting does.
- All 60 tests pass with the checks on, and every scene in `scenes.txt` renders the same, by hash, as without them.

The benchmark was a throwaway program and is not kept.
