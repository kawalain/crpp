# Plan: Text selection, a context menu and keyboard-only use

Agreed with the user on 2026-11-09. Delete this file when the work is done; what lasts goes to `CLAUDE.md` and the
ADRs. Decision on pack formats: [ADR 0009](../adr/0009-a-catalog-address-per-pack-format.md).

Goal: text can be selected and copied as in any Windows program, a right-click or the menu key opens a menu of what
can be done with it, and every screen can be used with the keyboard alone, following Windows conventions.

Upstream selects only chat bodies and its two text fields (`user-select: none` on `body`), suppresses every context
menu, and has no keyboard navigation. Chat selection is upstream's; the rest is a difference to list in `CLAUDE.md`.

## Order

Each is its own commit or series of commits, in this order.

0. **`fix(assets)`**: an exe never fetches a pack in a format it does not read, and keeps the installed one. Pin it
   with a test. v0.2.0 overwrites a pack with whatever the catalog lists, without checking the format.
1. **Pack format 2** (ADR 0009): the official Repository gets a catalog address per format and the format-1 one is
   frozen; the exe reads formats 1 and 2. A format-1 pack has `focused` always false and no `context_menu`. No notice
   is shown for one.
2. **Selection, copy and the context menu.**
3. **Keyboard-only use.** It starts with a `fix(ui)` of its own: in Cloud, Enter in a dialog does not reach Confirm,
   as only a Dialog pane (Retro) maps it.

Screen readers (UI Automation) are a separate piece of work later. The focus model is built so that it can serve one:
it knows what has focus, and each stop's role.

## Selection and copy

- **Fields** (`TextField`) already select with Shift+arrows, Ctrl+A and the mouse, and copy with Ctrl+C/X/V. Added:
  drag selection in the chat input, double-click for a word, triple-click for a paragraph (all of a single-line
  field), Ctrl+Left/Right by word, Ctrl+Backspace/Delete by word, and scrolling while a drag is past the edge, faster
  the farther it is, on a timer that runs only during the drag. Undo is separate work.
- **Read-only text** that can be selected: chat bodies, the changelog's text and a Repository's key fingerprint (in
  the system tab and in the question when adding one). Labels, buttons and nicknames are not.
- **Chat** selection spans messages; a copy joins the bodies with line breaks, without nicknames or times. A selection
  is held by the messages, so it stays while new ones arrive and while scrolling; a message dropped from the cache
  leaves it. Selecting chat text takes keyboard focus from the chat input, as in upstream; a click on the input gives
  it back.
- **Keyboard** for read-only text, which has no caret: the chat log is one tab stop, Up/Down move between messages,
  Shift+Up/Down select whole messages, and Ctrl+C or the menu copies, the focused message when nothing is selected.
  The changelog takes Ctrl+A then Ctrl+C. The fingerprint is a tab stop, and Ctrl+C copies all of it.
- **Paste** into a single-line field turns line breaks and tabs into a space; the Room code field drops spaces and
  upper-cases; anything past the maximum length is cut. The chat input keeps line breaks.
- **Clipboard**: plain text only (`CF_UNICODETEXT`), both ways. A link copies as its address.
- **Feedback**: none for an ordinary copy. Only a copy that fails says so. The Room code button keeps its tip.
- The selection's colour comes from the Theme's Palette, as a field's does now.

## The context menu

- Drawn from the Skin Pack, as the screen `context_menu`, like the member menu. A pack without that screen shows no
  menu; the keys still work. Cloud's and Retro's packs draw it.
- **Items.** A field: 잘라내기(T), 복사(C), 붙여넣기(P), 삭제(D), 모두 선택(A); in English Cut, Copy, Paste, Delete,
  Select All with their access keys. What cannot be done now is greyed, not hidden; a password field always greys Cut
  and Copy. Read-only text: 복사 and 모두 선택, where Select All takes the whole area. On a chat link, "링크 주소 복사"
  comes first. The labels and access keys are in the Locale tables. The program hands each item's shortcut text to the
  pack, which decides whether to show it.
- **Opening.** A right-click, the menu key or Shift+F10; the keys open it at the caret or selection. A right-click
  inside a selection keeps it; outside one, a field moves its caret there and read-only text drops its selection (Copy
  then greys). Where there is no text, nothing opens; members keep the member menu. During IME composition the
  composition is committed first.
- **Place.** At the pointer, moved or flipped to stay inside the window the click was in; a Retro panel with a window
  of its own keeps it in that window. It closes on a click outside, when the window loses focus, or when it resizes.
- **Keys.** Up/Down move and wrap, Home/End go to the ends, greyed items are skipped, Enter or Space runs, Escape
  closes, Alt or F10 closes, and an access key runs its item.
- **Motion.** It fades in over 120 ms like a dropdown, and appears at once under Retro or without "Show animations".

## Keyboard-only use

- **Scope.** Every screen: the first-run prompt, Home, Room, the settings panel and its tabs, dialogs, the profile
  card, the emoji picker, the changelog, the member menu and Retro's menu bar.
- **Tab stops.** A surface says which hit ids are tab stops and their role (button, toggle, slider, dropdown, tab,
  list, field, link): that is behaviour, the same in every Skin. The order is where the node stands in the pack, not
  the order hits are registered (a container's comes after its children's); a node may set `tab_order` to change it.
  A stop scrolled out of view is still a stop, and focusing it scrolls it fully into view with the usual 160 ms
  motion. A control that cannot act now is skipped.
- **Scope of Tab.** Tab and Shift+Tab cycle within the topmost layer that is open (a dialog, the settings panel, the
  emoji picker, the profile card, the member menu), or else the whole screen, and wrap. A Retro panel with a window of
  its own cycles within it. With nothing focused, Tab goes to the first stop.
- **Opening and closing.** A layer that opens takes focus: a dialog's default button, except Cancel for a question
  that cannot be undone (leaving, logging out, removing a Repository), or its field if it has one. A layer that closes
  gives focus back to the control that opened it.
- **Mouse.** A click on a control moves focus to it with the indicator hidden, so Tab continues from there. A click on
  nothing clears focus.
- **Indicator and underlines.** Hidden while the mouse is used and shown once Tab, Alt or an arrow is pressed, as
  `WM_UPDATEUISTATE` does; always shown when the system's "Underline access keys" (`SPI_GETKEYBOARDCUES`) is on. A pack
  sees it as the state `focused`, beside `hot` and `pressed`, true only while it is shown. Cloud and Retro draw it on
  every control; Retro draws the dotted rectangle of its time. `scenes.txt` gets `focus-*` scenes to compare by eye.
- **Keys per control**, after Windows and WAI-ARIA:
  - Button: Enter, Space. Toggle: Space.
  - Slider: Left/Right a step, Page Up/Down a large step, Home/End the ends.
  - Dropdown: Alt+Down, F4 or Space opens; Up/Down change the value while closed; a first letter jumps.
  - Settings tabs: Ctrl+Tab and Ctrl+Page Up/Down anywhere in the panel, Left/Right on the tab row.
  - Lists and the emoji grid: arrows move, Enter picks.
  - Scroll areas with focus: Up/Down, Page Up/Down, Home/End.
  - Dialogs: Tab stays inside, Enter is the default button, Escape cancels.
- **Room.** Arrows move between members, Enter opens a profile, the menu key or Shift+F10 opens the member menu. On a
  pinnable switch, the menu key or Shift+F10 toggles the pin, as a right-click does. Tab in the chat input moves on
  instead of typing. Keys used to navigate a Room are heard by the others like any key, as Raw Input reports them.
- **Retro's menu bar.** Alt or F10 enters it, Alt+letter opens a menu, Left/Right move between menus.

## Records

- `GLOSSARY.md`: nothing; selection and context menus are not this project's own concepts.
- `CLAUDE.md`, Differences from upstream: the context menu, what can be selected, keyboard-only use and its
  indicator, Alt keys in Retro's menu bar (replacing "there are no Alt keys yet"), and the menu's motion.
- `skins/README.md`: `focused`, `tab_order` and the `context_menu` screen.

## Testing

- CTest: selection over messages and its survival, paste rules, word moves, menu items and their greying, tab order
  from a pack, skipping, scopes, focus return, the dialog Enter fix, format reading and the fetch refusal.
- `ui_harness` drives keys through `App` without a window.
- `--shot` of the menu and of `focus-*` scenes in both Skins, compared with upstream and by eye.
- A scripted run on a hidden desktop. The clipboard, IME and real menu keys need a real desktop; what does not run
  there goes to Verification debt.
- Idle memory and CPU measured afterwards: nothing may run while idle.
