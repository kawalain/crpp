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

  enum Id : int { Droplet, Overlay, Thumb, Version, Track, BarUp, BarDown, TabBase = 100 };
  [[nodiscard]] std::optional<Role> role(int id) const override {
    if (id >= TabBase) return Role::Tab;
    if (id == Droplet) return Role::Button;
    if (id == Version) return Role::Link;
    return std::nullopt;
  }
  [[nodiscard]] bool holds_keys(int id) const override { return id == Overlay; }
  bool stop_key(int id, unsigned vk, bool ctrl, bool shift, bool alt) override;
  // Ctrl+Tab and Ctrl+Page Down go to the next tab, with Shift or Page Up the one before, wherever the keyboard is.
  bool layer_key(unsigned vk, bool ctrl, bool shift) override;

  // What a Skin Pack can name on this panel.
  [[nodiscard]] static skin::ScreenSchema schema();
  [[nodiscard]] static skin::ScreenSchema droplet_schema();

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
  void scroll_to(float offset);
  [[nodiscard]] float scroll_offset() const { return scroll_.offset; }
  [[nodiscard]] float left_fade_top() const { return left_fade_top_; }
  // The scrollbar shows on scrolling or under the pointer, then fades away.
  [[nodiscard]] bool scrollbar_shown() const { return bar_shown_; }

  // Draws the panel when open; the droplet is on top of it.
  void paint(Painter& p);
  void before_click(const Surface* owner, int id) override;
  void click(int id, float x, float y, bool shift) override;
  [[nodiscard]] bool acts_on_press(int id) const override { return id == Thumb || id == Track || id == BarUp || id == BarDown; }
  [[nodiscard]] bool repeats(int id) const override;
  void drag(float x, float y) override;
  bool wheel(float x, float y, float delta) override;
  bool escape() override;
  [[nodiscard]] Cursor cursor(int id) const override;
  [[nodiscard]] bool is_link(int id) const override { return id == Version; }
  [[nodiscard]] bool animating() const override;
  bool tick(unsigned long long now_ms) override;
  [[nodiscard]] unsigned long long wake_at() const override;

 private:
  [[nodiscard]] SettingsTab& tab();
  [[nodiscard]] SettingsTab& tab_of(SettingsPage page);
  [[nodiscard]] const SettingsTab& tab() const { return const_cast<SettingsPanel*>(this)->tab(); }
  void paint_pack(Painter& p, const skin::Screen& screen);
  void watch_scrollbar();
  void reset_scrollbar();

  SoundTab sound_;
  DisplayTab display_;
  NotifyTab notify_;
  SystemTab system_;
  AccountTab account_;
  bool open_ = false;
  SettingsPage page_ = SettingsPage::Sound;
  Scroll scroll_;
  Rect content_area_{settings_view};
  Rect scroll_track_{835, 187, 8, 403};  // .settings-scrollbar
  SettingsPage left_page_ = SettingsPage::Sound;  // the tab fading out, while left_at_ is set
  float left_scroll_ = 0, left_fade_top_ = 0, left_fade_bottom_ = 0;
  unsigned long long left_at_ = 0;
  std::string version_;

  bool bar_shown_ = false, bar_over_ = false, reveal_pending_ = false;
  unsigned long long hide_at_ = 0;
  float grab_y_ = 0, grab_offset_ = 0;
  // Where scroll_to asked to go before a pack's tab said how tall it is; below 0, nowhere.
  float pending_scroll_ = -1;
  unsigned tab_changes_ = 0, selects_ = 0;
  bool was_animating_ = false;
};

}  // namespace crpp::ui
