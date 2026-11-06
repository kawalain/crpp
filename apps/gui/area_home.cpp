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

void wire(Shell& s) {
  Shell* sp = &s;
  HomeScreen& home = s.app.home();
  home.busy = [sp] { return !sp->bridge->idle(); };
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
}

bool scene(Shell& s, std::string_view name) {
  if (name == "toast") {
    s.app.toasts().toast("서버 연결이 종료되었습니다.");
    return true;
  }
  if (name == "dialog") {
    s.app.dialogs().show({"설정에서 변경한 모든 값을\n초기화 할까요?", "초기화", "취소", false, nullptr, nullptr});
    return true;
  }
  if (name == "home-bubble") {  // the real path of a code that is too short
    s.app.home().click(HomeScreen::Join, 0, 0, false);
    return true;
  }
  if (name == "home-bubble-find") {
    s.app.home().click(HomeScreen::Find, 0, 0, false);
    s.app.home().show_error("공개된 구름을 찾을 수 없어요.");
    return true;
  }
  return name == "home";
}

}  // namespace crpp::gui::area::home
