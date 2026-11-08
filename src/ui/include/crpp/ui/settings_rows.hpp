#pragma once
// What the settings panel and the lists in it share.
#include "crpp/ui/controls.hpp"

namespace crpp::ui {

inline constexpr Rect settings_view{296, 137, 566, 504};  // .settings-content

// Which way the list of a dropdown just opened goes, as main.js decides it. `button`
// is in stage coordinates.
void place_dropdown(const Ui& ui, Dropdown& dropdown, Rect button);

}  // namespace crpp::ui
