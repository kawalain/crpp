#pragma once
// The Cloudroom++ UI: the surfaces on screen, independent of the window they are
// shown in. All coordinates are device-independent pixels (1/96 inch), and the
// render target's DPI maps them to the monitor. The shell converts what comes from
// window messages.
//
// Layout follows the upstream web UI: a fixed 951x692 "stage" scaled to cover
// the window (the cloud art), content areas that follow the window but keep
// their own size, and a few controls fixed to the window corners.
#include <span>
#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
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
#include "crpp/ui/window_frame.hpp"

namespace crpp::ui {

enum class View : std::uint8_t { Setup, Home, Session, Prompt };

// How large the window wants to be for what is on screen, in DIPs. For a Skin whose controls
// keep their size, each screen has a window size of its own; `key` is what it is saved under.
// An empty key means this Skin has one window size for all.
struct ScreenSize {
  std::string_view key;
  float w = 0, h = 0, min_w = 0, min_h = 0;
};

// The windows of the app. A Skin that draws its own frame shows each panel on a window of its
// own; under Cloud only Main exists and the panels are layers in it. The order is the order
// they are made in, so a later one comes up on top.
enum class Pane : std::uint8_t { Main, Options, Changelog, Profile, Emoji, Offline, Dialog };
inline constexpr std::size_t pane_count = 7;

// How a pane's window is made, in DIPs. One that resizes keeps its size under `key`.
struct PaneInfo {
  std::string_view key;
  std::string title;
  float w = 0, h = 0, min_w = 0, min_h = 0;
  bool resizable = false, close = true;
};

// What decides which windows should exist and how large; the shell compares it with what it made.
struct PaneState {
  unsigned open = 0;
  unsigned revision = 0;
  bool raising = false, hidden = false;
  bool operator==(const PaneState&) const = default;
};

// Every screen a Skin Pack can draw, with the names it may use there.
[[nodiscard]] std::span<const skin::ScreenSchema> screen_schemas();

class App {
 public:
  App();
  // An empty assets_dir, or load_font false for a font file about to be replaced, draws
  // with a system font until reload_assets.
  bool init(const std::filesystem::path& assets_dir, bool load_font = true);
  void reload_assets(const std::filesystem::path& assets_dir);
  [[nodiscard]] ID2D1Factory* factory() const { return ui_.gfx.d2d(); }
  // For a window nobody sees: lets go of everything made from the factory and of the
  // factory, and with it of the GPU device. The render target must be gone first. The
  // next frame makes it all again, which takes about 150 ms.
  void release_graphics();

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
  [[nodiscard]] WindowFrame& frame() { return frame_; }
  // Does the app draw the window's frame?
  [[nodiscard]] bool own_frame() const { return !ui_.skin().scales_stage(); }
  [[nodiscard]] ScreenSize screen_size() const;
  [[nodiscard]] FrameMetrics frame_metrics() const;
  // Is the pointer in these windows the pack's? Otherwise it is the system's.
  [[nodiscard]] bool theme_cursors() const;
  // The settings surfaces read and change; in memory until the shell sets its own.
  void set_prefs(Prefs& prefs) { ui_.prefs = &prefs; }

  // An id from themes.hpp; an unknown one means the default.
  void set_theme(std::string_view id);
  // Draws from the Skin Pack in this folder what it has. The error says what is wrong with it.
  [[nodiscard]] std::expected<void, std::string> load_skin_dir(const std::filesystem::path& dir);
  void skin_changed();
  // A window's size from the pack in use, or else this.
  [[nodiscard]] float window_size(std::string_view name, std::string_view side, float code) const;
  // Whether the system or the caller wants motion; a Skin may have none anyway.
  void set_animations(bool wanted);
  void show_setup(int percent);
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

  // For --shot luna-controls: nothing but the page of controls.
  void show_controls_sheet() { controls_sheet_ = true; }
  void show_cursor_sheet() { cursor_sheet_ = true; }

  // --- windows ---
  // Which panels should have a window now, in the order to make them.
  [[nodiscard]] std::vector<Pane> open_panes() const;
  [[nodiscard]] PaneInfo pane_info(Pane pane) const;
  [[nodiscard]] PaneState pane_state() const;
  // All that follows, drawing and input, is about this window. See PaneScope.
  void use_pane(Pane pane);
  [[nodiscard]] Pane current_pane() const { return pane_; }
  // Its close button, or the system asking.
  void close_pane(Pane pane);
  void set_pane_active(Pane pane, bool active);
  // The window lost the keyboard: what closes on a click elsewhere closes. True if it matters.
  bool window_deactivated();
  // A window has gone: it starts afresh next time.
  void forget_pane(Pane pane);
  // A command asked for a window that may already be open.
  [[nodiscard]] std::optional<Pane> take_raised_pane();

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
  // When all that moves is a speaking disc: the only part the next frame has to draw.
  [[nodiscard]] std::optional<Rect> only_moving_area();
  // Something outside that part changed, whatever the frame was asked to draw.
  [[nodiscard]] bool wants_full_frame() const;
  bool tick(unsigned long long now_ms);  // expires effects; true if the view changed
  // When tick is next needed though nothing is animating; 0 = never.
  [[nodiscard]] unsigned long long wake_at();
  [[nodiscard]] Cursor cursor() const;
  [[nodiscard]] Rect caret_rect() const { return ui_.caret; }  // for IME placement
  [[nodiscard]] bool wants_blink() const { return ui_.focus != nullptr; }

 private:
  // The surfaces on the current window now, topmost first.
  [[nodiscard]] std::vector<Surface*> layers();
  // The surfaces on every window, for what runs whichever window is in front.
  [[nodiscard]] std::vector<Surface*> all_layers();
  void paint_pane(Painter& p, float width, float height);
  void view_left(View from);
  void finish_leaving_room();
  void paint_screen(Painter& p, View view);
  void paint_screens(Painter& p);
  void paint_controls_sheet(Painter& p);
  void paint_cursor_sheet(Painter& p);
  void paint_layers(Painter& p);

  Ui ui_;
  [[nodiscard]] std::expected<void, std::string> read_pack(const std::string& overlay);
  void point_at_palette();
  std::filesystem::path pack_dir_;
  std::optional<skin::Pack> pack_;
  std::unique_ptr<Skin> pack_skin_;
  std::string theme_id_ = "cloudroom";  // the Theme a Ui starts with, until a choice is read
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
  WindowFrame frame_{ui_};
  WindowFrame pane_frame_{ui_};
  Pane pane_ = Pane::Main;
  std::array<WindowState, pane_count> windows_;  // of those not in use
  std::array<bool, pane_count> pane_active_{};
  std::optional<Pane> raised_pane_;
  float main_pixels_per_dip_ = 1;
  float pixels_per_dip_ = 1;
  View view_ = View::Home;
  View prompt_return_ = View::Home;
  // The screen fading out while left_at_ is set. A Room fading out resets after that.
  View left_view_ = View::Home;
  unsigned long long left_at_ = 0;
  bool room_leaving_ = false;
  bool full_frame_ = false;
  bool animations_wanted_ = true;
  bool controls_sheet_ = false, cursor_sheet_ = false;
};

// While this lives, the app is on this window. A window's messages run in one, as what they
// do may make another window handle something before they return.
class PaneScope {
 public:
  PaneScope(App& app, Pane pane) : app_(app), before_(app.current_pane()) { app_.use_pane(pane); }
  ~PaneScope() { app_.use_pane(before_); }
  PaneScope(const PaneScope&) = delete;
  PaneScope& operator=(const PaneScope&) = delete;

 private:
  App& app_;
  Pane before_;
};

}  // namespace crpp::ui
