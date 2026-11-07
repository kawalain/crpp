# Plan: the messenger Themes, and room for more Themes and Locales

Agreed with the user on 2026-11-07. Delete this file when the work is done; what lasts goes to
`CLAUDE.md`, `GLOSSARY.md` and the ADRs.

Goal: 메신저 and 메신저 다크, a look as close as possible to MSN Messenger on Windows XP Luna, plus a
code base where a Theme or a Locale is easy to add. References:
[research note](../research/retro-messenger-look.md). Decisions:
[ADR 0005](../adr/0005-skins-own-shape-and-layout.md), [ADR 0006](../adr/0006-locales-compiled-into-the-exe.md).

## Limits

- Values and shapes are re-created as vectors. Nothing is taken from `luna.msstyles`, no logo, person
  icon, original sound or converted Microsoft font. "MSN", "Luna" and "XP" are never shown.
- No protocol change and no new feature. Menus and toolbars hold only working commands. No nudge,
  winks, status menu or public Room list: the server has none of these.
- Cloud Themes stay pixel-identical, and 구름 stays the default.
- No screenshot of the MSN 6-7 conversation window could be opened, so the requester's pictures and
  memory are the reference there.

## Steps

Steps run in this order; 2 and 3 touch the same code, so never in parallel. Each ends in small commits.

### 1. Freeze a baseline

Render every `--shot` scene to a local folder outside the repository, at 100% and `--dpi 144`, before
touching anything. Ten scenes rendered twice gave identical files, so hashes can compare them; the
whole set must pass the same test first. Baselines depend on this machine and are not committed.

### 2. Locale

- New pure module `src/locale`: `Text` keys, one table per Locale, positional placeholders, functions
  for clock, relative day, stay, count and date, and word lists for random Nicknames.
- Move about 260 lines of literals out of `ui`, `api`, `update` and `apps/gui`, including tray, window
  and message box text. Existing tests keep pinning the Korean table.
- `locale` setting, a key of this client's own; empty follows the OS language. A list in the system tab.
- A Locale decides its own line breaks, so they stay in the tables; what overflows in English is fixed
  by its wording and seen in the shots. The language tag of text layout comes from the Locale.
- Add English.

Done: Korean scenes are identical to the baseline; a test proves every Locale has every key and the same
placeholders; English scenes show no overflow.

### 3. Skin and Theme registry

- One table of Themes (id, label, Skin, Palette, light or dark) replaces `ThemeId` and the id arrays;
  settings lists derive from it.
- Add `--theme <id>` and `--locale <id>` to `--shot`, replacing the scene-name suffixes for theme. Fix the
  third argument in `CLAUDE.md`: it is the hour, not milliseconds.
- `Skin` interface, Cloud behind it: buttons, fields, panels, cards, scrollbars, switches, sliders,
  dropdowns, tabs, shadows, progress, frame, motion. Move literal radii and colours there. About ten
  screens that draw their own controls move too.
- Separate state from layout in Home, Room, prompt and settings frame.

Done: every Cloud scene in both Locales matches; idle memory and CPU are unchanged within measurement.

### 4. Window frame, as a throwaway first

Try in a throwaway build, then undo: `WM_NCCALCSIZE`, `WM_NCHITTEST`, `SetWindowRgn` for the top corners,
`SWP_FRAMECHANGED` for switching at run time. See with own eyes on a real desktop: Snap, resize edges,
minimize and restore, Alt+Space, right-click on the caption, DPI change between monitors, taskbar title.
Measure memory and CPU. If a region fails, stop and bring the alternatives back to the user.

Then build it for real: the title bar is a surface painted by `App`, so `--shot` sees it. Close and minimize
only. Hover is tracked with `TME_NONCLIENT`.

### 5. Luna controls

- `Gfx`: gradients in any direction, crisp hairlines, a font and rendering mode per Skin and Locale.
- Fonts: Korean Gulim 12 px aliased, falling back to Malgun Gothic aliased; English Tahoma 11 px; captions
  Trebuchet MS bold 13 px with Gulim bold for Hangul. Pretendard is not loaded while Luna is in use.
- Every Luna control per the research note. A `luna-controls` scene shows them all with hover, pressed,
  focused and disabled.
- New: menu bar with popup, group box, tooltip `#FFFFE1`. Mouse and Escape at first; Alt keys are a later step.

Done: `release_graphics` still lets go of everything, proved by the existing reference-count test.

### 6. Luna screens

- Window size per Skin and screen: Home 300x560, Room 640x500 DIPs to start; Cloud's saved size is left alone.
  Controls have a fixed size and layouts reflow.
- Home: header with picture frame, Nickname and greeting; code field and enter button; a list of Seats to
  return to, replacing the return dialog; a task panel with create and find public.
- Room: "대화 상대" line, chat with `이름 님의 말:` above an indented message and the time at the right,
  grouped as now; a list of up to 16 members with 32 px frames; a toolbar; compose and send; status bar.
- Typing: the member's frame glows orange and only that frame redraws.
- Prompt: sign-in window. Settings: options dialog with categories on the left and group boxes.
- Same layout with Luna controls: dialogs, profile card, member menu, emoji picker, update popup, changelog,
  offline popup, setup, reconnect, notice. Toasts: a pale blue panel inside the window. Tray: unchanged.
- No screen transitions, stretch or edge fade.

### 7. 메신저 다크

Same shapes. Starting values: caption `#4a6491`, `#1d2f52`, `#142440`; window face `#232833`; lists and fields
`#12161f`; text `#e6ebf5`. Orange accents stay. System sync may pair Skins freely, and the frame and size
follow when the OS flips.

### 8. Notification sounds

Own synthesized notification sounds for Luna, short and bright, chosen by the Skin. Keyboard and mouse packs
and the existing on-off settings do not change. Judged by ear with `crpp_soundtest`.

### 9. Close out

Update `CLAUDE.md` (Differences from upstream, Verification debt, module design), README and this plan's
removal. Measure idle and scrolling CPU and memory for each Luna scene and record them.

## Scenes to compare

Run with `--theme messenger` and `--theme messengerdark`, `--locale ko` and `en`, at 100% and `--dpi 144`:

`setup`, `prompt-*`, `home`, `home-hover`, `home-return`, `toast`, `dialog`, `session`, `chat`, `room-muted`,
`room-leave`, `room-profile`, `room-menu`, `room-calm`, `room-emoji`, `room-options`, `settings-display`,
`settings-sound`, `settings-notify`, `settings-system`, `settings-account`, `offline`, `reconnecting`,
`update-popup`, `update-progress`, `update-failed`, `changelog`, and new `luna-controls`.

Compared by eye against the requester's pictures and the research note. Pixel equality applies only to Cloud.

## Testing

- CTest: Locale table completeness and placeholders; each Locale's functions; Theme registry and
  `effective_theme`, including Skins paired under system sync; hit tests of the frame; window size per screen.
- `ui_harness`: flows under Luna through the same state and callbacks.
- Hidden desktop for scripted runs. The frame and Snap need a real desktop, by the user's or own eyes.

## Open risks

- A region may drop the DWM shadow: acceptable by the look of XP, but not yet seen. Windows 11 cannot be tried
  on this PC, which runs Windows 10 19044; it goes to Verification debt.
- Aliased text at 125% and above is less like the original, since there are more pixels.
- Layout per Skin for Room is the largest change, and its speaking and hit logic must stay shared.
- Publishing a close imitation in a public release carries some trade-dress risk, accepted by the user.
- English wording needs the user's review.
