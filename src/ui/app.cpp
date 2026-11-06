#include "crpp/ui/app.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace crpp::ui {

namespace {

constexpr const char* droplet_path = "M18 1C13 10 2 20 2 31a16 16 0 0 0 32 0C34 20 23 10 18 1Z";
constexpr const char* default_emoji = "1f4a7";

// Speaker icon pieces (main.js SPEAKER_*), 24x24 viewBox.
constexpr const char* speaker_body =
    "M2.6,9 H5.6 L10.3,5.1 C10.9,4.6 11.8,5 11.8,5.8 V18.2 C11.8,19 10.9,19.4 10.3,18.9 L5.6,15 H2.6 C2,15 1.5,14.5 "
    "1.5,13.9 V10.1 C1.5,9.5 2,9 2.6,9 Z";
constexpr const char* speaker_waves[3] = {"M14.6,9.2 C15.6,10.9 15.6,13.1 14.6,14.8",
                                          "M17.6,6.6 C19.8,9.9 19.8,14.1 17.6,17.4",
                                          "M21.3,4.2 C24.3,8.8 24.3,15.2 21.3,19.8"};
constexpr const char* speaker_slash = "M3.2,4.6 L17,18.6";
constexpr float speaker_level_shift[4] = {0, 3.05f, 1.1f, -1.4f};

D2D1_MATRIX_3X2_F scale_about(float cx, float cy, float s) {
  return D2D1::Matrix3x2F::Translation(-cx, -cy) * D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(cx, cy);
}

// The home cloud art is drawn at this stage position (main.js HOME_CLOUD_Y).
constexpr float home_cloud_x = -66.0667f;
constexpr float home_cloud_y = 100.9f;
constexpr float session_cloud_dy = 33;  // main.js SESSION_CLOUD_DY

constexpr float fab_size = 42;

}  // namespace

bool App::init(const std::filesystem::path& assets_dir) {
  if (!gfx_.init()) return false;
  emoji_.emplace(assets_dir);
  cloud_ = gfx_.parse_path(cloud_path_data);
  droplet_ = gfx_.parse_path(droplet_path);
  code_.max_length = 6;
  code_.uppercase = true;

  // The room cloud: four circles and a rectangle, unioned (main.js sessionCloudSvg).
  ComPtr<ID2D1EllipseGeometry> circles[4];
  const float c[4][3] = {{292, 190, 169}, {610, 160, 250}, {860, 400, 185}, {150, 405, 180}};
  ID2D1Geometry* parts[5] = {};
  ComPtr<ID2D1RectangleGeometry> rect;
  for (int i = 0; i < 4; ++i) {
    if (FAILED(gfx_.d2d()->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(c[i][0], c[i][1]), c[i][2], c[i][2]),
                                                 circles[i].put()))) {
      return false;
    }
    parts[i] = circles[i].get();
  }
  if (FAILED(gfx_.d2d()->CreateRectangleGeometry(D2D1::RectF(150, 300, 860, 585), rect.put()))) return false;
  parts[4] = rect.get();
  ComPtr<ID2D1GeometryGroup> group;
  if (FAILED(gfx_.d2d()->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, parts, 5, group.put()))) return false;
  session_cloud_ = group.upcast<ID2D1Geometry>();
  return cloud_ && droplet_ && session_cloud_;
}

// --- state -----------------------------------------------------------------

void App::show_home() {
  view_ = View::Home;
  members_.clear();
  options_open_ = mute_popup_open_ = false;
  pulses_.clear();
}

void App::show_session(std::string room_code) {
  view_ = View::Session;
  room_code_ = std::move(room_code);
  focus_ = Focus::None;
  options_open_ = mute_popup_open_ = false;
}

void App::set_room(std::vector<MemberView> members, std::uint32_t me, std::optional<std::uint32_t> host,
                   bool is_public, bool chat) {
  members_ = std::move(members);
  me_ = me;
  host_ = host;
  public_ = is_public;
  chat_ = chat;
}

void App::pulse(std::uint32_t slot) {
  const auto until = GetTickCount64() + 140;
  for (auto& p : pulses_) {
    if (p.first == slot) {
      p.second = until;
      return;
    }
  }
  pulses_.emplace_back(slot, until);
}

bool App::speaking(std::uint32_t slot) const {
  for (const auto& p : pulses_) {
    if (p.first == slot && p.second > now_ms_) return true;
  }
  return false;
}

bool App::tick(unsigned long long now_ms) {
  now_ms_ = now_ms;
  const auto before = pulses_.size();
  std::erase_if(pulses_, [&](const auto& p) { return p.second <= now_ms; });
  return pulses_.size() != before;
}

// --- hit testing -----------------------------------------------------------

void App::add_hit(Rect r, int id) {
  hits_.push_back({{ox_ + r.x * scale_, oy_ + r.y * scale_, r.w * scale_, r.h * scale_}, id});
}
void App::add_hit_window(Rect r, int id) { hits_.push_back({r, id}); }

int App::hit_at(float x, float y) const {
  // Later hits are drawn on top.
  for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
    if (!it->rect.contains(x, y)) continue;
    if (it->id == Create) {
      // The click area is the cloud's shape, not a rectangle.
      BOOL inside = FALSE;
      cloud_->FillContainsPoint(D2D1::Point2F(to_stage_x(x), to_stage_y(y)),
                                D2D1::Matrix3x2F::Translation(home_cloud_x, home_cloud_y), &inside);
      if (!inside) continue;
    }
    return it->id;
  }
  return None;
}

// --- layout ------------------------------------------------------------------

// Where the member list goes (main.js layoutSession). The area follows the
// window but its contents keep their size, so everything here is window pixels.
App::SessionLayout App::session_layout() const {
  const float k = scale_;
  const float vis_w = width_ / k, vis_h = height_ / k;
  const float vis_left = stage_w / 2 - vis_w / 2, vis_right = stage_w / 2 + vis_w / 2;
  const float vis_top = stage_h / 2 - vis_h / 2, vis_bottom = stage_h / 2 + vis_h / 2;
  const float top = std::max(74 + session_cloud_dy, vis_top + 20);
  const float bottom = std::max(top + 140 / k, std::min(537 + session_cloud_dy, vis_bottom - 74 / k));
  const float left = std::max(190.0f, vis_left + 20);
  const float right = std::max(left + (219 + 250) / k, std::min(825.0f, vis_right - 20));
  const float stage_x = chat_ ? left - 16 / k : (left + std::min(762.0f, right)) / 2 - 286 / k;
  return {ox_ + stage_x * k, oy_ + top * k, (bottom - top) * k, chat_};
}

// --- drawing ---------------------------------------------------------------

void App::paint(ID2D1RenderTarget* rt, float width, float height) {
  width_ = width;
  height_ = height;
  now_ms_ = GetTickCount64();
  // Cover: the stage fills the window; the overflow is cropped, centered.
  scale_ = std::max(width / stage_w, height / stage_h);
  ox_ = (width - stage_w * scale_) / 2;
  oy_ = (height - stage_h * scale_) / 2;
  hits_.clear();

  Painter p(gfx_, rt);
  p.set_base(D2D1::Matrix3x2F::Scale(scale_, scale_) * D2D1::Matrix3x2F::Translation(ox_, oy_));
  p.clear(theme_.sky);
  if (view_ == View::Home) paint_home(p);
  else paint_session(p);
  paint_corner_controls(p);
}

void App::paint_home(Painter& p) {
  const Theme& t = theme_;
  // Sky: the large cloud at lower right, then the create cloud at upper left.
  p.cloud(cloud_.get(), 306.33f, 295.56f, 1.3906f, 0.09f, 6, t);
  const bool create_hot = hover_ == Create;
  if (create_hot) p.push(scale_about(182, 260, 1.03f));
  p.cloud(cloud_.get(), home_cloud_x, home_cloud_y, 1, 0.06f, 3.742f, t);
  p.text("구름 띄우기", {0, 318, 458, 110}, {.size = 44, .line_height = 110, .align = Align::Center}, t.hl_text);
  p.pop();
  add_hit({-66, 100, 500, 330}, Create);

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
  caret_ = code_.paint(p, field, {.size = 34, .line_height = 60, .letter_spacing = 34 * 0.08f}, input_text,
                       L"코드 입력", {.size = 26, .line_height = 60}, faint, input_text, with_alpha(t.hl, 0.4f),
                       focus_ == Focus::Code, caret_on_);
  caret_ = {ox_ + caret_.x * scale_, oy_ + caret_.y * scale_, caret_.w * scale_, caret_.h * scale_};

  // Enter arrow.
  const Rect enter{entry.x + 221, entry.y + 10, 52, 60};
  const bool enter_hot = hover_ == Join;
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
  add_hit(entry, Code);
  add_hit(enter, Join);

  // Public room search button.
  const Rect search{759, 522, 92, 80};
  const bool search_hot = hover_ == Find;
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
  add_hit(search, Find);
}

D2D1_MATRIX_3X2_F App::code_focus_scale() const {
  if (focus_ != Focus::Code) return D2D1::Matrix3x2F::Identity();
  return scale_about(469 + 279 / 2.f, 522 + 40, 1.012f);
}

void App::paint_session(Painter& p) {
  const Theme& t = theme_;
  // The cloud, pushed down by session_cloud_dy.
  p.push(D2D1::Matrix3x2F::Translation(0, session_cloud_dy));
  if (t.cloud_shadow_scale > 0) p.shadow_geometry(session_cloud_.get(), 18, 6, rgb(0x2f6f99, 0.14f));
  p.fill_vertical_gradient(session_cloud_.get(), 0, 585,
                           {{0, t.cloud_top}, {0.42f, t.cloud_mid}, {0.8f, t.cloud_low}, {1, t.cloud_foot}});
  p.pop();

  // Members live in window pixels, not stage pixels.
  const SessionLayout layout = session_layout();
  p.set_base(D2D1::Matrix3x2F::Identity());
  const float col_w = layout.single_column ? 227.0f : 274.0f;
  const int cols = layout.single_column ? 1 : 2;
  const int n = static_cast<int>(members_.size());
  const int rows = (n + cols - 1) / cols;
  const float pad = (layout.members_h - static_cast<float>(std::min(rows, 8)) * 49) / 2;
  const float area_w = cols * col_w + (cols - 1) * 24;
  p.rt()->PushAxisAlignedClip(D2D1::RectF(layout.members_x, layout.members_y, layout.members_x + area_w,
                                          layout.members_y + layout.members_h),
                              D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  for (int i = 0; i < n; ++i) {
    const MemberView& m = members_[static_cast<std::size_t>(i)];
    const int col = i % cols, row = i / cols;
    const float x = layout.members_x + static_cast<float>(col) * (col_w + 24);
    const float y = layout.members_y + std::max(0.0f, pad) + static_cast<float>(row) * 49 + 1;
    const Rect box{x, y, col_w, 47};
    const int id = MemberBase + static_cast<int>(m.slot);
    if (hover_ == id) p.fill_round(box, 14, with_alpha(t.hl, 0.2f));
    const float cx = x + 6 + 22, cy = y + 1.5f + 22;
    if (speaking(m.slot)) p.fill_ellipse(cx, cy, 22, 22, t.hl);
    const SvgImage* image = &emoji_->get(gfx_, m.emoji.empty() ? default_emoji : m.emoji);
    if (image->shapes.empty()) image = &emoji_->get(gfx_, default_emoji);  // unknown emoji id
    draw_svg(p, *image, {cx - 18, cy - 18, 36, 36}, m.offline ? 0.55f : 1.0f);
    Color name = t.hl_text;
    if (m.offline) name = with_alpha(t.hl_text, 0.5f);
    p.text(m.nick.empty() ? "게스트" : m.nick, {x + 58, y + (47 - 26) / 2.0f, col_w - 58 - 12, 26},
           {.size = 21, .weight = 700, .line_height = 26, .ellipsis = true}, name);
    add_hit_window(box, id);
  }
  p.rt()->PopAxisAlignedClip();

  paint_session_controls(p);
}

void App::paint_corner_controls(Painter& p) {
  // Settings droplet: fixed to the window's top-right corner, not the stage.
  const Theme& t = theme_;
  p.set_base(D2D1::Matrix3x2F::Identity());
  const Rect drop{width_ - 18 - 35, 16, 35, 49};
  const bool hot = hover_ == Settings;
  const float s = 35 / 36.f;
  D2D1_MATRIX_3X2_F m = D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(drop.x, drop.y + (49 - 49 * s) / 2);
  if (hot) m = m * scale_about(drop.x + 17.5f, drop.y + 24.5f, 1.08f);
  p.push(m);
  p.fill_geometry(droplet_.get(), hot ? mix(t.droplet, t.hl_text_dark, 92) : t.droplet);
  p.pop();
  add_hit_window(drop, Settings);
}

void App::paint_session_controls(Painter& p) {
  const Theme& t = theme_;
  const float y = height_ - 18 - fab_size;
  struct Fab {
    float x;
    int id;
  };
  const Fab fabs[3] = {{18, Leave}, {width_ - 68 - fab_size, Mute}, {width_ - 18 - fab_size, More}};
  for (const Fab& f : fabs) {
    const Rect r{f.x, y, fab_size, fab_size};
    const bool hot = hover_ == f.id;
    const bool alarm = f.id == Mute && muted_;
    if (hot) p.push(scale_about(r.x + 21, r.y + 21, 1.08f));
    p.shadow_round(r, 21, 8, 2, rgb(0x2e6498, 0.16f));
    p.fill_round(r, 21, alarm ? rgb(0xfa5462) : (t.dark ? t.cloud_base : t.surface));
    const Color ink = alarm ? rgb(0xFFFFFF) : (hot ? t.hl_text : t.hl_text_soft);
    const float cx = r.x + 21, cy = r.y + 21;
    if (f.id == Leave) {
      const auto arrow = gfx_.parse_path("M19 12H5M11 6l-6 6 6 6");
      p.push(D2D1::Matrix3x2F::Scale(22 / 24.f, 22 / 24.f) * D2D1::Matrix3x2F::Translation(cx - 11, cy - 11));
      p.stroke_geometry(arrow.get(), ink, 3);
      p.pop();
    } else if (f.id == More) {
      for (const float dx : {-10.0f, 0.0f, 10.0f}) p.fill_ellipse(cx + dx, cy, 3, 3, ink);
    } else {
      const int level = volume_ <= 0 ? 0 : volume_ < 34 ? 1 : volume_ < 67 ? 2 : 3;
      const float group = muted_ ? 0 : speaker_level_shift[std::min(3, std::max(1, level))];
      p.push(D2D1::Matrix3x2F::Translation(cx - 12 + group, cy - 12));
      const auto body = gfx_.parse_path(speaker_body);
      p.push(D2D1::Matrix3x2F::Translation(muted_ ? 2.5f : 0.0f, 0) *
             D2D1::Matrix3x2F::Translation(cx - 12 + group, cy - 12));
      p.fill_geometry(body.get(), ink);
      p.pop();
      p.push(D2D1::Matrix3x2F::Translation(cx - 12 + group, cy - 12));
      if (muted_) {
        const auto slash = gfx_.parse_path(speaker_slash);
        p.push(D2D1::Matrix3x2F::Translation(2.5f, 0) * D2D1::Matrix3x2F::Translation(cx - 12 + group, cy - 12));
        p.stroke_geometry(slash.get(), ink, 2.1f);
        p.pop();
      } else {
        for (int i = 0; i < 3; ++i) {
          if (level > i) {
            const auto wave = gfx_.parse_path(speaker_waves[i]);
            p.stroke_geometry(wave.get(), ink, 2.1f);
          }
        }
      }
      p.pop();
      p.pop();
    }
    if (hot) p.pop();
    add_hit_window(r, f.id);
  }
  if (options_open_) paint_options_popup(p);
  if (mute_popup_open_) paint_mute_popup(p);
}

void App::paint_switch(Painter& p, Rect r, bool on, bool disabled, bool hot) {
  const Theme& t = theme_;
  const float a = disabled ? 0.5f : 1.0f;
  Color track = on ? t.hl : (t.dark ? t.hl_pale2 : rgb(0xcfe3ef));
  if (hot && !disabled) track = mix(rgb(0x000000), track, 5);
  p.fill_round(r, 14, with_alpha(track, a));
  const float tx = r.x + (on ? 27 : 3);
  p.fill_ellipse(tx + 10.5f, r.y + 13.5f, 10.5f, 10.5f,
                 with_alpha(t.dark ? (on ? t.on_accent : t.hl_text_soft) : t.surface, a));
}

void App::paint_options_popup(Painter& p) {
  const Theme& t = theme_;
  const float w = 251, h = 24 + 41 + 12 + 41 + 41 + 24;
  const Rect r{width_ - 12 - w, height_ - 72 - h, w, h};
  p.shadow_round(r, 14, 16, 4, rgb(0x2e78aa, 0.22f));
  p.fill_round(r, 14, t.surface);
  add_hit_window(r, Popup);
  const TextStyle label{.size = 20, .weight = 700, .line_height = 41};
  float y = r.y + 24;
  // Room code.
  p.text("구름 코드", {r.x + 25, y, 150, 41}, label, t.hl_text);
  const bool code_hot = hover_ == OptCode;
  const Rect code_rect{r.x + w - 25 - 90, y, 90, 41};
  p.text(room_code_, code_rect, {.size = 20, .weight = 700, .line_height = 41, .align = Align::Right, .letter_spacing = 0.4f},
         code_hot ? t.hl_text_dark : t.hl_text);
  add_hit_window(code_rect, OptCode);
  y += 41 + 5;
  p.fill_round({r.x + 25, y, w - 50, 2}, 1, t.hl_pale2);
  y += 2 + 5;
  const bool host = is_host();
  const Color ink = host ? t.hl_text : with_alpha(t.hl_text, 0.5f);
  p.text("공개 구름", {r.x + 25, y, 150, 41}, label, ink);
  const Rect pub{r.x + 25 + 201 - 6 - 51, y + (41 - 27) / 2.0f, 51, 27};
  paint_switch(p, pub, public_, !host, hover_ == OptPublic);
  add_hit_window(pub, OptPublic);
  y += 41;
  p.text("채팅", {r.x + 25, y, 150, 41}, label, ink);
  const Rect chat{r.x + 25 + 201 - 6 - 51, y + (41 - 27) / 2.0f, 51, 27};
  paint_switch(p, chat, chat_, !host, hover_ == OptChat);
  add_hit_window(chat, OptChat);
}

void App::paint_mute_popup(Painter& p) {
  const Theme& t = theme_;
  const float w = 200, h = 50;
  const Rect r{width_ - 12 - w, height_ - 72 - h, w, h};
  p.shadow_round(r, 14, 16, 4, rgb(0x2e78aa, 0.22f));
  p.fill_round(r, 14, t.surface);
  add_hit_window(r, Popup);
  p.text(std::to_string(volume_) + "%", {r.x + 14, r.y + 14, 42, 22},
         {.size = 16, .weight = 700, .line_height = 22}, t.hl_text);
  const float tx = r.x + 14 + 42 + 8, tw = w - 28 - 42 - 8;
  const float thumb = 21;
  volume_track_ = {tx, r.y + 14, tw, 22};
  const float ty = r.y + h / 2;
  const float pos = tx + thumb / 2 + (tw - thumb) * static_cast<float>(volume_) / 100.0f;
  p.fill_round({tx, ty - 3, tw, 6}, 3, t.hl_pale2);
  p.fill_round({tx, ty - 3, pos - tx, 6}, 3, t.hl);
  p.fill_ellipse(pos, ty, thumb / 2, thumb / 2, t.hl);
  p.fill_ellipse(pos, ty, thumb / 2 - 2, thumb / 2 - 2, t.dark ? t.hl_text_dark : t.surface);
  add_hit_window(volume_track_, VolumeSlider);
}

// --- input -----------------------------------------------------------------

void App::set_volume_from(float x) {
  const float thumb = 21;
  const float v = (x - volume_track_.x - thumb / 2) / (volume_track_.w - thumb);
  volume_ = static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 100));
  if (on_volume) on_volume(volume_);
}

bool App::mouse_move(float x, float y) {
  if (dragging_volume_) {
    set_volume_from(x);
    return true;
  }
  const int now = hit_at(x, y);
  if (now == hover_) return false;
  hover_ = now;
  return true;
}

bool App::mouse_leave() {
  if (hover_ == None) return false;
  hover_ = None;
  return true;
}

bool App::mouse_up() {
  const bool was = dragging_volume_;
  dragging_volume_ = false;
  return was;
}

bool App::right_click(float x, float y) {
  // Right-clicking the speaker opens the volume popup (main.js contextmenu).
  if (view_ != View::Session || hit_at(x, y) != Mute) return false;
  mute_popup_open_ = !mute_popup_open_;
  options_open_ = false;
  return true;
}

bool App::mouse_down(float x, float y, bool shift) {
  const int id = hit_at(x, y);
  focus_ = id == Code || id == Join ? Focus::Code : Focus::None;
  caret_on_ = true;
  // Popups close when anything outside them is clicked.
  const bool in_options = id == Popup || id == OptCode || id == OptPublic || id == OptChat || id == More;
  const bool in_mute = id == Popup || id == VolumeSlider || id == Mute;
  if (options_open_ && !in_options) options_open_ = false;
  if (mute_popup_open_ && !in_mute) mute_popup_open_ = false;

  switch (id) {
    case Create:
      if (on_create) on_create();
      break;
    case Join:
      if (on_join && !code_.text().empty()) on_join(narrow(code_.text()));
      break;
    case Find:
      if (on_find_public) on_find_public();
      break;
    case Settings:
      if (on_settings) on_settings();
      break;
    case Code:
      code_.click_at(gfx_, {.size = 34, .line_height = 60, .letter_spacing = 34 * 0.08f}, to_stage_x(x) - (469 + 18),
                     shift);
      break;
    case Leave:
      if (on_leave) on_leave();
      break;
    case More:
      options_open_ = !options_open_;
      mute_popup_open_ = false;
      break;
    case Mute:
      muted_ = !muted_;
      if (on_mute) on_mute(muted_);
      break;
    case OptCode:
      if (on_copy) on_copy(room_code_);
      break;
    case OptPublic:
      if (is_host() && on_set_public) on_set_public(!public_);
      break;
    case OptChat:
      if (is_host() && on_set_chat) on_set_chat(!chat_);
      break;
    case VolumeSlider:
      dragging_volume_ = true;
      set_volume_from(x);
      break;
    default:
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
  if (dragging_volume_) return Cursor::Hand;
  if (hover_ == None || hover_ == Popup) return Cursor::Arrow;
  if (hover_ == Code) return Cursor::IBeam;
  return Cursor::Hand;
}

}  // namespace crpp::ui
