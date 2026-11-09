#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "crpp/ui/prefs.hpp"
#include "crpp/ui/settings_tabs.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

constexpr skin::Name slider_tip_text_names[] = {{"text", 0}};
constexpr skin::Name slider_tip_number_names[] = {{"track_x", 0}, {"track_y", 1}, {"track_w", 2}, {"percent", 3}, {"on", 4}};

constexpr int balance_min = 55, balance_max = 90;  // BALANCE_MIN, BALANCE_MAX
// The volume of each row, by SoundDevice after the speaker's.
constexpr std::array<pref::Int, 4> volume_rows{pref::volume, pref::keyboard_volume, pref::mouse_volume, pref::pen_volume};

struct ToggleRow {
  SoundTab::Id id;
  pref::Bool key;
};
constexpr std::array<ToggleRow, 6> toggle_rows{{
    {SoundTab::ListenSelf, pref::listen_self},
    {SoundTab::OthersUseMine, pref::others_use_my_sounds},
    {SoundTab::MuteNotifications, pref::mute_notifications_on_mute},
    {SoundTab::MuteOnIdle, pref::mute_on_idle},
    {SoundTab::Spatial, pref::spatial_audio},
    {SoundTab::BalanceEnabled, pref::channel_balance_enabled},
}};

constexpr bool row_shown(std::size_t row) { return show_pen || row < 3; }

}  // namespace

std::string_view balance_word(int value) {
  if (value <= 65) return tr().sound_balance_weak;
  return value <= 78 ? tr().sound_balance_medium : tr().sound_balance_strong;
}

SoundTab::SoundTab(Ui& ui) : SettingsTab(ui) {
  speaker_.box.id_base = SpeakerList;
  packs_[0].box.id_base = KeyboardList;
  packs_[1].box.id_base = MouseList;
  packs_[2].box.id_base = PenList;
  reverb_.box.id_base = ReverbList;
  reverb_.ids = {"off", "office", "large"};
  hotkey_.box.id_base = HotkeyList;
  hotkey_.ids = {""};
  for (const MuteHotkey& key : mute_hotkeys) hotkey_.ids.emplace_back(key.id);
  refresh_speakers();
}

void SoundTab::shown() {
  close_lists(nullptr);
  if (on_shown) on_shown();
}

void SoundTab::set_speakers(std::vector<SoundChoice> speakers) {
  speakers_ = std::move(speakers);
  refresh_speakers();
}

void SoundTab::set_packs(SoundDevice device, std::vector<SoundChoice> packs) {
  Picker& picker = packs_[static_cast<std::size_t>(device)];
  picker.ids.clear();
  picker.box.options.clear();
  for (SoundChoice& pack : packs) {
    picker.ids.push_back(std::move(pack.id));
    picker.box.options.push_back(std::move(pack.name));
  }
}

void SoundTab::refresh_speakers() {
  const std::string current = ui_.prefs ? ui_.prefs->get(pref::speaker_device) : std::string{};
  speaker_.ids = {""};
  speaker_.box.options = {tr().sound_default_output};
  // A saved device that is away stays on the list, so the choice can be seen.
  if (!current.empty() && std::ranges::none_of(speakers_, [&](const SoundChoice& c) { return c.id == current; })) {
    const std::string name = ui_.prefs->get(pref::speaker_device_name);
    speaker_.ids.push_back(current);
    speaker_.box.options.push_back(name.empty() ? std::string(tr().sound_current_output) : name);
  }
  for (const SoundChoice& speaker : speakers_) {
    speaker_.ids.push_back(speaker.id);
    speaker_.box.options.push_back(speaker.name);
  }
}

void SoundTab::sync_selection() {
  const auto index_of = [](const Picker& picker, const std::string& id) {
    const auto found = std::ranges::find(picker.ids, id);
    return found == picker.ids.end() ? -1 : static_cast<int>(found - picker.ids.begin());
  };
  const std::string speaker = ui_.prefs->get(pref::speaker_device);
  if (index_of(speaker_, speaker) < 0) refresh_speakers();
  speaker_.box.selected = std::max(0, index_of(speaker_, speaker));
  packs_[0].box.selected = std::max(0, index_of(packs_[0], ui_.prefs->get(pref::keyboard_sound)));
  packs_[1].box.selected = std::max(0, index_of(packs_[1], ui_.prefs->get(pref::mouse_sound)));
  packs_[2].box.selected = std::max(0, index_of(packs_[2], ui_.prefs->get(pref::pen_sound)));
  // A name this version does not know sounds like the default, so it shows as that.
  const int reverb = index_of(reverb_, ui_.prefs->get(pref::reverb));
  reverb_.box.selected = reverb >= 0 ? reverb : index_of(reverb_, pref::reverb.fallback);
  hotkey_.box.selected = std::max(0, index_of(hotkey_, ui_.prefs->get(pref::mute_hotkey)));
}

SoundTab::Picker* SoundTab::picker_of_row(std::size_t row) {
  return row == 0 ? &speaker_ : &packs_[row - 1];
}

void SoundTab::paint_tip(Painter& p) {
  // Follows the hover or the drag; fades both ways (.volume-tooltip).
  int hovered = -1;
  for (std::size_t row = 0; row < volume_rows.size(); ++row) {
    const int id = SpeakerVolume + static_cast<int>(row);
    if (row_shown(row) && (held_ == id || ui_.hot(this, id))) hovered = static_cast<int>(row);
  }
  const bool on = hovered >= 0;
  if (on) tip_row_ = hovered;
  tip_on_ = on;
  if (const skin::Screen* screen = ui_.pack_screen("slider_tip")) {
    if (tip_row_ < 0) return;
    const auto row = static_cast<std::size_t>(tip_row_);
    const int value = ui_.prefs->get(volume_rows[row]);
    const std::string text = std::to_string(value) + "%";
    class Shown final : public skin::Source {
     public:
      Shown(std::string_view words, Rect track, int percent, bool on) : text_(words), track_(track), percent_(percent), on_(on) {}
      std::string_view text(int, int) override { return text_; }
      float number(int id, int) override {
        switch (id) {
          case 0: return track_.x;
          case 1: return track_.y;
          case 2: return track_.w;
          case 4: return on_ ? 1.0f : 0.0f;
          default: return static_cast<float>(percent_);
        }
      }

     private:
      std::string_view text_;
      Rect track_;
      int percent_;
      bool on_;
    } shown(text, slider_rects_[row], value, on);
    draw_screen(ui_, p, this, *screen, {0, 0, ui_.width, ui_.height}, shown);
  }
}

void SoundTab::paint(Painter& p, Rect area) {
  if (const skin::Screen* screen = ui_.pack_screen("settings_sound")) paint_pack(p, *screen, area);
}

skin::ScreenSchema SoundTab::tip_schema() {
  return {.screen = "slider_tip", .texts = slider_tip_text_names, .numbers = slider_tip_number_names};
}

void SoundTab::paint_popups(Painter& p, Rect) {
  for (std::size_t i = 0; i < volume_rows.size(); ++i) {
    if (row_shown(i)) picker_of_row(i)->box.paint_list(ui_, this, p, combo_rects_[i]);
  }
  reverb_.box.paint_list(ui_, this, p, combo_rects_[4]);
  hotkey_.box.paint_list(ui_, this, p, combo_rects_[5]);
}

void SoundTab::close_lists(const Picker* except) {
  speaker_.box.open = speaker_.box.open && &speaker_ == except;
  reverb_.box.open = reverb_.box.open && &reverb_ == except;
  hotkey_.box.open = hotkey_.box.open && &hotkey_ == except;
  for (Picker& picker : packs_) picker.box.open = picker.box.open && &picker == except;
}

void SoundTab::before_click(const Surface* owner, int id) {
  if (owner != this) {
    close_lists(nullptr);
    return;
  }
  const Picker* keep = nullptr;
  if (speaker_.box.owns(id)) keep = &speaker_;
  if (reverb_.box.owns(id)) keep = &reverb_;
  if (hotkey_.box.owns(id)) keep = &hotkey_;
  for (const Picker& picker : packs_) {
    if (picker.box.owns(id)) keep = &picker;
  }
  close_lists(keep);
}

void SoundTab::set_slider(int id, float window_x) {
  const float x = ui_.to_stage_x(window_x);
  if (id == Balance) {
    const int value = slider_value(balance_rect_, x, balance_min, balance_max);
    if (value != ui_.prefs->get(pref::channel_balance)) ui_.prefs->set(pref::channel_balance, value);
    return;
  }
  const auto row = static_cast<std::size_t>(id - SpeakerVolume);
  const int value = slider_value(slider_rects_[row], x, 0, 100);
  if (value != ui_.prefs->get(volume_rows[row])) ui_.prefs->set(volume_rows[row], value);
}

void SoundTab::pick(Picker& picker, int id) {
  if (id == picker.box.id_base && !picker.box.open) {
    const auto row = &picker == &hotkey_  ? std::size_t{5}
                     : &picker == &reverb_ ? std::size_t{4}
                     : &picker == &speaker_ ? std::size_t{0}
                                            : static_cast<std::size_t>(&picker - packs_.data()) + 1;
    place_dropdown(ui_, picker.box, combo_rects_[row]);
  }
  if (!picker.box.click(id)) return;
  const auto index = static_cast<std::size_t>(picker.box.selected);
  if (index >= picker.ids.size()) return;
  const std::string& chosen = picker.ids[index];
  if (&picker == &speaker_) {
    ui_.prefs->set(pref::speaker_device_name, chosen.empty() ? std::string_view{} : std::string_view(picker.box.options[index]));
    ui_.prefs->set(pref::speaker_device, chosen);
    refresh_speakers();
    if (on_speaker_picked) on_speaker_picked();
  } else if (&picker == &reverb_) {
    ui_.prefs->set(pref::reverb, chosen);
  } else if (&picker == &hotkey_) {
    hotkey_taken_ = false;  // until the shell has tried the new one
    ui_.prefs->set(pref::mute_hotkey, chosen);
  } else if (&picker == &packs_[0]) {
    ui_.prefs->set(pref::keyboard_sound, chosen);
  } else if (&picker == &packs_[1]) {
    ui_.prefs->set(pref::mouse_sound, chosen);
  } else {
    ui_.prefs->set(pref::pen_sound, chosen);
  }
}

bool SoundTab::acts_on_press(int id) const {
  if ((id >= SpeakerVolume && id <= PenVolume) || id == Balance) return true;
  if (id == speaker_.box.id_base || id == reverb_.box.id_base || id == hotkey_.box.id_base) return true;
  return std::ranges::any_of(packs_, [id](const Picker& picker) { return id == picker.box.id_base; });
}

void SoundTab::click(int id, float x, float, bool) {
  if ((id >= SpeakerVolume && id <= PenVolume) || id == Balance) {
    held_ = id;
    ui_.capture = this;
    set_slider(id, x);
    return;
  }
  for (const ToggleRow& row : toggle_rows) {
    if (row.id != id) continue;
    ui_.prefs->set(row.key, !ui_.prefs->get(row.key));
    return;
  }
  if (speaker_.box.owns(id)) {
    pick(speaker_, id);
    return;
  }
  if (reverb_.box.owns(id)) {
    pick(reverb_, id);
    return;
  }
  if (hotkey_.box.owns(id)) {
    pick(hotkey_, id);
    return;
  }
  for (Picker& picker : packs_) {
    if (picker.box.owns(id)) {
      pick(picker, id);
      return;
    }
  }
}

void SoundTab::drag(float x, float) {
  if (held_ >= 0) set_slider(held_, x);
}

void SoundTab::release() { held_ = -1; }

bool SoundTab::wheel(float x, float y, float delta) {
  if (speaker_.box.wheel(ui_, x, y, delta) || reverb_.box.wheel(ui_, x, y, delta) || hotkey_.box.wheel(ui_, x, y, delta)) return true;
  return std::ranges::any_of(packs_, [&](Picker& picker) { return picker.box.wheel(ui_, x, y, delta); });
}

const Dropdown* SoundTab::dropdown_of(int id) const {
  for (const Dropdown* box : {&speaker_.box, &reverb_.box, &hotkey_.box, &packs_[0].box, &packs_[1].box, &packs_[2].box}) {
    if (box->owns(id)) return box;
  }
  return nullptr;
}

bool SoundTab::stop_key(int id, unsigned vk, bool ctrl, bool, bool alt) {
  if (ctrl) return false;
  if ((id >= SpeakerVolume && id <= PenVolume) || id == Balance) {
    const bool balance = id == Balance;
    const pref::Int key = balance ? pref::channel_balance : volume_rows[static_cast<std::size_t>(id - SpeakerVolume)];
    const std::optional<int> value = slider_key(vk, ui_.prefs->get(key), balance ? balance_min : 0, balance ? balance_max : 100);
    if (!value) return false;
    if (*value != ui_.prefs->get(key)) ui_.prefs->set(key, *value);
    return true;
  }
  for (Dropdown* box : {&speaker_.box, &reverb_.box, &hotkey_.box, &packs_[0].box, &packs_[1].box, &packs_[2].box}) {
    if (box->owns(id)) {
      const std::optional<int> hit = box->key(ui_, this, vk, alt);
      if (!hit) return false;
      if (*hit != Dropdown::taken_alone) click(*hit, 0, 0, false);
      return true;
    }
  }
  return false;
}

bool SoundTab::escape() {
  const bool any = speaker_.box.open || reverb_.box.open || hotkey_.box.open || std::ranges::any_of(packs_, [](const Picker& picker) { return picker.box.open; });
  close_lists(nullptr);
  return any;
}

}  // namespace crpp::ui
