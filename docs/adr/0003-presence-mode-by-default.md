# Presence mode by default

Combined with timing, the protocol's keyboard kinds (the row of a letter key, Space, Enter, Backspace, modifiers) let others guess what someone is typing, so by default every key is sent as kind 49, which says nothing about the key; the listener's client picks a random letter sound for it (**Presence mode**). This deliberately gives up per-key sound variety, unlike upstream. **Detail mode** is an opt-in setting whose description states what it reveals: letter keys say their row and special keys keep their own kinds, as upstream does, which restores all of the variety but exposes word boundaries, line breaks and the row of every letter. Someone who never turns it on gives up none of this. An earlier version of this decision kept letters at kind 49 in both modes; upstream's developer asked for the rows back, and an opt-in that says what it reveals is enough to protect those who do not choose it.

The timing of input stays visible in both modes, because sharing it is the point of a Room. Windows hands every key code to the app and that cannot be avoided, so the app classifies it at once and discards it; the only key state kept is which keys are held right now, to ignore auto-repeat, and it never leaves the `input` module.

## Considered options

- Kind 16 (`KeyNormal`): no Sound pack has a recording for it, so listeners would hear nothing.
