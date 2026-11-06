#pragma once
// The Cloudroom UI: the surfaces on screen, independent of the window they are
// shown in. All coordinates are device-independent pixels (1/96 inch), and the
// render target's DPI maps them to the monitor. The shell converts what comes from
// window messages.
//
// Layout follows the upstream web UI: a fixed 951x692 "stage" scaled to cover
// the window (the cloud art), content areas that follow the window but keep
// their own size, and a few controls fixed to the window corners.
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "crpp/ui/changelog_view.hpp"
#include "crpp/ui/dialogs.hpp"
#include "crpp/ui/emoji_picker.hpp"
#include "crpp/ui/home_screen.hpp"
#include "crpp/ui/notice_card.hpp"
#include "crpp/ui/offline_popup.hpp"
#include "crpp/ui/prefs.hpp"
#include "crpp/ui/prompt_screen.hpp"
#include "crpp/ui/room_screen.hpp"
#include "crpp/ui/settings_panel.hpp"
#include "crpp/ui/setup_screen.hpp"
#include "crpp/ui/surface.hpp"
#include "crpp/ui/toasts.hpp"
#include "crpp/ui/update_popup.hpp"

namespace crpp::ui {

enum class View : std::uint8_t { Setup, Home, Session, Prompt };

class App {
 public:
  App();
  // An empty assets_dir draws with a system font until reload_assets.
  bool init(const std::filesystem::path& assets_dir);
  void reload_assets(const std::filesystem::path& assets_dir);
  [[nodiscard]] ID2D1Factory* factory() const { return ui_.gfx.d2d(); }

  // The parts of the screen; each has its own state and callbacks.
  [[nodiscard]] Ui& ui() { return ui_; }
  [[nodiscard]] SetupScreen& setup() { return setup_; }
  [[nodiscard]] HomeScreen& home() { return home_; }
  [[nodiscard]] RoomScreen& room() { return room_; }
  [[nodiscard]] NoticeCard& notice() { return notice_; }
  [[nodiscard]] OfflinePopup& offline() { return offline_; }
  [[nodiscard]] SettingsPanel& settings() { return settings_; }
  [[nodiscard]] Dialogs& dialogs() { return dialogs_; }
  [[nodiscard]] Toasts& toasts() { return toasts_; }
  [[nodiscard]] EmojiPicker& emoji_picker() { return emoji_picker_; }
  [[nodiscard]] UpdatePopup& update_popup() { return update_popup_; }
  [[nodiscard]] ChangelogView& changelog() { return changelog_; }
  // The settings surfaces read and change; in memory until the shell sets its own.
  void set_prefs(Prefs& prefs) { ui_.prefs = &prefs; }

  void set_theme(ThemeId id) { ui_.theme = make_theme(id); }
  void show_setup(int percent, std::string error = {});
  void show_home();
  void show_session(std::string room_code);
  [[nodiscard]] View view() const { return view_; }
  // The prompt covers the screen it was opened from, which goes on underneath: a Room
  // stays connected, and what show_home and show_session do waits for close_prompt.
  void show_prompt(Prompt prompt);
  void close_prompt();
  [[nodiscard]] PromptScreen& prompt() { return prompt_; }
  // The view under the prompt, or the view itself when there is none.
  [[nodiscard]] View base_view() const { return view_ == View::Prompt ? prompt_return_ : view_; }

  // --- drawing ---
  void paint(ID2D1RenderTarget* rt, float width, float height);

  // --- input; each returns true if the window should repaint ---
  bool mouse_move(float x, float y);
  bool mouse_leave();
  bool mouse_down(float x, float y, bool shift);
  bool mouse_up();
  bool right_click(float x, float y);
  bool wheel(float x, float y, float delta);  // delta in notches (+ = up)
  bool char_input(wchar_t c, bool shift);
  bool key_down(unsigned vk, bool ctrl, bool shift);
  bool ime_composition(std::wstring_view composition, std::wstring_view result);
  bool blink();  // caret timer tick
  // True while a timed effect (member ring pulse) needs more frames.
  [[nodiscard]] bool animating();
  bool tick(unsigned long long now_ms);  // expires effects; true if the view changed
  // When tick is next needed though nothing is animating; 0 = never.
  [[nodiscard]] unsigned long long wake_at();
  [[nodiscard]] Cursor cursor() const;
  [[nodiscard]] Rect caret_rect() const { return ui_.caret; }  // for IME placement
  [[nodiscard]] bool wants_blink() const { return ui_.focus != nullptr; }

 private:
  // The surfaces on screen now, topmost first.
  [[nodiscard]] std::vector<Surface*> layers();

  Ui ui_;
  MemoryPrefs memory_prefs_;
  SetupScreen setup_{ui_};
  HomeScreen home_{ui_};
  RoomScreen room_{ui_};
  NoticeCard notice_{ui_};
  OfflinePopup offline_{ui_};
  SettingsPanel settings_{ui_};
  EmojiPicker emoji_picker_{ui_};
  Toasts toasts_{ui_};
  UpdatePopup update_popup_{ui_};
  ChangelogView changelog_{ui_};
  Dialogs dialogs_{ui_};
  PromptScreen prompt_{ui_};
  View view_ = View::Home;
  View prompt_return_ = View::Home;
};

}  // namespace crpp::ui
