#include "crpp/ui/mini_window.hpp"

#include <string>

#include "crpp/locale/locale.hpp"
#include "crpp/skin/draw.hpp"
#include "crpp/ui/room_screen.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

using locale::tr;

enum TextId : int { MuteLabel };
enum NumberId : int { Members, MemberLit, MemberOffline, Muted, Active };
enum ReportId : int { Width, Height, Radius };
constexpr skin::Name texts[] = {{"mute_label", MuteLabel}};
constexpr skin::Name numbers[] = {{"members", Members}, {"member_lit", MemberLit}, {"member_offline", MemberOffline}, {"muted", Muted}, {"active", Active}};
constexpr skin::Name pictures[] = {{"member", 0}};
constexpr skin::Name hits[] = {{"close", MiniWindow::Close}, {"mute", MiniWindow::Mute}};
constexpr skin::Name reports[] = {{"width", Width}, {"height", Height}, {"radius", Radius}};

class Shown final : public skin::Source {
 public:
  Shown(const RoomScreen& room, Ui& ui, Painter* painter, bool active)
      : room_(room), ui_(ui), p_(painter), muted_(ui.prefs->get(pref::mute_others)), active_(active) {}

  std::string_view text(int, int) override { return muted_ ? tr().unmute : tr().mute; }

  float number(int id, int index) override {
    const auto yes = [](bool b) { return b ? 1.0f : 0.0f; };
    switch (id) {
      case Members: return static_cast<float>(room_.members().size());
      case Muted: return yes(muted_);
      case Active: return yes(active_);
      default: break;
    }
    const MemberView* m = member(index);
    if (!m) return 0;
    return id == MemberLit ? yes(room_.lit(*m)) : yes(m->offline);
  }

  void picture(int, int index, skin::Box box, float opacity, float gray) override {
    const MemberView* m = member(index);
    if (p_ && m) draw_svg(*p_, ui_.emoji_image(m->emoji), rect_of(box), opacity, gray);
  }

  void report(int id, float value) override {
    if (id == Width) shape.width = value;
    else if (id == Height) shape.height = value;
    else shape.radius = value;
  }

  MiniWindow::Shape shape;

 private:
  [[nodiscard]] const MemberView* member(int index) const {
    const auto& all = room_.members();
    return index >= 0 && static_cast<std::size_t>(index) < all.size() ? &all[static_cast<std::size_t>(index)] : nullptr;
  }

  const RoomScreen& room_;
  Ui& ui_;
  Painter* p_;
  bool muted_, active_;
};

}  // namespace

skin::ScreenSchema MiniWindow::schema() {
  return {.screen = "mini", .texts = texts, .numbers = numbers, .pictures = pictures, .hits = hits, .reports = reports};
}

MiniWindow::Shape MiniWindow::shape() const {
  const skin::Screen* screen = ui_.pack_screen("mini");
  if (!screen) return {200, 100, 0};
  Shown shown(room_, ui_, nullptr, active_);
  measure_screen(ui_, *screen, {0, 0, 2000, 2000}, shown);
  return shown.shape;
}

void MiniWindow::paint(Painter& p) {
  const skin::Screen* screen = ui_.pack_screen("mini");
  if (!open_ || !screen) return;
  p.set_base(D2D1::Matrix3x2F::Identity());
  Shown shown(room_, ui_, &p, active_);
  draw_layer(ui_, p, this, *screen, ui_.content, shown);
}

void MiniWindow::click(int id, float, float, bool) {
  if (id == Close) close();
  // As the Room's own button, which says so and does the rest.
  else if (id == Mute) room_.click(RoomScreen::Mute, 0, 0, false);
}

Cursor MiniWindow::cursor(int id) const { return id == Close || id == Mute ? Cursor::Hand : Cursor::Arrow; }

}  // namespace crpp::ui
