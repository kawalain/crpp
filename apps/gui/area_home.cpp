#include "areas.hpp"
#include "crpp/ui/home_logic.hpp"
#include "shell.hpp"

using namespace crpp::ui;

namespace crpp::gui::area::home {

namespace {

void enter_room(Shell& s, const ui::home::Entry& entry) {
  using Kind = ui::home::Entry::Kind;
  s.bridge->entering();
  switch (entry.kind) {
    case Kind::Random:
      s.client->join_random();
      break;
    case Kind::Join:
      s.client->join(entry.code);
      break;
    case Kind::Resume:
      s.bridge->expect_resume(entry.code);
      s.client->resume(entry.code, entry.key, std::nullopt, true);
      break;
  }
}

}  // namespace

void read_clock(Shell& s) {
  static int last = -1;
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  if (localtime_s(&local, &now) != 0) return;
  const int key = (local.tm_hour * 100 + local.tm_mon + 1) * 100 + local.tm_mday;
  if (key == last) return;
  last = key;
  s.app.home().set_hour(local.tm_hour);
  s.app.home().set_date(local.tm_mon + 1, local.tm_mday);
  if (s.hwnd && s.app.base_view() == View::Home) InvalidateRect(s.hwnd, nullptr, FALSE);
}

void wire(Shell& s) {
  Shell* sp = &s;
  HomeScreen& home = s.app.home();
  home.busy = [sp] { return !sp->bridge->idle(); };
  home.on_shown = [sp] {
    read_clock(*sp);
    area::connection::count_public_rooms(*sp);
  };
  home.on_create = [sp] {
    const Prefs& prefs = *sp->app.ui().prefs;
    sp->bridge->entering();
    sp->client->create(prefs.get(pref::pin_public), prefs.get(pref::pin_chat));
  };
  home.on_join = [sp](std::string code) {
    const auto seat = sp->store->find_seat(code, unix_ms());
    enter_room(*sp, ui::home::entry_for_code(std::move(code), seat ? std::optional(seat->key) : std::nullopt));
  };
  home.on_find_public = [sp] { enter_room(*sp, {}); };
  home.on_return = [sp] {
    const auto seat = sp->store->latest_seat(unix_ms());
    enter_room(*sp, ui::home::entry_for_return(seat ? std::optional(ui::home::HeldSeat{seat->code, seat->key}) : std::nullopt));
  };
  home.held_seats = [sp] {
    const auto now = unix_ms();
    std::vector<ui::HeldSeatShown> shown;
    for (store::SeatReturn& seat : sp->store->held_seats(now)) {
      shown.push_back({std::move(seat.code), static_cast<std::uint64_t>(std::max<std::int64_t>(0, seat.expires_at - now))});
    }
    return shown;
  };
  home.my_emoji = [sp] { return sp->store->string_or("emoji", ""); };
  home.held_seat_ms = [sp]() -> std::optional<std::uint64_t> {
    const auto now = unix_ms();
    const auto seat = sp->store->latest_seat(now);
    if (!seat) return std::nullopt;
    return static_cast<std::uint64_t>(seat->expires_at - now);
  };
}

namespace {

// A Seat that lasts past any screenshot.
void hold_a_seat(Shell& s) {
  s.app.home().held_seat_ms = []() -> std::optional<std::uint64_t> { return 60000; };
  s.app.home().held_seats = [] { return std::vector<ui::HeldSeatShown>{{"ABC234", 60000}, {"XYZ789", 14 * 60000 - 2000}}; };
}

}  // namespace

bool scene(Shell& s, std::string_view name) {
  // "home-return@dark": any of this area's scenes in another theme.
  if (const auto at = name.find('@'); at != std::string_view::npos && name.starts_with("home")) {
    s.app.set_theme(name.substr(at + 1) == "dark" ? "darkcloud" : "pastel");
    name = name.substr(0, at);
  }
  if (name == "home-hover") {
    s.app.ui().hover_owner = &s.app.home();
    s.app.ui().hover_id = HomeScreen::Find;
    return true;
  }
  if (name == "home-hover-enter") {
    s.app.ui().hover_owner = &s.app.home();
    s.app.ui().hover_id = HomeScreen::Join;
    return true;
  }
  if (name == "home-hover-droplet") {
    s.app.ui().hover_owner = &s.app.settings();
    s.app.ui().hover_id = SettingsPanel::Droplet;
    return true;
  }
  if (name == "home-winter" || name == "home-spring" || name == "home-autumn") {  // what falls in each season
    s.app.home().set_date(name == "home-winter" ? 1 : name == "home-spring" ? 4 : 10, 15);
    return true;
  }
  if (name == "home-christmas" || name == "home-birthday") {  // a day with a greeting of its own
    if (name == "home-christmas") s.app.home().set_date(12, 25);
    else s.app.home().set_date(ui::home::birthday_month, ui::home::birthday_day);
    return true;
  }
  if (name == "home-public" || name == "home-public-none") {  // as /health answered
    s.app.home().set_public_rooms(name == "home-public" ? 3 : 0);
    return true;
  }
  if (name == "home-returning") {
    hold_a_seat(s);
    return true;
  }
  if (name == "home-return") {
    hold_a_seat(s);
    s.app.home().click(HomeScreen::Find, 0, 0, false);
    return true;
  }
  if (name == "toast") {
    s.app.toasts().toast(tr().lost_server);
    return true;
  }
  if (name == "notice") {
    s.app.notice().set(Notice{tr().notice_shared, tr().notice_move, tr().notice_close});
    return true;
  }
  if (name == "dialog") {
    s.app.dialogs().show({tr().system_reset_question, tr().system_reset_confirm, tr().cancel, false, nullptr, nullptr});
    return true;
  }
  if (name == "menu-open") {  // under a Skin with a menu bar
    s.app.frame().set_in_room(true);
    s.app.frame().click(WindowFrame::MenuBase, 0, 0, false);
    return true;
  }
  if (name == "menu-keys" || name == "menu-keys-open") {  // Alt alone, and then Right and Down
    s.app.sys_key(VK_MENU, false);
    s.app.sys_key_up(VK_MENU);
    if (name == "menu-keys-open") {
      s.app.key_down(VK_RIGHT, false, false);
      s.app.key_down(VK_DOWN, false, false);
    }
    return true;
  }
  if (name == "home-bubble") {  // the real path of a code that is too short
    s.app.home().click(HomeScreen::Join, 0, 0, false);
    return true;
  }
  if (name == "home-bubble-find") {
    s.app.home().click(HomeScreen::Find, 0, 0, false);
    s.app.home().show_error(tr().join_no_public);
    return true;
  }
  return name == "home";
}

}  // namespace crpp::gui::area::home
