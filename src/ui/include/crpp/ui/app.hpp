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

#include "crpp/ui/context_menu.hpp"
#include "crpp/ui/changelog_view.hpp"
#include "crpp/ui/dialogs.hpp"
#include "crpp/ui/emoji_picker.hpp"
#include "crpp/ui/home_screen.hpp"
#include "crpp/ui/konami.hpp"
#include "crpp/ui/mini_window.hpp"
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
// they are made in, so a later one comes up on top. Mini is a window under every Skin.
enum class Pane : std::uint8_t { Main, Options, Changelog, Profile, Emoji, Offline, Dialog, Mini };
inline constexpr std::size_t pane_count = 8;

// How a pane's window is made, in DIPs. One that resizes keeps its size under `key`.
struct PaneInfo {
  std::string_view key;
  std::string title;
  float w = 0, h = 0, min_w = 0, min_h = 0;
  bool resizable = false, close = true;
  // Its own: always on top, owned by no window, and drawn whole by its screen, which says how round it is.
  bool floating = false;
  float radius = 0;
};

// What decides which windows should exist and how large; the shell compares it with what it made.
struct PaneState {
  unsigned open = 0;
  unsigned revision = 0;
  std::size_t members = 0;  // the Mini window's size follows them
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
  [[nodiscard]] const Ui& ui() const { return ui_; }
  [[nodiscard]] SetupScreen& setup() { return setup_; }
  [[nodiscard]] HomeScreen& home() { return home_; }
  [[nodiscard]] RoomScreen& room() { return room_; }
  [[nodiscard]] NoticeCard& notice() { return notice_; }
  [[nodiscard]] OfflinePopup& offline() { return offline_; }
  [[nodiscard]] SettingsPanel& settings() { return settings_; }
  [[nodiscard]] Achievements& achievements() { return achievements_; }
  [[nodiscard]] MiniWindow& mini() { return mini_; }
  // Opens the Mini window in a Room, or closes it.
  void toggle_mini();
  // Whether a point of the Mini window has nothing to click, so that it moves the window.
  [[nodiscard]] bool mini_drags_at(float x, float y) const;
  // Each Achievement as it is reached, after its toast.
  std::function<void(AchievementId)> on_achievement;
  // Back from a Room: the time in it has been counted.
  std::function<void()> on_left_room;
  // My own input since the last call, by count only: the weather, totals and time in the Room follow it.
  void note_own_input(std::uint32_t events, std::uint32_t presses, std::uint32_t clicks);
  [[nodiscard]] Dialogs& dialogs() { return dialogs_; }
  [[nodiscard]] Toasts& toasts() { return toasts_; }
  [[nodiscard]] EmojiPicker& emoji_picker() { return emoji_picker_; }
  [[nodiscard]] UpdatePopup& update_popup() { return update_popup_; }
  [[nodiscard]] ChangelogView& changelog() { return changelog_; }
  [[nodiscard]] WindowFrame& frame() { return frame_; }
  // Does the app draw the window's frame?
  [[nodiscard]] bool own_frame() const { return !ui_.scales_stage(); }
  [[nodiscard]] ScreenSize screen_size() const;
  [[nodiscard]] FrameMetrics frame_metrics() const;
  // Is the pointer in these windows the pack's? Otherwise it is the system's.
  [[nodiscard]] bool theme_cursors() const;
  // The settings surfaces read and change; in memory until the shell sets its own.
  void set_prefs(Prefs& prefs) { ui_.prefs = &prefs; }

  // A Palette of the pack in use; one it does not have means its default Theme.
  void set_theme(std::string_view id);
  // The Skin's own notification sounds: 0 none, 1 small bells, 2 plain tones.
  [[nodiscard]] int notification_style() const;
  // A folder of Skin Packs, and what comes before their ids: the first digits of the fingerprint of
  // the Repository they came from, or nothing for the official one (ADR 0008).
  struct SkinFolder {
    std::filesystem::path dir;
    std::string prefix;
  };
  // Every folder with a pack.cskin in these can be chosen, those of the first first; set_parent_finder
  // says where their parents are. False when there is none.
  bool find_skins(std::span<const SkinFolder> folders);
  // What the last find_skins could not read: the id settings would name it by, and why.
  struct SkippedPack {
    std::string id, why;
  };
  [[nodiscard]] const std::vector<SkippedPack>& skipped_packs() const { return skipped_; }
  bool find_skins(const std::filesystem::path& dir) {
    const SkinFolder one[] = {{dir, {}}};
    return find_skins(one);
  }
  // Where the pack a pack extends is: its id, its Repository's fingerprint when it names one, and
  // the folder of the pack that extends it. Without one set, a folder beside it, for no fingerprint.
  using ParentFinder = std::function<std::optional<std::filesystem::path>(std::string_view id, std::string_view fingerprint,
                                                                          const std::filesystem::path& from)>;
  void set_parent_finder(ParentFinder finder) { parent_finder_ = std::move(finder); }
  // Draws what the settings choose, among the packs found.
  void apply_skin_choice(bool system_dark);
  // Draws from the Skin Pack in this folder what it has. The error says what is wrong with it.
  [[nodiscard]] std::expected<void, std::string> load_skin_dir(const std::filesystem::path& dir);
  void skin_changed();
  // A window's size from the pack in use, or else this.
  [[nodiscard]] float window_size(std::string_view name, std::string_view side, float code) const;
  // Whether the system or the caller wants motion; a Skin may have none anyway.
  void set_animations(bool wanted);
  void show_setup(const SetupScreen::Status& status);
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

  // For --shot cursors: nothing but the page of the pack's cursors.
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
  bool mouse_up(float x, float y);
  // The button will not come up here: the window lost the mouse.
  bool mouse_cancel();
  // While the button holds something that repeats: how long until hold_step, otherwise 0.
  // The shell keeps a timer for it only until the button comes up.
  [[nodiscard]] unsigned hold_wait_ms() const;
  [[nodiscard]] unsigned hold_repeat_ms() const;
  bool hold_step();
  bool right_click(float x, float y);
  bool wheel(float x, float y, float delta);  // delta in notches (+ = up)
  bool char_input(wchar_t c, bool shift);
  bool key_down(unsigned vk, bool ctrl, bool shift);
  // The Konami code: hidden Themes are listed from now on.
  void found_hidden_themes();
  // Keys the system sees first, with Alt or F10. Shift+F10 opens the context menu, and Alt with a
  // title's letter that menu of an own frame.
  bool sys_key(unsigned vk, bool shift);
  // Alt or F10 coming up. Alone, it puts the keyboard in an own frame's menu bar. True when the
  // system must not see it, as it would take the keyboard into a menu the window does not have.
  bool sys_key_up(unsigned vk);
  // The character of a key sys_key took for a menu: the system would look for a menu of its own.
  [[nodiscard]] bool take_sys_char(wchar_t c);
  // The menu key or Shift+F10: the context menu of what has the keys, at its caret.
  bool context_key();
  // Tab and Shift+Tab: the keyboard goes to the next stop, or the one before, and that shows.
  bool move_stop(int step);
  [[nodiscard]] ContextMenu& context_menu() { return context_menu_; }
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
  [[nodiscard]] std::vector<Surface*> layers_below_menu();
  // The surfaces on every window, for what runs whichever window is in front.
  [[nodiscard]] std::vector<Surface*> all_layers();
  void paint_pane(Painter& p, float width, float height);
  void view_left(View from);
  void finish_leaving_room();
  void paint_screen(Painter& p, View view);
  void paint_screens(Painter& p);
  void paint_cursor_sheet(Painter& p);
  void paint_layers(Painter& p);
  // The pack's focus_ring over the stop the keyboard is on, while that shows.
  void paint_focus(Painter& p);

  Ui ui_;
  [[nodiscard]] std::expected<void, std::string> read_pack(const std::string& overlay);
  void point_at_palette();
  std::filesystem::path pack_dir_;
  ParentFinder parent_finder_;
  std::vector<SkippedPack> skipped_;
  [[nodiscard]] skin::LoadOptions load_options(std::string overlay, const std::filesystem::path& folder) const;
  std::optional<skin::Pack> pack_;
  std::string theme_id_ = "cumulus";  // the Theme a Ui starts with, until a choice is read
  MemoryPrefs memory_prefs_;
  Achievements achievements_;
  KonamiCode konami_;
  SystemClipboard system_clipboard_;
  // The press before, to count clicks in a row.
  unsigned long long last_press_ms_ = 0;
  float last_press_x_ = 0, last_press_y_ = 0;
  const Surface* last_press_owner_ = nullptr;
  int last_press_id_ = 0;
  SetupScreen setup_{ui_};
  HomeScreen home_{ui_};
  RoomScreen room_{ui_};
  MiniWindow mini_{ui_, room_};
  NoticeCard notice_{ui_};
  OfflinePopup offline_{ui_};
  SettingsPanel settings_{ui_};
  EmojiPicker emoji_picker_{ui_};
  Toasts toasts_{ui_};
  UpdatePopup update_popup_{ui_};
  ChangelogView changelog_{ui_};
  Dialogs dialogs_{ui_};
  ContextMenu context_menu_{ui_};
  // The character the last key makes, when a menu, a question or a stop took that key: it is not
  // typed. After a key of the menu, whose access keys are letters, any character.
  static constexpr wchar_t any_char = 0xFFFF;
  wchar_t taken_char_ = 0;
  wchar_t taken_sys_char_ = 0;
  // Alt or F10, down with no other key or click since: coming up, it enters the menu bar.
  unsigned alone_ = 0;
  // The main window's own frame has the keyboard in its menus.
  [[nodiscard]] bool in_frame_menus() const { return own_frame() && pane_ == Pane::Main && frame_.keyboard_in_menus(); }
  // Enter or Space on the stop the keyboard is on, as a click on it.
  bool act_on_stop(unsigned vk);
  // A key the stop the keyboard is on takes itself, with Alt when it came as a key of the system.
  bool stop_key(unsigned vk, bool ctrl, bool shift, bool alt);
  // After a frame: a layer that opened takes the keyboard, and one that closed gives it back.
  void settle_stop();
  // The keyboard goes to this stop, which takes the keys if it is a field and comes into view.
  void go_to_stop(Surface* owner, int id);
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
  bool cursor_sheet_ = false;
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
