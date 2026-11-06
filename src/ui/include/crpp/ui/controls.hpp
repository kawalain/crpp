#pragma once
// Controls shared by the screens. They are drawing functions and small value types;
// the surface that uses one keeps its state and registers its hits.
#include "crpp/ui/gfx.hpp"

namespace crpp::ui {

// A toggle (.switch, .toggle-row input). The knob is inset 3 from the track's edges.
void paint_switch(Painter& p, const Theme& t, Rect r, bool on, bool disabled, bool hot);

}  // namespace crpp::ui
