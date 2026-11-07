# A Skin owns shape and layout, not only colours

A Theme used to be colours only, with radii, shadows and layout written at each call site. Close imitation of Windows XP's Luna needs what data cannot describe: buttons of several rings, tabs, scrollbars with arrows, an own-drawn window frame, and controls of a fixed size that reflow with the window, where the Cloud Skin scales a fixed 951x692 stage. So drawing and layout go behind a Skin, with Cloud and Luna as its two implementations, and a Theme is one Skin with one Palette. Screens keep one set of state, hit ids and callbacks whatever the Skin.

## Considered options

- More fields in `Theme` (radii, gradients, flags): cheap, but the screens would still branch for every Luna control, and the Luna layouts would not fit.
- A second copy of each screen for Luna: the simplest drawing, but every future behaviour would have to be written twice, and only the Skin's look may differ.

## Consequences

- Only Home, Room with its chat, the first-run prompt, settings and the window frame have a layout per Skin. Other surfaces use the Skin's controls in one shared layout.
- Screens that draw their own buttons and panels now go through the Skin.
- Under a Skin with its own frame, panels are real windows owned by the main one, on one set of surfaces and one `Ui`, whose state per window is swapped as each window is drawn or handles input. Cloud keeps them as layers of its only window.
- A Skin decides its window frame and motion. Luna has no screen transitions, stretch or edge fades, which is why they are not global. It also has notification sounds of its own.
- A new Palette is one entry. A new Skin is large, on purpose.
