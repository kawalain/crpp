# Writing a Skin Pack

A Skin Pack is how the program looks, as data: no code, nothing to run. Each folder here is one pack. `schema/` is
not a pack: it holds the JSON Schemas, which are written by a tool and are not edited by hand.

```
<pack>/pack.jsonc           Palettes, styles, parts and constants
<pack>/screens/<name>.jsonc one per screen
```

Files are JSONC, so they can hold comments and a comma after the last item. Each names its schema on its first line,
which gives an editor that follows `$schema`, such as VS Code, completion, descriptions and checking as you type:

```jsonc
{
  "$schema": "../../schema/screen.home.schema.json",
  "draw": [
    // A node is one thing to draw, chosen by which key it has.
    {"fill": "$face"},
    {"use": "button", "text": "#ok", "hit": "create", "right": "W-8", "y": 8},
  ],
}
```

`report` tells the program what a screen found out. A screen of a panel that has a window of its own reports `width`
and `height`, and the window takes that size: the program walks the screen without drawing to learn it.

A screen's schema lists what the program offers there: `@numbers` and `@texts` to read, and the names of its hits,
pictures, fields, slots, reports and notes. A screen with no file is drawn by the program itself, so a pack can grow
one screen at a time. The node language is described at the top of `src/skin/include/crpp/skin/pack.hpp`.

## What the program reads

`constants` are names for numbers anywhere in the pack, and the program reads some itself: `pack.schema.json` lists them. `own_frame`
says the pack draws its window frame and gives every panel a window, which takes the frame's sizes and each window's too. `fonts` names
installed fonts, the first this system has. `cursors` names a part for each of the seven, drawn in a box of 32 by 32, with the point that
clicks: look at them with the scene `cursors`. Without `cursors`, the pointer is the system's.

## Trying a pack

Check it, which says what is refused and where, then render it. Both stay on this machine. Build first with
`.github\scripts\msvc-build.cmd build-msvc`.

```
build-msvc\src\ui\crpp_skin_schema.exe --pack skins\messenger
tests\shots\render.ps1 -Exe build-msvc\apps\gui\crpp.exe -Out out -Size auto -Extra '--theme','messenger','--skin-dir','"skins\messenger"'
```

Schemas catch wrong keys and names, but not what is inside an expression; `--pack` is the last word, as it runs the
loader the program uses. A pack that a Theme does not name a Palette for is not drawn under that Theme.

## When the program changes

A new screen, name or node key changes the schemas, and the test `ui.skin_schema` then fails until they are written again:

```
build-msvc\src\ui\crpp_skin_schema.exe skins\schema
```
