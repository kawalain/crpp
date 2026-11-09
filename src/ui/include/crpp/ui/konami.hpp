#pragma once
// The Konami code, ↑↑↓↓←→←→BA, as virtual-key codes. Pure: it only watches the keys go by.
#include <array>
#include <cstddef>

namespace crpp::ui {

class KonamiCode {
 public:
  // Windows' virtual-key codes: the arrows, then the letters as their capitals.
  static constexpr unsigned up = 0x26, down = 0x28, left = 0x25, right = 0x27;
  static constexpr std::array<unsigned, 10> sequence{up, up, down, down, left, right, left, right, 'B', 'A'};

  // True on the key that completes it, which starts it over.
  bool key(unsigned vk) {
    if (vk == sequence[at_]) {
      if (++at_ < sequence.size()) return false;
      at_ = 0;
      return true;
    }
    // A wrong key may still begin it: ↑↑↑ is ↑↑ with one too many.
    at_ = vk != up ? 0 : at_ == 2 ? 2 : 1;
    return false;
  }

 private:
  std::size_t at_ = 0;
};

}  // namespace crpp::ui
