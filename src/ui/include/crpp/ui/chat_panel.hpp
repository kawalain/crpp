#pragma once
// A Room's chat: the message log and the compose box.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "crpp/ui/chat_logic.hpp"
#include "crpp/ui/surface.hpp"
#include "crpp/ui/tip.hpp"

namespace crpp::ui {

struct ChatEntry {
  std::uint32_t slot = 0;
  std::string cid, nick, emoji, text;
  long long time_ms = 0;  // unix epoch, milliseconds
};

class ChatPanel final : public Surface {
 public:
  explicit ChatPanel(Ui& ui);

  // False if it could not be sent; the text stays in the box.
  std::function<bool(std::string text)> on_send;
  // Opens a link in the browser. False if that failed.
  std::function<bool(const std::string& url)> on_open_link;

  // Fixed values plus LinkBase + log index * links_per_entry + link index.
  enum Id : int { Input, Send, Area, LinkBase = 1000 };
  static constexpr int links_per_entry = 64;

  void add(ChatEntry entry);
  void clear();
  // The server refused a message and allows the next one after `ms`.
  void blocked(std::uint64_t ms);

  [[nodiscard]] std::size_t log_size() const { return log_.size(); }
  [[nodiscard]] std::size_t shown() const { return rows_.size(); }
  [[nodiscard]] const ChatLimiter& limiter() const { return limiter_; }
  [[nodiscard]] TextField& input() { return input_; }
  [[nodiscard]] float scroll_offset() const { return scroll_; }
  [[nodiscard]] const HoverTip& link_tip() const { return link_tip_; }

  void paint(Painter& p, Rect panel);  // window coordinates
  void click(int id, float x, float y, bool shift) override;
  bool wheel(float x, float y, float delta) override;
  void enter(bool shift) override;
  bool key(unsigned vk, bool ctrl, bool shift) override;
  [[nodiscard]] Cursor cursor(int id) const override;
  [[nodiscard]] bool animating() const override;
  bool tick(unsigned long long now_ms) override;
  [[nodiscard]] unsigned long long wake_at() const override { return limiter_.wake_at(); }

 private:
  // Where a shown message goes. Measured once; only those in view are laid out again
  // to draw, so a long history costs nothing per frame.
  struct Row {
    float y_head = 0, y_text = 0, h_text = 0;
    bool head = false;
    std::vector<LinkSpan> links;
  };

  void send();
  void sync_rows(float list_w);
  void append_row(std::size_t index, float list_w);
  void paint_send(Painter& p, Rect anchor);
  void paint_calm(Painter& p, Rect anchor);
  void shake();
  [[nodiscard]] bool shaking() const;
  [[nodiscard]] bool calm_moving() const;

  TextField input_;
  std::vector<ChatEntry> log_;
  std::vector<Row> rows_;  // log_[first_ + i]
  std::size_t first_ = 0;
  bool fresh_ = true;   // nothing shown yet: the first paint shows the last page
  bool dirty_ = true;   // rows_ must be measured again
  float rows_w_ = 0;    // the width rows_ was measured at
  float next_y_ = 24;   // where the next row starts
  float scroll_ = 0;
  bool stick_ = true;
  float content_h_ = 0, area_h_ = 0;
  Rect area_;
  bool older_pending_ = false;  // a page was added above since the last paint

  ChatLimiter limiter_;
  std::string last_sent_;  // what comes back if the server refuses it
  unsigned long long shake_at_ = 0;
  bool calm_shown_ = false;  // the state the bubble is moving towards
  unsigned long long calm_since_ = 0;
  std::string calm_text_;
  HoverTip link_tip_;
};

}  // namespace crpp::ui
