// --shot: renders one scene offscreen, without a window, a store or a connection.
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>

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

// A moment of a motion, to compare by eye: a scene is set up and painted, something
// happens, and the image shows it `at` ms later.
struct Motion {
  std::function<void(Shell&)> before, happens;
};

void room_with_two(Shell& s) {
  s.app.show_session("ABC234");
  s.app.room().set_room({{0, "크롬이", "", false}, {1, "구름이", "", false}}, 0, 0, false, true);
  s.app.room().chat().add({0, "c0", "크롬이", "", "안녕하세요", 1700000000000});
  s.app.room().chat().add({1, "c1", "구름이", "", "반가워요", 1700000060000});
}

const Motion* motion_named(std::string_view name) {
  static const std::map<std::string_view, Motion> motions{
      {"motion-enter-room", {[](Shell&) {}, room_with_two}},
      {"motion-leave-room", {room_with_two, [](Shell& s) { s.app.show_home(); }}},
      {"motion-settings-open", {[](Shell&) {}, [](Shell& s) { s.app.settings().open(SettingsPage::Sound); }}},
      {"motion-settings-close", {[](Shell& s) { s.app.settings().open(SettingsPage::Sound); }, [](Shell& s) { s.app.settings().close(); }}},
      {"motion-tab", {[](Shell& s) { s.app.settings().open(SettingsPage::Sound); }, [](Shell& s) { s.app.settings().open(SettingsPage::Display); }}},
      {"motion-tab-scrolled",
       {[](Shell& s) {
          s.app.settings().open(SettingsPage::Sound);
          s.app.settings().scroll_to(150);
        },
        [](Shell& s) { s.app.settings().select(SettingsPage::Display); }}},
      {"motion-message",
       {room_with_two, [](Shell& s) { s.app.room().chat().add({1, "c1", "구름이", "", "새 메시지가 올라와요", 1700000120000}); }}},
      {"motion-member",
       {room_with_two,
        [](Shell& s) {
          s.app.room().set_room({{0, "크롬이", "", false}, {1, "구름이", "", false}, {2, "비구름", "", false}}, 0, 0, false, true);
        }}},
      {"motion-scroll", {[](Shell& s) { s.app.settings().open(SettingsPage::Sound); }, [](Shell& s) { s.app.wheel(600, 400, -1); }}},
      {"motion-stretch", {[](Shell& s) { s.app.settings().open(SettingsPage::Sound); }, [](Shell& s) { s.app.wheel(600, 400, 5); }}},
  };
  const auto found = motions.find(name);
  return found == motions.end() ? nullptr : &found->second;
}

int run_motion(Shell& s, const Motion& motion, const std::wstring& path, unsigned w, unsigned h, float dpi, int at) {
  constexpr unsigned long long start = 1000000;
  s.app.ui().animations = true;
  hold_clock_at(start);
  motion.before(s);
  // Twice: what starts in the first frame has run its course before the second.
  if (!save_png(s.app, path, w, h, dpi)) return 1;
  hold_clock_at(start + 10000);
  if (!save_png(s.app, path, w, h, dpi)) return 1;
  motion.happens(s);
  if (!save_png(s.app, path, w, h, dpi)) return 1;  // hovers and fades take their starting time here
  hold_clock_at(start + 10000 + static_cast<unsigned long long>(at));
  return save_png(s.app, path, w, h, dpi) ? 0 : 1;
}

}  // namespace

int run_shot(Shell& s, int argc, char** argv, float dpi) {
  s.app.ui().animations = false;
  unsigned w = 951, h = 692;
  if (argc >= 4) std::sscanf(argv[3], "%ux%u", &w, &h);
  if (argc >= 8 && std::string_view(argv[6]) == "--at") {
    if (const Motion* motion = motion_named(argv[5])) {
      if (argc >= 5) s.app.home().set_hour(std::atoi(argv[4]));
      s.app.home().set_nickname("크롬이");
      return run_motion(s, *motion, widen(argv[2]), w, h, dpi, std::atoi(argv[7]));
    }
  }
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
