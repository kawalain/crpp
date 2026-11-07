# Memory floor of the native client

Findings from public sources and a throwaway probe (researched 2026-11-07). Items marked
*unverified* were not confirmed in a primary source. Items marked *estimate* are not sourced
and need a measurement before anyone relies on them.

**About the numbers.** Unless a line says otherwise, figures come from a probe, not the app;
"The app, measured" below has the app's own. To split OS and driver cost from app cost, a separate
probe (one C++ file, MSVC `/O2 /MT`, delay-loaded DLLs, kept outside the repository)
ran on a hidden desktop: same APIs, a 1189x865 px target at 120 DPI (the app's 951x692 at 125%),
and a Home-like scene of 40 rounded rectangles, a gradient path, 34 Hangul labels and layers. It
read `PrivateUsage`, `QueryWorkingSet` (shared bit), a `VirtualQuery` walk and
`IDXGIAdapter3::QueryVideoMemoryInfo`. **"Measured" below means this probe on this one machine:**
RTX 4070 Ti (32.0.16.1656) plus UHD 770, Windows 10 19044, with MacType hooked into every process
(about 0.2 MB of commit). Repeat runs moved private by up to 2 MB and working set by up to 5 MB.
A hidden desktop has no composition: flip `Present` returned `0x087A0001`
(`DXGI_STATUS_OCCLUDED`), so CPU per frame excludes real presentation. All sizes are in MB.

## Conclusion

**The 63 MB is mostly not the app's memory, and on this GPU it cannot go much lower while the
window is visible. It can fall by about 75% while hidden.**

| State (this machine) | Today (app) | Realistic floor, hardware D2D | What sets the floor |
| --- | --- | --- | --- |
| Visible Home, private | 63 | 55-60 | D3D device and NVIDIA driver: 50 before anything is drawn |
| Visible, settings open, private | 74 | 60-65 (*estimate*) | Same, plus unbounded nested layers today |
| Visible Home, working set | 44 | 38 stays; 10-15 after a one-off trim until next used | About 25-28 of it is shared DLL pages |
| Task Manager "Memory" (private working set) | 15 | about 5 after a trim, 14 once used | Driver, D2D and heap pages |
| Hidden, private | 62 | 10-20 (probe 10.4, plus the app's own data, *estimate*) | Blocked by `ID2D1HwndRenderTarget`; see below |

- **Imposed by the driver.** Creating the hardware device takes the probe from 2.5 to 52-56
  private. Of that, 32.9 is writable, copy-on-write image sections of the driver DLLs
  (`nvwgf2umx.dll` alone 25.0, of which 0.6 is resident). That is commit charge, not RAM. Another
  20 or so is driver heap. Nothing in the process can shrink this while a hardware device exists.
- **The app's share is small.** An empty hardware Direct2D window with one text call is 58 private
  and 37-40 working set, against the app's 63 and 44, so the app adds about 5 of each at Home. The
  sizes derivable from the code (below) total under 2 MB at Home for a user with no cached chat. A
  heap measurement would split the rest.
- **Two findings are worth acting on.** Unbounded nested opacity layers cost about 5 private and
  5 GPU per nesting level and are avoidable at no CPU cost. And releasing the render target frees
  nothing, because the factory keeps the device alive: "render resources released while hidden"
  today saves working set only.
- **Going lower visibly means leaving the GPU.** WARP reaches 14-26 private but costs about 10
  times the CPU per frame, and its private working set is only about 2.5 lower (11.4 against
  13.9). It trades commit, not RAM, for CPU. That conflicts with the CPU-first priority, so the
  decision to draw with the GPU is what sets the visible floor.

## What was done

Tried in the app after this note was first written. "Before" and "after" are the app, measured as
in section 9.

- **Layer bounds (measure 1): no saving in ordinary flows.** Settings open stayed at 74 private. The
  panel's fade needs one full-window layer anyway, and a tab change then reuses that pool. Bounds
  can only help where layers nest. The estimate in the table below was too high.
- **Release when hidden (measure 2): done as 2b, not 2a.** The HWND target stays, so the visible path
  is untouched: this note's own table shows 2a's device context at about twice the CPU per frame,
  and it cannot be checked on a hidden desktop. The worry about 2b was the holders of geometry, but
  only about ten members were left, and they now use `Gfx::path`.

| State | Private | Working set | Threads | Modules |
| --- | --- | --- | --- | --- |
| Home, idle | 63 | 44 | 28 | 74 |
| Minimized, 4 s | 63 | 8 | 28 | 74 |
| Minimized, past the 30 s delay | 24 | 1.5 | 12 | 67 |
| Restored | 65 | 34 | 28 | 74 |

Restoring cost 141 ms of CPU once. The 24 is above the probe's 10-11: 18 of it is private-type
commit, whose owner was not identified. After the settings screen has been used the hidden figure is
28-40, not 24. Three hide and show cycles past the delay, then ten quick ones, showed no growth in
private, threads or handles.

## Ranked measures

Savings are measured in the probe unless marked *estimate*; none has been tried in the app.

| # | Measure | Expected saving | Sure? | CPU and stability cost | Effort | Verify by |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Give `push_opacity` content bounds, or multiply alpha into the colour for one primitive (`src/ui/gfx.cpp:351`) | Settings and fades: 3-13 private, 3-12 GPU. Working set: none | High that it works; size in the app is an *estimate* | CPU slightly better (0.62 against 0.68 ms/frame). A wrong bound clips content such as shadows | Small: about 10 callers | Private and GPU local at settings open, then compare `--shot motion-*` |
| 2 | Release the device when hidden for some time. Own the device (#2a) or also drop the factory (#2b) | Hidden: 45-55 private, about 20 GPU | High here; other drivers unknown | Show costs 130-190 ms CPU after an earlier use, about 400 ms CPU and 780 ms wall when cold. Debounce so minimize and restore does not pay | Medium: 2a about 150 lines in `window.cpp` with resize and device loss; 2b rebuilds every geometry | Hidden private and module list on Intel, AMD, NVIDIA; show time; repeated hide and show |
| 3 | With 2a: `ClearResources(0)` and `Trim` once after a quiet period while visible | 6-8 private, 5 GPU local, 2.6 GPU non-local | High | Comes back on the next frame, which took 3-4 ms against about 1. One-shot timer only | Small after 2a | Same counters, frame time after an idle period |
| 4 | One-shot `SetProcessWorkingSetSize(-1,-1)` once settled, not only on hide (`apps/gui/area_os.cpp:218`) | Working set 38-44 to about 10 after the next frame; private working set 14 to about 5. Commit: none | High on the number, low on real benefit | About 2,500 soft faults once; hard faults under memory pressure; dirty pages may be written to the page file (*unverified*). Never periodic, never while moving | Tiny | Working set and page faults; time of the next frame on a machine under pressure |
| 5 | With 2a: `D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS` on the hardware device | 3-4 private, 47 to 21 threads against plain 2a | Medium | No CPU change here, but Microsoft says "not recommended for general use", and the stated purpose is debugging | One flag | Scrolling CPU on several drivers. The current HWND target already has only about 20 threads |
| 6 | `HeapSetInformation(NULL, HeapOptimizeResources, ...)` on hide | 1-2 private; about 0.5 with segment heap | High | One call on hide | Tiny | Hidden private |
| 7 | `heapType` SegmentHeap in the manifest | Probe: within noise, 1 at most after release. The app's own heap may gain more (*estimate*) | Low | No first-party CPU figure found (*unverified*); the app allocates per frame, so measure it | One line (`apps/gui/crpp.manifest`) | Private and CPU while scrolling, before and after |
| 8 | Keep only the current Room's chat cache in memory (`src/store/store.cpp:179`) | Under about 1 per full Room left in the cache (*estimate*) | Medium | Read on entering a Room | Small | Heap with a seeded `chat.bin` |
| 9 | Check `render_pixels` on notifications (`apps/gui/area_os.cpp:74`, `apps/gui/tray.cpp:45`) | Unknown. A software target loads `D3D10Warp.dll`; a full-window one added 5 at creation, then 15 and 13-24 threads once drawing. Small icons may cost far less (*estimate*) | Low | Would undo #2 in a hidden Room if it holds WARP | Measure first | Module list and thread count after a chat notification |
| 10 | Drop Resamplers after use; evict packs nobody in the Room uses | 0.09 and up to 3.5 of recordings | High for sizes | Reload on need | Small | Heap in a Room with mixed packs |

Order of work: 1 alone is cheap and helps every screen. 2a is the only large win and unlocks 3 and
5. 4 and 6 are add-ons. The rest are too small to do without measuring first.

**Why 2a over 2b.** 2b looks simpler but rebuilds geometries held in many surfaces, since they
belong to the factory (`Ui::cloud` in `src/ui/include/crpp/ui/surface.hpp:45`, plus
`chat_panel.hpp:104`, `room_screen.hpp:131`, `prompt_screen.hpp:77`, `settings_panel.hpp:75`,
`settings_tabs.hpp:111`, `changelog_view.hpp:93`). 2a keeps them. It takes `ID2D1DeviceContext`,
which derives from `ID2D1RenderTarget`, so `Painter` and `App::paint` should not change; only
`ensure_target`, `WM_SIZE`, `WM_DPICHANGED` and the present path do (`apps/gui/window.cpp:29-38,
92, 123, 133-138`). Its cost is one more buffer: GPU local 22 against 17.9. It also changes the
present path, which needs a real-desktop run for CPU and resize behaviour.

## Evidence

### 1. What the numbers mean

- Working set: pages currently resident. A soft fault resolves without the backing store, for
  example a page in another process's working set or in transition
  (learn.microsoft.com/en-us/windows/win32/memory/working-set).
- `PrivateUsage` is the commit charge: "the total amount of private memory that the memory
  manager has committed" (learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex). Committed pages take
  physical memory only on first access, but charge RAM plus page file
  (learn.microsoft.com/en-us/windows/win32/memory/page-state).
- Copy-on-write file views count toward commit, ordinary file views do not (Russinovich,
  techcommunity.microsoft.com/t5/windows-blog-archive/pushing-the-limits-of-windows-virtual-memory/ba-p/723750;
  search extract only, the full post was not read). This fits the 32.9 of writable image above.
- What a user sees: Task Manager's "Memory" column is the private working set, and commit and
  private working set are the two metrics the Edge team tracks
  (blogs.windows.com/msedgedev/2021/01/13/investigate-microsoft-edge-memory-usage/). So "private
  bytes 63" limits the system's commit headroom, while the number a user reads is about 14.
- What the system feels: resident private pages under pressure, and commit against the limit.
  Shared pages cost once across processes (learn.microsoft.com/en-us/windows/win32/memory/memory-protection).
- Trimming: `(SIZE_T)-1` for both sizes "removes as many pages as possible"; `EmptyWorkingSet` does
  the same (learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-setprocessworkingsetsize,
  learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-emptyworkingset). Commit is untouched: measured, private stays at 67.2
  while working set goes from 40.3 to 0.2. Touched pages return as faults: 9.9 back after one frame
  (private 4.9, shared 5.0), so a frame needs about a quarter of the resident pages; the rest was
  start-up. That frame took 2.7-3.9 ms wall against 0.8-1.4. The system trims an idle process under
  pressure anyway (same page), so a trim brings that forward; it does not create free memory that
  pressure would not have found. Whether dirty pages then go to the page file: *unverified*.
- `SetProcessInformation(ProcessMemoryPriority)`: lower-priority pages are trimmed first, intended
  for background work (learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation). It does not lower any
  counter. It could suit hidden outside a Room; in a Room it invites hard faults on the sound path.
- An Old New Thing post on trimming was searched for and not found.

### 2. Direct2D and Direct3D

Measured, after 300 frames:

| Path | Private | Working set (private / shared) | GPU local / non-local | Threads | CPU ms/frame |
| --- | --- | --- | --- | --- | --- |
| HWND target, hardware | 65-67 | 38-42 (14 / 25-28) | 17.9 / 2.1 | 20 | 0.6-1.4 |
| Same, no layers in scene | 60-62 | 38 (14 / 25) | 12.8 / 2.3 | 19 | 0.5-1.2 |
| Device context, flip 2 buffers, hardware | 67-68 | 43-48 (17-19 / 25-29) | 22.2 / 3 | 47 | 1.5-3.0 |
| Same, prevent-threading flag | 62.7 | 39.6 (14.4 / 25.2) | 22.0 / 2.8 | 21 | 1.5 |
| HWND target, software | 25-26 | 29-31 (17-19 / 12) | none | 28 | 6.2 |
| Device context, WARP, default | 24 | 37 (16 / 20) | none | 20 | 8.7 |
| Device context, WARP, prevent-threading | 14.4 | 32 (11.4 / 20.4) | none | 5 | 7.8 |

- **Device.** Plain step costs: window with GDI 2.0 private; adding the D2D factory and DirectWrite
  with the font 2.5; D3D device 51-52; first frames add 5-14. 200-odd MB of image is loaded, mostly
  shared and never resident: 86 MB `nvwgf2umx.dll`, 106 MB `nvgpucomp64.dll` with only 3-7
  resident. The driver also pulls in `SHELL32`, `windows.storage`, `CRYPT32`.
- **Not avoidable with a hardware device.** The user-mode driver is what the runtime loads for the
  adapter. Graphics drivers "periodically allocate internal memory buffers" that count against the
  app (learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgidevice3-trim). How much they keep is the vendor's choice and is
  not documented.
- **Release does not free the device.** Releasing the HWND target: private 68.2 unchanged, GPU 17.9
  unchanged, 20 threads, driver loaded. This matches the app's 62 MB while hidden. Releasing the
  **factory** as well: 9.4, or 31.8 in a run that had not finished releasing yet; `nvwgf2umx.dll` is
  gone (image 258 to 63), 4-5 threads. So the factory holds the device. Microsoft does not document
  this (*observed only*), and it is why the app cannot release it without a device of its own.
- **Owning the device.** Releasing swap chain, context and device with the factory kept: 10.4-11.2
  private, working set 7.4-7.9 then 0.2 after a trim, GPU 0. 4.6 of writable image stays loaded;
  what holds it is unknown. Destruction is deferred: "Direct3D will normally defer the destruction
  of D3D objects" (Trim page), and once 57 fell to 20 only some 200 ms later. Measure after a delay.
- **`ClearResources` and `Trim`.** Both documented for discarding caches
  (learn.microsoft.com/en-us/windows/win32/api/d2d1_1/nf-d2d1_1-id2d1device-clearresources; Trim page, which says only call it when going
  idle). Measured: private 68.8 to 60.8, GPU local 22.2 to 16.8, non-local 4.3 to 1.7. Back within
  a frame. Only reachable through `ID2D1Device`; the HWND target has no route to it.
- **`SetMaximumTextureMemory`.** It sets when caches are purged, and D2D may exceed it for a frame
  (learn.microsoft.com/en-us/windows/win32/api/d2d1_1/nf-d2d1_1-id2d1device-setmaximumtexturememory). The default read back as 128 MB; a limit of 8
  changed nothing measurable (67.4 against 67.8), since the caches never get near it.
- **Layers.** With a null layer D2D "reuse[s] layers internally"
  (learn.microsoft.com/en-us/windows/win32/direct2d/improving-direct2d-performance), and the default content bounds are "effectively
  ... the bounds of the render target" (learn.microsoft.com/en-us/windows/win32/direct2d/direct2d-layers-overview). Measured:

  | Scene | Private | GPU local |
  | --- | --- | --- |
  | No layers | 60-62 | 12.8 |
  | One bounded layer, one infinite (`LayerParameters()` default) | 65-67 | 17.9 |
  | Both bounded | 62.9 | 14.9 |
  | Infinite, nested 3 deep | 77.5 | 27.9 |
  | Bounded, nested 3 deep | 64.5 | 15.9 |

  So each nested full-target layer adds about 5 private and 5 GPU: one 4.1 buffer (1189 x 865 x 4)
  plus overhead. Working set does not move, since this is commit and GPU memory. The app uses the
  infinite default at `src/ui/gfx.cpp:351`. The settings panel nests one (`settings_panel.cpp:85`)
  around tab cross-fades (`settings_panel.cpp:154,158`), inside the screen cross-fade (`app.cpp:151,
  155`). That fits settings' +11 private and +4 working set (*estimate*: the app was not measured
  this way). The pool stays until the device goes, so one fade costs it for the rest of the run.
  `push_edge_fade` is already bounded (`src/ui/controls.cpp:248`). Microsoft also advises multiplying
  opacity into the brush for a single primitive (layers overview).
- **Flip against HWND target.** HWND targets are double-buffered (learn.microsoft.com/en-us/windows/win32/api/d2d1/nn-d2d1-id2d1hwndrendertarget).
  Flip shares buffers with the compositor with minimal copies
  (learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model). What the HWND target uses inside is
  not documented, and only the flip path loaded `dcomp.dll`. A real-desktop comparison is open.
- **`D2D1_RENDER_TARGET_USAGE`** is about remoting and GDI compatibility; nothing documented bears
  on memory (learn.microsoft.com/en-us/windows/win32/api/d2d1/ne-d2d1-d2d1_render_target_usage). The code uses the default (`window.cpp:34`).
- **Software.** `D2D1_RENDER_TARGET_TYPE_SOFTWARE` loaded `D3D10Warp.dll` in the probe. The WARP flag
  stops extra threads (learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_create_device_flag) and saved 9 private here, at 11
  ms wall per frame against 6. The software HWND target ran 28 threads here.
- **Mixed adapters.** Creating the device on the UHD 770 while the window sits on the NVIDIA card
  gave 147 private and 128 working set, with both drivers loaded. That is a cross-adapter case, not
  an Intel-only figure, but its device step (37.2 private, 10 of it writable image, 42 ms) shows
  drivers split this very differently. Hybrid laptops need their own run.

### 3. DirectWrite

- The font is a local file reference (`src/ui/gfx.cpp:112`,
  learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritefactory-createfontfilereference). Measured: **no mapped view of the
  font file was present** after drawing. Only `~FontCache-FontFace.dat` (16 MB view, 20 KB resident)
  and the per-user cache (8 MB, 32 KB) were. The shared factory uses cross-process caching
  (learn.microsoft.com/en-us/windows/win32/api/dwrite/ne-dwrite-dwrite_factory_type). How DirectWrite reads a private font is not documented
  (*unverified*).
- Against Malgun Gothic from the system collection, with the same scene: +0.2 private, +0.6 to +1.2
  working set. **The 13.39 MB file costs about 1 MB of memory.**
- Font tables, parsed from the file: `gvar` 7.05 MB (52.7%), `glyf` 5.46 MB (40.8%), 23,410 glyphs,
  `wght` 45-930, 9 named instances. Deltas are stored per glyph and unpacked for the whole outline
  (learn.microsoft.com/en-us/typography/opentype/spec/gvar). The UI uses weights 400, 700 and 800 (`grep` over `src/ui`).
  A subset or static instances would save download size, not this process's memory, and three
  statics would together be larger than the file.
- `CreateInMemoryFontFileLoader` (learn.microsoft.com/en-us/windows/win32/api/dwrite_3/nf-dwrite_3-idwritefactory5-createinmemoryfontfileloader)
  would hold font data in the process. Sizes were not measured, but holding 13 MB privately is the
  wrong direction.
- Glyph atlas: no first-party description of D2D's atlas was found; a third party mentions it
  (github.com/lhecker/dwrite-hlsl, pointer only, *unverified*). Measured: drawing 5,400 distinct
  Hangul at 17 px adds 1.4-1.7 private, 1.4 working set, 1 GPU non-local. It is not a meaningful cost.
- Layouts: `Painter::text` builds a format and layout for every call on every frame
  (`src/ui/gfx.cpp:424`, `:305-322`). This churns the heap but holds nothing. Chat keeps layouts
  only for rows in view (`src/ui/include/crpp/ui/chat_panel.hpp:76`, `src/ui/chat_panel.cpp:343-350`).

### 4. Start-up, DLLs, linking

- `crpp.exe` (parsed): 1.41 MB, `.text` 1005 KB, `.rdata` 278 KB, writable `.data` 70 KB, 1 MB stack
  reserve with 4 KB commit. 14 static imports. Only `.data` is private, and only when written. Even
  removing half of it saves under 0.1 MB.
- `/OPT:REF` and `/OPT:ICF` are already on: they default on unless `/DEBUG`
  (learn.microsoft.com/en-us/cpp/build/reference/opt-optimizations), and the Release link line has neither `/DEBUG` nor
  `/OPT:NO*` (`build-msvc/CMakeCache.txt`). `/Gw` is not set and could shrink `.rdata` and `.data`
  slightly (learn.microsoft.com/en-us/cpp/build/reference/gw-optimize-global-data); not worth a measurement on 70 KB.
- `/DELAYLOAD`: data imports are not supported, nor is `kernel32`, and a late load can change
  behaviour (learn.microsoft.com/en-us/cpp/build/reference/linker-support-for-delay-loaded-dlls). Per DLL the measured cost is 4-40 KB of
  writable image and 12-56 KB of private working set, while the driver loads `SHELL32` and `CRYPT32`
  anyway. A DLL once loaded stays, so it would help only a session that never needs it.
- `/MT` against `/MD`: `ucrtbase.dll` is in the process regardless, since system DLLs load it (module
  list of the bare `/MT` probe). `/MD` would trade some shared exe pages for files the one-exe
  release does not carry (learn.microsoft.com/en-us/cpp/c-runtime-library/crt-library-features). No gain.
- Section layout (`/ORDER`): affects which file-backed pages get touched, which the trim shows is at
  most a few MB of shared pages. Not tried.

### 5. Heaps and threads

- Static UCRT allocates from the process heap (`ucrt/heap/heap_handle.cpp:23`, `malloc_base.cpp:33`
  in SDK 10.0.26100), so the manifest setting governs the CRT heap too.
- Segment heap: "will generally reduce your overall memory usage", 19041 and later
  (learn.microsoft.com/en-us/windows/win32/sbscs/application-manifests); Edge reported up to 27% in internal tests
  (blogs.windows.com/msedgedev/2020/06/17/improving-memory-usage/). A browser's heap is its main
  cost; here the probe showed nothing beyond noise while visible (66.6 against 66.5; 14.1 against 14.4),
  and 4.5 against 5.6 hidden on WARP. Whether driver heaps change: *unverified*.
- LFH is automatic since Vista (learn.microsoft.com/en-us/windows/win32/memory/low-fragmentation-heap); nothing to enable.
- `HeapOptimizeResources` decommits LFH caches when possible (learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapsetinformation).
  Measured: 1.0-2.2 on the NT heap.
- Stacks: `std::thread` passes 0 as stack size (`stl/inc/thread:79`, github.com/microsoft/STL), so
  the header's reserve and commit apply. Measured: 8 idle threads add 0.2 private in all, no
  working-set change, and a 64 KB reserve makes no difference. Stack tuning buys nothing.

### 6. Audio, network, input, COM, IME

Measured on top of a 1.7 private, 5.6 working set process:

| Step | Private | Working set | Note |
| --- | --- | --- | --- |
| COM apartment, MTA or STA | +0.1-0.2 | +1.3-1.6 | Mostly shared |
| WIC factory | +0.1 | +0.9 | `tray.cpp:46`, `png.cpp:11` |
| WASAPI enumerator and initialized client, with COM | +0.5 | +3.5 | 1056-frame buffer; not started |
| Same, after release | no change | no change | **Closing returns nothing** |
| WinHTTP session | +0.6 | +2.5 | +4 threads |
| One TLS request through Schannel | +0.7 | +2.7 | No change after closing |
| Window with GDI | +0.2 | +1.9 | `MSCTF.dll` and `uxtheme` already load here |
| IME context and focus | +0.6 | +3.4 | `TextInputFramework.dll` loads, +2 threads |

- WASAPI: the engine already opens on the first sound and stops when idle (`src/sound/engine.cpp:200-
  203, 208-226`). Closing the client would save nothing and would pay `Initialize` again, which may be
  called only once per client (learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize).
- Raw Input uses one thread and a message-only window with a fixed stack buffer
  (`src/input/raw_input_source.cpp:28, 78`), running only in a Room. Its cost is a thread: well under
  0.1 by the thread measurement.
- IME: `ImmDisableTextFrameService` is unavailable since Vista
  (learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immdisabletextframeservice), and `ImmDisableIME` would remove Korean input
  (learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immdisableime). Not an option.
- Not measured against a real server: `WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY`, as used at
  `src/transport/winhttp_transport.cpp:110`, was tested only for one HTTPS request.

### 7. What the app holds by design

Derived from code and assets at a 48 kHz device (mix format measured: 48 kHz, 2 ch, 32-bit float).

- **Recordings** (`src/sound/include/crpp/sound/engine.hpp:179`): mono float after resampling
  (`src/sound/pcm.cpp:101-106`). Computed from the WAV headers, as bytes at 48 kHz:

  | Pack | Files | Stored PCM16 | Float at 48 kHz |
  | --- | --- | --- | --- |
  | keyboard/plastic | 19 | 208 KB | 452 KB |
  | keyboard/thocky | 19 | 177 KB | 382 KB |
  | keyboard/walnut | 27 | 731 KB | 1,591 KB |
  | mouse/plastic | 14 | 270 KB | 587 KB |
  | mouse/rubber | 11 | 246 KB | 535 KB |
  | mouse/walnut | 11 | 200 KB | 436 KB |
  | pen/paper | 6 | 241 KB | 526 KB |
  | notifications, system | 4 | 40 KB | 86 KB |
  | **All** | 113 | 2.11 MB | 4.60 MB |

  Nothing loads until played (`engine.cpp:284-296`): 0 at Home, about 1.04 MB with both default packs
  once every variant has played. They are never evicted.
- **PCM16 instead of float** would save half: 0.5 MB by default, 2.3 MB at worst, in exchange for a
  conversion per sample on the render path. CPU comes first, so not worth it. Sharing the file's pages
  is impossible when the rates differ, and nearly all files are 44.1 kHz.
- **Resampler** (`pcm.cpp:127-128`): 44.1 to 48 kHz is 160 phases x 137 taps = 21,920 floats, 88 KB,
  kept per input rate (`engine.hpp:180`) though needed only while loading.
- **Reverb** (`src/sound/mixer.cpp:97,102`): (11,024 + 1,563) samples per channel x 48000/44100, plus
  the stereo spread, is about 27,700 floats, 111 KB. Allocated when the device opens.
- **Chat**: at most 1000 entries per panel (`src/ui/chat_panel.cpp:89`), text limited to 200 UTF-16
  units (`chat_panel.cpp:83`). Four strings plus fields is about 144 bytes each, so under 0.9 MB at
  worst and usually far less. The store holds the same log again as a JSON tree, per Room, up to 1000
  each for 18 hours (`src/store/include/crpp/store/store.hpp:28,30`), loaded at start
  (`src/store/store.cpp:179`). Per-node JSON size was not derived.
- **Emoji**: 166 files, 713 KB on disk. At most 48 parsed in the shared cache (`svg.hpp:46`) and 72 in
  the picker's (`emoji_picker.hpp:23`), which goes when it closes (`emoji_picker.cpp:49-53`); both go
  on hide (`apps/gui/area_os.cpp:215`). D2D geometry size is not documented or measured.
- **UI**: path geometries held by surfaces, fade masks until the target goes (`surface.hpp:117`).
  Small, and not derivable beyond counting them.
- **Assets at rest**: `src/assets` reads files only to verify them; nothing stays beyond the manifest
  in `.rdata`.

### 8. Reference points

**No first-party figure was found** for a minimal Win32, GDI or Direct2D window. These are from the
probe, on this machine, including MacType:

| Program | Private | Working set (private / shared) |
| --- | --- | --- |
| CRT only, no window | 1.7 | 5.6 (1.6 / 4.5) |
| Window, GDI paint | 2.0 | 7.6 (1.8 / 5.8) |
| Plus D2D factory, DirectWrite, private font, no target | 2.5 | 9.0 (2.1 / 6.8) |
| Direct2D window, WARP, one thread | 14.4 | 32 (11.4 / 20.4) |
| Direct2D window, hardware | 65-67 | 38-42 (14 / 25-28) |

### 9. The app, measured

MSVC Release (`/O2 /MT`), 1189x865 px at 120 DPI, hidden desktop, same machine. Breakdown by a
`VirtualQueryEx` and `QueryWorkingSetEx` walk from outside the process.

| State | Private | Working set | Unshared resident | Threads | Modules |
| --- | --- | --- | --- | --- | --- |
| Home, idle | 63 | 44 | 15 | 28 | 74 |
| Settings open | 73-79 | 47-49 | 17-19 | 28 | 76 |
| After scrolling | 77-85 | 50-51 | 20-21 | 28 | 76 |
| Minimized, from Home | 64 | 8 | 2.4 | 28 | 74 |
| Restored, on a hidden desktop | 64 | 12 | 5.5 | 28 | 74 |
| Temporary patch: software target, Home | 17 | 27 | 10.5 | 23 | 55 |
| Same, after scrolling | 38 | 45 | 27 | 32 | 58 |

Minimal windows measured the same way, for comparison:

| Program | Private | Working set | Threads | Modules |
| --- | --- | --- | --- | --- |
| Empty window | 2.6 | 11 | 6 | 38 |
| GDI text | 3.6 | 12 | 6 | 38 |
| Software D2D, text | 11.9 | 21 | 17 | 42 |
| Hardware D2D, text | 58 | 37-40 | 22 | 64 |
| Hardware D2D, target released, factory kept | 57 | 37 | 22 | 64 |
| Hardware D2D, target and factory released | 20.6 | 20.6 | 7 | 57 |
| Software D2D, target and factory released | 4.2 | 14.6 | 17 | 41 |

- **Hiding frees no commit today.** Private stays at 64 when minimized. The working set falls to 8,
  which the explicit trim on hide accounts for. This confirms the probe's finding in the app.
- **Software rendering costs CPU in the app too.** Continuous wheel scrolling took 9.7% of one core
  against 0.9-1.9% on the hardware target. This desktop does not really present, so figures on a real
  desktop will differ. Software private also grows with use: 17 to 38 after
  scrolling, which narrows its lead over the hardware path's 85 to about 47.
- **The first render target took 147-150 ms** to create in the app, and 3.9 ms for software. Later
  targets created without destroying the factory took 0.3-0.5 ms, since the device stayed.
- **Releasing only the target when idle did nothing.** A patch that reset the target 1.5 s after the
  last paint left private at 63. One of two runs also seemed not to respond to a posted click, and
  showed 33 threads and 85 modules. The cause was not found. Anything like it needs a stress test.

## Considered and rejected

- **WARP or software as the default.** 40-50 lower private, but 6-9 ms against 0.6-1.5 ms CPU per
  frame in a measurement that leaves out presenting. The app draws on demand, so idle stays 0, but a
  Room pulses on every member's input. CPU comes first. The private working set barely differs.
  Reasonable as the existing fallback only: `D2D1_RENDER_TARGET_TYPE_DEFAULT` falls back already.
- **GDI-only UI.** 2-3 MB is the true floor, but it conflicts with the Direct2D decision and the
  vector and motion requirements. Named as a blocker, not proposed.
- **Software target while hidden, instead of no device.** A hidden window draws nothing, so nothing
  needs a target at all.
- **Static or subset font, in-memory font.** See section 3. Save nothing or cost more.
- **`SetMaximumTextureMemory`.** No effect at this load.
- **`IDXGIDevice2::OfferResources`.** For resources the app creates; it has none worth offering.
- **Tuning `/STACK`, thread count, `/OPT`, `/ORDER`, `/MD`, `/DELAYLOAD`.** Each is under about 0.3 MB.
- **Closing the audio client when idle.** Returns nothing, costs a re-open.
- **Periodic working-set trimming.** Moves cost to page faults repeatedly, against CPU-first; the
  documented guidance points the same way for `Trim`. Undocumented alternatives are not considered.
- **Disabling TSF or the IME.** Korean input is required.

## Open questions only measurement can settle

1. **The app's own heap.** The breakdown is in section 9, but the private region still mixes the
   app's heap with the driver's. Measure the heap at Home, settings open and after scrolling.
2. **Real desktop.** A visible window under DWM: CPU and memory of flip against the HWND target, and
   the effect of presenting on software paths.
3. **Other drivers.** Intel-only, AMD, hybrid laptops, Remote Desktop. Commit split, whether the
   driver unloads on release, and device creation time. All figures here are one NVIDIA driver.
4. **Show latency.** 130-190 ms CPU warm, about 780 ms wall cold: is that acceptable on restore, and
   what debounce avoids it on ordinary minimize and restore?
5. **Hidden in a Room.** Resident size while sounds and network keep touching pages; whether a chat
   notification brings WARP in (`apps/gui/area_os.cpp:74`).
6. **Layer pool.** After a one-off full-window fade, does a pool stay at its largest for the whole
   run, and does `ClearResources` empty it in the app as in the probe?
7. **Trim in practice.** Page-file writes after a trim, and first-frame time under real memory
   pressure.
8. **Segment heap on the app.** Its allocation rate per frame differs from the probe's.
9. **What holds 4.6 of writable image** after the device is released.
