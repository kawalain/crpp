#pragma once
// Renders the UI offscreen and saves it as a PNG (used to check layouts).
#include <string>

#include "crpp/ui/app.hpp"

namespace crpp::ui {

// width and height are in DIPs; the image has them scaled by dpi / 96.
bool save_png(App& app, const std::wstring& path, unsigned width, unsigned height, float dpi = 96);

}  // namespace crpp::ui
