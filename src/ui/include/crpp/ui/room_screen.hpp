#pragma once
// Inside a Room: the cloud, the member list, the chat and the room's controls.
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "crpp/ui/chat_panel.hpp"
#include "crpp/ui/surface.hpp"

namespace crpp::ui {

struct MemberView {
  std::uint32_t slot = 0;
  std::string nick;
  std::string emoji;  // Twemoji file id; empty = the default droplet
  bool offline = false;
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
  std::function<void(std::string)> on_copy;

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
  [[nodiscard]] std::size_t member_count() const { return members_.size(); }

  void paint(Painter& p);
  void before_click(const Surface* owner, int id) override;
  void click(int id, float x, float y, bool shift) override;
  bool right_click(int id, float x, float y) override;
  void drag(float x, float y) override;
  bool wheel(float x, float y, float delta) override;
  [[nodiscard]] Cursor cursor(int id) const override;
  [[nodiscard]] bool animating() const override { return !pulses_.empty(); }
  bool tick(unsigned long long now_ms) override;

 private:
  // Fixed values plus MemberBase + slot.
  enum Id : int { Leave, Mute, More, OptCode, OptPublic, OptChat, VolumeSlider, Popup, MemberBase = 1000 };
  struct Layout {
    float members_x, members_y, members_h;  // window coordinates
    bool single_column;
    Rect chat;  // the chat panel, window coordinates
  };

  [[nodiscard]] Layout layout() const;
  void paint_controls(Painter& p);
  void paint_options_popup(Painter& p);
  void paint_mute_popup(Painter& p);
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
  int volume_ = 30;
  Rect volume_track_;
  std::vector<std::pair<std::uint32_t, unsigned long long>> pulses_;  // slot, expiry (ms)
};

}  // namespace crpp::ui
