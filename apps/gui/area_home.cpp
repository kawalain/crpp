#include "areas.hpp"
#include "shell.hpp"

using namespace crpp::ui;

namespace crpp::gui::area::home {

void wire(Shell& s) {
  Shell* sp = &s;
  s.app.home().on_create = [sp] {
    const Prefs& prefs = *sp->app.ui().prefs;
    sp->client->create(prefs.get(pref::pin_public), prefs.get(pref::pin_chat));
  };
  s.app.home().on_join = [sp](std::string code) {
    if (const auto seat = sp->store->find_seat(code, unix_ms())) {
      sp->bridge->expect_resume(code);
      sp->client->resume(code, seat->key, std::nullopt, true);
    } else {
      sp->client->join(code);
    }
  };
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
  return name == "home";
}

}  // namespace crpp::gui::area::home
