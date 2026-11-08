# A Skin owns shape and layout, not only colours

This says what a Skin owns; ADR 0007 says how one is made, as data in a Skin Pack.

A Theme used to be colours only, with radii, shadows and layout written at each call site. Close imitation of Windows XP's Luna needs more than colours: buttons of several rings, tabs, scrollbars with arrows, an own-drawn window frame, and controls of a fixed size that reflow with the window, where the Cloud Skin scales a fixed 951x692 stage. So drawing and layout belong to a Skin, Cloud or Retro, and a Theme is one Skin with one Palette. Screens keep one set of state, hit ids and callbacks whatever the Skin.

## Considered options

- More fields in `Theme` (radii, gradients, flags): cheap, but the screens would still branch for every Luna control, and the Luna layouts would not fit.
- A second copy of each screen for Luna: the simplest drawing, but every future behaviour would have to be written twice, and only the Skin's look may differ.

## Consequences

- Every screen, control and the window frame are the Skin's to lay out and draw. What happens on a click is not.
- Under a Skin with its own frame, panels are real windows owned by the main one, on one set of surfaces and one `Ui`, whose state per window is swapped as each window is drawn or handles input. Cloud keeps them as layers of its only window.
- A Skin decides its window frame and motion. Retro has no screen transitions, stretch or edge fades, which is why they are not global. It also has notification sounds of its own.
- A new Palette is one entry. A new Skin is a Skin Pack, which may extend another and change only what differs, as Luna does over Retro's classic look.
