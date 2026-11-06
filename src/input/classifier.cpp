#include "crpp/input/classifier.hpp"

namespace crpp::input {

namespace {

using wire::Kind;

struct Special {
  Kind press;
  std::optional<Kind> release;
};

// Scan code set 1. Only the keys with their own sound; both Shift, Ctrl and Alt
// keys and both Enter keys share one, and E0 only tells the pairs apart.
std::optional<Special> special_key(std::uint16_t make) noexcept {
  switch (make) {
    case 0x01: return Special{Kind::KeyEscape, {}};
    case 0x0E: return Special{Kind::KeyBackspace, {}};
    case 0x0F: return Special{Kind::KeyTab, {}};
    case 0x1C: return Special{Kind::KeyEnter, {}};
    case 0x1D: return Special{Kind::KeyCtrlHold, Kind::KeyCtrlUp};
    case 0x2A:
    case 0x36: return Special{Kind::KeyShiftHold, Kind::KeyShiftUp};
    case 0x38: return Special{Kind::KeyAltHold, Kind::KeyAltUp};
    case 0x39: return Special{Kind::KeySpace, {}};
    case 0x3A: return Special{Kind::KeyCaps, {}};
    default: return std::nullopt;
  }
}

}  // namespace

std::optional<wire::Kind> KeyClassifier::on_key(std::uint16_t make, bool extended, bool up) noexcept {
  const std::size_t code = (make & 0xFF) | (extended ? 0x100u : 0u);
  std::uint64_t& word = held_[code / 64];
  const std::uint64_t bit = std::uint64_t{1} << (code % 64);
  const bool was_held = (word & bit) != 0;
  if (up) word &= ~bit;
  else word |= bit;

  const auto special = privacy_ == Privacy::Detail ? special_key(make) : std::nullopt;
  if (up) {
    if (!was_held || !special) return std::nullopt;
    return special->release;
  }
  if (was_held) return std::nullopt;  // auto-repeat
  return special ? special->press : Kind::KeyAny;
}

}  // namespace crpp::input
