#pragma once
// One class per settings tab. Each tab reads and changes settings through ui.prefs.
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
