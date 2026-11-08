# Glossary

**Cloudroom++**: this client's name, shown wherever the program names itself.

**Cloudroom**: the service and the upstream client. This client never calls itself that.

**Development build** (개발 빌드): any build that is not a release. It runs beside anything already running and keeps its data apart from the user's. An official build is a release.

**Room** (구름): a shared session of up to 16 people, identified by a 6-character code. Everyone in a Room hears each other's input as sound.

**Seat** (slot): a member's place in a Room. A Seat is held for a while after its owner disconnects or leaves, so the owner can return to it.

**Input kind**: the category of a keyboard, mouse or pen action (for example a normal key, space, or a left click). Only the kind is shared with other members, never which key was pressed.

**Presence mode**: the default, where every key press is the same Input kind, so others learn only that a key was pressed.

**Detail mode**: an opt-in setting where letter keys say their row and special keys (Space, Enter, modifiers and the like) keep their own Input kinds, trading privacy for sound variety.

**Sound pack**: a named set of recordings for one device type (keyboard, mouse or pen). Each member picks their own pack per device.

**Pen** (타블렛): a stylus used on a drawing tablet or pen display. "Pen" is the canonical term; "타블렛" is only the label users see.

**Desk**: where a member's sound comes from in the virtual office one listener hears. Each listener arranges the members at Desks for themselves, so a member's Desk differs between listeners. Not a Seat, which is the same for everyone.

**Virtual surround** (가상 서라운드): placing each member's sound around the listener by their Desk. Without it every member sounds centred.

**Left-right difference** (좌우 차이): how strongly a Desk to one side leans toward the nearer ear, under Virtual surround. Upstream calls it channel balance; it is shown in words because it moves level, delay and muffling together.

**Reverb** (울림): the ringing of a space after a sound. Chosen on its own, apart from Virtual surround, and it can be turned off completely. "Echo" is not used for this.

**Guest**: a member without an account. Identified only by a signed guest ID kept on their device.

**Account**: an optional login (username and password) that adds a unique Handle.

**Nickname** (별명): the name other members see. Every member has one, Guest or not, and can change it.

**Handle** (사용자명): a unique `@name` that only an Account has.

**Host**: the member who controls the Room's visibility and chat. When the Host leaves, another online member becomes Host.

**Dark mode** (다크 모드): off, on or following the system, picked after the Theme. It chooses between a Theme and its dark variant, and is locked while the Theme has none.

**Theme** (테마): what a user picks within a Skin in the display settings, for example classic or luna_blue in Retro. It has a Palette, and may have a dark variant, which is another Palette of the same Theme.

**Skin**: how the program is built on screen: the shape of its controls, the layout of its screens, its window frame, its cursors and its motion. Features and behaviour do not depend on the Skin. There are two: Cloud and Retro. The program knows no Skin by name.

**Skin Pack** (스킨 팩): what is downloaded to get a Skin: all of the above with its Themes, fonts, pictures and notification sounds. It holds no code, and may extend one other Skin Pack, its parent, changing only what differs. Sound packs are not part of one, because another member's pack must be heard whichever Skin is in use.

**Repository** (저장소): a place that serves a signed catalog and the files it lists: Skin Packs, Sound packs and the other files the program downloads. The official Repository always comes first; a user may add others by address. Not a git repository, though a GitHub repository can serve as one.

**Palette**: the colours of one Theme, or of its dark variant. A dark variant has the same shapes as its Theme and differs only in its Palette.

**Cloud**: the Skin of the upstream client, with clouds as its panels. 구름, 먹구름 and 파스텔 are its Themes.

**Retro**: the Skin of old Windows desktops. Its Themes are classic (the default), modelled on Windows 95 and 98, and the Luna Themes.

**Luna**: the look of Windows XP written over classic, as the real one was. luna_blue, luna_homestead and luna_metallic are its Themes within Retro.

**Locale** (언어): the language the program speaks: its wording, and how it writes times and counts. It never touches what members write, nor Nicknames.

**Protected store**: this client's own data, with secrets encrypted for the current OS user.

**Shared store**: the upstream client's data files, used in place when they already exist and no Protected store does. Secrets there are not protected.
