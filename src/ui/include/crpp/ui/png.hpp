#pragma once
// Renders the UI offscreen and saves it as a PNG (used to check layouts).
#include <string>

#include "crpp/ui/app.hpp"

namespace crpp::ui {

bool save_png(App& app, const std::wstring& path, unsigned width, unsigned height);

}  // namespace crpp::ui
