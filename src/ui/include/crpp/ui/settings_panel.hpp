#pragma once
// The settings droplet in the window's corner and the panel it opens: the cloud,
// the tab buttons, a scrolled content column and the version button.
#include <array>
#include <functional>
#include <string>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/hover.hpp"
#include "crpp/ui/settings_tabs.hpp"
#include "crpp/ui/surface.hpp"

namespace crpp::ui {

enum class SettingsPage : std::uint8_t { Sound, Display, Notify, System, Account };

class SettingsPanel final : public Surface {
 public:
  explicit SettingsPanel(Ui& ui)
      : Surface(ui), sound_(ui), display_(ui), notify_(ui), system_(ui), account_(ui) {}
  bool init();  // needs ui.gfx

  enum Id : int { Droplet, Overlay, Thumb, Version, TabBase = 100 };

  // The version button, which opens the changelog.
  std::function<void()> on_version;
  void set_version(std::string version) { version_ = std::move(version); }

  [[nodiscard]] SoundTab& sound() { return sound_; }
  [[nodiscard]] DisplayTab& display() { return display_; }
  [[nodiscard]] NotifyTab& notify() { return notify_; }
  [[nodiscard]] SystemTab& system() { return system_; }
  [[nodiscard]] AccountTab& account() { return account_; }

  void open(SettingsPage page);
  void close();
  [[nodiscard]] bool is_open() const { return open_; }
  // Goes back to the first tab's top the next time it opens by the droplet.
  void select(SettingsPage page);
  [[nodiscard]] SettingsPage page() const { return page_; }
  // Where the content is scrolled to; the next paint keeps it in range.
  void scroll_to(float offset) { scroll_.offset = offset; }

  // Draws the panel when open; the droplet is on top of it.
  void paint(Painter& p);
  void before_click(const Surface* owner, int id) override;
  void click(int id, float x, float y, bool shift) override;
  void drag(float x, float y) override;
  bool wheel(float x, float y, float delta) override;
  bool escape() override;
  [[nodiscard]] Cursor cursor(int id) const override;
  [[nodiscard]] bool animating() const override;
  bool tick(unsigned long long now_ms) override;

 private:
  [[nodiscard]] bool drop_moving(unsigned long long now) const;
  [[nodiscard]] SettingsTab& tab();
  void paint_panel(Painter& p);
  void paint_droplet(Painter& p);

  SoundTab sound_;
  DisplayTab display_;
  NotifyTab notify_;
  SystemTab system_;
  AccountTab account_;
  ComPtr<ID2D1PathGeometry> droplet_, droplet_x_;
  bool open_ = false;
  SettingsPage page_ = SettingsPage::Sound;
  Scroll scroll_;
  std::string version_;
  Tween drop_scale_{140};  // .droplet transition
  Tween drop_color_{150};
  Tween drop_tip_{tip_ms};
  bool drop_was_moving_ = false;
};

}  // namespace crpp::ui
