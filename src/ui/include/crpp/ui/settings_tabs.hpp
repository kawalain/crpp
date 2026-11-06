#pragma once
// One class per settings tab. Each tab reads and changes settings through ui.prefs.
#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/prefs.hpp"
#include "crpp/ui/settings_rows.hpp"
#include "crpp/ui/surface.hpp"

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
};

class SoundTab final : public SettingsTab {
 public:
  using SettingsTab::SettingsTab;
  void paint(Painter& p, Rect area) override;
  [[nodiscard]] float content_height() const override;
};

// The theme in use: upstream's effectiveTheme, with an unknown name taken as the default.
[[nodiscard]] ThemeId effective_theme(const Prefs& prefs, bool system_dark);

class DisplayTab final : public SettingsTab {
 public:
  explicit DisplayTab(Ui& ui);
  enum Id : int { SystemTheme = 1, ScaleSlider, ThemeList = 100, LightList = 110, DarkList = 120 };

  // Asked whenever the tab comes on screen, for who the chat sample shows.
  std::function<void()> on_shown;
  void set_sample(std::string nick, std::string emoji);

  void paint(Painter& p, Rect area) override;
  void paint_popups(Painter& p, Rect area) override;
  [[nodiscard]] float content_height() const override;
  void shown() override;
  void before_click(const Surface* owner, int id) override;
  void click(int id, float x, float y, bool shift) override;
  void drag(float x, float y) override;
  bool wheel(float x, float y, float delta) override;
  bool escape() override;
  [[nodiscard]] Cursor cursor(int /*id*/) const override { return Cursor::Hand; }
  [[nodiscard]] bool animating() const override;

 private:
  void paint_sample(Painter& p, Rect box, float zoom);
  [[nodiscard]] float chat_top() const;

  Dropdown theme_, light_, dark_;
  Rect list_rect_[3];
  ToggleAnim system_anim_;
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
  enum Id : int { Autostart = 1, Tray, Detail, Use24h, Logging, OpenLogs, Reset, IdleList = 100 };

  std::function<void()> on_open_logs;

  void paint(Painter& p, Rect area) override;
  void paint_popups(Painter& p, Rect area) override;
  [[nodiscard]] float content_height() const override;
  void before_click(const Surface* owner, int id) override;
  void click(int id, float x, float y, bool shift) override;
  bool wheel(float x, float y, float delta) override;
  bool escape() override;
  [[nodiscard]] bool animating() const override;

 private:
  Dropdown idle_;
  Rect idle_rect_;
  std::array<ToggleAnim, 5> toggles_;
  Fade open_logs_, reset_;
};

class AccountTab final : public SettingsTab {
 public:
  using SettingsTab::SettingsTab;
  void paint(Painter& p, Rect area) override;
  [[nodiscard]] float content_height() const override;
};

}  // namespace crpp::ui
