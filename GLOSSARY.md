# Glossary

**Cloudroom++**: this client's name, shown wherever the program names itself.

**Cloudroom**: the service and the upstream client. This client never calls itself that.

**Room** (구름): a shared session of up to 16 people, identified by a 6-character code. Everyone in a Room hears each other's input as sound.

**Seat** (slot): a member's place in a Room. A Seat is held for a while after its owner disconnects or leaves, so the owner can return to it.

**Input kind**: the category of a keyboard, mouse or pen action (for example a normal key, space, or a left click). Only the kind is shared with other members, never which key was pressed.

**Presence mode**: the default, where every key press is the same Input kind, so others learn only that a key was pressed.

**Detail mode**: an opt-in setting where special keys (Space, Enter, modifiers and the like) keep their own Input kinds, trading some privacy for sound variety.

**Sound pack**: a named set of recordings for one device type (keyboard, mouse or pen). Each member picks their own pack per device.

**Pen** (타블렛): a stylus used on a drawing tablet or pen display. "Pen" is the canonical term; "타블렛" is only the label users see.

**Virtual surround** (가상 서라운드): placing each Seat's sound around the listener by its place in the Room. Without it every Seat sounds centred.

**Left-right difference** (좌우 차이): how strongly a Seat to one side leans toward the nearer ear, under Virtual surround. Upstream calls it channel balance; it is shown in words because it moves level, delay and muffling together.

**Guest**: a member without an account. Identified only by a signed guest ID kept on their device.

**Account**: an optional login (username and password) that adds a unique Handle.

**Nickname** (별명): the name other members see. Every member has one, Guest or not, and can change it.

**Handle** (사용자명): a unique `@name` that only an Account has.

**Host**: the member who controls the Room's visibility and chat. When the Host leaves, another online member becomes Host.

**Protected store**: this client's own data, with secrets encrypted for the current OS user.

**Shared store**: the upstream client's data files, used in place when they already exist and no Protected store does. Secrets there are not protected.
