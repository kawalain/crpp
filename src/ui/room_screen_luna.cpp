// A Room in the Luna Skin: who is here, a toolbar, the chat, and a column of pictures. It shares
// its state, hit ids and callbacks with the Cloud layout.
#include <algorithm>
#include <cmath>

#include "crpp/ui/emoji_picker.hpp"
#include "crpp/ui/luna.hpp"
#include "crpp/ui/room_screen.hpp"

namespace crpp::ui {

namespace {

constexpr float picture_size = 36, member_row_h = 62, member_column_w = 112;
constexpr const char* muted_picture = "1f507";

constexpr const char* speaker_shape =
    "M2.6,9 H5.6 L10.3,5.1 C10.9,4.6 11.8,5 11.8,5.8 V18.2 C11.8,19 10.9,19.4 10.3,18.9 L5.6,15 H2.6 C2,15 1.5,14.5 "
    "1.5,13.9 V10.1 C1.5,9.5 2,9 2.6,9 Z";
constexpr const char* speaker_wave = "M14.6,9.2 C15.6,10.9 15.6,13.1 14.6,14.8M17.6,6.6 C19.8,9.9 19.8,14.1 17.6,17.4";
constexpr const char* speaker_cross = "M15 9 21 15M21 9 15 15";

// An icon is a small picture, so it keeps its soft edges.
class SmoothEdges {
 public:
  explicit SmoothEdges(Painter& p) : rt_(p.rt()), mode_(rt_->GetAntialiasMode()) { rt_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE); }
  ~SmoothEdges() { rt_->SetAntialiasMode(mode_); }
  SmoothEdges(const SmoothEdges&) = delete;
  SmoothEdges& operator=(const SmoothEdges&) = delete;

 private:
  ID2D1RenderTarget* rt_;
  D2D1_ANTIALIAS_MODE mode_;
};

}  // namespace

Rect RoomScreen::luna_picture_of(Rect box) { return {std::floor(box.x + (box.w - picture_size) / 2), box.y + 5, picture_size, picture_size}; }

void RoomScreen::paint_luna(Painter& p) {
  const Theme& t = ui_.theme;
  const LunaColors& l = t.luna;
  needs_full_ = false;
  ended_area_ = {};
  const Rect c = ui_.content;
  p.set_base(D2D1::Matrix3x2F::Identity());
  p.fill_rect(c, l.face);

  // Who else is here.
  std::string names;
  for (const MemberView& m : members_) {
    if (m.slot == me_ && members_.size() > 1) continue;
    if (!names.empty()) names += ", ";
    names += m.nick.empty() ? std::string(tr().guest) : m.nick;
  }
  const Rect to{c.x, c.y, c.w, 22};
  p.fill_down(to, l.header);
  p.text(locale::fill(tr().room_contacts, {names}), {to.x + 8, to.y, to.w - 16, to.h}, luna::body(p, to.h), l.ink);

  // The toolbar.
  const Rect bar{c.x, to.bottom(), c.w, 30};
  p.fill_rect({bar.x, bar.y, bar.w, 1}, l.edge_light);
  p.fill_rect({bar.x, bar.bottom() - 1, bar.w, 1}, l.edge_dark);
  const bool muted = ui_.prefs->get(pref::mute_others);
  const auto tool_width = [&](std::string_view label) {
    DWRITE_TEXT_METRICS m{};
    if (const auto layout = ui_.gfx.layout(widen(label), luna::body(p, 24), 300)) layout->GetMetrics(&m);
    return std::ceil(m.widthIncludingTrailingWhitespace) + 32;
  };
  const char* mute_label = muted ? tr().unmute : tr().mute;
  const Rect mute{bar.x + 4, bar.y + 3, tool_width(mute_label), 24};
  luna::toolbar_button(p, t, mute, mute_label, ui_.hot(this, Mute), ui_.pressed(this, Mute), muted);
  {
    const SmoothEdges smooth(p);
    p.push(D2D1::Matrix3x2F::Scale(2 / 3.f, 2 / 3.f) * D2D1::Matrix3x2F::Translation(mute.x + 5, mute.y + 4));
    p.fill_geometry(ui_.gfx.path(speaker_shape), l.bar_arrow);
    if (muted) p.stroke_geometry(ui_.gfx.path(speaker_cross), t.error_text, 2.4f);
    else p.stroke_geometry(ui_.gfx.path(speaker_wave), l.bar_arrow, 2.1f);
    p.pop();
  }
  ui_.add_hit_window(this, mute, Mute);
  fab_rects_[1] = mute;
  const Rect more{mute.right() + 2, mute.y, tool_width(tr().room_options), 24};
  luna::toolbar_button(p, t, more, tr().room_options, ui_.hot(this, More), ui_.pressed(this, More), options_open_);
  for (const float dx : {0.0f, 5.0f, 10.0f}) p.fill_rect({more.x + 5 + dx, more.y + 11, 3, 3}, l.bar_arrow);
  ui_.add_hit_window(this, more, More);
  fab_rects_[2] = more;

  // The status line.
  const Rect status{c.x, c.bottom() - 20, c.w, 20};
  p.fill_rect({status.x, status.y, status.w, 1}, l.edge_dark);
  p.fill_rect({status.x, p.snap(status).y + p.line(), status.w, 1}, l.edge_light);
  p.text(std::string(tr().room_code) + ": " + room_code_, {status.x + 8, status.y + 2, status.w - 16, 18}, luna::body(p, 18), l.ink_soft);

  const Rect main{c.x + 6, bar.bottom() + 6, c.w - 12, status.y - bar.bottom() - 12};
  if (chat_) {
    const Rect members{main.right() - member_column_w, main.y, member_column_w, main.h};
    paint_members_luna(p, members, 1);
    chat_panel_.paint(p, {main.x, main.y, main.w - member_column_w - 6, main.h});
  } else {
    paint_members_luna(p, main, std::max(1, static_cast<int>((main.w - luna::scrollbar_w) / member_column_w)));
  }

  if (controls_shown_) {
    if (options_open_) paint_options_luna(p);
    if (mute_popup_open_) paint_mute_luna(p);
  }
  paint_tips(p);
}

// Hangs under its toolbar button: the code, then two boxes to tick.
void RoomScreen::paint_options_luna(Painter& p) {
  const Theme& t = ui_.theme;
  const LunaColors& l = t.luna;
  constexpr float row_h = 24, pad = 5, w = 190;
  const Rect r{std::min(fab_rects_[2].x, ui_.content.right() - w - 4), fab_rects_[2].bottom() + 1, w, 2 * pad + 3 * row_h + 5};
  ui_.skin().panel(p, t, r, PanelKind::Popup);
  ui_.add_hit_window(this, r, Popup);
  float y = r.y + pad;
  p.text(tr().room_code, {r.x + 10, y, 100, row_h}, luna::body(p, row_h), l.ink);
  code_rect_ = {r.right() - 10 - 70, y, 70, row_h};
  luna::link(p, t, code_rect_, room_code_, ui_.hot(this, OptCode));
  ui_.add_hit_window(this, code_rect_, OptCode);
  y += row_h + 2;
  p.fill_rect({r.x + 5, y, r.w - 10, 1}, l.edge_dark);
  p.fill_rect({r.x + 5, p.snap({r.x, y, 1, 1}).y + p.line(), r.w - 10, 1}, l.edge_light);
  y += 3;
  const auto row = [&](const char* label, bool on, bool off, bool pinned, int id) {
    const Rect line{r.x + 10, y, r.w - 20, row_h};
    const Rect box{line.x, line.y + 5, luna::check_size, luna::check_size};
    luna::check_box(p, t, box, on, ui_.hot(this, id), off);
    p.text(label, {line.x + 18, line.y, line.w - 30, line.h}, luna::body(p, line.h), off ? l.off_ink : l.ink);
    if (pinned) {  // kept for the next Room, as a small square at the end
      p.fill_rect({line.right() - 7, line.y + 9, 6, 6}, l.link);
    }
    ui_.add_hit_window(this, line, id);
    y += row_h;
    return line;
  };
  const bool host = is_host();
  public_rect_ = row(tr().room_public, public_, !host, ui_.prefs->get(pref::pin_public), OptPublic);
  chat_rect_ = row(tr().room_word_chat, chat_, !host || chat_locked(), ui_.prefs->get(pref::pin_chat), OptChat);
}

void RoomScreen::paint_mute_luna(Painter& p) {
  const Theme& t = ui_.theme;
  constexpr float w = 190, h = 33;
  const Rect r{fab_rects_[1].x, fab_rects_[1].bottom() + 1, w, h};
  ui_.skin().panel(p, t, r, PanelKind::Popup);
  ui_.add_hit_window(this, r, Popup);
  p.text(std::to_string(volume_) + "%", {r.x + 8, r.y, 34, h}, luna::body(p, h), t.luna.ink);
  volume_track_ = {r.x + 44, r.y + 5, w - 52, h - 10};
  paint_slider(p, t, volume_track_, volume_, 0, 100);
  ui_.add_hit_window(this, volume_track_, VolumeSlider);
}

void RoomScreen::paint_members_luna(Painter& p, Rect area, int cols) {
  const Theme& t = ui_.theme;
  const LunaColors& l = t.luna;
  luna::sunken(p, t, area);
  const Rect inner{area.x + 1, area.y + 1, area.w - 2, area.h - 2};
  const int n = static_cast<int>(members_.size());
  const int rows = (n + cols - 1) / cols;
  members_area_ = inner;
  members_scroll_.view_h = inner.h;
  members_scroll_.content_h = static_cast<float>(rows) * member_row_h;
  members_scroll_.clamp();
  members_scroll_.advance(ui_);
  const bool overflow = members_scroll_.max_offset() > 0;
  const float list_w = inner.w - (overflow ? luna::scrollbar_w : 0);
  const float col_w = std::floor(list_w / static_cast<float>(cols));

  member_rects_.clear();
  const bool muted_all = ui_.prefs->get(pref::mute_others);
  p.rt()->PushAxisAlignedClip(D2D1::RectF(inner.x, inner.y, inner.x + list_w, inner.bottom()), D2D1_ANTIALIAS_MODE_ALIASED);
  ui_.push_hit_clip({inner.x, inner.y, list_w, inner.h});
  for (int i = 0; i < n; ++i) {
    const MemberView& m = members_[static_cast<std::size_t>(i)];
    const Rect box{inner.x + static_cast<float>(i % cols) * col_w, inner.y + static_cast<float>(i / cols) * member_row_h - members_scroll_.drawn(), col_w, member_row_h};
    if (box.bottom() <= inner.y || box.y >= inner.bottom()) continue;
    const int id = MemberBase + static_cast<int>(m.slot);
    const bool hot = ui_.hot(this, id);
    if (hot) p.fill_rect(box, t.hl_pale);
    const Rect picture = luna_picture_of(box);
    const bool muted_here = m.slot != me_ && member_volume(m.cid) == 0;
    const float level = !muted_here && !muted_all ? speaking_level(m.slot) : 0;
    // Only this much changes while someone types, so nothing else must depend on what is drawn under it.
    p.fill_rect({picture.x - 3, picture.y - 3, picture.w + 6, picture.h + 6}, hot ? t.hl_pale : l.field);
    luna::picture_frame(p, t, picture, level);
    draw_svg(p, ui_.emoji_image(muted_here ? muted_picture : m.emoji), {picture.x + 2, picture.y + 2, picture.w - 4, picture.h - 4}, m.offline ? 0.55f : 1.0f, muted_all ? 1.0f : 0.0f);
    p.text(m.nick.empty() ? std::string(tr().guest) : m.nick, {box.x + 3, picture.bottom() + 3, box.w - 6, 16}, luna::body(p, 16, Align::Center), m.offline ? l.ink_soft : l.ink);
    ui_.add_hit_window(this, box, id);
    member_rects_.emplace_back(m.slot, box);
  }
  ui_.pop_hit_clip();
  p.rt()->PopAxisAlignedClip();
  if (overflow) {
    const Rect bar{inner.right() - luna::scrollbar_w, inner.y, luna::scrollbar_w, inner.h};
    const auto parts = luna::scrollbar_parts(bar, members_scroll_.view_h, members_scroll_.content_h, members_scroll_.offset);
    members_track_ = parts.track;
    ui_.add_hit_window(this, bar, MembersBar);
    luna::scrollbar(p, t, parts, ui_.hot(this, MembersBar), false, false);
  }
}

}  // namespace crpp::ui
