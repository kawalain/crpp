#include "crpp/ui/emoji_picker.hpp"
#include "crpp/ui/settings_tabs.hpp"

namespace crpp::ui {

namespace {

// .settings-inner children, from its top-left.
constexpr float bottom_pad = 25;  // SETTINGS_BOTTOM_PAD
constexpr Rect logout_rect{550 - 16 - 154, 440, 154, 38};

}  // namespace

void AccountTab::paint(Painter& p, Rect area) {
  const Theme& t = ui_.theme;
  p.text("계정", {area.x + 9, area.y + 25, 400, 32}, {.size = 26, .weight = 700, .line_height = 32}, t.hl_text);

  // .profile: the icon and the names beside it.
  const Rect icon{area.x + 16, area.y + 76, 72, 72};
  const bool icon_hot = ui_.hot(this, Emoji);
  const float cx = icon.x + 36, cy = icon.y + 36;
  if (icon_hot) p.push(scale_about(cx, cy, 1.05f));
  p.fill_ellipse(cx, cy, 36, 36, icon_hot ? t.hl_pale2 : t.hl_pale);
  draw_svg(p, ui_.emoji_image(profile_.emoji), {cx - 23, cy - 23, 46, 46});
  if (icon_hot) p.pop();
  ui_.add_hit(this, icon, Emoji);
  if (icon_hot) {  // .tip
    constexpr float tip_w = 112, tip_h = 32;
    const Rect r{cx - tip_w / 2, icon.y - 8 - tip_h, tip_w, tip_h};
    p.fill_round(r, 11, t.hl);
    p.text("아이콘 변경", {r.x, r.y + 5, r.w, 22}, {.size = 16, .weight = 700, .line_height = 22, .align = Align::Center}, t.on_accent);
  }

  const float names_x = icon.right() + 18;
  const float names_w = area.x + 550 - 16 - names_x;
  if (!profile_.nickname.empty() && !profile_.handle.empty()) {
    const float top = cy - (30 + 2 + 20) / 2.0f;
    p.text(profile_.nickname, {names_x, top, names_w, 30}, {.size = 24, .weight = 700, .line_height = 30, .ellipsis = true}, t.hl_text);
    p.text("@" + profile_.handle, {names_x, top + 32, names_w, 20}, {.size = 16, .line_height = 20, .ellipsis = true}, t.hl_text_soft);
  } else {
    p.text(profile_.display, {names_x, cy - 17, names_w, 34}, {.size = 28, .weight = 700, .line_height = 34, .ellipsis = true}, t.hl_text);
  }

  const auto button = [&](Rect r, std::string_view label, int id) {
    const bool hot = ui_.hot(this, id);
    if (hot) p.push(scale_about(r.x + r.w / 2, r.y + r.h / 2, 1.03f));
    p.fill_round(r, 8, hot ? t.hl_dark : t.hl);
    p.text(label, r, {.size = 17, .line_height = r.h, .align = Align::Center}, t.on_accent);
    if (hot) p.pop();
    ui_.add_hit(this, r, id);
  };
  // .account-buttons
  button({area.x + 16, area.y + 165, 154, 38}, "별명 변경", Nickname);
  if (profile_.has_account) button({area.x + 16 + 154 + 12, area.y + 165, 154, 38}, "사용자명 변경", Handle);
  button({area.x + logout_rect.x, area.y + logout_rect.y, logout_rect.w, logout_rect.h}, "로그아웃", Logout);
}

float AccountTab::content_height() const { return logout_rect.bottom() + bottom_pad; }

void AccountTab::click(int id, float, float, bool) {
  switch (id) {
    case Emoji:
      ui_.emoji_picker->show(profile_.emoji, [this](std::string picked) {
        if (on_emoji) on_emoji(std::move(picked));
      });
      break;
    case Nickname:
      if (on_change_nickname) on_change_nickname();
      break;
    case Handle:
      if (on_change_handle) on_change_handle();
      break;
    case Logout:
      if (on_logout) on_logout();
      break;
    default:
      break;
  }
}

}  // namespace crpp::ui
