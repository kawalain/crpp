#include "crpp/ui/home_screen.hpp"

#include "crpp/ui/toasts.hpp"

namespace crpp::ui {

namespace {

// The create cloud art is drawn at this stage position (main.js HOME_CLOUD_Y).
constexpr float cloud_x = -66.0667f;
constexpr float cloud_y = 100.9f;

constexpr TextStyle code_style{.size = 34, .line_height = 60, .letter_spacing = 34 * 0.08f};

}  // namespace

HomeScreen::HomeScreen(Ui& ui) : Surface(ui) {
  code_.max_length = 6;
  code_.uppercase = true;
}

void HomeScreen::paint(Painter& p) {
  const Theme& t = ui_.theme;
  // Sky: the large cloud at lower right, then the create cloud at upper left.
  p.cloud(ui_.cloud.get(), 306.33f, 295.56f, 1.3906f, 0.09f, 6, t);
  if (ui_.hot(this, Create)) p.push(scale_about(182, 260, 1.03f));
  p.cloud(ui_.cloud.get(), cloud_x, cloud_y, 1, 0.06f, 3.742f, t);
  p.text("구름 띄우기", {0, 318, 458, 110}, {.size = 44, .line_height = 110, .align = Align::Center}, t.hl_text);
  p.pop();
  ui_.add_hit(this, {-66, 100, 500, 330}, Create);

  // Greeting.
  const char* hello = hour_ >= 5 && hour_ < 12    ? "좋은 아침이에요"
                      : hour_ >= 12 && hour_ < 18 ? "좋은 오후예요"
                      : hour_ >= 18 && hour_ < 22 ? "좋은 저녁이에요"
                                                  : "좋은 밤이에요";
  const TextStyle greeting{.size = 44, .line_height = 44, .align = Align::Center};
  p.text(hello, {440, 108, 420, 44}, greeting, rgb(0xFFFFFF));
  p.text(nickname_ + " 님!", {440, 108 + 44 + 13, 420, 44}, greeting, rgb(0xFFFFFF));

  // Find a cloud.
  p.text("구름 찾기", {470, 452, 400, 60}, {.size = 40, .line_height = 60}, t.hl_text);

  const Rect entry{469, 522, 279, 80};
  const bool focused = ui_.focus == &code_;
  p.push(focused ? scale_about(469 + 279 / 2.f, 522 + 40, 1.012f) : D2D1::Matrix3x2F::Identity());
  p.fill_round(entry, 18, t.entry);
  const Color faint = with_alpha(t.dark ? t.hl_text_soft : rgb(0xFFFFFF), t.dark ? 1.0f : 0.85f);
  const Color input_text = t.dark ? t.hl_text : t.on_accent;
  const Rect field{entry.x + 18, entry.y + 10, 205, 60};
  ui_.caret = ui_.to_window(code_.paint(p, field, code_style, input_text, L"코드 입력", {.size = 26, .line_height = 60},
                                        faint, input_text, with_alpha(t.hl, 0.4f), focused, ui_.caret_on));

  // Enter arrow.
  const Rect enter{entry.x + 221, entry.y + 10, 52, 60};
  const bool enter_hot = ui_.hot(this, Join);
  if (enter_hot) p.fill_round(enter, 13, mix(t.entry, rgb(0xFFFFFF), 40));
  const Color enter_color = enter_hot ? t.hl_text : (t.dark ? t.hl_text_soft : faint);
  {
    const auto arrow = ui_.gfx.parse_path("M10 22h23M23 11l11 11-11 11");
    p.push(D2D1::Matrix3x2F::Scale(32 / 44.f, 32 / 44.f) *
           D2D1::Matrix3x2F::Translation(enter.x + 26 - 16, enter.y + 30 - 16));
    p.stroke_geometry(arrow.get(), enter_color, 3);
    p.pop();
  }
  p.pop();
  ui_.add_hit(this, entry, Code);
  ui_.add_hit(this, enter, Join);

  // Public room search button.
  const Rect search{759, 522, 92, 80};
  const bool search_hot = ui_.hot(this, Find);
  if (search_hot) p.push(scale_about(search.x + search.w / 2, search.y + search.h / 2, 1.05f));
  p.fill_round(search, 18, search_hot ? t.hl_dark : t.entry);
  {
    const Color icon = search_hot ? t.on_accent : (t.dark ? t.hl_text_soft : faint);
    p.push(D2D1::Matrix3x2F::Scale(42 / 44.f, 42 / 44.f) *
           D2D1::Matrix3x2F::Translation(search.x + 46 - 21, search.y + 40 - 21));
    p.stroke_ellipse(19, 19, 11, 11, icon, 3.2f);
    p.stroke_line(27.5f, 27.5f, 37, 37, icon, 3.2f);
    p.pop();
  }
  if (search_hot) p.pop();
  ui_.add_hit(this, search, Find);
}

bool HomeScreen::accepts(int id, float x, float y) const {
  if (id != Create) return true;
  // The click area is the cloud's shape, not a rectangle.
  BOOL inside = FALSE;
  ui_.cloud->FillContainsPoint(D2D1::Point2F(ui_.to_stage_x(x), ui_.to_stage_y(y)),
                               D2D1::Matrix3x2F::Translation(cloud_x, cloud_y), &inside);
  return inside != FALSE;
}

void HomeScreen::join() {
  if (code_.text().empty()) return;
  last_action_ = Join;
  if (on_join) on_join(narrow(code_.text()));
}

void HomeScreen::show_error(std::string text) {
  if (last_action_ == Join) {
    ui_.toasts->bubble(ui_.to_window({469 + 221, 522 + 10, 52, 60}), std::move(text), 18);
  } else if (last_action_ == Find) {
    ui_.toasts->bubble(ui_.to_window({759, 522, 92, 80}), std::move(text));
  } else {
    ui_.toasts->toast(std::move(text));
  }
}

void HomeScreen::click(int id, float x, float, bool shift) {
  switch (id) {
    case Create:
      last_action_ = Create;
      if (on_create) on_create();
      break;
    case Join:
      ui_.set_focus(this, &code_);
      join();
      break;
    case Code:
      ui_.set_focus(this, &code_);
      code_.click_at(ui_.gfx, code_style, ui_.to_stage_x(x) - (469 + 18), shift);
      break;
    case Find:
      last_action_ = Find;
      if (on_find_public) on_find_public();
      break;
    default:
      break;
  }
}

void HomeScreen::enter(bool) { join(); }

Cursor HomeScreen::cursor(int id) const { return id == Code ? Cursor::IBeam : Cursor::Hand; }

}  // namespace crpp::ui
