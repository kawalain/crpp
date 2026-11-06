#pragma once
// Picks a profile emoji, from the account tab and from one's own profile card.
#include <functional>
#include <string>

#include "crpp/ui/surface.hpp"

namespace crpp::ui {

class EmojiPicker final : public Surface {
 public:
  using Surface::Surface;

  void show(std::string current, std::function<void(std::string id)> on_pick);
  void close();
  [[nodiscard]] bool is_open() const { return open_; }

  void paint(Painter& p);
  bool escape() override;

 private:
  bool open_ = false;
  std::string current_;
  std::function<void(std::string id)> on_pick_;
};

}  // namespace crpp::ui
