# The look of early-2000s messengers

References for a nostalgic messenger theme with light and dark variants. The theme is inspired
by the era: no logo, mascot, artwork or sound from these programs is copied.

Status marks: **read** means the page was read; **snippet** means only a search summary was seen.
Exact pixel values come from open-source re-creations, not from Microsoft.

## The requester's pictures

Five pictures came with the request; they are not kept in this repository.

| Picture | What it shows |
|---|---|
| BuddyBuddy 5.6 login | Sky-blue panel with clouds, rounded fields, a large key-icon login button |
| BuddyBuddy 7.0 login | Tall window, olive-green title band and form panel, illustrated meadow |
| A Korean chat window (대화하기) | Teal gradient bands, tab, pale panels, `name:` line then an indented message, avatars on the right |
| MSN Messenger, Live era | Blue title bar, status header, side tabs, search field, collapsible groups, small person icons |
| MSN Messenger on Windows XP | Luna frame, pale-blue list, side tabs, online/offline groups, "I want to..." panel, ad band |

## Timeline

Source: [Windows Live Messenger, Wikipedia](https://en.wikipedia.org/wiki/Windows_Live_Messenger) (read).

| Version | Year | Visible change |
|---|---|---|
| 4.6 | 2001 | Major interface change; contact groups |
| 6.1 | 2003-04 | Display pictures, conversation backgrounds, per-user theme colour |
| 7.0 | 2005 | Contact list styled like the conversation window; nudge |
| 8.0 | 2006 | Colour schemes for the whole application; timestamps and grouped messages |
| 8.5 | 2007 | Vista-style look (the sources disagree on beta or final) |
| 2009 | 2009 | Scenes, gem-shaped status icons, picture borders, standard Vista frame |

- [LiveSide, WLM 9 beta compared with 8.5](https://liveside.net/?p=1297) (read): header buttons, status colour on the picture rim.
- [Windows Wallpaper Wiki](https://windowswallpaper.miraheze.org/wiki/Windows_Live_Messenger) (read): backgrounds from 6.1 to before 2011.
- [Forest Watch on 8.5](https://forest.watch.impress.co.jp/article/2007/11/07/livemessenger85.html) (snippet): flat, glossy Vista style.
- [Skeuoss, MSN Messenger history](https://skeuoss.net/blogs/blog/msn-messenger-history) (read): the nudge shakes the window with a sound.
- [Escargot](https://escargot.chat/) (read): a revival of the service and old clients, not a restyle. No client dark mode.

No official dark skin was found. A dark variant is this client's own design.

## Korea

- [BuddyBuddy closure, GameMeca, 2012](https://www.gamemeca.com/view.php?gid=8329) (read): started January 2000, closed in stages in 2012.
- [BuddyBuddy 7.0, Boannews](https://www.boannews.com/html/detail.html?idx=13158) (read): December 2008, a simpler design.
- [Messenger round-up, Electronic Times, 2005](https://www.etnews.com/200505100033) (read): notes, animated notes and emoticons in BuddyBuddy 5.5; Tachy's semi-transparent mini chat window.
- [MSN overtaken by NateOn, Electronic Times, 2013](https://www.etnews.com/201303110177) (read): MSN was first with about 12 million users in early 2004, overtaken in March 2005. A [2009 paper](https://ssu.scholarworks.kr/handle/2018.sw.ssu/16414?mode=full) says September 2005.
- [Office use of messengers, Seoul Shinmun, 2001](https://www.seoul.co.kr/news/2001/05/02/20010502008005) (read).
- [대화명 culture, Maeil, 2007](https://www.imaeil.com/page/view/2007040516162006263) (read): moods, announcements and symbols in display names.
- [Korean social media of times past, JoongAng Daily](https://www.koreajoongangdaily.com/lifestyle/a-look-at-koreas-social-media-platforms-of-times-gone-by/10973905) (read): BuddyBuddy's teenage users.
- [Cyworld, Wikipedia](https://en.wikipedia.org/wiki/Cyworld) (read): minihompy, miniroom, the wider nostalgia of the period.

## Concrete values from re-creations

[XP.css](https://github.com/botoxparty/XP.css), MIT, `themes/XP` (read):

- Title bar gradient, top to bottom: 0% `#0997ff`, 8% `#0053ee`, 40% `#0050ee`, 88% `#0066ff`, 93% `#0066ff`, 95% `#005bff`, 96% `#003dd7`, 100% `#003dd7`. Height 28 px, top corners 8 px and 7 px.
- Title text: Trebuchet MS 13 px, white and bold, shadow 1 px `#0f1089`.
- Frame: `#0831d9`, `#001ea0`, `#00138c`, `#166aee`, `#003bda`, `#0855dd`.
- Window face: `#ece9d8`.
- Button: border `#003c74`, radius 3 px, face 0% `#ffffff`, 86% `#ecebe5`, 100% `#d8d0c4`.
- Button pressed: 0% `#cdcac3`, 8% `#e3e3db`, 94% `#e5e5de`, 100% `#f2f2f1`.
- Button hover glow: `#fff0cf`, `#fdd889`, `#fbc761`, `#e5a01a`. Focus glow: `#cee7ff`, `#98b8ea`, `#bcd4f6`, `#89ade4`.
- Scrollbar thumb `#c8d6fb`, 1 px white border, radius 2 px; track from `rgb(197,213,255)` to `rgb(181,211,255)`.

More controls from XP.css:

- Caption buttons: 21x21 with a 2 px gap; close ramps from `#e46142` to `#dc451f` with a white cross; the others are blue with white glyphs.
- Button: at least 75x23, padding 0 12 px. Disabled text `#808080` with a white shadow.
- Text field: 1 px `#7f9db9`, white, height 23 px, padding 3 px 4 px. Selection `#2267cb` with white text.
- Dropdown: border `#7f9db9`, arrow button 15x17.
- Checkbox and radio: 13 px, border `#1d5281`, face at 135 degrees from `#dcdcd7` to `#ffffff`, pressed from `#b0b0a7` to `#e3e1d2`, hover rings `#f8b636`, `#fedf9c`, tick `#22a122`.
- Slider: 2 px track `#ecebe4` with `#f3f2ea` edges, thumb 11x21.
- Progress bar: 14 px, border `#686868`, radius 4 px, green blocks 8 px wide with 2 px gaps; ramp `#acedad`, `#7be47d`, `#4cda50`, `#2ed330`, `#42d845`, `#76e275`, `#8fe791`.
- Tab: face `#ffffff`, `#fafaf9` 26%, `#f0f0ea` 95%, `#ece9e5`; border `#91a7b4`, radius 3 px. Selected: fill `#fcfcfe`, top line `#e68b2c`, inner line `#ffc73c`.
- Scrollbar: 17 px, thumb `#c8d6fb`, inner `#bad1fc` and `#b7caf5`, arrows 17x17, corner `#dfdfdf`.
- Group box: white, 1 px `#d0d0bf`, radius 4 px, legend `#0046d5`.
- XP.css has no rules for menus, tooltips or list selection. Its bundled font is a conversion of a Microsoft font: not to be copied.

Luna system colours. Three third-party tables agree; Microsoft explains the meanings in [GetSysColor](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getsyscolor) but gives no values:
[zaxbux](https://gist.github.com/zaxbux/64b5a88e2e390fb8f8d24eb1736f71e0), [xeondesktop](https://github.com/xeons/xeondesktop), [WinPaletter](https://github.com/Abdelrhman-AK/WinPaletter).

- Control `#ECE9D8`, highlight `#316AC5`, tooltip `#FFFFE1`, hot track `#000080`, scrollbar `#D4D0C8`, desktop `#004E98`.
- Active caption `#0054E3` to `#3D95FF`, inactive `#7A96DF` to `#9DB9EB`. These describe a horizontal gradient for classic drawing; the Luna caption itself is the vertical ramp above.
- Metrics per WinPaletter: caption 25 px, scrollbar 17 px, caption text Trebuchet MS 9.75 pt bold, other text Tahoma 8.25 pt. These ship with Windows 10 and 11.

The XP balloon is yellow ([Petri](https://petri.com/disable_balloon_messages/)), matching the tooltip colour.

Screenshots that open: [MSN Messenger 4.6](https://upload.wikimedia.org/wikipedia/en/a/a4/MSN_Messenger_4.6.png), [Live Messenger 8.0](https://upload.wikimedia.org/wikipedia/en/9/9b/Windows-Live-Messenger-80-236x300.png), [Live Messenger 2009 on Escargot](https://upload.wikimedia.org/wikipedia/commons/7/77/Escargot_Windows_Live_Messenger.png). No screenshot of the 6.x or 7.x conversation window or of its toast could be opened: the requester's own pictures and memory stand in for them.

## An own-drawn window frame

- [WM_NCCALCSIZE](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-nccalcsize) and [Custom Frame](https://learn.microsoft.com/en-us/windows/win32/dwm/customframe): removing the standard frame, and `SWP_FRAMECHANGED` to apply it. Windows Terminal's `NonClientIslandWindow.cpp` is a public worked example.
- [WM_NCHITTEST](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-nchittest) and [TRACKMOUSEEVENT](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-trackmouseevent): caption buttons report through hit tests, and hover has to be tracked by the app.
- [Rounded corners](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-rounded-corners) and [corner preference](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_window_corner_preference): DWM rounds all four corners on Windows 11 and none on Windows 10, and has no top-only option. A window with a region is never rounded by the system.
- [SetWindowRgn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowrgn): the cheap way to round only the top. Layered windows and DirectComposition need a full-window surface. No cost figures were found; to be measured.
- Unverified: what a region does to the DWM shadow, Aero Shake, the minimize animation.

[7.css](https://github.com/khang-nd/7.css), MIT, `gui` (read), for the later Aero period:

- Font 9 pt Segoe UI. Surface `#f0f0f0`. Window base `#4580c4`, radius 6 px.
- Button: radius 3 px, border `#8e8f8f`, face `#f2f2f2` 45%, `#ebebeb` 45%, `#cfcfcf`; hover `#eaf6fd` 45%, `#bee6fd` 45%, `#a7d9f5`.

[Windows XP visual styles, Wikipedia](https://en.wikipedia.org/wiki/Windows_XP_visual_styles) (read): Luna came as Blue, Olive and Silver; title bars used Trebuchet MS and most elements Tahoma.

## Fonts available today

- [Windows 10 font list](https://learn.microsoft.com/en-us/typography/fonts/windows_10_font_list), [Windows 11 font list](https://learn.microsoft.com/en-us/typography/fonts/windows_11_font_list) (read): Tahoma, Segoe UI and, on 11, Malgun Gothic are base fonts. Gulim, Dotum, Batang and Gungsuh are in the optional Korean Supplemental Fonts package, usually added when the language is enabled.
- [Microsoft font information](https://support.microsoft.com/en-au/topic/font-information-762e3fdd-876c-4dd9-b1b3-8ceeb5c285bf) (read): Gulim was the Korean UI default of Windows XP.

## Sounds

- [Emotion-free quiet MSN Messenger sound scheme, 2004](https://learn.microsoft.com/en-us/archive/blogs/tristank/more-fiddling-emotion-free-quiet-msn-messenger-sound-scheme) (read): "bling" for a contact online, "optimistic bleeps" for a message.

Nothing was found on the copyright of these sounds, so none may be reused; only their character can inform new ones.

## Could not be verified

- The `Name says:` line of MSN and its Korean wording. The Korean chat picture does show `name:` above an indented message.
- MSN's status colours, the exact gradient of its contact list, and any font size it used.
- The look of NateOn, Geni and most of Tachy.
- The Korean name of the nudge, and the login and chat notices of the time.

Unreachable: Neowin (403 and bot challenge), mvps.org, msgplus.net and msgpluslive.net. Screenshot archives gave no usable page.
