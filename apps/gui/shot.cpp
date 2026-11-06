// --shot: renders one scene offscreen, without a window, a store or a connection.
#include <cstdio>
#include <cstdlib>

#include "areas.hpp"
#include "crpp/ui/png.hpp"
#include "shell.hpp"

using namespace crpp::ui;

namespace crpp::gui {

namespace {

bool apply_scene(Shell& s, std::string_view name) {
  return area::connection::scene(s, name) || area::home::scene(s, name) || area::room::scene(s, name) ||
         area::sound::scene(s, name) || area::preferences::scene(s, name) || area::os::scene(s, name) ||
         area::account::scene(s, name) || area::update::scene(s, name);
}

}  // namespace

int run_shot(Shell& s, int argc, char** argv, float dpi) {
  unsigned w = 951, h = 692;
  if (argc >= 4) std::sscanf(argv[3], "%ux%u", &w, &h);
  if (argc >= 5) s.app.home().set_hour(std::atoi(argv[4]));
  s.app.home().set_nickname("크롬이");
  if (argc >= 6) (void)apply_scene(s, argv[5]);
  if (std::string(argv[argc - 1]) == "notice") {
    s.app.notice().set(Notice{"기존 Cloudroom의 설정 파일을 함께 쓰고 있어요. 이 파일에는 로그인 정보가 암호화되지 않은 채 "
                              "저장돼요.",
                              "안전한 저장소로 옮기기", "닫기"});
  }
  return save_png(s.app, widen(argv[2]), w, h, dpi) ? 0 : 1;
}

}  // namespace crpp::gui
