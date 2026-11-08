# Draft: the pack language

A draft for the user to judge, not a decision yet. Delete it once the language is settled; what lasts goes to
`skins/README.md` and ADR 0007. Agreed so far (`skin-packs.md`, "Pack language"): a small language of our own in
`.cskin` files, nodes with positional arguments, `key value` and children in braces, expressions as syntax, units,
a sigil on every name the exe defines, bare names for a pack's own, and the same tree and engine behind it.

## Home of Cloud, side by side

Today, `skins/cloud/screens/home.jsonc` (3,925 bytes):

```jsonc
{
  "$schema": "../../schema/screen.home.schema.json",
  "draw": [
    // Sky: the large cloud at lower right, then the create cloud at upper left, whose shape takes the click.
    {"use": "cloud", "x": 306.33, "y": 295.56, "args": {"scale": 1.3906, "shadow": 0.09, "sigma": 6}},
    {"hit": "create", "hit_path": "M388.527 317.44H101.915C54.679 …Z", "hit_path_x": -66.0667, "hit_path_y": 100.9, "group": [
      {"let": "grow", "to": "hot", "ms": 160},
      {"scale": "1+0.03*grow", "about_x": 182, "about_y": 260, "group": [
        {"use": "cloud", "x": -66.0667, "y": 100.9, "args": {"shadow": 0.06, "sigma": 3.742}},
        {"text": "#home_create", "style": "center", "size": 44, "line_height": 110, "color": "$hl_text", "y": 318, "w": 458, "h": 110}
      ]}
    ]},

    {"text": "@greeting", "style": "center", "size": 44, "line_height": 44, "color": "dark ? $hl_text_dark : $white", "x": 440, "y": 108, "w": 420, "h": 44},
    {"text": "@hello", "style": "center", "size": 44, "line_height": 44, "color": "dark ? $hl_text_dark : $white", "x": 440, "y": 165, "w": 420, "h": 44},

    {"text": "#home_find", "style": "plain", "size": 40, "line_height": 60, "color": "$hl_text", "x": 470, "y": 452, "w": 400, "h": 60},

    // The code, which grows a little while it has the focus.
    {"let": "focus", "to": "@code_focus", "ms": 150},
    {"let": "faint", "value": "dark ? $hl_text_soft : alpha($white, 0.85)"},
    {"x": 469, "y": 522, "w": 279, "h": 80, "scale": "1+0.012*focus", "group": [
      {"fill": "$entry", "radius": 18},
      {"field": "code", "style": "code", "color": "dark ? $hl_text : $on_accent", "placeholder": "#home_code_placeholder", "placeholder_style": "code_placeholder", "placeholder_color": "faint", "caret": "dark ? $hl_text : $on_accent", "selection": "alpha($hl, 0.4)", "x": 18, "y": 10, "w": 205, "h": 60},
      {"hit": "code", "group": []},
      {"hit": "join", "x": 221, "y": 10, "w": 52, "h": 60, "group": [
        {"let": "over", "to": "hot", "ms": 150},
        {"if": "over>0", "fill": "alpha(dark ? $hl_soft : mix($entry, $white, 40), over)", "radius": 13},
        {"path": "M10 22h23M23 11l11 11-11 11", "color": "mix(dark ? $hl_text_dark : $hl_text, dark ? $hl_text_soft : faint, over*100)", "x": 10, "y": 14, "scale": "32/44", "width": 3}
      ]}
    ]},

    // Public room search. A held Seat lights it.
    {"hit": "find", "x": 759, "y": 522, "w": 92, "h": 80, "group": [
      {"let": "over", "to": "hot", "ms": 150},
      {"let": "grow", "to": "hot", "ms": 140},
      {"let": "light", "to": "@returning", "ms": 150},
      {"let": "shown", "to": "@find_tip", "ms": 150},
      {"scale": "1+0.05*grow", "group": [
        {"fill": "mix($hl_dark, mix($hl, $entry, light*100), over*100)", "radius": 18},
        {"let": "rest", "value": "dark ? mix($on_accent, $hl_text_soft, light*100) : faint"},
        {"let": "icon", "value": "mix($on_accent, rest, over*100)"},
        {"x": 25, "y": 19, "w": 44, "h": 44, "scale": "42/44", "about_x": 25, "about_y": 19, "group": [
          {"stroke": "icon", "ellipse": true, "x": 8, "y": 8, "w": 22, "h": 22, "width": 3.2},
          {"line": "icon", "x": 27.5, "y": 27.5, "x2": 37, "y2": 37, "width": 3.2}
        ]},
        // The label is the button's child, so it grows with it.
        {"if": "shown>0", "group": [
          {"let": "tip_w", "value": "textw('tip', '#home_find_public', 600)+30"},
          {"use": "tip", "text": "#home_find_public", "args": {"shown": "shown"}, "x": "(W-tip_w)/2", "y": "-40+4*(1-shown)", "w": "tip_w", "h": 32}
        ]}
      ]}
    ]}
  ]
}
```

Proposed, `skins/cloud/screens/home.cskin` (2,397 bytes, 39% less; the cloud's path moved to the pack as a `shape`, without which it would be about 33%):

```
screen home

// Sky: the large cloud at lower right, then the create cloud at upper left, whose shape takes the click.
use cloud at (306.33, 295.56) scale 1.3906 shadow 0.09 sigma 6
hit create shape cloud_outline at (-66.0667, 100.9) {
  let grow = ease(@hot, 160ms)
  group scale 1 + 0.03*grow about (182, 260) {
    use cloud at (-66.0667, 100.9) shadow 0.06 sigma 3.742
    text @tr.home_create (0, 318, 458, 110) style center size 44 line 110 color hl_text
  }
}

let greeting_color = @dark ? hl_text_dark : white
text @greeting (440, 108, 420, 44) style center size 44 line 44 color greeting_color
text @hello    (440, 165, 420, 44) style center size 44 line 44 color greeting_color

text @tr.home_find (470, 452, 400, 60) style plain size 40 line 60 color hl_text

// The code, which grows a little while it has the focus.
let focus = ease(@code_focus, 150ms)
let faint = @dark ? hl_text_soft : alpha(white, 85%)
group (469, 522, 279, 80) scale 1 + 0.012*focus {
  fill entry radius 18
  field code (18, 10, 205, 60) style code color @dark ? hl_text : on_accent
    placeholder @tr.home_code_placeholder placeholder_style code_placeholder placeholder_color faint
    caret @dark ? hl_text : on_accent selection alpha(hl, 40%)
  hit code
  hit join (221, 10, 52, 60) {
    let over = ease(@hot, 150ms)
    fill alpha(@dark ? hl_soft : mix(entry, white, 40%), over) radius 13 if over > 0
    path "M10 22h23M23 11l11 11-11 11" at (10, 14) scale 32/44 width 3
      color mix(@dark ? hl_text_dark : hl_text, @dark ? hl_text_soft : faint, over)
  }
}

// Public room search. A held Seat lights it.
hit find (759, 522, 92, 80) {
  let over = ease(@hot, 150ms)
  let grow = ease(@hot, 140ms)
  let light = ease(@returning, 150ms)
  let shown = ease(@find_tip, 150ms)
  group scale 1 + 0.05*grow {
    fill mix(hl_dark, mix(hl, entry, light), over) radius 18
    let rest = @dark ? mix(on_accent, hl_text_soft, light) : faint
    let icon = mix(on_accent, rest, over)
    group (25, 19, 44, 44) scale 42/44 about (25, 19) {
      ellipse (8, 8, 22, 22) stroke icon width 3.2
      line (27.5, 27.5) to (37, 37) color icon width 3.2
    }
    // The label is the button's child, so it grows with it.
    group if shown > 0 {
      let tip_w = textw(tip, @tr.home_find_public, 600) + 30
      use tip ((@w - tip_w)/2, -40 + 4*(1 - shown), tip_w, 32) text @tr.home_find_public shown shown
    }
  }
}
```

And in `pack.cskin`, the parts and a Palette it uses:

```
pack cloud format 1 version 0.1.0
name ko "구름" en "Cloud"
theme cumulus name ko "뭉게구름" en "Cumulus" dark darkcloud
theme pastel name ko "파스텔" en "Pastel"
default_theme cumulus

shape cloud_outline "M388.527 317.44H101.915C54.679 317.44 16.386 279.073 16.386 231.744c0-42.356 …Z"

// Upstream's cloud, set at this box's corner, with a soft shadow in the light Themes.
part cloud(scale = 1, shadow = 0, sigma = 0) {
  path cloud_outline scale scale color alpha(cloud_shadow, shadow) blur sigma*3 dy 6/scale if !@dark
  path cloud_outline scale scale color cloud_base
  path cloud_outline scale scale color gradient(cloud_gradient, 8, 318)
}

// .tip: its box is the label's own, and it rises 4 px as it fades in.
part tip(text, shown = 1) {
  fill alpha(hl, shown) radius 11
  text text (15, 5, @w - 29, 22) style tip color alpha(on_accent, shown)
}

palette cumulus {
  white #ffffff
  blue #3a9af0
  hl #66ccff
  hl_soft mix(hl, white, 70%)
  hl_dark mix(hl, blue, 88%)
  cloud_gradient stops(0: #ffffff, 1: #eff8ff)
}
```

## The rules in the draft

- A line is a node: its kind, then positional arguments, then `key value` pairs. Children go in `{ }`. A line
  indented deeper than the node it follows, without a `{` between, continues that node.
- A box is `(x, y, w, h)`; a point is `(x, y)`, after `at` or in a `line … to …`. Omitted, a box fills its parent.
- `let name = expr` binds a value for what follows in the block; `ease(target, 150ms)` moves it towards its target
  over that time, with an optional curve as a third argument.
- `if expr` is a property like any other: on a node it decides whether the node and its children draw.
- Units: `150ms`, `40%` (0.4) and `px` where a reader may doubt. `mix` takes a fraction, so `over` needs no `*100`.
- A part is defined with `part name(arg = default, …)` and used with `use name`, its arguments as properties, so a
  later engine node never collides with a pack's part.
- `shape name "…"` names an SVG path once; `path`, `hit … shape` and others take the name.
- Strings only hold SVG paths and text in a Theme's own words (`name ko "…"`). Everything else is a name or a number.
- `//` comments, to the end of the line.

## Decided on the draft

Agreed with the user on 2026-11-08:

- **Colours and wording.** `#` is a colour (`#66ccff`), as everyone writes one. Wording is `@tr.home_create`, so
  every name the exe defines carries `@`, wording under `tr` as `tr()` in the code. This replaces `#` for wording.
- **Built-ins.** `@hot`, `@dark` and `@w`/`@h`, the box's size, carry `@` as the exe defines them.
- **Line continuation.** A line indented deeper than the node it follows, with no `{` between, continues that node.
