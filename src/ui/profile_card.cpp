#include "crpp/ui/profile_card.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/skin_canvas.hpp"
#include "crpp/ui/toasts.hpp"

namespace crpp::ui {

namespace {

enum TextId : int { Nick, Handle, Joined, Stayed, Inputs };
enum NumberId : int { Mine, HasHandle, HasJoined };
enum ReportId : int { Width, Height };
constexpr skin::Name text_names[] = {{"nick", Nick}, {"handle", Handle}, {"joined", Joined}, {"stayed", Stayed}, {"inputs", Inputs}};
constexpr skin::Name number_names[] = {{"mine", Mine}, {"has_handle", HasHandle}, {"has_joined", HasJoined}};
constexpr skin::Name picture_names[] = {{"icon", 0}};
constexpr skin::Name hit_names[] = {{"backdrop", ProfileCard::Backdrop}, {"card", ProfileCard::Card}, {"icon", ProfileCard::Icon}};
constexpr skin::Name report_names[] = {{"width", Width}, {"height", Height}};
constexpr skin::Name note_names[] = {{"icon", 0}};


std::string format_date(std::int64_t seconds) {
  const auto t = static_cast<std::time_t>(seconds);
  std::tm tm{};
  localtime_s(&tm, &t);
  char date[32];
  std::snprintf(date, sizeof date, "%d.%02d.%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
  return date;
}

}  // namespace

skin::ScreenSchema ProfileCard::schema() {
  return {.screen = "profile", .texts = text_names, .numbers = number_names, .pictures = picture_names, .hits = hit_names, .reports = report_names, .notes = note_names};
}

namespace {

class ShownProfile final : public skin::Source {
 public:
  ShownProfile(const ProfileView& profile, bool mine, Ui& ui, Painter* painter, Rect* icon) : v_(profile), mine_(mine), ui_(ui), p_(painter), icon_(icon) {
    nick_ = v_.nick.empty() ? std::string(tr().guest) : v_.nick;
    if (v_.handle) handle_ = "@" + *v_.handle;
    if (v_.created) joined_ = locale::fill(tr().profile_joined, {format_date(*v_.created)});
    stayed_ = locale::fill(tr().profile_stayed, {locale::format_stay(v_.stayed)});
    inputs_ = locale::fill(tr().profile_inputs, {locale::format_times(v_.events)});
  }

  std::string_view text(int id, int) override {
    switch (id) {
      case Nick: return nick_;
      case Handle: return handle_;
      case Joined: return joined_;
      case Stayed: return stayed_;
      default: return inputs_;
    }
  }
  float number(int id, int) override {
    switch (id) {
      case Mine: return mine_ ? 1.0f : 0.0f;
      case HasHandle: return v_.handle && nick_ != handle_ ? 1.0f : 0.0f;
      default: return v_.created ? 1.0f : 0.0f;
    }
  }
  void picture(int, int, skin::Box box, float opacity, float gray) override {
    if (p_) draw_svg(*p_, ui_.emoji_image(v_.emoji), rect_of(box), opacity, gray);
  }
  void report(int id, float value) override { (id == Width ? size.width : size.height) = value; }
  void note(int, int, skin::Box box) override {
    if (icon_) *icon_ = rect_of(box);
  }

  D2D1_SIZE_F size{};

 private:
  const ProfileView& v_;
  bool mine_;
  Ui& ui_;
  Painter* p_;
  Rect* icon_;
  std::string nick_, handle_, joined_, stayed_, inputs_;
};

}  // namespace

void ProfileCard::request(std::uint32_t slot) {
  pending_ = slot;
  give_up_at_ = clock_ms() + answer_timeout_ms;
  if (on_request) on_request(slot);
}

void ProfileCard::answer(ProfileView profile, bool mine) {
  const bool awaited = pending_ && *pending_ == profile.slot;
  const bool refresh = open_ && profile_.slot == profile.slot;
  if (!awaited && !refresh) return;
  if (awaited) pending_.reset();
  profile_ = std::move(profile);
  mine_ = mine;
  if (refresh) return;  // only the numbers move
  open_ = true;
  opened_at_ = clock_ms();
  was_animating_ = true;
  refresh_at_ = opened_at_ + refresh_ms;
  ui_.set_focus(nullptr, nullptr);
}

void ProfileCard::close() {
  open_ = false;
  pending_.reset();
  was_animating_ = false;
}

D2D1_SIZE_F ProfileCard::content_size() const {
  const skin::Screen* screen = ui_.pack_screen("profile");
  if (!screen) return {0, 0};
  ShownProfile shown(profile_, mine_, ui_, nullptr, nullptr);
  measure_screen(ui_, *screen, ui_.content, shown);
  return shown.size;
}

void ProfileCard::paint(Painter& p) {
  const skin::Screen* screen = ui_.pack_screen("profile");
  if (!open_ || !screen) return;
  p.set_base(D2D1::Matrix3x2F::Identity());
  Rect icon;
  ShownProfile shown(profile_, mine_, ui_, &p, &icon);
  draw_layer(ui_, p, this, *screen, ui_.content, shown);
  HoverTip::Spec spec;
  spec.text = tr().profile_change_icon;
  spec.anchor = icon;
  tip_.paint(ui_, p, mine_ && ui_.hot(this, Icon) ? &spec : nullptr);
}

void ProfileCard::click(int id, float, float, bool) {
  if (id == Backdrop) close();
  else if (id == Icon && mine_ && on_change_icon) on_change_icon();
}

bool ProfileCard::escape() {
  if (!open_) return false;
  close();
  return true;
}

bool ProfileCard::animating() const { return open_ && tip_.animating(ui_); }

bool ProfileCard::tick(unsigned long long now_ms) {
  bool changed = open_ && tip_.animating(ui_);
  if (was_animating_) {  // the card's own transition asks for its frames from the one this brings
    changed = true;
    was_animating_ = false;
  }
  if (pending_ && now_ms >= give_up_at_) {
    pending_.reset();
    if (ui_.toasts) ui_.toasts->toast(tr().profile_load_failed);
    changed = true;
  }
  if (open_ && now_ms >= refresh_at_) {
    refresh_at_ = now_ms + refresh_ms;
    if (on_request) on_request(profile_.slot);
  }
  return changed;
}

unsigned long long ProfileCard::wake_at() const {
  unsigned long long at = pending_ ? give_up_at_ : 0;
  if (open_ && (at == 0 || refresh_at_ < at)) at = refresh_at_;
  return at;
}

}  // namespace crpp::ui
