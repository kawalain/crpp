#pragma once
// Colours, and the little the program reads of the Theme in use. A Theme's colours are its pack's.
#include <cstdint>

#include "crpp/skin/color.hpp"

namespace crpp::ui {

using skin::Color;
using skin::GradientStop;

constexpr Color rgb(std::uint32_t hex, float a = 1.0f) {
  return {static_cast<float>((hex >> 16) & 0xFF) / 255.0f, static_cast<float>((hex >> 8) & 0xFF) / 255.0f,
          static_cast<float>(hex & 0xFF) / 255.0f, a};
}

using skin::mix;
using skin::with_alpha;

// What the program itself reads of the Theme in use; everything else of it is the pack's.
struct Theme {
  bool dark = false;
  Color sky = rgb(0x66ccff);  // behind everything, and the window's background
};

}  // namespace crpp::ui
