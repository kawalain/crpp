#pragma once
// One class per settings tab. Each tab reads and changes settings through ui.prefs.
#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "crpp/skin/pack.hpp"
#include "crpp/ui/controls.hpp"
#include "crpp/ui/prefs.hpp"
#include "crpp/ui/settings_rows.hpp"
#include "crpp/ui/surface.hpp"
#include "crpp/ui/themes.hpp"

namespace crpp::ui {

class SettingsTab : public Surface {
 public:
  using Surface::Surface;

  // Draws from area's top-left, in stage coordinates. The panel has already applied
  // the scroll to area.y and clips drawing and hits to the visible part. Content is
  // 550 wide (.settings-inner).
  virtual void paint(Painter& p, Rect area) = 0;
  // Drawn after the clip is gone, with the same area: an open Dropdown's list.
  virtual void paint_popups(Painter& /*p*/, Rect /*area*/) {}
  [[nodiscard]] virtual float content_height() const = 0;
  // The tab came on screen.
  virtual void shown() {}

 protected:
  // Is the height of this tab what its layout came out as, and not known ahead?
  [[nodiscard]] bool flows(std::string_view screen) const { return ui_.pack_screen(screen) || !ui_.skin().scales_stage(); }
  // How tall a Skin's flowing layout came out the last time it was drawn.
  float flow_height_ = 0;
};

// Pen's volume and pack, built but hidden until Pen is verified on a real device.
inline constexpr bool show_pen = false;

struct SoundChoice {
  std::string id, name;
};
enum class SoundDevice : std::uint8_t { Keyboard, Mouse, Pen };

// What the left-right difference's slider says at a value from 55 to 90.
[[nodiscard]] std::string_view balance_word(int value);

class SoundTab final : public SettingsTab {
 public:
  explicit SoundTab(Ui& ui);

  // What a Skin Pack can name on this tab, and for the value over a slider.
  [[nodiscard]] static skin::ScreenSchema schema();
  [[nodiscard]] static skin::ScreenSchema tip_schema();

  enum Id : int {
    SpeakerVolume,
    KeyboardVolume,
    MouseVolume,
    PenVolume,
    ListenSelf,
    OthersUseMine,
    MuteNotifications,
    MuteOnIdle,
    Spatial,
    BalanceEnabled,
    Balance,
    SpeakerList = 100,
    KeyboardList = 200,
    MouseList = 300,
    PenList = 400,
    ReverbList = 500
  };

  // The tab came on screen: time to fill the lists.
  std::function<void()> on_shown;
  // The user picked another output; the setting has changed already.
  std::function<void()> on_speaker_picked;

  // The outputs there are now, without "기본 출력 장치". A saved one that is missing
  // from them stays on the list under its saved name.
  void set_speakers(std::vector<SoundChoice> speakers);
  void set_packs(SoundDevice device, std::vector<SoundChoice> packs);
  // For tests: the list as shown.
  [[nodiscard]] const std::vector<std::string>& speaker_options() const { return speaker_.box.options; }
  [[nodiscard]] bool tip_shown() const { return tip_on_; }

  void paint(Painter& p, Rect area) override;
  void paint_popups(Painter& p, Rect area) override;
  [[nodiscard]] float content_height() const override;
  void shown() override;
  void before_click(const Surface* owner, int id) override;
  void click(int id, float x, float y, bool shift) override;
  [[nodiscard]] bool acts_on_press(int id) const override;
  void drag(float x, float y) override;
  void release() override;
  bool wheel(float x, float y, float delta) override;
  bool escape() override;
  [[nodiscard]] bool animating() const override;

 private:
  struct Picker {
    Dropdown box;
    std::vector<std::string> ids;  // parallel to box.options
  };
  void refresh_speakers();
  void sync_selection();
  void close_lists(const Picker* except);
  void set_slider(int id, float window_x);
  void pick(Picker& picker, int id);
  [[nodiscard]] Picker* picker_of_row(std::size_t row);
  void paint_luna(Painter& p, Rect area);
  void paint_pack(Painter& p, const skin::Screen& screen, Rect area);
  void paint_tip(Painter& p, Rect area);
  void paint_tip_luna(Painter& p, int row);
  void paint_balance(Painter& p, Rect area);
  void paint_person(Painter& p, float x, float y, Color c, bool marked);

  Picker speaker_;
  std::array<Picker, 3> packs_;  // by SoundDevice
  Picker reverb_;
  std::vector<SoundChoice> speakers_;
  Rect last_area_;
  // Where the flowing layout last put these: by volume row, the list after them for reverb.
  std::array<Rect, 4> slider_rects_;
  std::array<Rect, 5> combo_rects_;
  Rect balance_rect_;
  int held_ = -1;  // the slider being dragged
  int knob_id_ = -1;
  unsigned long long knob_start_ = 0;
  int tip_row_ = -1;
  bool tip_on_ = false;
  unsigned long long tip_start_ = 0;
};

// The theme in use: upstream's effectiveTheme, with an unknown name taken as the default.
[[nodiscard]] const ThemeEntry& effective_theme(const Prefs& prefs, bool system_dark);

class DisplayTab final : public SettingsTab {
 public:
  explicit DisplayTab(Ui& ui);
  [[nodiscard]] static skin::ScreenSchema schema();
  enum Id : int {
    SystemTheme = 1,
    ScaleSlider,
    ThemeCursor,
    Use24h,
    FetchCancel,
    FetchRetry,
    ThemeList = 100,
    LightList = 110,
    DarkList = 120,
    SkinThemeList = 130,  // every Skin's Themes, each Skin's under its name
    DarkModeList = 190,
    IdleList = 300,
    LanguageList = 330
  };

  // Asked whenever the tab comes on screen, for who the chat sample shows.
  std::function<void()> on_shown;
  // A Theme of a pack that is not installed was picked, or picked again after it failed: the pack is
  // to be fetched, and the Theme applied once it is there if it is still the last one picked.
  std::function<void(const std::string& skin, const std::string& theme)> on_fetch;
  std::function<void()> on_cancel_fetch;
  // An installed pack's Theme, as when picked in the list: the dark mode stays.
  void use_theme(const std::string& skin, const std::string& theme);
  void set_sample(std::string nick, std::string emoji);

  void paint(Painter& p, Rect area) override;
  void paint_popups(Painter& p, Rect area) override;
  [[nodiscard]] float content_height() const override;
  void shown() override;
  void before_click(const Surface* owner, int id) override;
  void click(int id, float x, float y, bool shift) override;
  [[nodiscard]] bool acts_on_press(int id) const override;
  void drag(float x, float y) override;
  bool wheel(float x, float y, float delta) override;
  bool escape() override;
  [[nodiscard]] Cursor cursor(int /*id*/) const override { return Cursor::Hand; }
  [[nodiscard]] bool animating() const override;

 private:
  void paint_luna(Painter& p, Rect area);
  void paint_pack(Painter& p, const skin::Screen& screen, Rect area);
  void paint_sample(Painter& p, Rect box, float zoom);
  [[nodiscard]] float chat_top() const;
  [[nodiscard]] float time_top() const;
  void sync_idle();
  void sync_language();

  void choose_theme(int index);
  void choose_dark(int index);
  Dropdown theme_, light_, dark_;
  Dropdown skin_theme_, dark_mode_;
  // Parallel to skin_theme_'s options: the Skin and its Theme, or -1 for a Skin's heading.
  std::vector<std::pair<int, int>> theme_at_;
  Dropdown idle_, language_;
  Rect list_rect_[6];
  Rect idle_rect_, language_rect_;
  ToggleAnim system_anim_, use_24h_anim_;
  Fade slider_tip_;
  Rect slider_;  // stage coordinates
  std::string sample_nick_, sample_emoji_;
};

// A sound in the notification list: `stem` is its file name, `id` upstream's key for it.
struct NotifySound {
  std::string stem, id, name;
};

class NotifyTab final : public SettingsTab {
 public:
  using SettingsTab::SettingsTab;
  [[nodiscard]] static skin::ScreenSchema schema();
  enum Id : int { TrayToggle = 1, FlashToggle, SoundBase = 100, PreviewBase = 300 };

  // Asked whenever the tab comes on screen, as the sounds may arrive after startup.
  std::function<void()> on_shown;
  std::function<void(const std::string& stem)> on_preview;
  void set_sounds(std::vector<NotifySound> sounds);

  void paint(Painter& p, Rect area) override;
  [[nodiscard]] float content_height() const override;
  void shown() override;
  void click(int id, float x, float y, bool shift) override;
  [[nodiscard]] bool animating() const override;

 private:
  void paint_luna(Painter& p, Rect area);
  void paint_pack(Painter& p, const skin::Screen& screen, Rect area);
  struct Row {
    ToggleAnim toggle;
    Fade grow, tip;
  };
  std::vector<NotifySound> sounds_;
  std::vector<Row> rows_;
  ToggleAnim tray_, flash_;
};

class SystemTab final : public SettingsTab {
 public:
  explicit SystemTab(Ui& ui);
  [[nodiscard]] static skin::ScreenSchema schema();
  enum Id : int { Autostart = 1, Tray, Detail, Logging, OpenLogs, Reset, ReleaseList = 300 };

  std::function<void()> on_open_logs;

  void paint(Painter& p, Rect area) override;
  void paint_popups(Painter& p, Rect area) override;
  [[nodiscard]] float content_height() const override;
  void before_click(const Surface* owner, int id) override;
  void click(int id, float x, float y, bool shift) override;
  [[nodiscard]] bool acts_on_press(int id) const override;
  bool wheel(float x, float y, float delta) override;
  bool escape() override;
  [[nodiscard]] bool animating() const override;

 private:
  void paint_luna(Painter& p, Rect area);
  void paint_pack(Painter& p, const skin::Screen& screen, Rect area);
  void sync_release();
  Dropdown release_;
  Rect release_rect_;
  std::array<ToggleAnim, 4> toggles_;
  Fade open_logs_, reset_;
};

class AccountTab final : public SettingsTab {
 public:
  using SettingsTab::SettingsTab;
  [[nodiscard]] static skin::ScreenSchema schema();
  enum Id : int { Emoji, Nickname, Handle, Logout };

  struct Profile {
    std::string nickname, handle;
    std::string display;  // shown alone unless there is both a nickname and a handle
    std::string emoji;
    bool has_account = false;  // a Guest has no handle to change
  };
  void set_profile(Profile profile) { profile_ = std::move(profile); }

  std::function<void(std::string id)> on_emoji;
  std::function<void()> on_change_nickname, on_change_handle, on_logout;

  void paint(Painter& p, Rect area) override;
  [[nodiscard]] float content_height() const override;
  void click(int id, float x, float y, bool shift) override;

 private:
  void paint_luna(Painter& p, Rect area);
  void paint_pack(Painter& p, const skin::Screen& screen, Rect area);
  Profile profile_;
};

}  // namespace crpp::ui
