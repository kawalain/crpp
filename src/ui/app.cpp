#include "crpp/ui/app.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace crpp::ui {

namespace {

constexpr const char* droplet_path = "M18 1C13 10 2 20 2 31a16 16 0 0 0 32 0C34 20 23 10 18 1Z";

D2D1_MATRIX_3X2_F scale_about(float cx, float cy, float s) {
  return D2D1::Matrix3x2F::Translation(-cx, -cy) * D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(cx, cy);
}

// The cloud art is drawn at this stage position (main.js HOME_CLOUD_Y).
constexpr float home_cloud_x = -66.0667f;
constexpr float home_cloud_y = 100.9f;

}  // namespace

bool App::init() {
  if (!gfx_.init()) return false;
  cloud_ = gfx_.parse_path(cloud_path_data);
  droplet_ = gfx_.parse_path(droplet_path);
  code_.max_length = 6;
  code_.uppercase = true;
  return cloud_ && droplet_;
}

// --- hit testing -----------------------------------------------------------

void App::add_hit(Rect r, Id id) {
  hits_.push_back({{ox_ + r.x * scale_, oy_ + r.y * scale_, r.w * scale_, r.h * scale_}, id});
}
void App::add_hit_window(Rect r, Id id) { hits_.push_back({r, id}); }

App::Id App::hit_at(float x, float y) const {
  // Later hits are drawn on top.
  for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
    if (!it->rect.contains(x, y)) continue;
    if (it->id == Id::Create) {
      // The click area is the cloud's shape, not a rectangle.
      BOOL inside = FALSE;
      cloud_->FillContainsPoint(D2D1::Point2F(to_stage_x(x), to_stage_y(y)),
                                D2D1::Matrix3x2F::Translation(home_cloud_x, home_cloud_y), &inside);
      if (!inside) continue;
    }
    return it->id;
  }
  return Id::None;
}

// --- drawing ---------------------------------------------------------------

void App::paint(ID2D1RenderTarget* rt, float width, float height) {
  width_ = width;
  height_ = height;
  // Cover: the stage fills the window; the overflow is cropped, centered.
  scale_ = std::max(width / stage_w, height / stage_h);
  ox_ = (width - stage_w * scale_) / 2;
  oy_ = (height - stage_h * scale_) / 2;
  hits_.clear();

  Painter p(gfx_, rt);
  p.set_base(D2D1::Matrix3x2F::Scale(scale_, scale_) * D2D1::Matrix3x2F::Translation(ox_, oy_));
  p.clear(theme_.sky);
  paint_home(p);
}

void App::paint_home(Painter& p) {
  const Theme& t = theme_;
  // Sky: the large cloud at lower right, then the create cloud at upper left.
  p.cloud(cloud_.get(), 306.33f, 295.56f, 1.3906f, 0.09f, 6, t);
  const bool create_hot = hover_ == Id::Create;
  if (create_hot) p.push(scale_about(182, 260, 1.03f));
  p.cloud(cloud_.get(), home_cloud_x, home_cloud_y, 1, 0.06f, 3.742f, t);
  p.text("구름 띄우기", {0, 318, 458, 110}, {.size = 44, .line_height = 110, .align = Align::Center}, t.hl_text);
  p.pop();
  add_hit({-66, 100, 500, 330}, Id::Create);

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
  p.push(code_focus_scale());
  p.fill_round(entry, 18, t.entry);
  const Color faint = with_alpha(t.dark ? t.hl_text_soft : rgb(0xFFFFFF), t.dark ? 1.0f : 0.85f);
  const Color input_text = t.dark ? t.hl_text : t.on_accent;
  const Rect field{entry.x + 18, entry.y + 10, 205, 60};
  const bool focused = focus_ == Focus::Code;
  caret_ = code_.paint(p, field, {.size = 34, .line_height = 60, .letter_spacing = 34 * 0.08f}, input_text,
                       L"코드 입력", {.size = 26, .line_height = 60}, faint, input_text, with_alpha(t.hl, 0.4f),
                       focused, caret_on_);
  caret_ = {ox_ + caret_.x * scale_, oy_ + caret_.y * scale_, caret_.w * scale_, caret_.h * scale_};

  // Enter arrow.
  const Rect enter{entry.x + 221, entry.y + 10, 52, 60};
  const bool enter_hot = hover_ == Id::Join;
  if (enter_hot) p.fill_round(enter, 13, mix(t.entry, rgb(0xFFFFFF), 40));
  const Color enter_color = enter_hot ? t.hl_text : (t.dark ? t.hl_text_soft : faint);
  {
    const auto arrow = gfx_.parse_path("M10 22h23M23 11l11 11-11 11");
    p.push(D2D1::Matrix3x2F::Scale(32 / 44.f, 32 / 44.f) *
           D2D1::Matrix3x2F::Translation(enter.x + 26 - 16, enter.y + 30 - 16));
    p.stroke_geometry(arrow.get(), enter_color, 3);
    p.pop();
  }
  p.pop();
  add_hit(entry, Id::Code);
  add_hit(enter, Id::Join);

  // Public room search button.
  const Rect search{759, 522, 92, 80};
  const bool search_hot = hover_ == Id::Find;
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
  add_hit(search, Id::Find);

  // Settings droplet: fixed to the window's top-right corner, not the stage.
  p.set_base(D2D1::Matrix3x2F::Identity());
  const Rect drop{width_ - 18 - 35, 16, 35, 49};
  const bool drop_hot = hover_ == Id::Settings;
  const float s = 35 / 36.f;
  D2D1_MATRIX_3X2_F m = D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(drop.x, drop.y + (49 - 49 * s) / 2);
  if (drop_hot) m = m * scale_about(drop.x + 17.5f, drop.y + 24.5f, 1.08f);
  p.push(m);
  p.fill_geometry(droplet_.get(), drop_hot ? mix(t.droplet, t.hl_text_dark, 92) : t.droplet);
  p.pop();
  add_hit_window(drop, Id::Settings);
}

// The code entry grows a little while typing (style.css .code-entry:focus-within).
D2D1_MATRIX_3X2_F App::code_focus_scale() const {
  if (focus_ != Focus::Code) return D2D1::Matrix3x2F::Identity();
  return scale_about(469 + 279 / 2.f, 522 + 40, 1.012f);
}

// --- input -----------------------------------------------------------------

bool App::mouse_move(float x, float y) {
  const Id now = hit_at(x, y);
  if (now == hover_) return false;
  hover_ = now;
  return true;
}

bool App::mouse_leave() {
  if (hover_ == Id::None) return false;
  hover_ = Id::None;
  return true;
}

bool App::mouse_down(float x, float y, bool shift) {
  const Id id = hit_at(x, y);
  focus_ = id == Id::Code || id == Id::Join ? Focus::Code : Focus::None;
  caret_on_ = true;
  switch (id) {
    case Id::Create:
      if (on_create) on_create();
      break;
    case Id::Join:
      if (on_join && !code_.text().empty()) on_join(narrow(code_.text()));
      break;
    case Id::Find:
      if (on_find_public) on_find_public();
      break;
    case Id::Settings:
      if (on_settings) on_settings();
      break;
    case Id::Code: {
      // Text starts 18 px inside the entry box.
      code_.click_at(gfx_, {.size = 34, .line_height = 60, .letter_spacing = 34 * 0.08f},
                     to_stage_x(x) - (469 + 18), shift);
      break;
    }
    case Id::None:
      break;
  }
  return true;
}

bool App::char_input(wchar_t c) {
  if (focus_ != Focus::Code) return false;
  if (c == L'\r') {
    if (on_join && !code_.text().empty()) on_join(narrow(code_.text()));
    return true;
  }
  code_.insert(std::wstring_view(&c, 1));
  caret_on_ = true;
  return true;
}

bool App::key_down(unsigned vk, bool ctrl, bool shift) {
  if (focus_ != Focus::Code) return false;
  caret_on_ = true;
  return code_.key_down(vk, ctrl, shift);
}

bool App::ime_composition(std::wstring_view composition, std::wstring_view result) {
  if (focus_ != Focus::Code) return false;
  if (!result.empty()) code_.insert(result);
  code_.set_composition(composition);
  caret_on_ = true;
  return true;
}

bool App::blink() {
  if (focus_ == Focus::None) return false;
  caret_on_ = !caret_on_;
  return true;
}

Cursor App::cursor() const {
  switch (hover_) {
    case Id::Code: return Cursor::IBeam;
    case Id::None: return Cursor::Arrow;
    default: return Cursor::Hand;
  }
}

}  // namespace crpp::ui
