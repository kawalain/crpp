# Binary size

Measured on 2026-11-09 with MSVC 14.44 (Build Tools 2022), a Release build of `crpp` at commit 25a7e8a, on one
Windows 10 PC with an NVIDIA card. Settings are in `cmake/size.cmake`.

## Size

| Settings | crpp.exe |
| --- | ---: |
| `/O2` (CMake's default Release) | 2,208,256 |
| `/O2`, `/GL` + `/LTCG`, `/Gw` | 2,154,496 |
| `/O1`, `/Gw` | 1,883,648 |
| `/O1`, `/GL` + `/LTCG`, `/Gw` | 1,875,968 |
| the same, `/GR-` | 1,789,952 |
| the same, the system's UCRT (hybrid CRT) | **1,537,536** |

v0.2.0 was 2,121,216 bytes. In the last build the code is the program's own: the largest parts are `crpp_ui`
(313 KB of code), `crpp_skin` (161 KB), and the shell's `main.cpp` and `startup.cpp`. The icon is 37 KB of resources.

## What it costs

Nothing that could be measured. With `/O2` and with the final settings:

- The mixer, 16 Desks sounding with reverb, 200 s of audio at 48 kHz: 1349 ms and 1266 ms.
- `crpp_skin check` on both official packs, 30 times: 43-45 ms a check either way, with the process start.
- Retro on a hidden desktop, the options window scrolled by a posted wheel 60 times a second for 10 s: 2.3-3.9% of
  one core across all settings and runs. Drawing goes to Direct2D and the driver, which these flags do not touch.
- Startup CPU (190-330 ms), memory (about 23 MB private at start, 10.5 MB once the graphics are let go) and the
  idle CPU (0.0 ms over 20 s) did not change.

## The hybrid CRT

The exe keeps the C++ runtime inside (`/MT`) and takes the Universal CRT from Windows: `/NODEFAULTLIB:libucrt.lib`
and `/DEFAULTLIB:ucrt.lib`. Microsoft's Windows App SDK builds itself this way
(`docs/Coding-Guidelines/HybridCRT.md` in microsoft/WindowsAppSDK). The UCRT has been part of Windows since
Windows 10 (https://learn.microsoft.com/en-us/cpp/windows/universal-crt-deployment), and there the copy in the
system directory is always used. The exe imports the `api-ms-win-crt-*` sets and no `vcruntime` or `msvcp` DLL.

## Not done

- Exceptions off (`/EHs-c-` with `_HAS_EXCEPTIONS=0`): the program throws none, but the MSVC STL does not support
  that mode, so it stays on.
- The icon's small sizes as PNG: Microsoft documents PNG for the 256 px image only.

## Memory

Once the graphics are let go, about 4.3 MB of the process's private memory is writable private pages: heaps and
stacks. Most of the rest is copy-on-write pages of modules, chiefly ones the GPU driver and font tools put into
every process (on this PC 3.7 MB of NVIDIA's `nvppex.dll`). With the options window open under Retro its render
target adds about 38 MB of private memory, mostly the driver's.
