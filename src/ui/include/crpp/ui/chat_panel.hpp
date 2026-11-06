#pragma once
// A Room's chat: the message log and the compose box.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "crpp/ui/surface.hpp"

namespace crpp::ui {

struct ChatEntry {
  std::uint32_t slot = 0;
  std::string cid, nick, emoji, text;
  long long time_ms = 0;  // unix epoch, milliseconds
};

class ChatPanel final : public Surface {
 public:
  explicit ChatPanel(Ui& ui);

  std::function<void(std::string text)> on_send;

  void add(ChatEntry entry);
  void clear();

  void paint(Painter& p, Rect panel);  // window coordinates
  void click(int id, float x, float y, bool shift) override;
  bool wheel(float x, float y, float delta) override;
  void enter(bool shift) override;
  bool key(unsigned vk, bool ctrl, bool shift) override;

 private:
  enum Id : int { Input, Send, Area };
  void send();

  TextField input_;
  std::vector<ChatEntry> log_;
  float scroll_ = 0;
  bool stick_ = true;
  float content_h_ = 0, area_h_ = 0;
  Rect area_;
};

}  // namespace crpp::ui
