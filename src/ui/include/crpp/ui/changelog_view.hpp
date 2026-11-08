#pragma once
// The patch notes (.modal-card.changelog): one version at a time, scrolled by the wheel.
// Scrolling on past the end first shows a hint, then moves to the next version.
#include <functional>
#include <string>
#include <vector>

#include "crpp/ui/read_selection.hpp"
#include "crpp/ui/controls.hpp"
#include "crpp/ui/surface.hpp"
#include "crpp/update/notes.hpp"

namespace crpp::ui {

struct ChangelogEntry {
  std::string tagline, date, version;
  std::vector<update::NoteBlock> blocks;
};

// Counts the wheel at the end of a version: changelog.js's overscrollStep. Pure, so its
// timing can be tested. direction is -1 towards newer, +1 towards older.
class ChangelogOverscroll {
 public:
  static constexpr int hint_at = 5, switch_after = 5;
  static constexpr unsigned long long hint_ms = 200, wheel_gap_ms = 140;

  struct Result {
    int hint = 0;  // the direction to show the hint for, or 0
    int go = 0;    // the direction to move, or 0
  };
  // notches: the size of this wheel event; below 0.4 it counts as a trackpad, whose
  // stream of small events is one input per gap.
  Result step(int direction, unsigned long long now, bool can_go, bool at_edge, float notches);
  void reset() { *this = {}; }
  [[nodiscard]] bool counting() const { return count_ != 0 || after_ != 0 || shown_; }

 private:
  int direction_ = 0, count_ = 0, after_ = 0;
  bool shown_ = false;
  unsigned long long shown_at_ = 0, last_ = 0;
};

class ChangelogView final : public Surface {
 public:
  using Surface::Surface;
  enum Id : int { Backdrop, Card, Close, BarTrack, BarThumb, BarUp, BarDown, Text };
  [[nodiscard]] std::optional<Role> role(int id) const override {
    return id == Close ? std::optional(Role::Button) : id == Text ? std::optional(Role::List) : std::nullopt;
  }
  [[nodiscard]] bool holds_keys(int id) const override { return id == Backdrop; }
  // The notes scroll by the keys of a page, while the keyboard is on them.
  bool stop_key(int id, unsigned vk, bool ctrl, bool shift, bool alt) override;

  [[nodiscard]] static skin::ScreenSchema schema();

  // Entries are newest first. An empty list says there is nothing to show.
  void open(std::vector<ChangelogEntry> entries, std::size_t index);
  // New data while open; stays on the same version if it is still there.
  void set_entries(std::vector<ChangelogEntry> entries);
  void close();
  [[nodiscard]] bool is_open() const { return open_; }
  [[nodiscard]] std::size_t index() const { return index_; }
  [[nodiscard]] std::size_t count() const { return entries_.size(); }
  [[nodiscard]] int hint() const { return hint_; }

  void paint(Painter& p);
  void click(int id, float x, float y, bool shift) override;
  [[nodiscard]] bool acts_on_press(int id) const override { return repeats(id) || id == BarThumb || id == Text; }
  [[nodiscard]] bool repeats(int id) const override { return id == BarTrack || id == BarUp || id == BarDown; }
  [[nodiscard]] float scroll_offset() const { return scroll_.offset; }
  void drag(float x, float y) override;
  void release() override { selecting_ = false; }
  void drop_selection() override { selection_.clear(); }
  bool key(unsigned vk, bool ctrl, bool shift) override;
  [[nodiscard]] std::vector<MenuItem> context_menu(int id, float x, float y) override;
  void menu_command(MenuCommand command) override;
  // What is selected of the notes, as a copy takes it.
  [[nodiscard]] std::wstring selected_text() const;
  bool wheel(float x, float y, float delta) override;
  bool escape() override;
  [[nodiscard]] Cursor cursor(int id) const override;
  [[nodiscard]] bool animating() const override;
  bool tick(unsigned long long now_ms) override;
  [[nodiscard]] unsigned long long wake_at() const override;

 private:
  struct Line {
    ComPtr<IDWriteTextLayout> layout;
    float x = 0, y = 0;  // relative to the top of the body
    float bullet_x = -1;
    std::wstring marker;
    float marker_x = 0;
    std::wstring text;  // what the layout holds, for a copy
  };
  // How the notes' lines are laid out and written.
  struct BodyLook {
    float pad_x, indent, margin, heading_top, heading_bottom, bottom, top;
    TextStyle heading, item;
  };
  void show(std::size_t index, bool at_bottom);
  void lay_out_body(const BodyLook& look, float width);
  void scroll_body();
  void paint_body(Painter& p, Rect body, const TextStyle& item, float marker_h, Color ink, Color selection);
  [[nodiscard]] TextPoint point_at(float x, float y) const;
  [[nodiscard]] std::wstring_view text_of(std::size_t line) const;
  // Under the button past the top or bottom of the notes, they scroll that way.
  void scroll_while_selecting();
  void paint_pack(Painter& p, const skin::Screen& screen);
  void show_hint(int direction);

  bool open_ = false;
  std::vector<ChangelogEntry> entries_;
  std::size_t index_ = 0;
  bool was_animating_ = false;

  std::vector<Line> lines_;
  bool built_ = false;
  float built_width_ = 0, body_h_ = 0, first_line_ = 0;
  Scroll scroll_;
  bool start_at_bottom_ = false;
  Rect bar_track_;
  float grab_y_ = 0, grab_offset_ = 0;
  // Lines of the notes are blocks of text to select; body_ is where they were drawn last.
  ReadSelection selection_;
  Rect body_;
  bool selecting_ = false;
  float drag_x_ = 0, drag_y_ = 0;
  unsigned long long scrolled_at_ = 0;

  ChangelogOverscroll overscroll_;
  int hint_ = 0, hint_shown_ = 0;  // the wanted direction; the one being drawn or fading
  unsigned long long idle_at_ = 0, ignore_until_ = 0;
  unsigned long long synthetic_steps_ = 0;  // with animations off, each wheel event is time passing
};

}  // namespace crpp::ui
