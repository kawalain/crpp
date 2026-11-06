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
  s.bridge->on_unauthorized = [sp] { area::account::token_rejected(*sp); };
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
