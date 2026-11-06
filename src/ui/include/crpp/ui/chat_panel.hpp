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

// A message's time, as the list shows it: "오후 3:07", or "15:07" on a 24-hour clock,
// with the day in front when it was not today.
[[nodiscard]] std::string chat_clock_text(long long ms, bool use_24h);

class ChatPanel final : public Surface {
 public:
  explicit ChatPanel(Ui& ui);

  // chatScale, percent: the whole list zooms, the compose box does not (.chat-list).
  void set_scale(int percent) { zoom_ = static_cast<float>(percent) / 100; }
  void set_use_24h(bool on) { use_24h_ = on; }

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
  float zoom_ = 1;
  bool use_24h_ = false;
  Rect area_;
};

}  // namespace crpp::ui
