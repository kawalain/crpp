# Presence mode by default

Combined with timing, the protocol's keyboard kinds (the row of a letter key, Space, Enter, Backspace, modifiers) let others guess what someone is typing, so by default every key is sent as kind 49, which says nothing about the key; the listener's client picks a random letter sound for it (**Presence mode**). This deliberately gives up per-key sound variety, unlike upstream. **Detail mode** is an opt-in setting whose description states what it reveals: special keys keep their own kinds, which restores variety but exposes word boundaries and line breaks. Letters and digits stay kind 49 in both modes, so the row of a key is never sent.

The timing of input stays visible in both modes, because sharing it is the point of a Room. Windows hands every key code to the app and that cannot be avoided, so the app classifies it at once and discards it; the only key state kept is which keys are held right now, to ignore auto-repeat, and it never leaves the `input` module.

## Considered options

- Kind 16 (`KeyNormal`): no Sound pack has a recording for it, so listeners would hear nothing.
