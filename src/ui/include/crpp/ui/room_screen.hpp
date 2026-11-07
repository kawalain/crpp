#pragma once
// Inside a Room: the cloud, the member list, the chat and the room's controls.
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "crpp/ui/chat_panel.hpp"
#include "crpp/ui/controls.hpp"
#include "crpp/ui/dialogs.hpp"
#include "crpp/ui/member_menu.hpp"
#include "crpp/ui/prefs.hpp"
#include "crpp/ui/profile_card.hpp"
#include "crpp/ui/settings_rows.hpp"
#include "crpp/ui/surface.hpp"
#include "crpp/ui/tip.hpp"

namespace crpp::ui {

struct MemberView {
  std::uint32_t slot = 0;
  std::string nick;
  std::string emoji;  // Twemoji file id; empty = the default droplet
  bool offline = false;
  std::string cid = {};      // who they are across reconnects
  bool reconnected = false;  // this update is them coming back, not entering
};

class RoomScreen final : public Surface {
 public:
  explicit RoomScreen(Ui& ui);
  void forget_graphics() { cloud_.reset(); }

  std::function<void()> on_leave;
  std::function<void(bool)> on_set_public;
  std::function<void(bool)> on_set_chat;
  std::function<void(int percent)> on_volume;
  std::function<void(bool)> on_mute;
  // False if the clipboard refused it.
  std::function<bool(std::string)> on_copy;

  std::function<void(std::uint32_t slot)> on_request_profile;
  // One member's loudness here. `on_member_muted` fires when it crosses 0%.
  std::function<void(const std::string& cid, int percent)> on_member_volume;
  std::function<void(const std::string& cid, bool muted)> on_member_muted;
  // The user picked a new emoji for themselves on their card.
  std::function<void(const std::string& id)> on_pick_emoji;

  // Hit ids tests click by.
  enum Id : int { Leave, Mute, More, OptCode, OptPublic, OptChat, VolumeSlider, Popup, MemberBase = 1000 };

  [[nodiscard]] ChatPanel& chat() { return chat_panel_; }
  [[nodiscard]] ProfileCard& profile_card() { return profile_; }
  [[nodiscard]] MemberMenu& member_menu() { return menu_; }
  // The profile card and the member menu, which speak about the Room as it was.
  void close_member_popups();

  void enter_room(std::string room_code);
  void reset();
  // Replaces the member list and room flags (from joined/members messages).
  void set_room(std::vector<MemberView> members, std::uint32_t me, std::optional<std::uint32_t> host,
                bool is_public, bool chat, std::uint64_t chat_lock_ms = 0);
  // The server's answer to a profile request.
  void show_profile(ProfileView profile);
  // A member made a sound: their emoji ring lights up briefly.
  void pulse(std::uint32_t slot);
  // How strongly a member's speaking disc shows, from 0 to 1.
  [[nodiscard]] float speaking_level(std::uint32_t slot) const;
  // When only speaking discs move: all that the next frame has to draw.
  [[nodiscard]] std::optional<Rect> only_moving_area() const;
  [[nodiscard]] bool needs_full() const { return needs_full_; }
  void pulse_me() { pulse(me_); }
  void set_volume(int percent) { volume_ = percent; }
  // The buttons are not drawn under the settings panel.
  void set_controls_shown(bool shown) { controls_shown_ = shown; }
  [[nodiscard]] std::size_t member_count() const { return members_.size(); }

  void paint(Painter& p);
  // The card and the menu, over the settings droplet as they are over everything else.
  void paint_overlay(Painter& p);
  void before_click(const Surface* owner, int id) override;
  void click(int id, float x, float y, bool shift) override;
  bool right_click(int id, float x, float y) override;
  void drag(float x, float y) override;
  bool wheel(float x, float y, float delta) override;
  bool escape() override;
  [[nodiscard]] Cursor cursor(int id) const override;
  [[nodiscard]] bool animating() const override;
  bool tick(unsigned long long now_ms) override;
  [[nodiscard]] unsigned long long wake_at() const override;

  // What the hover or the last copy shows now, for tests.
  [[nodiscard]] const HoverTip& hover_tip() const { return tip_; }
  [[nodiscard]] const HoverTip& copy_tip() const { return copy_tip_; }
  [[nodiscard]] float members_offset() const { return members_scroll_.offset; }

 private:
  struct Layout {
    float members_x, members_y, members_h;  // window coordinates
    bool single_column;
    Rect chat;  // the chat panel, window coordinates
  };

  [[nodiscard]] Layout layout() const;
  void paint_controls(Painter& p);
  void paint_options_popup(Painter& p);
  void paint_mute_popup(Painter& p);
  void paint_members(Painter& p, const Layout& l);
  void paint_tips(Painter& p);
  void paint_pin(Painter& p, Rect sw, bool on, bool disabled);
  void ask(Dialog dialog);
  void ask_leave();
  void notice_answered(pref::Bool seen);
  void block_switch(int id, const char* text);
  void toggle_public();
  void toggle_chat();
  void toggle_pin(pref::Bool key, bool on);
  [[nodiscard]] bool chat_locked() const;
  [[nodiscard]] bool shaking() const;
  // A hovered thing's label, if it has one.
  [[nodiscard]] std::optional<HoverTip::Spec> tip_for_hover() const;
  void copy_code();
  void change_icon();
  void set_member_volume(std::uint32_t slot, int percent);
  [[nodiscard]] int member_volume(const std::string& cid) const;
  void set_volume_from(float x);
  [[nodiscard]] bool is_host() const { return host_ && *host_ == me_; }
  // main.js SPEAKING_MS and .emoji-ring::before's transition. While held at full strength,
  // renewing it needs no frame.
  static constexpr unsigned long long speaking_hold_ms = 160;
  static constexpr float speaking_fade_ms = 100;
  struct Pulse {
    std::uint32_t slot = 0;
    float rise_from = 0;
    unsigned long long start = 0, until = 0;
    bool was_moving = true;
    [[nodiscard]] float level(unsigned long long now, bool animations) const;
    [[nodiscard]] bool moving(unsigned long long now, bool animations) const;
  };
  [[nodiscard]] bool others_moving() const;
  void add_area(Rect& area, std::uint32_t slot) const;

  ChatPanel chat_panel_;
  ProfileCard profile_;
  MemberMenu menu_;
  // This session's loudness per member; absent means 100%.
  std::vector<std::pair<std::string, int>> member_volumes_;
  void make_cloud();
  ComPtr<ID2D1Geometry> cloud_;
  std::string room_code_;
  std::vector<MemberView> members_;
  std::uint32_t me_ = 0;
  std::optional<std::uint32_t> host_;
  bool public_ = false, chat_ = false;
  bool options_open_ = false, mute_popup_open_ = false;
  bool controls_shown_ = true;
  int volume_ = 30;
  Rect volume_track_;
  std::vector<Pulse> pulses_;
  Rect ended_area_;  // of pulses gone since the last frame, which still has to clear them
  bool needs_full_ = false;
  Fade mute_fade_;
  Scroll members_scroll_;
  std::vector<std::pair<std::uint32_t, unsigned long long>> arrivals_;  // slot, when it arrived
  Rect members_area_;  // window coordinates
  // Where the labelled things were last drawn, window coordinates.
  Rect fab_rects_[3], code_rect_, public_rect_, chat_rect_;
  std::vector<std::pair<std::uint32_t, Rect>> member_rects_;
  HoverTip tip_, copy_tip_;
  std::string copy_text_;
  unsigned long long copy_until_ = 0, hint_muted_until_ = 0;
  unsigned long long chat_lock_at_ = 0;  // after this, chat cannot be turned off
  bool lock_pending_ = false;
  bool dialog_open_ = false;  // Dialogs is showing a question of this screen's
  int blocked_id_ = -1;       // the switch showing why it did not move
  std::string blocked_text_;
  unsigned long long blocked_at_ = 0;
};

}  // namespace crpp::ui
