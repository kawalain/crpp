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
#include "crpp/ui/surface.hpp"
#include "crpp/ui/tip.hpp"

namespace crpp::ui {

struct MemberView {
  std::uint32_t slot = 0;
  std::string nick;
  std::string emoji;  // Twemoji file id; empty = the default droplet
  bool offline = false;
  std::string cid;  // identifies them across reconnects
};

class RoomScreen final : public Surface {
 public:
  explicit RoomScreen(Ui& ui);
  bool init();  // needs ui.gfx

  std::function<void()> on_leave;
  std::function<void(bool)> on_set_public;
  std::function<void(bool)> on_set_chat;
  std::function<void(int percent)> on_volume;
  std::function<void(bool)> on_mute;
  // False if the clipboard refused it.
  std::function<bool(std::string)> on_copy;

  // Hit ids tests click by.
  enum Id : int { Leave, Mute, More, OptCode, OptPublic, OptChat, VolumeSlider, Popup, MemberBase = 1000 };

  [[nodiscard]] ChatPanel& chat() { return chat_panel_; }

  void enter_room(std::string room_code);
  void reset();
  // Replaces the member list and room flags (from joined/members messages).
  void set_room(std::vector<MemberView> members, std::uint32_t me, std::optional<std::uint32_t> host,
                bool is_public, bool chat);
  // A member made a sound: their emoji ring lights up briefly.
  void pulse(std::uint32_t slot);
  void pulse_me() { pulse(me_); }
  void set_volume(int percent) { volume_ = percent; }
  // The buttons are not drawn under the settings panel.
  void set_controls_shown(bool shown) { controls_shown_ = shown; }
  [[nodiscard]] std::size_t member_count() const { return members_.size(); }

  void paint(Painter& p);
  void before_click(const Surface* owner, int id) override;
  void click(int id, float x, float y, bool shift) override;
  bool right_click(int id, float x, float y) override;
  void drag(float x, float y) override;
  bool wheel(float x, float y, float delta) override;
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
  // A hovered thing's label, if it has one.
  [[nodiscard]] std::optional<HoverTip::Spec> tip_for_hover() const;
  void copy_code();
  void set_volume_from(float x);
  [[nodiscard]] bool is_host() const { return host_ && *host_ == me_; }
  [[nodiscard]] bool speaking(std::uint32_t slot) const;

  ChatPanel chat_panel_;
  ComPtr<ID2D1Geometry> cloud_;
  std::string room_code_;
  std::vector<MemberView> members_;
  std::uint32_t me_ = 0;
  std::optional<std::uint32_t> host_;
  bool public_ = false, chat_ = false;
  bool options_open_ = false, mute_popup_open_ = false;
  bool muted_ = false;
  bool controls_shown_ = true;
  int volume_ = 30;
  Rect volume_track_;
  std::vector<std::pair<std::uint32_t, unsigned long long>> pulses_;  // slot, expiry (ms)
  Scroll members_scroll_;
  Rect members_area_;  // window coordinates
  // Where the labelled things were last drawn, window coordinates.
  Rect fab_rects_[3], code_rect_;
  std::vector<std::pair<std::uint32_t, Rect>> member_rects_;
  HoverTip tip_, copy_tip_;
  std::string copy_text_;
  unsigned long long copy_until_ = 0, hint_muted_until_ = 0;
};

}  // namespace crpp::ui
