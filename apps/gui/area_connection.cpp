#include "areas.hpp"
#include "shell.hpp"

using namespace crpp::ui;

namespace crpp::gui::area::connection {

void wire(Shell& s) {
  Shell* sp = &s;
  s.bridge->on_room_changed = [sp](bool in_room) {
    if (in_room) (void)sp->input->start();
    else sp->input->stop();
  };
  // There is no sign-in screen yet, so continue as a guest; the token stays saved.
  s.bridge->on_unauthorized = [sp] {
    if (sp->config.token.empty()) return;
    sp->config.token.clear();
    sp->input->stop();  // it calls into the client being replaced
    sp->client->stop();
    start_client(*sp);
    sp->app.notice().on_action = nullptr;
    sp->app.notice().set(Notice{"로그인이 만료되어 게스트로 접속해요. 다시 시도해 주세요.", "", "닫기"});
    if (!sp->smoke_path.empty()) sp->client->create(false, false);
  };
}

bool scene(Shell& s, std::string_view name) {
  if (name == "setup") {
    s.app.show_setup(42);
    return true;
  }
  if (name == "setup-failed") {
    s.app.show_setup(0, "파일을 받지 못했어요. 인터넷 연결을 확인해 주세요.");
    return true;
  }
  return false;
}

}  // namespace crpp::gui::area::connection
