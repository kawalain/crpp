// Home drawn from a Skin Pack. State, hit ids and callbacks stay the screen's own; the pack says
// where everything goes and how it looks.
#include "crpp/skin/draw.hpp"
#include "crpp/ui/home_logic.hpp"
#include "crpp/ui/home_screen.hpp"
#include "crpp/ui/prefs.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

enum TextId : int { Nickname, Greeting, Seat, Hello, SeatLeft, PublicRoomsText };
enum NumberId : int { Seats, Returning, CodeFocus, FindTip, PublicRooms, SeasonN };
enum PictureId : int { MyEmoji };
enum FieldId : int { CodeField };

constexpr skin::Name text_names[] = {{"nickname", Nickname}, {"greeting", Greeting}, {"seat", Seat}, {"hello", Hello}, {"seat_left", SeatLeft}, {"public_rooms_text", PublicRoomsText}};
constexpr skin::Name number_names[] = {{"seats", Seats}, {"returning", Returning}, {"code_focus", CodeFocus}, {"find_tip", FindTip}, {"public_rooms", PublicRooms}, {"season", SeasonN}};
constexpr skin::Name picture_names[] = {{"my_emoji", MyEmoji}};
constexpr skin::Name field_names[] = {{"code", CodeField}};
constexpr skin::Name hit_names[] = {{"create", HomeScreen::Create}, {"join", HomeScreen::Join}, {"code", HomeScreen::Code}, {"find", HomeScreen::Find}, {"seat", HomeScreen::SeatBase}};
constexpr skin::Name return_text_names[] = {{"left", 0}};
constexpr skin::Name return_hit_names[] = {{"backdrop", HomeScreen::ReturnBackdrop}, {"card", HomeScreen::ReturnCard}, {"confirm", HomeScreen::ReturnConfirm}, {"random", HomeScreen::ReturnRandom}};

}  // namespace

skin::ScreenSchema HomeScreen::schema() {
  return {.screen = "home", .texts = text_names, .numbers = number_names, .pictures = picture_names, .fields = field_names, .hits = hit_names};
}

skin::ScreenSchema HomeScreen::return_schema() { return {.screen = "home_return", .texts = return_text_names, .hits = return_hit_names}; }

void HomeScreen::paint_return_pack(Painter& p, const skin::Screen& screen) {
  p.set_base(D2D1::Matrix3x2F::Identity());
  SkinCanvas canvas(ui_, p, this);
  canvas.in_window_coordinates();
  // How long the Seat it offers is still held.
  class Shown final : public skin::Source {
   public:
    explicit Shown(HomeScreen& home) : home_(home), left_(home.held_seat_ms ? home.held_seat_ms() : std::nullopt) {
      if (left_) text_ = seat_time_left(*left_);
    }
    std::string_view text(int, int) override {
      if (left_) home_.note_time_left(*left_);
      return text_;
    }

   private:
    HomeScreen& home_;
    std::optional<std::uint64_t> left_;
    std::string text_;
  } shown(*this);
  skin::draw(*ui_.pack, screen, ui_.palette, {0, 0, ui_.width, ui_.height}, canvas, shown);
  after_drawing_times(ui_.now_ms);
}

void HomeScreen::paint_pack(Painter& p, const skin::Screen& screen) {
  refresh_seat(ui_.now_ms);
  next_minute_ms_ = 0;

  class Shown final : public skin::Source {
   public:
    Shown(HomeScreen& home, Painter& painter) : home_(home), p_(painter) {
      if (home_.held_seats) seats_ = home_.held_seats();
      for (const HeldSeatShown& seat : seats_) lefts_.push_back(seat_time_left(seat.left_ms));
      if (const auto open = home_.public_rooms_) {
        public_rooms_ = *open == 0 ? std::string(tr().home_public_none) : locale::fill(tr().home_public_open, {std::to_string(*open)});
      }
      hello_ = locale::fill(tr().home_hello, {home_.nickname_});
    }

    std::string_view text(int id, int index) override {
      switch (id) {
        case Nickname: return home_.nickname_;
        case Greeting: {
          switch (home::special_day(home_.month_, home_.day_)) {
            case home::SpecialDay::NewYear: return tr().home_new_year;
            case home::SpecialDay::AprilFools: return tr().home_april_fools;
            case home::SpecialDay::Birthday: return tr().home_birthday;
            case home::SpecialDay::Christmas: return tr().home_christmas;
            case home::SpecialDay::None: break;
          }
          const int hour = home_.hour_;
          return hour >= 5 && hour < 12 ? tr().home_morning : hour >= 12 && hour < 18 ? tr().home_afternoon : hour >= 18 && hour < 22 ? tr().home_evening : tr().home_night;
        }
        case Seat: return static_cast<std::size_t>(index) < seats_.size() ? seats_[static_cast<std::size_t>(index)].code : std::string_view{};
        case SeatLeft: {
          if (static_cast<std::size_t>(index) >= seats_.size()) return {};
          const HeldSeatShown& seat = seats_[static_cast<std::size_t>(index)];
          home_.note_time_left(seat.left_ms);
          return lefts_[static_cast<std::size_t>(index)];
        }
        case Hello: return hello_;
        case PublicRoomsText: return public_rooms_;
        default: return {};
      }
    }

    float number(int id, int) override {
      switch (id) {
        case Seats: return static_cast<float>(seats_.size());
        case Returning: return home_.returning() ? 1.0f : 0.0f;
        case CodeFocus: return home_.ui_.focus == &home_.code_ ? 1.0f : 0.0f;
        case FindTip: return home_.tip_wanted(home_.ui_.now_ms) ? 1.0f : 0.0f;
        case PublicRooms: return home_.public_rooms_ ? static_cast<float>(*home_.public_rooms_) : -1.0f;
        case SeasonN: return static_cast<float>(home_.season());  // 0 none, 1 winter, 2 spring, 3 autumn
        default: return 0;
      }
    }

    void picture(int, int, skin::Box box, float, float) override {
      draw_svg(p_, home_.ui_.emoji_image(home_.my_emoji ? home_.my_emoji() : std::string{}), rect_of(box));
    }

    void field(int, int, skin::Box box, const skin::FieldLook& look) override {
      const TextStyle style = text_style_of(look.text);
      home_.pack_code_style_ = style;
      home_.code_x_ = box.x;
      home_.ui_.caret = home_.ui_.to_window(home_.code_.paint(p_, rect_of(box), style, look.color, widen(look.placeholder), text_style_of(look.placeholder_text), look.placeholder_color, look.caret, look.selection, home_.ui_.focus == &home_.code_, home_.ui_.caret_on));
    }

   private:
    HomeScreen& home_;
    Painter& p_;
    std::vector<HeldSeatShown> seats_;
    std::string hello_;
    std::vector<std::string> lefts_;
    std::string public_rooms_;
  } shown(*this, p);

  SkinCanvas canvas(ui_, p, this);
  const Rect c = ui_.screen_area();
  skin::draw(*ui_.pack, screen, ui_.palette, {c.x, c.y, c.w, c.h}, canvas, shown);
  after_drawing_times(ui_.now_ms);

  // A bubble goes by where these were drawn.
  const auto on_stage = [&](Rect r) { return Rect{ui_.to_stage_x(r.x), ui_.to_stage_y(r.y), r.w / ui_.scale, r.h / ui_.scale}; };
  if (const Ui::Hit* hit = ui_.find_hit(this, Join)) join_rect_ = on_stage(hit->rect);
  if (const Ui::Hit* hit = ui_.find_hit(this, Find)) find_rect_ = on_stage(hit->rect);
}

}  // namespace crpp::ui
