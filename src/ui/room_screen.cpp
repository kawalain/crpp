#include "crpp/ui/room_screen.hpp"

#include <windows.h>

#include <algorithm>

#include "crpp/ui/emoji_picker.hpp"

namespace crpp::ui {

namespace {

// Speaker icon pieces (main.js SPEAKER_*), 24x24 viewBox.
constexpr const char* speaker_body =
    "M2.6,9 H5.6 L10.3,5.1 C10.9,4.6 11.8,5 11.8,5.8 V18.2 C11.8,19 10.9,19.4 10.3,18.9 L5.6,15 H2.6 C2,15 1.5,14.5 "
    "1.5,13.9 V10.1 C1.5,9.5 2,9 2.6,9 Z";
constexpr const char* speaker_waves[3] = {"M14.6,9.2 C15.6,10.9 15.6,13.1 14.6,14.8",
                                          "M17.6,6.6 C19.8,9.9 19.8,14.1 17.6,17.4",
                                          "M21.3,4.2 C24.3,8.8 24.3,15.2 21.3,19.8"};
constexpr const char* speaker_slash = "M3.2,4.6 L17,18.6";
constexpr float speaker_level_shift[4] = {0, 3.05f, 1.1f, -1.4f};

constexpr float cloud_dy = 33;  // main.js SESSION_CLOUD_DY
constexpr float fab_size = 42;
constexpr const char* muted_emoji = "1f507";  // shown only to whoever muted them

constexpr const char* pin_tip_blocked = "우클릭하여 기본값 고정 해제 후\n조작할 수 있습니다.";
constexpr const char* chat_tip_locked = "이 구름에서는 이제 채팅을\n비활성화 할 수 없습니다.";

// @keyframes tip-shake, over 320 ms.
constexpr std::pair<float, float> tip_shake[] = {{0, 0}, {0.2f, -7}, {0.4f, 7}, {0.6f, -4}, {0.8f, 3}, {1, 0}};
constexpr float tip_shake_ms = 320;

}  // namespace

RoomScreen::RoomScreen(Ui& ui) : Surface(ui), chat_panel_(ui), profile_(ui), menu_(ui) {
  profile_.on_request = [this](std::uint32_t slot) {
    if (on_request_profile) on_request_profile(slot);
  };
  profile_.on_change_icon = [this] { change_icon(); };
  menu_.on_profile = [this](std::uint32_t slot) { profile_.request(slot); };
  menu_.on_volume = [this](std::uint32_t slot, int percent) { set_member_volume(slot, percent); };
}

bool RoomScreen::init() {
  // The room cloud: four circles and a rectangle, unioned (main.js sessionCloudSvg).
  ComPtr<ID2D1EllipseGeometry> circles[4];
  const float c[4][3] = {{292, 190, 169}, {610, 160, 250}, {860, 400, 185}, {150, 405, 180}};
  ID2D1Geometry* parts[5] = {};
  ComPtr<ID2D1RectangleGeometry> rect;
  ID2D1Factory* d2d = ui_.gfx.d2d();
  for (int i = 0; i < 4; ++i) {
    if (FAILED(d2d->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(c[i][0], c[i][1]), c[i][2], c[i][2]),
                                          circles[i].put()))) {
      return false;
    }
    parts[i] = circles[i].get();
  }
  if (FAILED(d2d->CreateRectangleGeometry(D2D1::RectF(150, 300, 860, 585), rect.put()))) return false;
  parts[4] = rect.get();
  ComPtr<ID2D1GeometryGroup> group;
  if (FAILED(d2d->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, parts, 5, group.put()))) return false;
  cloud_ = group.upcast<ID2D1Geometry>();
  return static_cast<bool>(cloud_);
}

void RoomScreen::enter_room(std::string room_code) {
  room_code_ = std::move(room_code);
  options_open_ = mute_popup_open_ = false;
}

void RoomScreen::reset() {
  members_.clear();
  members_scroll_ = {};
  chat_panel_.clear();
  options_open_ = mute_popup_open_ = false;
  pulses_.clear();
  copy_until_ = hint_muted_until_ = 0;
  lock_pending_ = false;
  blocked_id_ = -1;
  // The picker is modal over the card, so an open card means it is the card's.
  if (profile_.is_open()) ui_.emoji_picker->close();
  profile_.close();
  menu_.close();
  member_volumes_.clear();
  // A question about this Room means nothing once it is gone.
  if (dialog_open_) ui_.dialogs->close();
  dialog_open_ = false;
}

void RoomScreen::set_room(std::vector<MemberView> members, std::uint32_t me, std::optional<std::uint32_t> host,
                          bool is_public, bool chat, std::uint64_t chat_lock_ms) {
  members_ = std::move(members);
  me_ = me;
  host_ = host;
  public_ = is_public;
  chat_ = chat;
  chat_lock_at_ = GetTickCount64() + chat_lock_ms;
  lock_pending_ = chat && chat_lock_ms > 0;
}

void RoomScreen::pulse(std::uint32_t slot) {
  const auto until = GetTickCount64() + 140;
  for (auto& p : pulses_) {
    if (p.first == slot) {
      p.second = until;
      return;
    }
  }
  pulses_.emplace_back(slot, until);
}

bool RoomScreen::speaking(std::uint32_t slot) const {
  for (const auto& p : pulses_) {
    if (p.first == slot && p.second > ui_.now_ms) return true;
  }
  return false;
}

void RoomScreen::show_profile(ProfileView profile) {
  const bool mine = profile.slot == me_;
  profile_.answer(std::move(profile), mine);
}

void RoomScreen::change_icon() {
  const std::string& emoji = profile_.profile().emoji;
  ui_.emoji_picker->show(emoji.empty() ? "1f4a7" : emoji, [this](std::string id) {
    profile_.set_emoji(id);
    if (on_pick_emoji) on_pick_emoji(id);
  });
}

int RoomScreen::member_volume(const std::string& cid) const {
  const auto it = std::ranges::find(member_volumes_, cid, [](const auto& v) -> const std::string& { return v.first; });
  return it == member_volumes_.end() ? 100 : it->second;
}

void RoomScreen::set_member_volume(std::uint32_t slot, int percent) {
  const auto member = std::ranges::find(members_, slot, &MemberView::slot);
  if (member == members_.end()) return;
  const int before = member_volume(member->cid);
  std::erase_if(member_volumes_, [&](const auto& v) { return v.first == member->cid; });
  if (percent != 100) member_volumes_.emplace_back(member->cid, percent);
  if (on_member_volume) on_member_volume(member->cid, percent);
  if ((before == 0) != (percent == 0) && on_member_muted) on_member_muted(member->cid, percent == 0);
}

bool RoomScreen::chat_locked() const { return chat_ && GetTickCount64() >= chat_lock_at_; }

bool RoomScreen::shaking() const {
  return ui_.animations && blocked_id_ >= 0 && static_cast<float>(ui_.now_ms - std::min(ui_.now_ms, blocked_at_)) < tip_shake_ms;
}

bool RoomScreen::animating() const {
  return !pulses_.empty() || tip_.animating(ui_) || copy_tip_.animating(ui_) || shaking() || profile_.animating() ||
         menu_.animating() || chat_panel_.animating();
}

bool RoomScreen::tick(unsigned long long now_ms) {
  const auto before = pulses_.size();
  std::erase_if(pulses_, [&](const auto& p) { return p.second <= now_ms; });
  bool changed = pulses_.size() != before;
  if (tip_.animating(ui_) || copy_tip_.animating(ui_) || shaking() || menu_.animating()) changed = true;
  changed = profile_.tick(now_ms) || changed;
  changed = chat_panel_.tick(now_ms) || changed;
  // The chat switch locks 5 minutes after chat was turned on.
  if (lock_pending_ && now_ms >= chat_lock_at_) {
    lock_pending_ = false;
    changed = true;
  }
  // A waiting copy tip or muted hint: one repaint when its time comes.
  if (copy_until_ != 0 && now_ms >= copy_until_) {
    copy_until_ = 0;
    changed = true;
  }
  if (hint_muted_until_ != 0 && now_ms >= hint_muted_until_) {
    hint_muted_until_ = 0;
    changed = true;
  }
  return changed;
}

unsigned long long RoomScreen::wake_at() const {
  unsigned long long at = 0;
  for (const auto wake : {copy_until_, hint_muted_until_, lock_pending_ ? chat_lock_at_ : 0ULL, profile_.wake_at(),
                          chat_panel_.wake_at()}) {
    if (wake != 0 && (at == 0 || wake < at)) at = wake;
  }
  return at;
}

// Where the member list goes (main.js layoutSession). The area follows the
// window but its contents keep their size, so everything here is window pixels.
RoomScreen::Layout RoomScreen::layout() const {
  const float k = ui_.scale;
  const float vis_w = ui_.width / k, vis_h = ui_.height / k;
  const float vis_left = stage_w / 2 - vis_w / 2, vis_right = stage_w / 2 + vis_w / 2;
  const float vis_top = stage_h / 2 - vis_h / 2, vis_bottom = stage_h / 2 + vis_h / 2;
  const float top = std::max(74 + cloud_dy, vis_top + 20);
  const float bottom = std::max(top + 140 / k, std::min(537 + cloud_dy, vis_bottom - 74 / k));
  const float left = std::max(190.0f, vis_left + 20);
  const float right = std::max(left + (219 + 250) / k, std::min(825.0f, vis_right - 20));
  const float stage_x = chat_ ? left - 16 / k : (left + std::min(762.0f, right)) / 2 - 286 / k;
  const Rect panel{ui_.ox + (left + 219 / k) * k, ui_.oy + (top + 7) * k, std::max(250.0f, (right - left) * k - 219),
                   (bottom - top - 7) * k};
  return {ui_.ox + stage_x * k, ui_.oy + top * k, (bottom - top) * k, chat_, panel};
}

void RoomScreen::paint(Painter& p) {
  const Theme& t = ui_.theme;
  // The cloud, pushed down by cloud_dy.
  p.push(D2D1::Matrix3x2F::Translation(0, cloud_dy));
  if (t.cloud_shadow_scale > 0) p.shadow_geometry(cloud_.get(), 18, 6, rgb(0x2f6f99, 0.14f));
  p.fill_vertical_gradient(cloud_.get(), 0, 585,
                           {{0, t.cloud_top}, {0.42f, t.cloud_mid}, {0.8f, t.cloud_low}, {1, t.cloud_foot}});
  p.pop();

  // Members live in window pixels, not stage pixels.
  const Layout l = layout();
  p.set_base(D2D1::Matrix3x2F::Identity());
  paint_members(p, l);

  if (chat_) chat_panel_.paint(p, l.chat);
  if (controls_shown_) paint_controls(p);
  paint_tips(p);
}

void RoomScreen::paint_overlay(Painter& p) {
  p.set_base(D2D1::Matrix3x2F::Identity());
  profile_.paint(p);
  menu_.paint(p);
}

void RoomScreen::paint_members(Painter& p, const Layout& l) {
  const Theme& t = ui_.theme;
  const float col_w = l.single_column ? 227.0f : 274.0f;
  const int cols = l.single_column ? 1 : 2;
  const int n = static_cast<int>(members_.size());
  const int rows = (n + cols - 1) / cols;
  const float pad = std::max(0.0f, (l.members_h - static_cast<float>(std::min(rows, 8)) * 49) / 2);
  const float area_w = static_cast<float>(cols) * col_w + static_cast<float>(cols - 1) * 24;
  members_area_ = {l.members_x, l.members_y, area_w, l.members_h};
  members_scroll_.view_h = l.members_h;
  members_scroll_.content_h = 2 * pad + static_cast<float>(rows) * 49;
  members_scroll_.clamp();

  const D2D1_RECT_F bounds = D2D1::RectF(members_area_.x, members_area_.y, members_area_.right(), members_area_.bottom());
  p.rt()->PushAxisAlignedClip(bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  // Whatever is cut off fades out over 27 px (.scroll-area's mask).
  ComPtr<ID2D1Layer> layer;
  ComPtr<ID2D1LinearGradientBrush> mask;
  if (members_scroll_.max_offset() > 0) {
    const float edge = 27 / l.members_h;
    const float top_alpha = members_scroll_.offset > 0 ? 0.0f : 1.0f;
    const float bottom_alpha = members_scroll_.offset < members_scroll_.max_offset() ? 0.0f : 1.0f;
    const auto stop = [](float at, float alpha) { return D2D1::GradientStop(at, D2D1::ColorF(0, 0, 0, alpha)); };
    const D2D1_GRADIENT_STOP stops[4] = {stop(0, top_alpha), stop(edge, 1), stop(1 - edge, 1), stop(1, bottom_alpha)};
    ComPtr<ID2D1GradientStopCollection> collection;
    if (SUCCEEDED(p.rt()->CreateGradientStopCollection(stops, 4, collection.put())) &&
        SUCCEEDED(p.rt()->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, members_area_.y), D2D1::Point2F(0, members_area_.bottom())),
            collection.get(), mask.put())) &&
        SUCCEEDED(p.rt()->CreateLayer(nullptr, layer.put()))) {
      p.rt()->PushLayer(D2D1::LayerParameters(bounds, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                              D2D1::IdentityMatrix(), 1.0f, mask.get()),
                        layer.get());
    } else {
      layer.reset();
    }
  }
  ui_.push_hit_clip(members_area_);
  member_rects_.clear();
  for (int i = 0; i < n; ++i) {
    const MemberView& m = members_[static_cast<std::size_t>(i)];
    const int col = i % cols, row = i / cols;
    const float x = l.members_x + static_cast<float>(col) * (col_w + 24);
    const float y = l.members_y + pad + static_cast<float>(row) * 49 + 1 - members_scroll_.offset;
    if (y + 47 <= members_area_.y || y >= members_area_.bottom()) continue;
    const Rect box{x, y, col_w, 47};
    const int id = MemberBase + static_cast<int>(m.slot);
    if (ui_.hot(this, id)) p.fill_round(box, 14, with_alpha(t.hl, 0.2f));
    const float cx = x + 6 + 22, cy = y + 1.5f + 22;
    if (speaking(m.slot)) p.fill_ellipse(cx, cy, 22, 22, t.hl);
    const bool muted_here = m.slot != me_ && member_volume(m.cid) == 0;
    draw_svg(p, ui_.emoji_image(muted_here ? muted_emoji : m.emoji), {cx - 18, cy - 18, 36, 36}, m.offline ? 0.55f : 1.0f);
    Color name = t.hl_text;
    if (m.offline) name = with_alpha(t.hl_text, 0.5f);
    p.text(m.nick.empty() ? "게스트" : m.nick, {x + 58, y + (47 - 26) / 2.0f, col_w - 58 - 12, 26},
           {.size = 21, .weight = 700, .line_height = 26, .ellipsis = true}, name);
    ui_.add_hit_window(this, box, id);
    member_rects_.emplace_back(m.slot, box);
  }
  ui_.pop_hit_clip();
  if (layer) p.rt()->PopLayer();
  p.rt()->PopAxisAlignedClip();
}

void RoomScreen::paint_controls(Painter& p) {
  const Theme& t = ui_.theme;
  const float y = ui_.height - 18 - fab_size;
  struct Fab {
    float x;
    int id;
  };
  const Fab fabs[3] = {{18, Leave}, {ui_.width - 68 - fab_size, Mute}, {ui_.width - 18 - fab_size, More}};
  for (const Fab& f : fabs) {
    const Rect r{f.x, y, fab_size, fab_size};
    fab_rects_[f.id == Leave ? 0 : f.id == Mute ? 1 : 2] = r;
    const bool hot = ui_.hot(this, f.id);
    const bool alarm = f.id == Mute && muted_;
    if (hot) p.push(scale_about(r.x + 21, r.y + 21, 1.08f));
    p.shadow_round(r, 21, 8, 2, rgb(0x2e6498, 0.16f));
    p.fill_round(r, 21, alarm ? rgb(0xfa5462) : (t.dark ? t.cloud_base : t.surface));
    const Color ink = alarm ? rgb(0xFFFFFF) : (hot ? t.hl_text : t.hl_text_soft);
    const float cx = r.x + 21, cy = r.y + 21;
    if (f.id == Leave) {
      const auto arrow = ui_.gfx.parse_path("M19 12H5M11 6l-6 6 6 6");
      p.push(D2D1::Matrix3x2F::Scale(22 / 24.f, 22 / 24.f) * D2D1::Matrix3x2F::Translation(cx - 11, cy - 11));
      p.stroke_geometry(arrow.get(), ink, 3);
      p.pop();
    } else if (f.id == More) {
      for (const float dx : {-10.0f, 0.0f, 10.0f}) p.fill_ellipse(cx + dx, cy, 3, 3, ink);
    } else {
      const int level = volume_ <= 0 ? 0 : volume_ < 34 ? 1 : volume_ < 67 ? 2 : 3;
      const float group = muted_ ? 0 : speaker_level_shift[std::min(3, std::max(1, level))];
      p.push(D2D1::Matrix3x2F::Translation(cx - 12 + group, cy - 12));
      const auto body = ui_.gfx.parse_path(speaker_body);
      p.push(D2D1::Matrix3x2F::Translation(muted_ ? 2.5f : 0.0f, 0) *
             D2D1::Matrix3x2F::Translation(cx - 12 + group, cy - 12));
      p.fill_geometry(body.get(), ink);
      p.pop();
      p.push(D2D1::Matrix3x2F::Translation(cx - 12 + group, cy - 12));
      if (muted_) {
        const auto slash = ui_.gfx.parse_path(speaker_slash);
        p.push(D2D1::Matrix3x2F::Translation(2.5f, 0) * D2D1::Matrix3x2F::Translation(cx - 12 + group, cy - 12));
        p.stroke_geometry(slash.get(), ink, 2.1f);
        p.pop();
      } else {
        for (int i = 0; i < 3; ++i) {
          if (level > i) {
            const auto wave = ui_.gfx.parse_path(speaker_waves[i]);
            p.stroke_geometry(wave.get(), ink, 2.1f);
          }
        }
      }
      p.pop();
      p.pop();
    }
    if (hot) p.pop();
    ui_.add_hit_window(this, r, f.id);
  }
  if (options_open_) paint_options_popup(p);
  if (mute_popup_open_) paint_mute_popup(p);
}

void RoomScreen::paint_options_popup(Painter& p) {
  const Theme& t = ui_.theme;
  const float w = 251, h = 24 + 41 + 12 + 41 + 41 + 24;
  const Rect r{ui_.width - 12 - w, ui_.height - 72 - h, w, h};
  p.shadow_round(r, 14, 16, 4, rgb(0x2e78aa, 0.22f));
  p.fill_round(r, 14, t.surface);
  ui_.add_hit_window(this, r, Popup);
  const TextStyle label{.size = 20, .weight = 700, .line_height = 41};
  float y = r.y + 24;
  // Room code.
  p.text("구름 코드", {r.x + 25, y, 150, 41}, label, t.hl_text);
  const Rect code_rect{r.x + w - 25 - 90, y, 90, 41};
  p.text(room_code_, code_rect, {.size = 20, .weight = 700, .line_height = 41, .align = Align::Right, .letter_spacing = 0.4f},
         ui_.hot(this, OptCode) ? t.hl_text_dark : t.hl_text);
  ui_.add_hit_window(this, code_rect, OptCode);
  code_rect_ = code_rect;
  y += 41 + 5;
  p.fill_round({r.x + 25, y, w - 50, 2}, 1, t.hl_pale2);
  y += 2 + 5;
  const bool host = is_host();
  const Color ink = host ? t.hl_text : with_alpha(t.hl_text, 0.5f);
  p.text("공개 구름", {r.x + 25, y, 150, 41}, label, ink);
  const Rect pub{r.x + 25 + 201 - 6 - 51, y + (41 - 27) / 2.0f, 51, 27};
  paint_switch(p, t, pub, public_, !host, ui_.hot(this, OptPublic));
  if (ui_.prefs->get(pref::pin_public)) paint_pin(p, pub, public_, !host);
  ui_.add_hit_window(this, pub, OptPublic);
  public_rect_ = pub;
  y += 41;
  p.text("채팅", {r.x + 25, y, 150, 41}, label, ink);
  const Rect chat{r.x + 25 + 201 - 6 - 51, y + (41 - 27) / 2.0f, 51, 27};
  const bool chat_disabled = !host || chat_locked();
  paint_switch(p, t, chat, chat_, chat_disabled, ui_.hot(this, OptChat));
  if (ui_.prefs->get(pref::pin_chat)) paint_pin(p, chat, chat_, chat_disabled);
  ui_.add_hit_window(this, chat, OptChat);
  chat_rect_ = chat;
}

// .switch.pinned: a small bar across the knob.
void RoomScreen::paint_pin(Painter& p, Rect sw, bool on, bool disabled) {
  const float cx = sw.x + (on ? 37.5f : 13.5f), cy = sw.y + 13.5f;
  p.fill_round({cx - 5.5f, cy - 1.5f, 11, 3}, 1.5f, with_alpha(ui_.theme.hl_mid, disabled ? 0.5f : 1.0f));
}

void RoomScreen::paint_mute_popup(Painter& p) {
  const Theme& t = ui_.theme;
  const float w = 200, h = 50;
  const Rect r{ui_.width - 12 - w, ui_.height - 72 - h, w, h};
  p.shadow_round(r, 14, 16, 4, rgb(0x2e78aa, 0.22f));
  p.fill_round(r, 14, t.surface);
  ui_.add_hit_window(this, r, Popup);
  p.text(std::to_string(volume_) + "%", {r.x + 14, r.y + 14, 42, 22},
         {.size = 16, .weight = 700, .line_height = 22}, t.hl_text);
  volume_track_ = {r.x + 14 + 42 + 8, r.y + 14, w - 28 - 42 - 8, 22};
  paint_slider(p, t, volume_track_, volume_, 0, 100);
  ui_.add_hit_window(this, volume_track_, VolumeSlider);
}

std::optional<HoverTip::Spec> RoomScreen::tip_for_hover() const {
  if (ui_.hover_owner != this) return std::nullopt;
  const int id = ui_.hover_id;
  HoverTip::Spec spec;
  if (id == Leave && controls_shown_) {
    spec.text = "구름에서 떠나기";
    spec.anchor = fab_rects_[0];
    spec.place = HoverTip::Place::Left;
  } else if (id == Mute && controls_shown_ && !mute_popup_open_) {
    spec.text = muted_ ? "음소거 해제" : "음소거";
    spec.anchor = fab_rects_[1];
  } else if (id == More && controls_shown_ && !options_open_) {
    spec.text = "더보기";
    spec.anchor = fab_rects_[2];
    spec.place = HoverTip::Place::Right;
  } else if (id == OptCode && options_open_ && hint_muted_until_ == 0) {
    spec.text = "클릭해 복사";
    spec.anchor = code_rect_;
    spec.place = HoverTip::Place::Right;
  } else if ((id == OptPublic || id == OptChat) && options_open_) {
    const Rect sw = id == OptPublic ? public_rect_ : chat_rect_;
    spec.place = HoverTip::Place::Right;
    if (!is_host()) {
      // Above the whole row (.host-tip).
      spec.text = "호스트만 조작할 수 있습니다.";
      spec.size = 15;
      spec.anchor = {sw.x, sw.y - 7, sw.w, sw.h + 14};
      return spec;
    }
    // An off switch has nothing to pin (.switch:has(input:not(:checked)) > .pin-tip).
    if (!(id == OptPublic ? public_ : chat_)) return std::nullopt;
    spec.anchor = sw;
    if (blocked_id_ == id) {
      spec.text = blocked_text_;
      if (shaking()) {
        spec.dx = keyframes(static_cast<float>(ui_.now_ms - std::min(ui_.now_ms, blocked_at_)) / tip_shake_ms, tip_shake);
      }
    } else if (id == OptChat && chat_locked()) {
      spec.text = chat_tip_locked;
    } else {
      spec.text = ui_.prefs->get(id == OptPublic ? pref::pin_public : pref::pin_chat)
                      ? "우클릭해 기본값 고정을 해제합니다."
                      : "우클릭해 활성화 상태를\n기본값으로 고정합니다.";
    }
  } else if (id >= MemberBase) {
    const auto slot = static_cast<std::uint32_t>(id - MemberBase);
    const auto member = std::ranges::find(members_, slot, &MemberView::slot);
    const auto rect = std::ranges::find(member_rects_, slot, [](const auto& r) { return r.first; });
    if (member == members_.end() || rect == member_rects_.end()) return std::nullopt;
    if (host_ && *host_ == slot) spec.text = "이 구름의 호스트에요.";
    if (member->offline) spec.text += spec.text.empty() ? "오프라인" : " · 오프라인";
    if (spec.text.empty()) return std::nullopt;
    // .member > .tip: 6 from the left, its bottom 2 inside the row.
    spec.anchor = {rect->second.x + 6, rect->second.y, rect->second.w, rect->second.h};
    spec.place = HoverTip::Place::Left;
    spec.size = 15;
    spec.gap = -2;
  } else {
    return std::nullopt;
  }
  return spec;
}

void RoomScreen::paint_tips(Painter& p) {
  // A refusal stays on the switch until the pointer leaves it.
  if (blocked_id_ >= 0 && !ui_.hot(this, blocked_id_)) blocked_id_ = -1;
  const auto want = tip_for_hover();
  tip_.paint(ui_, p, want ? &*want : nullptr);
  HoverTip::Spec copied;
  const bool copying = options_open_ && copy_until_ != 0;
  if (copying) {
    copied.text = copy_text_;
    copied.anchor = code_rect_;
    copied.place = HoverTip::Place::Right;
    copied.out_ms = 350;
  }
  copy_tip_.paint(ui_, p, copying ? &copied : nullptr);
}

void RoomScreen::set_volume_from(float x) {
  volume_ = slider_value(volume_track_, x, 0, 100);
  if (on_volume) on_volume(volume_);
}

void RoomScreen::copy_code() {
  const bool copied = on_copy && on_copy(room_code_);
  copy_text_ = copied ? "복사 완료 !" : "복사 실패";
  const auto now = GetTickCount64();
  copy_until_ = now + 1000;
  // Right after copying the hint stays away, even under the cursor.
  hint_muted_until_ = now + 5000;
}

void RoomScreen::ask(Dialog dialog) {
  dialog_open_ = true;
  dialog.on_confirm = [this, then = std::move(dialog.on_confirm)] {
    dialog_open_ = false;
    if (then) then();
  };
  dialog.on_cancel = [this, then = std::move(dialog.on_cancel)] {
    dialog_open_ = false;
    if (then) then();
  };
  ui_.dialogs->show(std::move(dialog));
}

void RoomScreen::ask_leave() {
  options_open_ = false;
  // A leaving Host hands the Room to someone, if anyone else is there.
  const bool others = std::ranges::any_of(members_, [&](const MemberView& m) { return m.slot != me_; });
  const bool delegates = is_host() && others;
  ask({delegates ? "이 구름에서 떠날까요?\n호스트를 무작위로 다른 사람에게 위임합니다." : "이 구름에서 떠날까요?", "떠나기", "취소",
       delegates,
       [this] {
         if (on_leave) on_leave();
       },
       nullptr});
}

// The public and chat notices show once each. The first answer to either brings the
// tip about pinning, once.
void RoomScreen::notice_answered(pref::Bool seen) {
  ui_.prefs->set(seen, true);
  if (ui_.prefs->get(pref::pin_tip_seen)) return;
  ui_.prefs->set(pref::pin_tip_seen, true);
  ask({"공개 구름 및 채팅 토글 스위치를\n우클릭하여 구름을 띄울 때\n기본값으로 고정할 수 있습니다.", "확인", "", false, nullptr, nullptr});
}

void RoomScreen::block_switch(int id, const char* text) {
  blocked_id_ = id;
  blocked_text_ = text;
  blocked_at_ = GetTickCount64();
}

void RoomScreen::toggle_public() {
  if (!is_host()) return;
  if (public_ && ui_.prefs->get(pref::pin_public)) {
    block_switch(OptPublic, pin_tip_blocked);
    return;
  }
  if (!public_ && !ui_.prefs->get(pref::public_notice_seen)) {
    ask({"이 구름을 공개 구름으로 전환합니다.\n메인 메뉴에서 누구나 이 구름에\n코드 없이 참여할 수 있습니다.", "전환", "취소", false,
         [this] {
           notice_answered(pref::public_notice_seen);
           if (on_set_public) on_set_public(true);
         },
         [this] { notice_answered(pref::public_notice_seen); }});
    return;
  }
  if (on_set_public) on_set_public(!public_);
}

void RoomScreen::toggle_chat() {
  if (!is_host()) return;
  if (chat_locked()) {
    block_switch(OptChat, chat_tip_locked);
    return;
  }
  if (chat_ && ui_.prefs->get(pref::pin_chat)) {
    block_switch(OptChat, pin_tip_blocked);
    return;
  }
  if (!chat_ && !ui_.prefs->get(pref::chat_notice_seen)) {
    ask({"이 구름에서 채팅 기능을 활성화하려 합니다.\n서버는 채팅 기록을 기록하지 않습니다.\n채팅 기능 활성화 후 5분이 지나면\n다시 비활성화 할 수 없습니다.",
         "활성화", "취소", true,
         [this] {
           notice_answered(pref::chat_notice_seen);
           if (on_set_chat) on_set_chat(true);
         },
         [this] { notice_answered(pref::chat_notice_seen); }});
    return;
  }
  if (on_set_chat) on_set_chat(!chat_);
}

// Pinned, a new Room starts with this on. Off is the default, so only an on switch
// can be pinned; anyone can pin, and any pin can be removed.
void RoomScreen::toggle_pin(pref::Bool key, bool on) {
  const bool pinned = ui_.prefs->get(key);
  if (!pinned && !on) return;
  ui_.prefs->set(key, !pinned);
}

void RoomScreen::before_click(const Surface* owner, int id) {
  menu_.before_click(owner, id);
  // Popups close when anything outside them is clicked, except a notice's buttons,
  // which leave the switches in view.
  const bool own = owner == this;
  const bool in_options = own && (id == Popup || id == OptCode || id == OptPublic || id == OptChat || id == More);
  const bool in_mute = own && (id == Popup || id == VolumeSlider || id == Mute);
  if (options_open_ && !in_options && owner != ui_.dialogs) options_open_ = false;
  if (mute_popup_open_ && !in_mute) mute_popup_open_ = false;
}

void RoomScreen::click(int id, float x, float, bool) {
  switch (id) {
    case Leave:
      ask_leave();
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
      copy_code();
      break;
    case OptPublic:
      toggle_public();
      break;
    case OptChat:
      toggle_chat();
      break;
    case VolumeSlider:
      ui_.capture = this;
      set_volume_from(x);
      break;
    default:
      if (id >= MemberBase) profile_.request(static_cast<std::uint32_t>(id - MemberBase));
      break;
  }
}

bool RoomScreen::right_click(int id, float x, float y) {
  if (id >= MemberBase) {
    const auto slot = static_cast<std::uint32_t>(id - MemberBase);
    const auto member = std::ranges::find(members_, slot, &MemberView::slot);
    if (member == members_.end()) return false;
    options_open_ = false;
    menu_.open(slot, slot == me_, member_volume(member->cid), x, y);
    return true;
  }
  if (id == OptPublic || id == OptChat) {
    toggle_pin(id == OptPublic ? pref::pin_public : pref::pin_chat, id == OptPublic ? public_ : chat_);
    return true;
  }
  // Right-clicking the speaker opens the volume popup (main.js contextmenu).
  if (id != Mute) return false;
  mute_popup_open_ = !mute_popup_open_;
  options_open_ = false;
  return true;
}

void RoomScreen::drag(float x, float) { set_volume_from(x); }

bool RoomScreen::escape() {
  if (menu_.escape()) return true;
  if (mute_popup_open_) {
    mute_popup_open_ = false;
    return true;
  }
  return profile_.escape();
}

bool RoomScreen::wheel(float x, float y, float delta) {
  if (profile_.is_open()) return true;  // modal
  if (chat_ && chat_panel_.wheel(x, y, delta)) return true;
  if (!members_area_.contains(x, y) || members_scroll_.max_offset() <= 0) return false;
  members_scroll_.wheel(delta);
  return true;
}

Cursor RoomScreen::cursor(int id) const { return id == Popup ? Cursor::Arrow : Cursor::Hand; }

}  // namespace crpp::ui
