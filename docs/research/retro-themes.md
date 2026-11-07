# Colours of the Retro Themes

What the Palettes in `skins/retro` come from, and what was chosen here. No file of Windows was read or copied.

## Sourced

Classic follows Windows Standard as Wine's defaults of 1999 give it
([`syscolor.c`](https://goma.googlesource.com/wine/+/refs/tags/wine-990704/windows/syscolor.c)), which is an emulation and
was not checked against Windows 98 itself: face `#c0c0c0`, highlight `#ffffff`, light `#dfdfdf`, shadow `#808080`, dark
shadow `#000000`, caption `#000080` to `#1084d0`, idle caption `#808080` to `#b8b4b8`, selection `#000080`, tooltip `#ffffc0`.
That source's gray text, `#c0c0c0`, equals the face and could not be read, so disabled text is `#808080` over a white offset.

Luna's solid colours come from [one table](https://www.brethorsting.com/blog/2007/12/windows-system-colors/), which was not
checked against the theme itself. Blue's face and highlight agree with other sources.

| | Blue | Metallic (Silver) | Homestead (Olive) |
|---|---|---|---|
| Caption | `#0054e3` | `#c0c0c0` | `#8ba169` |
| Idle caption | `#7a96df` | `#ffffff` | `#d4d6ba` |
| Face | `#ece9d8` | `#e0dfe3` | `#ece9d8` |
| Highlight | `#316ac5` | `#b2b4bf` | `#93a070` |
| Button shadow | `#aca899` | `#9d9da1` | `#aca899` |

## Drawn here

- No public source gives the gradient stops, frame, caption button or scroll bar colours of Homestead and Metallic: they live
  in the theme file. Their stops follow the shape of `luna_blue`'s around the caption colour above.
- Metallic's caption text is black with a white shadow, and its selected text is black, because white on `#b2b4bf` is hard to read.
- Every dark variant, including `classic_dark`. None of the four existed.
- `luna_blue` and `luna_blue_dark` are the colours the Messenger Themes had, from `retro-messenger-look.md`.

All of this needs eyes, on a real display.
