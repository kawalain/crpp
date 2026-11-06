#include "crpp/ui/emoji_picker.hpp"

namespace crpp::ui {

void EmojiPicker::show(std::string current, std::function<void(std::string id)> on_pick) {
  open_ = true;
  current_ = std::move(current);
  on_pick_ = std::move(on_pick);
}

void EmojiPicker::close() {
  open_ = false;
  on_pick_ = nullptr;
}

void EmojiPicker::paint(Painter&) {}

bool EmojiPicker::escape() {
  if (!open_) return false;
  close();
  return true;
}

}  // namespace crpp::ui
