#pragma once
// Picks a profile emoji, from the account tab and from one's own profile card.
//
// Only what is on screen is parsed, into a small cache of the picker's own that goes
// away when it closes, so the 172 files never stay in memory together.
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/surface.hpp"

namespace crpp::ui {

class EmojiPicker final : public Surface {
 public:
  using Surface::Surface;
  // Fixed values plus CellBase + index.
  enum Id : int { Backdrop, Card, Track, Thumb, CellBase = 100 };
  [[nodiscard]] std::optional<Role> role(int id) const override { return id >= CellBase ? std::optional(Role::List) : std::nullopt; }
  // The grid is one stop, at the emoji the keyboard is on: the one in use, or the first.
  [[nodiscard]] int stop_for(int id) const override { return id >= CellBase ? CellBase + keyboard_cell() : id; }
  [[nodiscard]] bool holds_keys(int id) const override { return id == Backdrop; }
  // The arrows go from emoji to emoji, Home and End to the ends of a row, and Enter or Space picks one.
  bool stop_key(int id, unsigned vk, bool ctrl, bool shift, bool alt) override;

  // Seven rows of eight can show at once; this keeps a little of the scroll around.
  static constexpr std::size_t cache_capacity = 72;

  void show(std::string current, std::function<void(std::string id)> on_pick);
  void close();
  void forget_graphics();
  [[nodiscard]] bool is_open() const { return open_; }

  [[nodiscard]] const std::vector<std::string>& ids() const { return ids_; }
  // How many emoji are parsed in memory now, for tests.
  [[nodiscard]] std::size_t parsed() const { return cache_ ? cache_->parsed() : 0; }

  // What the picker takes inside a window of its own.
  [[nodiscard]] static skin::ScreenSchema schema();
  [[nodiscard]] D2D1_SIZE_F content_size() const;
  void paint(Painter& p);
  void click(int id, float x, float y, bool shift) override;
  [[nodiscard]] bool acts_on_press(int id) const override { return id == Track || id == Thumb; }
  void drag(float x, float y) override;
  void release() override;
  bool wheel(float x, float y, float delta) override;
  bool escape() override;
  [[nodiscard]] Cursor cursor(int id) const override;
  bool tick(unsigned long long now_ms) override;
  [[nodiscard]] unsigned long long wake_at() const override;

 private:
  void reveal_thumb();
  void paint_pack(Painter& p, const skin::Screen& screen);
  void release_shown();

  bool open_ = false;
  unsigned long long closed_at_ = 0;  // fading out since then
  float shown_left_ = 0;  // what a pack's screen said of its fade
  std::string current_;
  int cell_ = -1;  // the emoji the keyboard is on, or the one in use
  // What the pack said of its grid: how many to a row, and how far down one row is from the next.
  int columns_ = 8;
  float row_h_ = 0;
  [[nodiscard]] int keyboard_cell() const;
  std::function<void(std::string id)> on_pick_;
  std::vector<std::string> ids_;
  std::optional<EmojiCache> cache_;
  Scroll scroll_;
  Rect track_;
  // The thumb shows while scrolling and under the pointer, then fades (.emoji-scrollbar).
  bool thumb_on_ = false;
  unsigned long long thumb_hide_at_ = 0;
};

}  // namespace crpp::ui
