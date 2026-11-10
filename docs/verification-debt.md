# Verification debt

What has only run in tests, in shots or on a hidden desktop. Each entry says what was seen and what
still needs a real desktop, real hardware, ears or a real server. Take an entry out once someone has
seen it; add one for what a change could not verify.

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
- The hardened exe (`docs/research/hardening.md`: STL bounds checks, Control Flow Guard, CET) has run only in
  tests and on a hidden desktop. Run it on a real desktop with a third-party IME and a sound device, and once on a
  CPU with CET shadow stacks (Intel 11th generation or AMD Zen 3 and later), where Windows enforces them.
- Since resources moved into `crpp_win` (handles, icons, GDI objects, WASAPI's COM pointers), the tray icon, a chat
  notification's icon, the window region of an own frame and switching sound devices have run in tests and shots
  only: watch each once on a real desktop.
- IME composition and candidate windows at a scale above 100%: they get physical
  client pixels, which Microsoft's DPI pages do not state for the IME calls.
- Output device changes (plug, unplug, default change) were never exercised on real
  hardware: only listing and opening every device is. Nor has the idle mute run a
  full idle period in a real Room. It ends on a key or click, not on mouse movement
  alone: Raw Input only reports Input kinds.
- Achievements have run in tests and scenes only: watch a real Room reach one, its toast and sound, and the record
  surviving a restart.
- Room weather has run in tests and scenes only. Its steps (20, 80 and 200 a minute) are starting values: watch a
  real Room and tune them by eye.
- The sound of spatial audio and reverb needs ears: run
  `crpp_soundtest assets/sounds`, which walks every Desk; `--balance` and `--reverb` compare
  values. The left-right difference and Office's level and decay are starting points until then. `--synth` plays the generated
  stand-ins instead, which also need ears.
- Following the OS theme: `WM_SETTINGCHANGE` was only posted in a scripted run.
  Flipping the real Windows setting and seeing the dark title bar are unverified.
- The Mini window has run in tests, shots and on a hidden desktop, where it was made topmost at the top right and
  cost about 1.5 MB private and no idle CPU. See on a real desktop: dragging it, staying on top of other programs and
  of a full-screen one, its rounded region under Cloud, the main window in the tray with it open, a real Room's
  discs lighting while the main window is hidden, and the window growing to the left as members join.
- The mute shortcut has run in tests and on a hidden desktop, where it was held only in a Room and a posted
  `WM_HOTKEY` muted and unmuted. A real key press has not: press each choice on a real desktop, in a game and over an
  elevated window, and pick one another program holds.
- The tray needs a real desktop: a hidden one has no taskbar, so the icon fails to
  add and a close exits. A chat notification was seen on one Windows 10 PC, with a window
  of a fake Room behind another. Upstream's developer saw none, on a machine not yet looked
  at; a notification respects Focus Assist. The menu (mute, leaving and options in a Room), flash, the Run key and a
  second start have
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
- The keyboard, selection and the context menu have run in tests, and on a hidden desktop from posted messages only:
  Tab, and the menu bar's Alt keys opening the options window and What's new. Real key presses have not: Alt and F10
  alone beside the system's own handling, Alt+Space with the menu bar, the menu key, "Underline access keys" turned
  on in Windows, the clipboard with other programs, and IME composition while Tab or a menu takes the keys. The
  focus ring, the context menu and a dropdown's keys have not been seen on a real desktop.
- The line above new chat follows `WM_ACTIVATE` of the app's windows, which has run in tests only: see it on a real
  desktop with Alt+Tab, the taskbar, minimizing and the tray, and between the main window and Retro's panels.
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
- Files by hash (ADR 0010): with a test key against localhost a first start fetched 327 files for 330 paths, and
  an install laid out by path as v0.3.0 leaves it moved into the store with only the catalog fetched again;
  `wrangler dev` answered the frozen catalogs' paths from `s/` with 200 and the same bytes. On 2026-11-10
  `kawalain/crpp-repository` was published (catalog-2.txt, signed with the release key, 409 files), and a
  development build with nothing beside it, on a hidden desktop with `--asset-url` at
  raw.githubusercontent.com, fetched the same 327 files in 10 s, each named by its hash, and a second start
  wrote nothing. Not yet run: an official build, which reads that address by itself. The Worker is frozen as
  v0.3.0's release left it (ADR 0011), so v0.2.x and v0.3.0 meet nothing new.
- Adding a Repository, fetching a Theme of it and removing it, updates behind the app and removing a
  downloaded Skin have run on a hidden desktop against localhost only, driven by a throwaway build, not by
  hand. So have a parent in another Repository, a pin that asks first or holds an update back, adding a
  parent's Repository with its child, a key that changed, and picking, failing and removing another
  Repository's Sound pack; a member playing one in a Room has not run. An update of the font itself, which waits a start, has only run in tests. A `github:` address has not been added from a real
  repository (raw.githubusercontent.com answered `HEAD` and a tag without a redirect), and the field, the
  dialog's long fingerprint and a pack's progress have not been seen on a real desktop.
- Choosing a server has run in tests and shots only: no real second server has been checked, trusted and used,
  nor has a certificate that does not hold been refused on a real host. Switching inside a Room (leave, then
  reconnect), the account of each server coming back, and the trust question's look in both packs need a real run
  against a local `server/`.
- A Repository's address as a link has run in tests and shots only: its underline under the pointer and the
  browser opening its page (github.com for a raw.githubusercontent.com folder) have not been seen.
- v0.1.0 updated itself to v0.1.1 from GitHub on one Windows 10 PC with Defender. The cause
  of the detection in `docs/releases/README.md` is unknown, and it may come back with any
  release. The exe has no code signature, so try another machine and SmartScreen.
- Retro's cursors, the hand above all, differ somewhat from the real ones: reported by the user on
  2026-11-08 and not yet compared side by side.
- A path outside the ANSI code page (a Korean user name on an English Windows) has only been read as
  MSVC's `filesystem.cpp`: this machine's code page is UTF-8, where the test passes either way. Run the
  app from such a folder on a machine whose code page is 949 or 1252.
- The Crash report has run in tests, on a hidden desktop and in shots: the app crashed by `--crash access` and
  `--crash stl` left reports, and the next start took the newest as shown. Not yet seen: the question on a real
  desktop, "올리기" opening the browser on a filled issue of the crash form (the form exists on GitHub only once
  pushed), a report that is too long for the address, and a `__fastfail` or CFG crash turned into a report from
  the event log on a machine with Windows Error Reporting on. This machine has it off, so only an older
  recorded crash was read back.
