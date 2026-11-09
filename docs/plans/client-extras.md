# Plan: Client extras for 0.3.0

Agreed with the user on 2026-11-09. Delete this file when the work is done; what lasts goes to `CLAUDE.md`, the
glossary and the ADRs. Everything here ships in 0.3.0, with the keyboard and selection work already on `main`.

Goal: more to enjoy and less friction, within what upstream's server already offers. The server sends no Room title,
capacity, list, version or notice; chat has no ids, history, reactions, edits or deletes; the only roles are Host and
member. What the client can use and does not yet: `/health`'s `public_rooms`, a held Seat's `expires_at`, an
account's `created`, and the timing of everyone's input.

## Rules for every item

1. No CPU while idle and no polling.
2. Only kinds and counts are shared or kept, never which key (ADR 0003).
3. Nothing that upstream's users would see broken: no chat command syntax, nothing put into a Nickname.
4. Every new part of a screen is in both Skin Packs (Cloud and Retro) and both Locales.
5. Every new behaviour is listed under Differences from upstream in `CLAUDE.md`.

Ruled out, as the server cannot carry them or upstream would show them broken: a Room list or title, capacity,
reactions, replies, edits or deletes, kicking, chat history on the server, status messages, a shared timer, chat
commands, and link previews (they would reach other sites).

## Order

Each is its own commit, in this order. Done means: state logic has CTest coverage, both Skins' shots compared with
the ones before, a scripted run on a hidden desktop, and idle memory and CPU measured for new UI.

1. QoL: A10, A1, A2, A5, A4, A3, A7.
2. Small fun: C5, C3, B2, C1, C2.
3. Appearance: B1, then Room weather.
4. Achievements, with the input and discovery ones (C4).
5. C6, the Hidden Theme.
6. A9, the Mini window, then A8.
7. The v0.3.0 notes rewritten, CI (`gh workflow run ci.yml`) and `release.sh 0.3.0 --dry-run` again.

## QoL

- **A10** Chat length: the server keeps the first 200 Unicode scalars; the chat field counts 200 UTF-16 units, so
  emoji stop it early. Count scalars. Past 170, show how many are left.
- **A1** "돌아갈 자리" shows each Seat's time left ("14분 남음"), from the stored `expires_at`. It changes only on the
  timer that already prunes expired Seats.
- **A2** Beside "공개된 구름 찾기", how many public Rooms are open, from `/health`'s `public_rooms` (its `rooms` counts
  the private ones). Asked once each time Home is shown, never polled; skipped where `/health` is (no `serverUrl` under
  `--data`). None open: the button says so ahead of a click.
- **A5** In the chat field, Up and Down bring back recently sent messages, a few kept in memory only, as a shell does.
  Only when the caret is on the first or last line, so a multi-line message still moves by line.
- **A4** A line "여기부터 새 메시지" above the first message that arrived while the window was hidden or behind
  another. It goes once the window is used again and the line was seen.
- **A3** A message naming my Nickname is marked, and notifies even when message notifications are off, by a setting
  of its own in the notification tab, on by default.
- **A7** The tray menu gets mute, leave the Room and options, besides quitting.

## Small fun

- **C5** "진정하세요" stays the catchphrase. A third block in a row says it more firmly ("정말 진정하세요").
- **C3** Special dates change the greeting: 1 April, Christmas, New Year, and the app's own anniversary.
- **B2** The profile card shows when an account was made, and an anniversary ("가입 1주년") on its day.
- **C1** Clicking the droplet several times makes it splash, a short motion that follows the system's animation
  setting.
- **C2** Words in a Room's code: `ZZZ` makes the avatars sleepy, `CAKE` puts a cake in the Room, and so on. The code
  alphabet has no I or O, so words avoid them.

## Appearance

- **B1** Seasonal decorations in Cloud: snow in winter, petals in spring, as vectors. They fall for about 3 s when
  Home appears and then stay still; with animations off, or in Retro, which has no motion, they are still.
- **Room weather**: the input of the whole Room over the last 60 s, averaged over its online members and counting my
  own, in four steps: clear under 20 a minute, cloudy from 20, rain from 80, storm from 200. A step holds for a while
  before changing, and only a change redraws. Cloud changes its sky or clouds; Retro shows a small weather icon in the
  status bar. The numbers are starting values, to be tuned by eye after a scripted run.

## Achievements

- Groups: **input** (key presses and mouse clicks, 10,000, 100,000 and 1,000,000 each), **together** (hours in Rooms:
  10, 100, 1,000; a first Room of 4, 8 and 16 people; 100 and 1,000 chat messages sent), **discovery** (C1, each word
  of C2, a special date of C3, C5, the Konami code of C6) and **hidden** ones, shown as "???" until unlocked; part of
  discovery is hidden. Input is read only in a Room, so only time in a Room counts.
- A settings tab of its own, "도전과제", in both Skins, Retro's options window included. The list shows progress
  ("37,512 / 100,000"). An unlock is a toast with the notification sound.
- A toggle at the top of the tab, on by default. Off stops counting and toasts and keeps what was reached; on again
  goes on from there. "기록 지우기" asks first and clears everything.
- Kept in the Protected store: totals only (presses, clicks, minutes, messages) and the Achievements reached, each with
  its date. No time of day and no key. Counted in memory and written with the settings or when leaving a Room. This is
  added to ADR 0003's consequences.
- Local only: the server has nowhere to carry them, so only one's own profile card shows them.

## The Hidden Theme

- **C6** The Konami code (↑↑↓↓←→←→BA) unlocks "핫도그 가게" ("Hot Dog Stand"), a Retro Theme with a loud red and
  yellow Palette of our own over classic's shapes, after the old Windows 3.1 scheme. It is in the Theme list only once
  unlocked. A third Skin, such as a green monospace Terminal, is later work of its own.

## The Mini window and the hotkey

- **A9** "작은 창": a small window, always on top, with only the members' discs (their speaking pulse) and mute. Opened
  from the Room's More menu, the menu bar (보기(V)) and the tray; it closes when leaving the Room. About 8 MB while
  open, nothing idle. Each Skin draws its frame and layout.
- **A8** "단축키로 음소거" in the sound tab, off by default, with a fixed choice of Ctrl+Alt+M, Ctrl+Shift+M or Pause,
  through `RegisterHotKey` (no hook). It acts only in a Room and toggles the Room's mute. A combination another program
  holds makes the row say so.
