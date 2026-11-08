#pragma once
// One class per settings tab. Each tab reads and changes settings through ui.prefs.
#include <array>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "crpp/skin/pack.hpp"
#include "crpp/ui/read_selection.hpp"
#include "crpp/ui/controls.hpp"
#include "crpp/ui/prefs.hpp"
#include "crpp/ui/settings_rows.hpp"
#include "crpp/ui/surface.hpp"
#include "crpp/ui/textfield.hpp"

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
  // The tab came on screen.
  virtual void shown() {}

  [[nodiscard]] float content_height() const { return flow_height_; }

 protected:
  // How tall the pack's layout came out the last time it was drawn.
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
  [[nodiscard]] std::optional<Role> role(int id) const override {
    if (id == SpeakerVolume || id == KeyboardVolume || id == MouseVolume || id == PenVolume || id == Balance) return Role::Slider;
    if (id == ListenSelf || id == OthersUseMine || id == MuteNotifications || id == MuteOnIdle || id == Spatial || id == BalanceEnabled) return Role::Toggle;
    if (id == SpeakerList || id == KeyboardList || id == MouseList || id == PenList || id == ReverbList) return Role::Dropdown;
    return std::nullopt;
  }
  bool stop_key(int id, unsigned vk, bool ctrl, bool shift, bool alt) override;

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
  void shown() override;
  void before_click(const Surface* owner, int id) override;
  void click(int id, float x, float y, bool shift) override;
  [[nodiscard]] bool acts_on_press(int id) const override;
  void drag(float x, float y) override;
  void release() override;
  bool wheel(float x, float y, float delta) override;
  bool escape() override;

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
  void paint_pack(Painter& p, const skin::Screen& screen, Rect area);
  void paint_tip(Painter& p);

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
  int tip_row_ = -1;
  bool tip_on_ = false;
};

class DisplayTab final : public SettingsTab {
 public:
  explicit DisplayTab(Ui& ui);
  [[nodiscard]] static skin::ScreenSchema schema();
  enum Id : int {
    ScaleSlider = 1,
    ThemeCursor,
    Use24h,
    FetchCancel,
    FetchRetry,
    SkinThemeList = 130,  // every Skin's Themes, each Skin's under its name
    DarkModeList = 190,
    IdleList = 300,
    LanguageList = 330
  };
  [[nodiscard]] std::optional<Role> role(int id) const override {
    if (id == ScaleSlider) return Role::Slider;
    if (id == ThemeCursor || id == Use24h) return Role::Toggle;
    if (id == FetchCancel || id == FetchRetry) return Role::Link;
    if (id == SkinThemeList || id == DarkModeList || id == IdleList || id == LanguageList) return Role::Dropdown;
    return std::nullopt;
  }
  bool stop_key(int id, unsigned vk, bool ctrl, bool shift, bool alt) override;

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
  void shown() override;
  void before_click(const Surface* owner, int id) override;
  void click(int id, float x, float y, bool shift) override;
  [[nodiscard]] bool acts_on_press(int id) const override;
  void drag(float x, float y) override;
  bool wheel(float x, float y, float delta) override;
  bool escape() override;
  [[nodiscard]] Cursor cursor(int /*id*/) const override { return Cursor::Hand; }

 private:
  void paint_pack(Painter& p, const skin::Screen& screen, Rect area);
  void sync_idle();
  void sync_language();

  void choose_theme(int index);
  void choose_dark(int index);
  Dropdown skin_theme_, dark_mode_;
  // Parallel to skin_theme_'s options: the Skin and its Theme, or -1 for a Skin's heading.
  std::vector<std::pair<int, int>> theme_at_;
  Dropdown idle_, language_;
  Rect list_rect_[6];
  Rect idle_rect_, language_rect_;
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
  [[nodiscard]] std::optional<Role> role(int id) const override {
    if (id == TrayToggle || id == FlashToggle) return Role::Toggle;
    if (id >= SoundBase) return Role::Button;
    return std::nullopt;
  }

  // Asked whenever the tab comes on screen, as the sounds may arrive after startup.
  std::function<void()> on_shown;
  std::function<void(const std::string& stem)> on_preview;
  void set_sounds(std::vector<NotifySound> sounds);

  void paint(Painter& p, Rect area) override;
  void shown() override;
  void click(int id, float x, float y, bool shift) override;

 private:
  void paint_pack(Painter& p, const skin::Screen& screen, Rect area);
  std::vector<NotifySound> sounds_;
};

class SystemTab final : public SettingsTab {
 public:
  explicit SystemTab(Ui& ui);
  [[nodiscard]] static skin::ScreenSchema schema();
  enum Id : int { Autostart = 1, Tray, Detail, Logging, OpenLogs, Reset, Address, AddRepository, ReleaseList = 300, RemoveRepository = 500, RemovePack = 700, UpdatePack = 900, Fingerprint = 1100 };
  [[nodiscard]] std::optional<Role> role(int id) const override {
    if (id == Autostart || id == Tray || id == Detail || id == Logging) return Role::Toggle;
    if (id == Address) return Role::Field;
    if (id == ReleaseList) return Role::Dropdown;
    if (id >= Fingerprint) return Role::List;
    if (id == OpenLogs || id == Reset || id == AddRepository || id >= RemoveRepository) return Role::Button;
    return std::nullopt;
  }
  bool stop_key(int id, unsigned vk, bool ctrl, bool shift, bool alt) override;
  void focus(int id) override;

  // A Repository in the list, the official one first, which cannot be removed.
  struct RepositoryRow {
    std::string address;
    std::string fingerprint;  // its first digits, as a pack's id has them
    bool official = false;
  };
  // A Repository being added: its catalog is being read, or could not be, and why.
  struct Adding {
    enum class State : std::uint8_t { None, Checking, Failed };
    State state = State::None;
    std::string reason;
  };
  void set_repositories(std::vector<RepositoryRow> rows) { repositories_ = std::move(rows); }
  [[nodiscard]] const std::vector<RepositoryRow>& repositories() const { return repositories_; }
  void set_adding(Adding adding) { adding_ = std::move(adding); }
  // A Skin Pack fetched from a Repository, with its size and where from, or why it stays. A held
  // one has an update that a pack extending it was not made for, which the user can take anyway.
  struct PackRow {
    std::string name, detail;
    bool removable = false;
    bool held = false;
  };
  void set_packs(std::vector<PackRow> rows) { packs_ = std::move(rows); }
  [[nodiscard]] const std::vector<PackRow>& packs() const { return packs_; }
  [[nodiscard]] const Adding& adding() const { return adding_; }
  void clear_address() { address_.clear(); }
  [[nodiscard]] TextField& address() { return address_; }

  std::function<void()> on_open_logs;
  // What was typed, as typed: the shell reads it as an address.
  std::function<void(std::string address)> on_add_repository;
  std::function<void(std::size_t index)> on_remove_repository;
  std::function<void(std::size_t index)> on_remove_pack;
  std::function<void(std::size_t index)> on_update_pack;

  void paint(Painter& p, Rect area) override;
  void paint_popups(Painter& p, Rect area) override;
  void before_click(const Surface* owner, int id) override;
  void click(int id, float x, float y, bool shift) override;
  [[nodiscard]] bool acts_on_press(int id) const override;
  bool wheel(float x, float y, float delta) override;
  bool escape() override;
  void drag(float x, float y) override;
  void release() override { selecting_ = false; }
  void drop_selection() override { selection_.clear(); }
  bool key(unsigned vk, bool ctrl, bool shift) override;
  void enter(bool shift) override;
  [[nodiscard]] Cursor cursor(int id) const override;
  [[nodiscard]] std::vector<MenuItem> context_menu(int id, float x, float y) override;
  void menu_command(MenuCommand command) override;
  // What is selected of the fingerprints, as a copy takes it.
  [[nodiscard]] std::wstring selected_text() const;
  // A Repository's fingerprint as drawn, a line of text to select: its box on the stage.
  void paint_fingerprint(Painter& p, std::size_t row, Rect box, const TextStyle& style, Color ink, Color selection);

 private:
  [[nodiscard]] bool is_fingerprint(int id) const {
    return id >= Fingerprint && static_cast<std::size_t>(id - Fingerprint) < repositories_.size();
  }
  [[nodiscard]] TextPoint fingerprint_point(float x, float y) const;
  [[nodiscard]] std::wstring_view fingerprint_of(std::size_t row) const;
  void paint_pack(Painter& p, const skin::Screen& screen, Rect area);
  void sync_release();
  void add();
  // Its text scrolls inside the box, as the prompt's does, to keep the caret in view. Colors: the
  // text, the placeholder, the caret, the selection and what is behind.
  void paint_address(Painter& p, Rect box, const TextStyle& style, std::span<const Color> colors);
  [[nodiscard]] std::string fingerprint_text(std::size_t index) const;
  [[nodiscard]] std::string status_text() const;
  Dropdown release_;
  Rect release_rect_;
  std::vector<RepositoryRow> repositories_;
  Adding adding_;
  std::vector<PackRow> packs_;
  TextField address_;
  TextStyle address_style_;
  float address_x_ = 0, address_scroll_ = 0;  // where its text starts on the stage, and how far it is scrolled
  // The fingerprints as drawn last, one block of text each.
  struct FingerprintLine {
    std::wstring text;
    Rect box;
  };
  std::vector<FingerprintLine> fingerprints_;
  TextStyle fingerprint_style_;
  ReadSelection selection_;
  bool selecting_ = false;
  bool menu_for_address_ = false;  // what the open context menu is about
};

class AccountTab final : public SettingsTab {
 public:
  using SettingsTab::SettingsTab;
  [[nodiscard]] static skin::ScreenSchema schema();
  enum Id : int { Emoji, Nickname, Handle, Logout };
  [[nodiscard]] std::optional<Role> role(int id) const override {
    return id == Emoji || id == Nickname || id == Handle || id == Logout ? std::optional(Role::Button) : std::nullopt;
  }

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
  void click(int id, float x, float y, bool shift) override;

 private:
  void paint_pack(Painter& p, const skin::Screen& screen, Rect area);
  Profile profile_;
};

}  // namespace crpp::ui
