// --shot: renders one scene offscreen, without a window, a store or a connection.
#include <functional>
#include <map>
#include <string_view>

#include "areas.hpp"
#include "crpp/ui/clipboard.hpp"
#include "crpp/ui/png.hpp"
#include "options.hpp"
#include "shell.hpp"

using namespace crpp::ui;

namespace crpp::gui {

namespace {

bool apply_scene(Shell& s, std::string_view name) {
  if (name == "cursors") {
    s.app.show_cursor_sheet();
    return true;
  }
  return area::connection::scene(s, name) || area::home::scene(s, name) || area::room::scene(s, name) ||
         area::sound::scene(s, name) || area::preferences::scene(s, name) || area::repositories::scene(s, name) || area::os::scene(s, name) ||
         area::account::scene(s, name) || area::update::scene(s, name) || area::achievements::scene(s, name);
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
      {"motion-snow", {[](Shell&) {}, [](Shell& s) { s.app.home().set_date(1, 15); }}},
      {"motion-splash", {[](Shell&) {}, [](Shell& s) { s.app.settings().splash(); }}},
      {"motion-scroll", {[](Shell& s) { s.app.settings().open(SettingsPage::Sound); }, [](Shell& s) { s.app.wheel(600, 400, -1); }}},
      {"motion-stretch", {[](Shell& s) { s.app.settings().open(SettingsPage::Sound); }, [](Shell& s) { s.app.wheel(600, 400, 5); }}},
  };
  const auto found = motions.find(name);
  return found == motions.end() ? nullptr : &found->second;
}

int run_motion(Shell& s, const Motion& motion, const std::wstring& path, unsigned w, unsigned h, float dpi, int at) {
  constexpr unsigned long long start = 1000000;
  s.app.set_animations(true);
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

// A shot holds the clipboard as it holds the clock: it always has text, and nothing leaves.
class HeldClipboard final : public Clipboard {
 public:
  bool set_text(std::wstring_view) override { return true; }
  std::optional<std::wstring> text() override { return L"text"; }
  bool has_text() override { return true; }
};

}  // namespace

int run_shot(Shell& s, int argc, char** argv, float dpi) {
  static HeldClipboard clipboard;
  s.app.ui().clipboard = &clipboard;
  s.app.set_animations(false);
  hold_wall_clock(1700000000);
  s.app.achievements().set_enabled(false);  // a scene reaches nothing, so no toast covers it
  unsigned w = 951, h = 692;
  if (argc >= 4) {  // <w>x<h>; anything else, as "auto", keeps the default
    const std::string_view size = argv[3];
    const std::size_t x = size.find('x');
    if (x != std::string_view::npos) {
      const unsigned width = number_or(size.substr(0, x), 0u), height = number_or(size.substr(x + 1), 0u);
      if (width > 0 && height > 0) w = width, h = height;
    }
  }
  if (argc >= 8 && std::string_view(argv[6]) == "--at") {
    if (const Motion* motion = motion_named(argv[5])) {
      if (argc >= 5) s.app.home().set_hour(number_or(std::string_view(argv[4]), 0));
      s.app.home().set_date(0, 0);
      s.app.home().set_nickname("크롬이");
      return run_motion(s, *motion, widen(argv[2]), w, h, dpi, number_or(std::string_view(argv[7]), 0));
    }
  }
  if (argc >= 5) s.app.home().set_hour(number_or(std::string_view(argv[4]), 0));
  s.app.home().set_date(0, 0);  // a shot holds the clock, so no day of its own unless a scene says so
  s.app.home().set_nickname("크롬이");
  const bool sized_by_screen = argc >= 4 && std::string_view(argv[3]) == "auto" && !s.app.screen_size().key.empty();
  if (sized_by_screen) {  // a layout of its own says where things are by drawing them
    const ScreenSize screen = s.app.screen_size();
    if (!save_png(s.app, widen(argv[2]), static_cast<unsigned>(screen.w), static_cast<unsigned>(screen.h), dpi)) return 1;
  }
  // focus-<scene>-<n>: the scene, and Tab pressed n times after its first frame.
  std::string_view scene = argc >= 6 ? std::string_view(argv[5]) : std::string_view();
  int tabs = 0;
  if (scene.starts_with("focus-") && scene.find_last_of('-') > 5) {
    const std::size_t dash = scene.find_last_of('-');
    tabs = number_or(scene.substr(dash + 1), 0);
    scene = scene.substr(6, dash - 6);
  }
  const auto press_tabs = [&] {
    for (int i = 0; i < tabs; ++i) {
      s.app.key_down(VK_TAB, false, false);
      s.app.char_input(L'\t', false);
    }
  };
  if (!scene.empty()) (void)apply_scene(s, scene);
  if (sized_by_screen) {
    const ScreenSize screen = s.app.screen_size();
    w = static_cast<unsigned>(screen.w);
    h = static_cast<unsigned>(screen.h);
  }
  if (std::string(argv[argc - 1]) == "notice") {
    s.app.notice().set(Notice{tr().notice_shared, tr().notice_move, tr().notice_close});
  }
  if (!save_png(s.app, widen(argv[2]), w, h, dpi)) return 1;
  if (!scene.empty() && area::room::after_frame(s, scene) && !save_png(s.app, widen(argv[2]), w, h, dpi)) return 1;
  // A panel with a window of its own is the picture: the one on top.
  const std::vector<Pane> panes = s.app.open_panes();
  if (panes.empty()) {
    if (tabs == 0) return 0;
    press_tabs();
    return save_png(s.app, widen(argv[2]), w, h, dpi) ? 0 : 1;
  }
  const PaneScope scope(s.app, panes.back());
  const PaneInfo info = s.app.pane_info(panes.back());
  if (!save_png(s.app, widen(argv[2]), static_cast<unsigned>(info.w), static_cast<unsigned>(info.h), dpi)) return 1;
  press_tabs();
  return save_png(s.app, widen(argv[2]), static_cast<unsigned>(info.w), static_cast<unsigned>(info.h), dpi) ? 0 : 1;
}

}  // namespace crpp::gui
