# Crash reports

How the app writes a Crash report, what it can and cannot catch, and what it costs. Measured on 2026-11-10
with MSVC 14.51 (Build Tools 2026), Release builds of `crpp` on one Windows 10 19044 PC. The code is in
`src/crash`; the build settings are in `cmake/hardening.cmake` and `cmake/symbols.cmake`.

## What can be caught inside the process

- An exception nothing handles (an access violation, a stack overflow, an illegal instruction) reaches the
  filter of `SetUnhandledExceptionFilter` on the thread that raised it, unless a debugger is attached.
- `abort()` raises `SIGABRT` first; the handler `signal` sets for it is the process's. `std::terminate` calls the
  handler of `std::set_terminate`, which in MSVC is kept per thread, so on the other threads it ends in `abort()`.
  A pure virtual call and an invalid parameter to the C runtime have handlers of their own
  (`_set_purecall_handler`, `_set_invalid_parameter_handler`).
- A failed check of the hardened STL calls `__fastfail(5)` by default, which ends the process in the kernel
  without running any handler of it. `_MSVC_STL_USE_ABORT_AS_DOOM_FUNCTION` makes it call `abort()` instead
  (`__msvc_doom_core.hpp` in MSVC 14.51). The check comes before the bad access, so the report is written from
  memory that is still whole. This costs 11,264 bytes of exe (`docs/research/hardening.md`).
- Not catchable: whatever ends in `__fastfail` (Control Flow Guard, `/GS`, a CET shadow stack, a corrupt heap
  that the heap reports), and the process being killed. Microsoft's documentation of `__fastfail` says exception
  handlers do not see it: https://learn.microsoft.com/en-us/cpp/intrinsics/fastfail.

## How the handler writes a report

It must work from a thread whose stack is nearly gone, while any lock may be held, so it:

- walks the stack with `RtlLookupFunctionEntry` and `RtlVirtualUnwind`, from the exception's context or from
  `RtlCaptureContext`. Every x64 image carries unwind tables (`.pdata`), so this works under `/O1` without
  frame pointers. A leaf function without an entry pops its return address. Frames that repeat right below
  themselves fold into one, so a stack overflow still reaches the bottom of the stack.
- names each frame module+offset from the image's own headers in memory: its export directory's name, its
  `TimeDateStamp` and `SizeOfImage`, and its CodeView record (PDB name, GUID, age). It does not call
  `GetModuleFileName`, which takes the loader lock, nor dbghelp, which allocates and loads DLLs. An exe exports
  nothing to be named by, so the app's own name is read at install.
- writes into memory set aside before (`.bss`, 16 KB of text, 64 frames, 24 modules) and with `CreateFileW` and
  `WriteFile`, then ends the process with `TerminateProcess`. A second crash while writing ends it at once;
  another thread's crash waits.
- `std::stacktrace` was not used: MSVC's loads `dbgeng.dll`, allocates and takes a lock on every call.
  `MiniDumpWriteDump` was not used either: Microsoft advises calling it from another process
  (https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump), and a
  dump holds the stack's memory, which may hold chat or a token; a report is published as it is.

Run by `crash.handler`, every caught kind left a report: an access violation on the main thread and on another,
an STL index out of range, `abort()`, `std::terminate`, a pure call, an invalid parameter, and a stack overflow,
whose recursion folded into one frame above `ntdll.dll`.

## What Windows records

Windows Error Reporting writes the event "Application Error" (ID 1000) to the Application log for a crash it
handles, with the module, offset, exception code, process id and the process's creation time, but no stack.
The app leaves `running-<pid>.txt` with that id and time while it runs and removes it at a clean end; the next
start looks up the event for a mark of a process that is gone, with `wevtapi.dll` loaded only then. Its data
items have no names on Windows 10 19044, so they are read in order.

Windows records nothing where Windows Error Reporting is off, as on the PC this was measured on
(`HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\Disabled` is 1). There a `__fastfail` leaves only the
mark, which the next start removes. `crash.event_log` then reads back an older crash the log holds, and checks
the whole `__fastfail` path only where reporting is on. WER's own dumps (`LocalDumps`) are set under HKLM only,
which needs an administrator (https://learn.microsoft.com/en-us/windows/win32/wer/collecting-user-mode-dumps).

## Symbols

Every MSVC Release build writes a PDB beside each exe: `/Z7` while compiling, and `/DEBUG` with `/OPT:REF`,
`/OPT:ICF` and `/INCREMENTAL:NO` named after it, as `/DEBUG` would otherwise turn them off
(https://learn.microsoft.com/en-us/cpp/build/reference/debug-generate-debug-info). `/PDBALTPATH:%_PDB%` keeps the
build machine's path out of the exe; it names `crpp.pdb` alone. A Release publishes `crpp.pdb`, and
`crpp_symbolize` reads a report with it through dbghelp, which loads a PDB directly at a base and size of our
choosing. It refuses a PDB whose GUID and age are not the report's. `crash.symbolize` checks it on a crashed test.

| | crpp.exe | `.text` | `.rdata` | PDB |
| --- | ---: | ---: | ---: | ---: |
| Without symbols | 1,741,824 | 1,374,828 | 267,602 | none |
| With them | 1,741,824 | 1,374,796 | 267,658 | 24,293,376 |

The code is the same to within 32 bytes of `.text`; `.rdata` holds the debug directory and the CodeView record.

## What it costs

- Exe: 1,690,624 bytes before, 1,701,888 with the STL calling `abort()`, and 1,741,824 with the crash module
  and its question: 40 KB for the module, 51 KB in all. The linker map gives 28.5 KB of it to the crash code itself. A sort of
  strings shared with the catalog saved 2.5 KB of it.
- Memory: the handler's buffers are 29 KB of `.bss`, touched only by a crash, except the breadcrumbs (4 KB, from
  the first diagnostic event) and what install keeps (the folder, the exe's name). On a hidden desktop with
  `--data`, three runs each, 10 s after start: private bytes 11,372-11,676 KB against 11,404-11,520 KB before,
  working set about 2 MB either way.
- CPU: install sets five handlers and reads the Windows version; a run writes one small file at its start and
  deletes it at its end. CPU time to 3 s after start was 344-484 ms against 297-469 ms before, the same spread.
  Idle stays at 0.
- Nothing runs while idle, and no thread is added.

The measuring script was throwaway and is not kept.
