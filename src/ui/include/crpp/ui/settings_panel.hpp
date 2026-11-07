#pragma once
// The settings droplet in the window's corner and the panel it opens: the cloud,
// the tab buttons, a scrolled content column and the version button.
#include <array>
#include <functional>
#include <string>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/settings_rows.hpp"
#include "crpp/ui/settings_tabs.hpp"
#include "crpp/ui/surface.hpp"

namespace crpp::ui {

enum class SettingsPage : std::uint8_t { Sound, Display, Notify, System, Account };

class SettingsPanel final : public Surface {
 public:
  explicit SettingsPanel(Ui& ui)
      : Surface(ui), sound_(ui), display_(ui), notify_(ui), system_(ui), account_(ui) {}

  enum Id : int { Droplet, Overlay, Thumb, Version, Track, TabBase = 100 };

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
  void scroll_to(float offset) { scroll_.jump_to(offset); }
  [[nodiscard]] float scroll_offset() const { return scroll_.offset; }
  // The scrollbar shows on scrolling or under the pointer, then fades away.
  [[nodiscard]] bool scrollbar_shown() const { return bar_shown_; }

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
  [[nodiscard]] unsigned long long wake_at() const override;

 private:
  [[nodiscard]] bool drop_moving(unsigned long long now) const;
  [[nodiscard]] SettingsTab& tab();
  [[nodiscard]] SettingsTab& tab_of(SettingsPage page);
  [[nodiscard]] const SettingsTab& tab() const { return const_cast<SettingsPanel*>(this)->tab(); }
  void paint_panel(Painter& p);
  void paint_tabs(Painter& p);
  void paint_scrollbar_faded(Painter& p);
  void paint_version(Painter& p);
  void paint_droplet(Painter& p);
  void reset_scrollbar();

  SoundTab sound_;
  DisplayTab display_;
  NotifyTab notify_;
  SystemTab system_;
  AccountTab account_;
  bool open_ = false;
  SettingsPage page_ = SettingsPage::Sound;
  Scroll scroll_;
  Fade panel_;  // the panel coming and going
  SettingsPage left_page_ = SettingsPage::Sound;  // the tab fading out, while left_at_ is set
  float left_scroll_ = 0;
  unsigned long long left_at_ = 0;
  std::string version_;

  struct TabAnim {
    Fade active, hover, grow;
  };
  std::array<TabAnim, 5> tab_anim_;
  Fade droplet_hover_, droplet_grow_, droplet_tip_, version_tip_;
  Fade bar_, thumb_hover_;
  bool bar_shown_ = false, bar_over_ = false, reveal_pending_ = false;
  unsigned long long hide_at_ = 0;
  float grab_y_ = 0, grab_offset_ = 0;
  bool was_animating_ = false;
};

}  // namespace crpp::ui
