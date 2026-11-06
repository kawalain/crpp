# Presence mode by default

Combined with timing, the protocol's keyboard kinds (the row of a letter key, Space, Enter, Backspace, modifiers) let others guess what someone is typing, so by default every key is sent as `KeyNormal` (**Presence mode**). This deliberately gives up per-key sound variety, unlike upstream. **Detail mode** is an opt-in setting whose description states what it reveals: it sends letters and digits on a random row and keeps special keys distinct, which restores variety but exposes word boundaries and line breaks.

The timing of input stays visible in both modes, because sharing it is the point of a Room.
