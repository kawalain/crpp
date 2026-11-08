#include "crpp/ui/tip.hpp"

#include <algorithm>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

enum NumberId : int { AnchorX, AnchorY, AnchorW, PlaceOf, Gap, Shift, LookOf, On, Again, Shakes };
constexpr skin::Name text_names[] = {{"text", 0}};
constexpr skin::Name number_names[] = {{"anchor_x", AnchorX}, {"anchor_y", AnchorY}, {"anchor_w", AnchorW}, {"place", PlaceOf}, {"gap", Gap}, {"dx", Shift}, {"look", LookOf}, {"on", On}, {"again", Again}, {"shakes", Shakes}};
constexpr skin::Name report_names[] = {{"shown", 0}};

}  // namespace

skin::ScreenSchema HoverTip::schema() { return {.screen = "tip", .texts = text_names, .numbers = number_names, .reports = report_names}; }

void HoverTip::paint(Ui& ui, Painter& p, const Spec* want) {
  seen_owner_ = ui.hover_owner;
  seen_id_ = ui.hover_id;
  if (want) {
    if (!shown_ || want->text != spec_.text) ++fades_in_;
    shown_ = true;
    spec_ = *want;
  } else {
    shown_ = false;
  }
  const skin::Screen* screen = ui.pack_screen("tip");
  if (spec_.text.empty() || !screen) return;
  class Shown final : public skin::Source {
   public:
    Shown(const Spec& spec, bool on, unsigned again) : s_(spec), on_(on), again_(again) {}
    std::string_view text(int, int) override { return s_.text; }
    float number(int id, int) override {
      switch (id) {
        case AnchorX: return s_.anchor.x;
        case AnchorY: return s_.anchor.y;
        case AnchorW: return s_.anchor.w;
        case PlaceOf: return static_cast<float>(s_.place);
        case Gap: return s_.gap;
        case LookOf: return static_cast<float>(s_.look);
        case On: return on_ ? 1.0f : 0.0f;
        case Again: return static_cast<float>(again_);
        case Shakes: return static_cast<float>(s_.shakes);
        default: return s_.dx;
      }
    }
    void report(int, float value) override { left = value; }

    float left = 0;  // a pack that says nothing has no fade to wait for

   private:
    const Spec& s_;
    bool on_;
    unsigned again_;
  } shown(spec_, shown_, fades_in_);
  SkinCanvas canvas(ui, p, nullptr);
  canvas.in_window_coordinates();
  canvas.motion_of(this);
  skin::draw(*ui.pack, *screen, ui.palette, {0, 0, ui.width, ui.height}, canvas, shown);
  if (!shown_ && shown.left <= 0) spec_.text.clear();
}

bool HoverTip::animating(const Ui& ui) const {
  return ui.animations && (ui.hover_owner != seen_owner_ || ui.hover_id != seen_id_);
}

}  // namespace crpp::ui
