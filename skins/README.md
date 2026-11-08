# Writing a Skin Pack

A Skin Pack is how the program looks, as data: no code, nothing to run. Each folder here is one pack.

```
<pack>/pack.cskin             Themes, Palettes, styles, parts and constants
<pack>/screens/<name>.cskin   one per screen
<pack>/overlays/<name>/       the same two again, put on top for the Themes that ask for it
```

Files are in the pack language. A line is one node, a thing to draw: the word for its kind, then `key value` pairs.
Children go in `{ }`, and a line indented deeper than the one before it, with no `{` between, goes on with it.
`//` starts a comment.

```
// A node is one thing to draw, chosen by its first word.
fill white radius 4
text @tr.ok (8, 8, @W-16, 20) style body color hl
hit create (221, 10, 52, 60) {
  let over = ease(@hot, 150ms)
  fill alpha(hl, over) radius 13
}
use tip(shown = 1) text @tr.ok hit create
    x @W-80 y 8
```

- A box is `(x, y)` or `(x, y, w, h)`, measured from the box around it. `x`, `y`, `w`, `h`, `right` and `bottom` can
  also stand alone.
- A value is an expression: `+ - * /`, comparisons, `&& || !`, `a ? b : c`, and `min`, `max`, `floor`, `ceil`,
  `round`, `snap`, `ease`, `bezier`, `mix`, `alpha`, `textw`, `texth`, `hit` and `note`. A function's `(` follows its
  name at once. Numbers may say `40%` (0.4) or `150ms`.
- Every name the program defines carries `@`: the screen's own (`@seats`, `@nickname`), the wording (`@tr.home_create`)
  and the built-ins: `@W @H` the box being drawn in, `@X @Y` where it is on the window, `@x @y @w @h` the node's own box,
  `@u` a line's thickness, `@font_size`, `@hot` and `@pressed` for the nearest hit around, `@i` the round of a repeat,
  `@flow` how far down a column has come, and `@dark` for a dark variant. So the program can add names later without
  taking one of yours.
- The pack's own names are bare: Palette entries, constants, parts, arguments and `let`. A Palette entry that shares
  its name with a constant, argument or `let` is a mistake. `id` gives a node a name its later siblings read as
  `name.x`, `.right`, `.bottom` and so on.
- Text is `@tr.key`, `@name`, or `"words"`. A colour is `#rrggbb` or `#rrggbbaa`.

The node kinds and their keys are listed at the top of `src/skin/include/crpp/skin/pack.hpp`, and in `src/skin/keys.hpp`,
which is what the reader checks against. `crpp_skin names` lists the screens, and `crpp_skin names <screen>` what the
program offers on one: its `@numbers` and `@texts`, and the names of its hits, pictures, fields, slots, reports and
notes. A screen with no file is drawn by the program itself, so a pack can grow one screen at a time.

`report width = …` tells the program what a screen found out. A screen of a panel that has a window of its own reports
`width` and `height`, and the window takes that size: the program walks the screen without drawing to learn it.

## pack.cskin

```
pack cloud format 1
names ko "구름" en "Cloud"
theme cumulus names ko "뭉게구름" en "Cumulus" dark darkcloud
default_theme cumulus

constants {
  moves 1
  list_gap 4
}

palette cumulus {
  white #ffffff
  hl #66ccff
  hl_soft mix(hl, white, 70%)
  shade alpha(hl, 0.5)
  sky stops(0: #ffffff, 1: #eff8ff)
}

style body size @font_size line_height 20 weight 400

part tip(shown = 1) w 120 {
  fill alpha(white, shown) radius 6
  content
}
```

A Palette entry is a colour, a `mix` or `alpha` of an entry above it, or the `stops` of a gradient. A part takes
arguments with their defaults, and a `use` of it passes them by name; what the `use` holds in `{ }` goes where the
part has `content`.

## What the program reads

`constants` are names for numbers anywhere in the pack, and the program reads some itself: `required_constants` in
`pack.hpp` lists them, and a check says which one is missing. `own_frame` says the pack draws its window frame and
gives every panel a window, which takes the frame's sizes and each window's too. `fonts { korean "…" other "…"
caption "…" korean_size 12 other_size 11 }` names installed fonts, the first this system has. `cursors { arrow part p
hot (1, 1) … }` names a part for each of the seven, drawn in a box of 32 by 32, with the point that clicks: look at
them with the scene `cursors`. `cursors hard` draws them with hard edges and enlarges them by whole pixels. Without
`cursors`, the pointer is the system's.

## Motion

A pack whose constant `moves` is not 0 can move. What changes between two states is a transition, a `let` that runs
from 0 to 1 and back:

```
hit join {
  let over = ease(@hot, 150ms)
  fill alpha(hl, over) radius 13
}
```

It takes `ms_off`, `curve` (`ease` or `linear`), `first jump` for a state that is simply there when first seen, and
`again`, whose new value runs it up again. What a frame does not walk is forgotten, so a screen that comes back runs
again. A transition asks for frames while it moves, even if nothing reads it, so guard one that should not run with
`if`.

A group takes `scale` (around `about (x, y)`, the middle of its box, or with `origin` its corner), `opacity`, `dy`,
`fade_top` and `fade_bottom`. `mix(a, b, 40%)` and `alpha(a, 0.5)` work out colours anywhere a colour goes. What stays
in the engine, scrolling, the cross-fade between screens and the speaking discs, reads its numbers from the
constants in `moving_constants` of `pack.hpp`.

## Themes, overlays and extending

`theme` lines list what a user picks in this Skin: its names by locale, and its Palette, which is named as the Theme
unless it says `palette`. `dark` names the Palette of its dark variant. A Palette can say `extends` to start from
another, and then lists only what differs.

A Theme that needs other shapes as well as other colours names an `overlay`, a folder under `overlays/`. What that
holds replaces whatever the pack has by the same name, whole: a constant, style, part, Palette, screen, or `fonts` or
`cursors`. A pack is read for one overlay at a time, and holds only the Palettes of that overlay's Themes, so those
need not share entries with the others.

A pack can extend another with `pack <id> format 1 extends <id>`, in the same way: it gets everything that pack has
and replaces by name. It is another download, and can itself extend one, up to four levels. Nothing inside a part or
screen can be patched: copy it and change it.

A pack may say its version on the same line, `version 0.2.1`. The pack it extends is one of the same Repository, or
else of the official one. One of another Repository, or one that must be a certain version or content, is pinned
with a `parent` block after the pack line:

```
pack aurora format 1 version 0.2.1 extends retro

parent retro {
  repository "github:someone/skins"
  fingerprint "<64 hex digits>"
  version 0.3
  sha256 "<64 hex digits>"
}
```

`fingerprint` is the SHA-256 of that Repository's key, which `sign-catalog.sh` prints and the program shows when the
Repository is added; `repository` is the address it is added by when it is not yet. `version 0.3` takes any 0.3.x,
and `0.3.2` only that. `sha256` is the digest of the parent's own files, which `crpp_catalog --digest <catalog.txt>
<id>` prints. Each is optional. When a parent no longer holds to them, as a Repository serves only its latest files,
the program asks before it goes on, and keeps the old one meanwhile. `crpp_skin check` looks for the parent in a
folder beside this one, whichever Repository it belongs to.

## Trying a pack

Check it, which says the first mistake as `file:line`, then render it. Both stay on this machine. Build first with
`.github/scripts/msvc-build.sh build-msvc` in Git Bash.

```bash
build-msvc/src/ui/crpp_skin.exe check skins/retro
tests/shots/render.sh --exe build-msvc/apps/gui/crpp.exe --out out --size auto -- --theme classic --skin-dir skins/retro
```

`check` runs the loader the program uses, once for each overlay. A pack that a Theme does not name a Palette for is not
drawn under that Theme.
