# Differences from upstream

Upstream (`pb29/Cloudroom`, branch `dev`) defines the features, screens and visuals. This client
differs where upstream is broken or a change is clearly better. Each difference is listed here, in
the commit that makes it.

- No input log. The advanced settings keep a diagnostic log without input data.
- "키보드 문자열 구분" is replaced by the Detail mode toggle, off by default: only with it on
  are the row of a key and the special keys sent
  (`docs/adr/0003-presence-mode-by-default.md`). The row is the key's place on the keyboard,
  so another layout than QWERTY can give another row than upstream's.
- No "최소화 시 절전 모드" toggle: it switches a WebView2 memory mode. Render
  resources are always released while the window is hidden, and after 30 s hidden so is
  the GPU driver's memory; see `docs/research/memory-floor.md`.
- Escape closes every dialog that has a cancel action.
- The server can be chosen, where upstream only reads `serverUrl` from its settings file: from the first login
  screen's "서버 바꾸기" and the advanced section of the system tab. An address is taken only over HTTPS (plain HTTP
  only to this machine), without a user name, query or fragment, when its `GET /health` answers within 5 s, with no
  redirect and a certificate that holds, in the form of upstream's server (`ok`, `rooms=`, `public_rooms=`), and once
  the user has confirmed that it receives their input signals, chat and account. Each server keeps its own account
  (`serverAccounts`, encrypted beside the token), so returning to one brings its account back. A Shared store
  keeps that key in upstream's `settings.json`, which upstream may drop when it saves.
- More text can be selected and copied: besides chat messages and the fields, the changelog and a Repository's key
  fingerprint, and a question's words with Ctrl+C, as in a Windows message box. Fields select by word and paragraph
  with a double or triple click and move and delete by word with Ctrl.
- Text has a context menu, where upstream suppresses every one: by a right-click, the menu key or Shift+F10, drawn
  from the Skin Pack. A field offers Cut, Copy, Paste, Delete and Select all, read-only text Copy and Select all, and
  a link in chat its address first. What cannot be done is greyed. Cloud's fades in over 120 ms.
- The keyboard alone works the controls, where upstream has no keyboard navigation. Tab and Shift+Tab go from control
  to control, staying inside what is open on top, and Enter and Space act. The arrows, Page keys, Home and End move
  sliders, dropdowns, tabs and the emoji grid, and Ctrl+Tab changes the settings tab, as in Windows. Where the keyboard
  is shows after Tab, an arrow or Alt, drawn by the Skin Pack. A layer that opens takes the keyboard and gives it back
  when it closes; a question that cannot be undone starts on Cancel. A menu's access keys are underlined once
  Alt was pressed or when the menu opened from the keyboard, as in Windows; in Korean the letter is in the words too,
  as 복사(C).
- A message that names this member, by Nickname or `@handle` anywhere in it and Latin letters in either case,
  stands out in the list and notifies even with tray notifications and the taskbar flash off, unless "누가 내 별명이나
  사용자명을 부르면 항상 알려 주기" is turned off (`chatMentionNotify`, a key of this client's own).
- What others write while every window of the app is behind another program, minimized or hidden sits under a line
  "여기부터 새 메시지". It goes when the app is left again after coming back, or when a message is sent.
- Up and Down at the chat field's first or last line go through the last 20 messages sent, as in a shell, and
  back to what was being written. They are kept in memory only.
- The chat field counts characters as the server does, so an emoji is one, and from 30 left it shows how many.
- A link in chat has a colour of its own, the Palette's `link`, where upstream's is the text's and shows only by
  an underline under the pointer.
- The channel balance is explained: "위치에 따라 좌우로 듣기" and "좌우 차이", in words
  instead of a ratio, and locked while virtual surround is off, when it changes nothing.
  Its settings keys are upstream's, but a value means more: straight to the side, 60 is 6 dB
  between the ears and 90 is 19 dB, so the default can be heard. The same value sounds weaker in
  upstream.
- The sound is built for working side by side, on panning rather than echo. Members sit
  far apart left to right while Desks are free, not at random. Reverb is a list of its own,
  "울림": 끔, 사무실 (the default, short and quiet), 넓은 공간 (the old one). It is kept under a
  key of this client's own and does not depend on virtual surround.
- Logging out inside a Room says it leaves the Room, then does so. Nickname and
  handle can be changed without leaving.
- The first-run prompt shows only to someone with no stored identity: a saved
  nickname, Guest ID or token goes straight to Home, and an account's details are
  refreshed in the background. Upstream waits for `api/me` before showing anything.
- Logging out, or a rejected token, also erases the Guest ID, so the next start asks
  again. A token refused on connecting leads to the sign-in prompt at once.
- HTTP redirects are not followed, so a password never goes to another address.
- The emoji picker from the account tab dims the whole window, not only the settings
  panel, so the droplet is not usable under it.
- The first time the window hides to the tray while in a Room, a notification says
  the Room is still connected, because input timing keeps being shared.
- Reachability is not polled in the background. `GET /health` runs at startup, after a
  connection that failed without reaching the server, and every 5 s while the offline
  popup is up. A server lost while on Home shows at the next attempt. Its answer's count of
  public Rooms is shown under the search, and coming back to Home asks once more for it.
- Reconnecting backs off and gives up at the server's grace instead of retrying every
  3 s forever; it does not ask `/health` first. A connection error names no reason in
  parentheses, because the transport reports none.
- A speaking disc costs less than upstream's: held up by someone who keeps typing it draws
  no frame, and otherwise only the avatars are drawn.
- A Room that cannot be resumed at startup says so; upstream returns to Home silently.
- A held Seat says how many minutes it is still held, in Retro's list and in Cloud's question. Home wakes for it only
  when the minute drawn changes.
- The program calls itself Cloudroom++, so its autostart entry does not collide with upstream's.
- Home greets by the day instead of the hour on New Year's Day, 1 April, 24 and 25 December and 7 November, the day
  v0.1.0 came out.
- A profile says "(오늘 2주년)" after the date an account was made, on that day of a later year by the local calendar;
  one made on 29 February has it on the 28th in other years.
- Cloud's settings droplet, clicked five times in a row with less than half a second between clicks, splashes for
  700 ms. Retro has no droplet and no motion.
- A Room whose code holds a word shows it: ZZZ puts a doze over every avatar, CAKE a cake (in Cloud at the bottom of
  the window, in Retro at the toolbar's end) and STAR stars (in Cloud's sky, in Retro by the cake). Codes are random,
  so these are rare.
- Cloud's Home has the season's fall behind its clouds, by the local month: snow from December to February, petals
  from March to May, leaves from September to November, nothing in summer. It comes down for 3 s each time Home
  appears and then holds still, and is still at once without animations. Retro has none.
- A Room has weather, from how busy it is: every input event over the last minute, mine too, per online member.
  From 20 a minute it is cloudy, from 80 it rains and from 200 there is a storm, and a step must hold 10 s before
  the sky follows. Cloud darkens its sky and cloud, with rain and lightning that do not move; Retro shows a cloud
  at the status line's end. Only counts go in, and nothing is kept or sent. A quiet Room looks again every 5 s
  only until the sky has cleared.
- "계절과 날씨 효과" in the display tab's Theme section (`skyEffects`, a key of this client's own, on by default)
  turns off the season and the weather together. Off, nothing is counted and nothing wakes, so the hidden
  Achievement for a storm cannot be reached until it is on again; a Room's sky starts clear when it comes back.
- Achievements, 24 of them in four groups (input, together, discovery, hidden), in a settings tab of their own after
  the account tab. Reaching one shows a toast with the message sound. They are counted only while "도전과제 기록하기"
  (`achievementsOn`, on by default) is on, and "기록 지우기" asks before clearing them. The record is four totals
  (presses, clicks, minutes in Rooms, messages sent) and the day each was reached, under `achievementRecord`, a key of
  this client's own beside the settings, so a Shared store's upstream may drop it when it saves. The server has
  nowhere to carry them: only my own profile card shows how many.
- The Konami code (↑↑↓↓←→←→BA) pressed anywhere in the app finds hidden Themes (`hiddenThemesFound`, a key of this
  client's own): Retro's "핫도그 가게" ("Hot Dog Stand"), classic's shapes in loud red and yellow after the Windows 3.1
  scheme of that name, in colours chosen here. A pack marks such a Theme `hidden`, and a catalog does not offer it.
- The tray menu has mute, the Mini window and leaving the Room while in one, and the options, besides quitting.
- "음소거 단축키" in the sound tab mutes the Room from any program, with Ctrl+Alt+M, Ctrl+Shift+M or Pause (`muteHotkey`,
  a key of this client's own, none by default). It is a system shortcut (`RegisterHotKey`, no hook), held only while
  in a Room so that other programs have it otherwise, and a choice another program holds makes the row say so.
- A Room can be kept beside other work in the Mini window ("작은 창"), opened from the Room's options, the tray and
  under Retro the menu bar's 도구(T): a small window, always on top and without a taskbar button, with every member's
  disc, lit while they make a sound, mute and close. It stays while the main window is minimized or in the tray,
  moves by any place with nothing to click, opens at the top right of the screen without taking the keyboard, and
  closes when the Room is left. A disc is lit or dark with no fade, so it draws only when one changes. Each Skin
  draws it whole: Cloud a rounded card, Retro a tool window.
- The system tab's advanced section has "가려져 있을 때 메모리 비우기" (at once, 10 s, 1 min, never), kept
  under a key of this client's own. Behind another program, outside a Room, the window lets go of its
  graphics device after that wait and makes it again when it next draws.
- Assets may be missing without stopping the app; see ADR 0004.
- Motion upstream does not have: wheel scrolling settles over 160 ms, the ends stretch up
  to 48 px and return over 240 ms, screens cross-fade over 180 ms with an 8 px rise,
  settings tabs over 140 ms, the settings panel and emoji picker over 160 ms with 6 px,
  a dropdown fades in over 120 ms, and a new chat message rises 8 px over 300 ms. Scrolled
  areas fade at the edge they cut, by up to 27 px. All of it follows the system's "Show
  animations" setting; there is no toggle of its own.
- The app is named Cloudroom++, and its data stays in the same folders as upstream's.
- Every JSON file this client reads may hold comments and a comma after the last item. It writes plain
  JSON, so upstream reads what it saved, but not a file someone commented by hand. Saving drops comments.
- English besides Korean, chosen in a section of its own; it follows the OS language until then.
  Other words for the same things: 구름 is "Room", 사용자명 is "handle".
- The wording is gentler than upstream's: Korean speaks in ~해요 and ~할게요, with plainer names (외관 for
  디스플레이, 대화 for 채팅, 나가기 for 떠나기), and English says "we" and "please". "진정하세요" stays as it is,
  as the app's own catchphrase. The third block in a row, each within a minute of the last, says "정말 진정하세요".
- The time section (24시간제, 자리비움으로 바뀌는 시간) and the language section are on the display tab, after the
  chat size, where upstream has time on the system tab. They are about what the screen shows.
- The changelog comes from GitHub release notes, so an empty one says it could not be
  loaded, and a first run ever does not open it. A new version's notes open when the app
  first notices it, not after the update, and not again once it is installed. They are
  the release's most important changes, then its commits by kind.
- A user picks a Theme and a dark mode (off, on, following the system) in the display tab, where
  upstream has a toggle and lists of light and dark Themes. One list holds every Skin's Themes, each Skin's
  under a heading with its name between lines, so a Theme brings its Skin. They are read from the packs in
  `skins/` beside the exe and kept under `skin`, `skinTheme` and `darkMode`; until those are set upstream's
  `theme`, `systemTheme` and `lightTheme` are read, and the choice never writes them. The dark mode is
  locked while a Theme has no dark variant, keeping its value. A Theme of a pack not yet installed is in the
  list with its size, and is fetched when picked, with progress and a cancel under the list.
- Skins come from Repositories (ADR 0008), listed in the system tab: the official one, which stays, and those
  the user adds by an HTTPS address or `github:owner/repo[@ref]`, after a dialog with the key's fingerprint.
  A pack of an added one is `<first 16 digits of the fingerprint>/<id>`. A pack may extend one of another
  Repository, pinned by its key's fingerprint and maybe a version and digest; an update it was not made for
  waits in the system tab until the user takes it. An added Repository may offer Sound packs too, which the sound
  tab fetches when picked. Upstream has none of this.
- Upstream's Theme 구름 is 뭉게구름 ("Cumulus"), as the Skin it belongs to is 구름 too. Its id is `cumulus`;
  upstream's keys keep upstream's `cloudroom`, which is read as `cumulus` wherever it is found.
- The create cloud on Home really grows under the pointer, as upstream's does.
- A second Skin upstream does not have, Retro, in the look of the desktops of the 1990s and early 2000s
  (`docs/research/retro-messenger-look.md`, `docs/research/retro-themes.md`). Values and shapes are re-created
  as vectors, and nothing is taken from Windows or the old programs: no logo, person icon, sound or font of
  theirs. 구름 stays the default. Under Retro:
  - The app draws its window frame, with minimize and close only. Those two are ordinary hits in
    the client area, not caption buttons.
  - The window has a size per screen, remembered per screen under `window<Screen>Width` and
    `Height`, so it shrinks to Home and grows to a Room. Cloud's saved size is left alone.
  - Options, What's new, a profile, the emoji picker, the offline message and every question are
    real windows, owned by the main one and without a taskbar button. A question holds the others
    off until it is answered. Only messages inside the main window, menus and popups stay in it.
  - Home lists the Seats to return to, where Cloud asks once in a dialog.
  - A menu bar holds only commands that work. Alt or F10 alone goes into it and Alt with a title's letter opens
    that menu, as in Windows: the arrows, Enter, an item's letter and Escape work inside. The letter shows in
    the words, as "파일(F)", and is underlined once Alt was pressed.
  - Only links show the hand cursor, and none of the motion listed above runs.
  - Text is in the fonts of the time on this system, not Pretendard.
  - A message and someone entering sound as tones made in `synth`, not the pack's recordings: plain tones
    under classic, and bells under Luna.
  - The pointer is drawn here too, and only in the app's windows: seven vector cursors of the Skin Pack, made into
    system cursors when first needed. "테마 커서 쓰기" in the display tab, on by default and shown only for a Theme
    with cursors, gives the system's back (`themeCursor`, a key of this client's own).
