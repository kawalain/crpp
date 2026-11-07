#pragma once
// A cursor of a Skin Pack as pixels, and those as a system cursor.
#include <windows.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "crpp/skin/pack.hpp"
#include "crpp/ui/surface.hpp"

namespace crpp::ui {

struct CursorImage {
  int size = 0;
  int hot_x = 0, hot_y = 0;
  // Top down, B G R A from the low byte, premultiplied.
  std::vector<std::uint32_t> pixels;
};

// Nothing when the pack in use has no cursors, or Direct2D refuses.
[[nodiscard]] std::optional<CursorImage> make_cursor_image(const Ui& ui, skin::CursorKind kind, int size);

// For SetCursor; the caller destroys it. Null when the system refuses.
[[nodiscard]] HCURSOR make_system_cursor(const CursorImage& image);

}  // namespace crpp::ui
