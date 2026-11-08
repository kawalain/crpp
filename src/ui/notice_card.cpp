#include "crpp/ui/notice_card.hpp"

#include <algorithm>

#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

enum TextId : int { Text, ActionText, DismissText };
constexpr skin::Name text_names[] = {{"text", Text}, {"action", ActionText}, {"dismiss", DismissText}};
constexpr skin::Name number_names[] = {{"has_action", 0}};
constexpr skin::Name hit_names[] = {{"card", NoticeCard::Card}, {"action", NoticeCard::Action}, {"dismiss", NoticeCard::Dismiss}};

}  // namespace

skin::ScreenSchema NoticeCard::schema() { return {.screen = "notice", .texts = text_names, .numbers = number_names, .hits = hit_names}; }

void NoticeCard::paint(Painter& p) {
  if (!notice_) return;
  p.set_base(D2D1::Matrix3x2F::Identity());
  if (const skin::Screen* screen = ui_.pack_screen("notice")) {
    class Shown final : public skin::Source {
     public:
      explicit Shown(const Notice& notice) : n_(notice) {}
      std::string_view text(int id, int) override {
        switch (id) {
          case Text: return n_.text;
          case ActionText: return n_.action;
          default: return n_.dismiss;
        }
      }
      float number(int, int) override { return n_.action.empty() ? 0.0f : 1.0f; }

     private:
      const Notice& n_;
    } shown(*notice_);
    draw_layer(ui_, p, this, *screen, ui_.content, shown);
  }
}

void NoticeCard::click(int id, float, float, bool) {
  if (id == Action) {
    // Moved out first: the action may set the next notice and its action.
    const auto action = std::move(on_action);
    on_action = nullptr;
    notice_.reset();
    if (action) action();
  } else if (id == Dismiss) {
    notice_.reset();
  }
}

Cursor NoticeCard::cursor(int id) const { return id == Card ? Cursor::Arrow : Cursor::Hand; }

}  // namespace crpp::ui
