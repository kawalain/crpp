#pragma once
// One class per settings tab. Each tab reads and changes settings through ui.prefs.
#include <array>
#include <functional>
#include <string>
#include <vector>

#include "crpp/ui/controls.hpp"
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

// Pen's volume and pack, built but hidden until Pen is verified on a real device.
inline constexpr bool show_pen = false;

struct SoundChoice {
  std::string id, name;
};
enum class SoundDevice : std::uint8_t { Keyboard, Mouse, Pen };

class SoundTab final : public SettingsTab {
 public:
  explicit SoundTab(Ui& ui);

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
    PenList = 400
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
  void paint_tip(Painter& p, Rect area);
  void paint_balance(Painter& p, Rect area);
  void paint_person(Painter& p, float x, float y, Color c, bool marked);

  Picker speaker_;
  std::array<Picker, 3> packs_;  // by SoundDevice
  std::vector<SoundChoice> speakers_;
  Rect last_area_;
  int held_ = -1;  // the slider being dragged
  int knob_id_ = -1;
  unsigned long long knob_start_ = 0;
  int tip_row_ = -1;
  bool tip_on_ = false;
  unsigned long long tip_start_ = 0;
  ComPtr<ID2D1PathGeometry> person_body_, balance_thumb_;
};

class DisplayTab final : public SettingsTab {
 public:
  using SettingsTab::SettingsTab;
  void paint(Painter& p, Rect area) override;
  [[nodiscard]] float content_height() const override;
};

class NotifyTab final : public SettingsTab {
 public:
  using SettingsTab::SettingsTab;
  void paint(Painter& p, Rect area) override;
  [[nodiscard]] float content_height() const override;
};

class SystemTab final : public SettingsTab {
 public:
  using SettingsTab::SettingsTab;
  void paint(Painter& p, Rect area) override;
  [[nodiscard]] float content_height() const override;
};

class AccountTab final : public SettingsTab {
 public:
  using SettingsTab::SettingsTab;
  void paint(Painter& p, Rect area) override;
  [[nodiscard]] float content_height() const override;
};

}  // namespace crpp::ui
