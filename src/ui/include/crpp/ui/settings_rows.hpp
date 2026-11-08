#pragma once
// What the settings panel and the surfaces with a small label or a list share.
#include <string_view>

#include "crpp/ui/controls.hpp"

namespace crpp::ui {

inline constexpr Rect settings_view{296, 137, 566, 504};  // .settings-content

[[nodiscard]] float text_width(const Painter& p, std::string_view text, const TextStyle& style);
// A .tip-like bubble filling `box`; alpha is how far it has faded in.
void paint_bubble(Painter& p, const Theme& t, std::string_view text, Rect box, const TextStyle& style, float radius, Color fill, Color ink,
                  float alpha);

// Which way the list of a dropdown just opened goes, as main.js decides it. `button`
// is in stage coordinates.
void place_dropdown(const Ui& ui, Dropdown& dropdown, Rect button);

}  // namespace crpp::ui
