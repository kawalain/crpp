// The settings drawn from a Skin Pack. The tabs keep their state, hit ids and callbacks.
#include <algorithm>
#include <string>

#include "crpp/skin/draw.hpp"
#include "crpp/ui/chat_panel.hpp"
#include "crpp/ui/emoji_picker.hpp"
#include "crpp/ui/settings_panel.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

constexpr float yes(bool b) { return b ? 1.0f : 0.0f; }
constexpr skin::Name height_report[] = {{"height", 0}};

// --- the panel ---
enum PanelNumber : int { Page, Overflow, View, Content, Offset, HasVersion, Open, StageScale, WindowX, WindowY, WindowW, WindowH, TabHeight, Drawn, FadeTop, FadeBottom,
                         TabChanges, Selects, LeftScroll, LeftHeight, LeftFadeTop, LeftFadeBottom, BarShown, BarOver, ThumbY, ThumbH };
enum PanelSlot : int { TabSlot, PopupsSlot, CurrentSlot, LeavingSlot };
enum PanelReport : int { ViewReport, ContentReport };
enum PanelNote : int { TrackNote, PaneNote };
constexpr skin::Name panel_texts[] = {{"version", 0}};
constexpr skin::Name panel_numbers[] = {{"page", Page}, {"overflow", Overflow}, {"view", View}, {"content", Content}, {"offset", Offset}, {"has_version", HasVersion},
                                       {"open", Open}, {"scale", StageScale}, {"window_x", WindowX}, {"window_y", WindowY}, {"window_w", WindowW}, {"window_h", WindowH}, {"tab_height", TabHeight},
                                       {"scroll", Drawn}, {"fade_top", FadeTop}, {"fade_bottom", FadeBottom}, {"tab_changes", TabChanges}, {"selects", Selects},
                                       {"left_scroll", LeftScroll}, {"left_height", LeftHeight}, {"left_fade_top", LeftFadeTop}, {"left_fade_bottom", LeftFadeBottom},
                                       {"bar_shown", BarShown}, {"bar_over", BarOver}, {"thumb_y", ThumbY}, {"thumb_h", ThumbH}};
constexpr skin::Name panel_reports[] = {{"view", ViewReport}, {"content", ContentReport}};
constexpr skin::Name droplet_numbers[] = {{"open", 0}};
constexpr skin::Name droplet_hits[] = {{"droplet", SettingsPanel::Droplet}};
constexpr skin::Name panel_slots[] = {{"tab", TabSlot}, {"popups", PopupsSlot}, {"current", CurrentSlot}, {"leaving", LeavingSlot}};
constexpr skin::Name panel_notes[] = {{"track", TrackNote}, {"pane", PaneNote}};
constexpr skin::Name panel_hits[] = {{"overlay", SettingsPanel::Overlay}, {"droplet", SettingsPanel::Droplet}, {"thumb", SettingsPanel::Thumb}, {"version", SettingsPanel::Version}, {"track", SettingsPanel::Track}, {"tab", SettingsPanel::TabBase}};

// --- sound ---
enum SoundNumber : int { SpeakerVolumeN, KeyboardVolumeN, MouseVolumeN, PenVolumeN, ShowPen, ListenSelfN, OthersN, MuteNotifyN, MuteIdleN, SpatialN, BalanceEnabledN, BalanceOn, BalanceN };
enum SoundNote : int { Slider0, Slider1, Slider2, Slider3, BalanceNote };
constexpr skin::Name sound_texts[] = {{"balance_word", 0}};
constexpr skin::Name sound_numbers[] = {{"speaker_volume", SpeakerVolumeN}, {"keyboard_volume", KeyboardVolumeN}, {"mouse_volume", MouseVolumeN}, {"pen_volume", PenVolumeN}, {"show_pen", ShowPen},
                                        {"listen_self", ListenSelfN}, {"others_use_mine", OthersN}, {"mute_notifications", MuteNotifyN}, {"mute_on_idle", MuteIdleN}, {"spatial", SpatialN},
                                        {"balance_enabled", BalanceEnabledN}, {"balance_on", BalanceOn}, {"balance", BalanceN}};
constexpr skin::Name sound_slots[] = {{"speaker_list", 0}, {"keyboard_list", 1}, {"mouse_list", 2}, {"pen_list", 3}, {"reverb_list", 4}};
constexpr skin::Name sound_notes[] = {{"slider_0", Slider0}, {"slider_1", Slider1}, {"slider_2", Slider2}, {"slider_3", Slider3}, {"balance", BalanceNote}};
constexpr skin::Name sound_hits[] = {{"speaker_volume", SoundTab::SpeakerVolume}, {"keyboard_volume", SoundTab::KeyboardVolume}, {"mouse_volume", SoundTab::MouseVolume}, {"pen_volume", SoundTab::PenVolume},
                                     {"listen_self", SoundTab::ListenSelf}, {"others_use_mine", SoundTab::OthersUseMine}, {"mute_notifications", SoundTab::MuteNotifications}, {"mute_on_idle", SoundTab::MuteOnIdle},
                                     {"spatial", SoundTab::Spatial}, {"balance_enabled", SoundTab::BalanceEnabled}, {"balance", SoundTab::Balance}};

// --- display ---
enum DisplayText : int { SampleSays, PercentText, SampleNick, SampleTime };
enum DisplayNumber : int { Sync, Percent, HasCursors, ThemeCursorOn, DisplayScale, Dragging };
constexpr skin::Name display_texts[] = {{"sample_says", SampleSays}, {"percent_text", PercentText}, {"sample_nick", SampleNick}, {"sample_time", SampleTime}};
constexpr skin::Name display_numbers[] = {{"sync", Sync}, {"percent", Percent}, {"has_cursors", HasCursors}, {"theme_cursor", ThemeCursorOn}, {"scale", DisplayScale}, {"dragging", Dragging}};
constexpr skin::Name display_pictures[] = {{"sample_emoji", 0}};
constexpr skin::Name display_slots[] = {{"theme_list", 0}, {"light_list", 1}, {"dark_list", 2}};
constexpr skin::Name display_notes[] = {{"slider", 0}};
constexpr skin::Name display_hits[] = {{"system_theme", DisplayTab::SystemTheme}, {"scale_slider", DisplayTab::ScaleSlider}, {"theme_cursor", DisplayTab::ThemeCursor}};

// --- notifications ---
enum NotifyNumber : int { TrayN, FlashN, Sounds, SoundOn };
constexpr skin::Name notify_texts[] = {{"sound_name", 0}};
constexpr skin::Name notify_numbers[] = {{"tray", TrayN}, {"flash", FlashN}, {"sounds", Sounds}, {"sound_on", SoundOn}};
constexpr skin::Name notify_hits[] = {{"tray", NotifyTab::TrayToggle}, {"flash", NotifyTab::FlashToggle}, {"sound", NotifyTab::SoundBase}, {"preview", NotifyTab::PreviewBase}};

// --- system ---
enum SystemNumber : int { AutostartN, SystemTrayN, DetailN, Use24hN, LoggingN };
constexpr skin::Name system_numbers[] = {{"autostart", AutostartN}, {"tray", SystemTrayN}, {"detail", DetailN}, {"use_24h", Use24hN}, {"logging", LoggingN}};
constexpr skin::Name system_slots[] = {{"idle_list", 0}, {"language_list", 1}};
constexpr skin::Name system_hits[] = {{"autostart", SystemTab::Autostart}, {"tray", SystemTab::Tray}, {"detail", SystemTab::Detail}, {"use_24h", SystemTab::Use24h}, {"logging", SystemTab::Logging}, {"open_logs", SystemTab::OpenLogs}, {"reset", SystemTab::Reset}};

// --- account ---
enum AccountText : int { NicknameT, HandleT, DisplayT };
enum AccountNumber : int { HasNames, HasAccount };
constexpr skin::Name account_texts[] = {{"nickname", NicknameT}, {"handle", HandleT}, {"display", DisplayT}};
constexpr skin::Name account_numbers[] = {{"has_names", HasNames}, {"has_account", HasAccount}};
constexpr skin::Name account_pictures[] = {{"me", 0}};
constexpr skin::Name account_hits[] = {{"emoji", AccountTab::Emoji}, {"nickname", AccountTab::Nickname}, {"change_handle", AccountTab::Handle}, {"logout", AccountTab::Logout}};

}  // namespace

skin::ScreenSchema SettingsPanel::schema() { return {.screen = "settings", .texts = panel_texts, .numbers = panel_numbers, .hits = panel_hits, .slots = panel_slots, .reports = panel_reports, .notes = panel_notes}; }
skin::ScreenSchema SettingsPanel::droplet_schema() { return {.screen = "droplet", .numbers = droplet_numbers, .hits = droplet_hits}; }
skin::ScreenSchema SoundTab::schema() { return {.screen = "settings_sound", .texts = sound_texts, .numbers = sound_numbers, .hits = sound_hits, .slots = sound_slots, .reports = height_report, .notes = sound_notes}; }
skin::ScreenSchema DisplayTab::schema() { return {.screen = "settings_display", .texts = display_texts, .numbers = display_numbers, .pictures = display_pictures, .hits = display_hits, .slots = display_slots, .reports = height_report, .notes = display_notes}; }
skin::ScreenSchema NotifyTab::schema() { return {.screen = "settings_notify", .texts = notify_texts, .numbers = notify_numbers, .hits = notify_hits, .reports = height_report}; }
skin::ScreenSchema SystemTab::schema() { return {.screen = "settings_system", .numbers = system_numbers, .hits = system_hits, .slots = system_slots, .reports = height_report}; }
skin::ScreenSchema AccountTab::schema() { return {.screen = "settings_account", .texts = account_texts, .numbers = account_numbers, .pictures = account_pictures, .hits = account_hits, .reports = height_report}; }

void SettingsPanel::paint_pack(Painter& p, const skin::Screen& screen) {
  if (!open_ && !ui_.skin().scales_stage()) return;  // a window of its own, that is going
  p.set_base(D2D1::Matrix3x2F::Scale(ui_.scale, ui_.scale) * D2D1::Matrix3x2F::Translation(ui_.ox, ui_.oy));

  class Shown final : public skin::Source {
   public:
    Shown(SettingsPanel& panel, Painter& painter) : s_(panel), p_(painter) {}

    std::string_view text(int, int) override { return s_.version_; }

    float number(int id, int) override {
      switch (id) {
        case Page: return static_cast<float>(s_.page_);
        case Overflow: return yes(overflow_);
        case View: return s_.scroll_.view_h;
        case Content: return s_.scroll_.content_h;
        case Offset: return s_.scroll_.offset;
        case HasVersion: return yes(!s_.version_.empty());
        case Open: return yes(s_.open_);
        case StageScale: return s_.ui_.scale;
        case WindowX: return s_.ui_.to_stage_x(0);
        case WindowY: return s_.ui_.to_stage_y(0);
        case WindowW: return s_.ui_.width / s_.ui_.scale;
        case WindowH: return s_.ui_.height / s_.ui_.scale;
        case TabHeight: return height_of_tab();
        case Drawn: return s_.scroll_.drawn();
        case FadeTop: return s_.scroll_.fade_top();
        case FadeBottom: return s_.scroll_.fade_bottom();
        case TabChanges: return static_cast<float>(s_.tab_changes_);
        case Selects: return static_cast<float>(s_.selects_);
        case LeftScroll: return s_.left_scroll_;
        case LeftHeight: return s_.tab_of(s_.left_page_).content_height();
        case LeftFadeTop: return s_.left_fade_top_;
        case LeftFadeBottom: return s_.left_fade_bottom_;
        case BarShown: return yes(s_.bar_shown_);
        case BarOver: return yes(s_.bar_over_);
        case ThumbY: return s_.scroll_.thumb(s_.scroll_track_).y;
        case ThumbH: return s_.scroll_.thumb(s_.scroll_track_).h;
        default: return 0;
      }
    }

    void slot(int id, int, skin::Box box, const skin::SlotValues& values) override {
      SettingsTab& current = s_.tab();
      Ui& ui = s_.ui_;
      // Closing, the panel is only a picture.
      const bool hits = ui.hits_enabled;
      ui.hits_enabled = hits && s_.open_;
      struct Restore {
        Ui& ui;
        bool hits;
        ~Restore() { ui.hits_enabled = hits; }
      } restore{ui, hits};
      if (id == PopupsSlot) {
        current.paint_popups(p_, area_);
        return;
      }
      if (id == LeavingSlot) {
        ui.hits_enabled = false;
        s_.tab_of(s_.left_page_).paint(p_, rect_of(box));
        return;
      }
      if (id == CurrentSlot) {
        area_ = rect_of(box);
        current.paint(p_, area_);
        if (current.content_height() != s_.scroll_.content_h) ui.more_frames = true;  // a row came or went
        return;
      }
      const Rect pane = rect_of(box);
      s_.content_area_ = pane;
      ui.dropdown_view = pane;
      if (current.content_height() <= 0) {  // only drawing a tab tells how tall it is
        draw_unseen(current, pane);
      }
      s_.scroll_.view_h = pane.h;
      s_.scroll_.content_h = current.content_height();
      overflow_ = s_.scroll_.content_h > pane.h;
      s_.scroll_.clamp();
      s_.scroll_.advance(ui);
      area_ = {pane.x, pane.y - s_.scroll_.drawn(), pane.w - (overflow_ ? values.numbers[0] : 0), s_.scroll_.content_h};
      ui.push_hit_clip(pane);
      p_.rt()->PushAxisAlignedClip(D2D1::RectF(pane.x, pane.y, pane.right(), pane.bottom()), D2D1_ANTIALIAS_MODE_ALIASED);
      current.paint(p_, area_);
      p_.rt()->PopAxisAlignedClip();
      ui.pop_hit_clip();
      if (current.content_height() != s_.scroll_.content_h) ui.more_frames = true;  // a row came or went
      s_.scroll_.content_h = current.content_height();
    }

    void note(int id, int, skin::Box box) override {
      if (id == TrackNote) {
        s_.scroll_track_ = rect_of(box);
      } else {
        s_.content_area_ = rect_of(box);
        s_.ui_.dropdown_view = s_.content_area_;
      }
    }

    // For a pack that scrolls the tab itself: its numbers then come from here.
    void report(int id, float value) override {
      Scroll& scroll = s_.scroll_;
      if (id == ViewReport) {
        scroll.view_h = value;
        return;
      }
      scroll.content_h = value;
      overflow_ = scroll.content_h > scroll.view_h;
      scroll.clamp();
      if (s_.pending_scroll_ >= 0 && value > 0) {
        scroll.jump_to(s_.pending_scroll_);
        s_.pending_scroll_ = -1;
      }
      scroll.advance(s_.ui_);
      s_.watch_scrollbar();
    }

   private:
    [[nodiscard]] float height_of_tab() {
      SettingsTab& current = s_.tab();
      if (current.content_height() <= 0) draw_unseen(current, s_.content_area_);
      return current.content_height();
    }

    // Only drawing a tab tells how tall it is.
    void draw_unseen(SettingsTab& tab, Rect at) {
      Ui& ui = s_.ui_;
      const bool hits = ui.hits_enabled;
      ui.hits_enabled = false;
      p_.rt()->PushAxisAlignedClip(D2D1::RectF(0, 0, 0, 0), D2D1_ANTIALIAS_MODE_ALIASED);
      tab.paint(p_, at);
      p_.rt()->PopAxisAlignedClip();
      ui.hits_enabled = hits;
    }

    SettingsPanel& s_;
    Painter& p_;
    bool overflow_ = false;
    Rect area_;
  } shown(*this, p);

  SkinCanvas canvas(ui_, p, this);
  const Rect c = ui_.screen_area();
  skin::draw(*ui_.pack, screen, ui_.palette, {c.x, c.y, c.w, c.h}, canvas, shown);

  if (const skin::Screen* droplet = ui_.pack_screen("droplet")) {
    class Droplet final : public skin::Source {
     public:
      explicit Droplet(bool open) : open_(open) {}
      float number(int, int) override { return yes(open_); }

     private:
      bool open_;
    } on_top(open_);
    p.set_base(D2D1::Matrix3x2F::Identity());
    draw_layer(ui_, p, this, *droplet, {0, 0, ui_.width, ui_.height}, on_top);
  }
}

void SoundTab::paint_pack(Painter& p, const skin::Screen& screen, Rect area) {
  reverb_.box.options = {tr().sound_reverb_off, tr().sound_reverb_office, tr().sound_reverb_large};
  speaker_.box.options[0] = tr().sound_default_output;
  last_area_ = area;
  sync_selection();

  class Shown final : public skin::Source {
   public:
    Shown(SoundTab& tab, Painter& painter) : s_(tab), p_(painter), prefs_(*tab.ui_.prefs) {}

    std::string_view text(int, int) override {
      return on() ? balance_word(balance()) : std::string_view(tr().sound_balance_off);
    }

    float number(int id, int) override {
      switch (id) {
        case SpeakerVolumeN: return static_cast<float>(prefs_.get(pref::volume));
        case KeyboardVolumeN: return static_cast<float>(prefs_.get(pref::keyboard_volume));
        case MouseVolumeN: return static_cast<float>(prefs_.get(pref::mouse_volume));
        case PenVolumeN: return static_cast<float>(prefs_.get(pref::pen_volume));
        case ShowPen: return yes(show_pen);
        case ListenSelfN: return yes(prefs_.get(pref::listen_self));
        case OthersN: return yes(prefs_.get(pref::others_use_my_sounds));
        case MuteNotifyN: return yes(prefs_.get(pref::mute_notifications_on_mute));
        case MuteIdleN: return yes(prefs_.get(pref::mute_on_idle));
        case SpatialN: return yes(prefs_.get(pref::spatial_audio));
        case BalanceEnabledN: return yes(prefs_.get(pref::channel_balance_enabled));
        case BalanceOn: return yes(on());
        case BalanceN: return static_cast<float>(balance());
        default: return 0;
      }
    }

    void slot(int id, int, skin::Box box, const skin::SlotValues&) override {
      const auto row = static_cast<std::size_t>(id);
      const Rect r = rect_of(box);
      s_.combo_rects_[row] = r;
      Dropdown& list = id == 4 ? s_.reverb_.box : s_.picker_of_row(row)->box;
      list.paint_button(s_.ui_, &s_, p_, r);
    }

    void note(int id, int, skin::Box box) override {
      if (id == BalanceNote) s_.balance_rect_ = rect_of(box);
      else s_.slider_rects_[static_cast<std::size_t>(id)] = rect_of(box);
    }

    void report(int, float value) override { s_.flow_height_ = value; }

   private:
    [[nodiscard]] bool on() const { return prefs_.get(pref::spatial_audio) && prefs_.get(pref::channel_balance_enabled); }
    [[nodiscard]] int balance() const { return std::clamp(prefs_.get(pref::channel_balance), 55, 90); }

    SoundTab& s_;
    Painter& p_;
    const Prefs& prefs_;
  } shown(*this, p);

  SkinCanvas canvas(ui_, p, this);
  skin::draw(*ui_.pack, screen, ui_.palette, {area.x, area.y, area.w, area.h}, canvas, shown);
  paint_tip(p, area);
}

void DisplayTab::paint_pack(Painter& p, const skin::Screen& screen, Rect area) {
  const Prefs& prefs = *ui_.prefs;
  const auto every = all_theme_entries(), light = themes_of(false), dark = themes_of(true);
  const auto fill = [](Dropdown& box, const std::vector<const ThemeEntry*>& list, const std::string& id) {
    box.options.clear();
    box.selected = 0;
    for (std::size_t i = 0; i < list.size(); ++i) {
      box.options.emplace_back(tr().*list[i]->label);
      if (list[i]->id == id) box.selected = static_cast<int>(i);
    }
  };
  fill(theme_, every, prefs.get(pref::theme));
  fill(light_, light, prefs.get(pref::light_theme));
  fill(dark_, dark, prefs.get(pref::dark_theme));

  class Shown final : public skin::Source {
   public:
    Shown(DisplayTab& tab, Painter& painter) : s_(tab), p_(painter), percent_(std::clamp(tab.ui_.prefs->get(pref::chat_scale), 80, 150)) {
      says_ = locale::fill(tr().chat_says, {s_.sample_nick_.empty() ? std::string(tr().guest) : s_.sample_nick_});
      percent_text_ = std::to_string(percent_) + "%";
      time_ = chat_clock_text(wall_clock_seconds() * 1000, s_.ui_.prefs->get(pref::use_24h));
    }

    std::string_view text(int id, int) override {
      switch (id) {
        case SampleSays: return says_;
        case SampleNick: return s_.sample_nick_.empty() ? std::string_view(tr().guest) : std::string_view(s_.sample_nick_);
        case SampleTime: return time_;
        default: return percent_text_;
      }
    }
    float number(int id, int) override {
      switch (id) {
        case Sync: return yes(s_.ui_.prefs->get(pref::system_theme));
        case HasCursors: return yes(s_.ui_.pack->cursors.has_value());
        case ThemeCursorOn: return yes(s_.ui_.prefs->get(pref::theme_cursor));
        case DisplayScale: return s_.ui_.scale;
        case Dragging: return yes(s_.ui_.capture == &s_);
        default: return static_cast<float>(percent_);
      }
    }

    void slot(int id, int, skin::Box box, const skin::SlotValues&) override {
      const Rect r = rect_of(box);
      s_.list_rect_[id] = r;
      Dropdown& list = id == 0 ? s_.theme_ : id == 1 ? s_.light_ : s_.dark_;
      list.paint_button(s_.ui_, &s_, p_, r);
    }

    void picture(int, int, skin::Box box, float, float) override { draw_svg(p_, s_.ui_.emoji_image(s_.sample_emoji_), rect_of(box)); }

    void note(int, int, skin::Box box) override { s_.slider_ = rect_of(box); }
    void report(int, float value) override { s_.flow_height_ = value; }

   private:
    DisplayTab& s_;
    Painter& p_;
    int percent_;
    std::string says_, percent_text_, time_;
  } shown(*this, p);

  SkinCanvas canvas(ui_, p, this);
  skin::draw(*ui_.pack, screen, ui_.palette, {area.x, area.y, area.w, area.h}, canvas, shown);
}

void NotifyTab::paint_pack(Painter& p, const skin::Screen& screen, Rect area) {
  class Shown final : public skin::Source {
   public:
    explicit Shown(NotifyTab& tab) : s_(tab) {}

    std::string_view text(int, int index) override { return s_.sounds_[static_cast<std::size_t>(index)].name; }

    float number(int id, int index) override {
      const Prefs& prefs = *s_.ui_.prefs;
      switch (id) {
        case TrayN: return yes(prefs.get(pref::chat_tray_notify));
        case FlashN: return yes(prefs.get(pref::chat_taskbar_flash));
        case Sounds: return static_cast<float>(s_.sounds_.size());
        case SoundOn: return yes(prefs.notification_sound(s_.sounds_[static_cast<std::size_t>(index)].id));
        default: return 0;
      }
    }

    void report(int, float value) override { s_.flow_height_ = value; }

   private:
    NotifyTab& s_;
  } shown(*this);

  SkinCanvas canvas(ui_, p, this);
  skin::draw(*ui_.pack, screen, ui_.palette, {area.x, area.y, area.w, area.h}, canvas, shown);
}

void SystemTab::paint_pack(Painter& p, const skin::Screen& screen, Rect area) {
  idle_.options = {tr().system_idle_15, tr().system_idle_30, tr().system_idle_60};
  sync_idle();
  sync_language();

  class Shown final : public skin::Source {
   public:
    Shown(SystemTab& tab, Painter& painter) : s_(tab), p_(painter) {}

    float number(int id, int) override {
      const Prefs& prefs = *s_.ui_.prefs;
      switch (id) {
        case AutostartN: return yes(prefs.get(pref::start_with_system));
        case SystemTrayN: return yes(prefs.get(pref::minimize_to_tray));
        case DetailN: return yes(prefs.get(pref::input_detail));
        case Use24hN: return yes(prefs.get(pref::use_24h));
        case LoggingN: return yes(prefs.get(pref::log_enabled));
        default: return 0;
      }
    }

    void slot(int id, int, skin::Box box, const skin::SlotValues&) override {
      const Rect r = rect_of(box);
      (id == 0 ? s_.idle_rect_ : s_.language_rect_) = r;
      (id == 0 ? s_.idle_ : s_.language_).paint_button(s_.ui_, &s_, p_, r);
    }

    void report(int, float value) override { s_.flow_height_ = value; }

   private:
    SystemTab& s_;
    Painter& p_;
  } shown(*this, p);

  SkinCanvas canvas(ui_, p, this);
  skin::draw(*ui_.pack, screen, ui_.palette, {area.x, area.y, area.w, area.h}, canvas, shown);
}

void AccountTab::paint_pack(Painter& p, const skin::Screen& screen, Rect area) {
  class Shown final : public skin::Source {
   public:
    Shown(AccountTab& tab, Painter& painter) : s_(tab), p_(painter), handle_("@" + tab.profile_.handle) {}

    std::string_view text(int id, int) override {
      switch (id) {
        case NicknameT: return s_.profile_.nickname;
        case HandleT: return handle_;
        case DisplayT: return s_.profile_.display;
        default: return {};
      }
    }

    float number(int id, int) override {
      return id == HasNames ? yes(!s_.profile_.nickname.empty() && !s_.profile_.handle.empty()) : yes(s_.profile_.has_account);
    }

    void picture(int, int, skin::Box box, float, float) override { draw_svg(p_, s_.ui_.emoji_image(s_.profile_.emoji), rect_of(box)); }
    void report(int, float value) override { s_.flow_height_ = value; }

   private:
    AccountTab& s_;
    Painter& p_;
    std::string handle_;
  } shown(*this, p);

  SkinCanvas canvas(ui_, p, this);
  skin::draw(*ui_.pack, screen, ui_.palette, {area.x, area.y, area.w, area.h}, canvas, shown);
}

}  // namespace crpp::ui
