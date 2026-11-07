// Home drawn from a Skin Pack. State, hit ids and callbacks stay the screen's own; the pack says
// where everything goes and how it looks.
#include "crpp/skin/draw.hpp"
#include "crpp/ui/home_screen.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

enum TextId : int { Nickname, Greeting, Seat };
enum NumberId : int { Seats };
enum PictureId : int { MyEmoji };
enum FieldId : int { CodeField };

constexpr skin::Name text_names[] = {{"nickname", Nickname}, {"greeting", Greeting}, {"seat", Seat}};
constexpr skin::Name number_names[] = {{"seats", Seats}};
constexpr skin::Name picture_names[] = {{"my_emoji", MyEmoji}};
constexpr skin::Name field_names[] = {{"code", CodeField}};
constexpr skin::Name hit_names[] = {{"create", HomeScreen::Create}, {"join", HomeScreen::Join}, {"code", HomeScreen::Code}, {"find", HomeScreen::Find}, {"seat", HomeScreen::SeatBase}};

}  // namespace

skin::ScreenSchema HomeScreen::schema() {
  return {.screen = "home", .texts = text_names, .numbers = number_names, .pictures = picture_names, .fields = field_names, .hits = hit_names};
}

void HomeScreen::paint_pack(Painter& p, const skin::Screen& screen) {
  refresh_seat(ui_.now_ms);
  p.set_base(D2D1::Matrix3x2F::Identity());

  class Shown final : public skin::Source {
   public:
    Shown(HomeScreen& home, Painter& painter) : home_(home), p_(painter) {
      if (home_.held_seats) seats_ = home_.held_seats();
    }

    std::string_view text(int id, int index) override {
      switch (id) {
        case Nickname: return home_.nickname_;
        case Greeting: {
          const int hour = home_.hour_;
          return hour >= 5 && hour < 12 ? tr().home_morning : hour >= 12 && hour < 18 ? tr().home_afternoon : hour >= 18 && hour < 22 ? tr().home_evening : tr().home_night;
        }
        case Seat: return static_cast<std::size_t>(index) < seats_.size() ? seats_[static_cast<std::size_t>(index)] : std::string_view{};
        default: return {};
      }
    }

    float number(int id) override { return id == Seats ? static_cast<float>(seats_.size()) : 0; }

    void picture(int, skin::Box box) override {
      draw_svg(p_, home_.ui_.emoji_image(home_.my_emoji ? home_.my_emoji() : std::string{}), rect_of(box));
    }

    void field(int, skin::Box box, const skin::FieldLook& look) override {
      const TextStyle style = text_style_of(look.text);
      home_.pack_code_style_ = style;
      home_.code_x_ = box.x;
      home_.ui_.caret = home_.code_.paint(p_, rect_of(box), style, look.color, widen(look.placeholder), style, look.placeholder_color, look.caret, look.selection, home_.ui_.focus == &home_.code_, home_.ui_.caret_on);
    }

   private:
    HomeScreen& home_;
    Painter& p_;
    std::vector<std::string> seats_;
  } shown(*this, p);

  SkinCanvas canvas(ui_, p, this);
  const Rect c = ui_.content;
  skin::draw(*ui_.pack, screen, ui_.palette, {c.x, c.y, c.w, c.h}, canvas, shown);

  // A click and a bubble go by where these were drawn.
  if (const Ui::Hit* hit = ui_.find_hit(this, Join)) join_rect_ = hit->rect;
  if (const Ui::Hit* hit = ui_.find_hit(this, Find)) find_rect_ = hit->rect;
}

}  // namespace crpp::ui
