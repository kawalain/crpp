#pragma once
// Turns physical input into Input kinds. This is the only place a key code is
// looked at; see docs/adr/0003-presence-mode-by-default.md. Pure and allocation
// free, so it can run on the input path.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "crpp/wire/input_frame.hpp"

namespace crpp::input {

// Presence reveals only that a key was pressed; Detail also names special keys.
enum class Privacy : std::uint8_t { Presence, Detail };

class KeyClassifier {
 public:
  void set_privacy(Privacy privacy) noexcept { privacy_ = privacy; }

  // A key went down or up. `make` is its scan code and `extended` its E0 prefix.
  // Empty when nothing should sound: auto-repeat, or a release without a sound.
  [[nodiscard]] std::optional<wire::Kind> on_key(std::uint16_t make, bool extended, bool up) noexcept;

  // Forgets held keys, whose releases may have been missed.
  void reset() noexcept { held_ = {}; }

 private:
  static constexpr std::size_t codes = 512;
  std::array<std::uint64_t, codes / 64> held_{};
  Privacy privacy_ = Privacy::Presence;
};

enum class MouseAction : std::uint8_t {
  LeftDown, LeftUp, MiddleDown, MiddleUp, RightDown, RightUp, BackDown, ForwardDown, Wheel,
};

[[nodiscard]] constexpr wire::Kind mouse_kind(MouseAction action) noexcept {
  using enum wire::Kind;
  switch (action) {
    case MouseAction::LeftDown: return MouseLeftHold;
    case MouseAction::LeftUp: return MouseLeftUp;
    case MouseAction::MiddleDown: return MouseMiddleHold;
    case MouseAction::MiddleUp: return MouseMiddleUp;
    case MouseAction::RightDown: return MouseRightHold;
    case MouseAction::RightUp: return MouseRightUp;
    case MouseAction::BackDown: return MouseSideBack;
    case MouseAction::ForwardDown: return MouseSideForward;
    case MouseAction::Wheel: return MouseWheel;
  }
  return MouseWheel;
}

}  // namespace crpp::input
