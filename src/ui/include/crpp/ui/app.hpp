#pragma once
// The Cloudroom UI: screens, hit testing and input handling, independent of the
// window it is shown in. All coordinates are device-independent pixels (1/96
// inch), and the render target's DPI maps them to the monitor. The shell converts
// what comes from window messages.
//
// Layout follows the upstream web UI: a fixed 951x692 "stage" scaled to cover
// the window (the cloud art), content areas that follow the window but keep
// their own size, and a few controls fixed to the window corners.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "crpp/ui/gfx.hpp"
#include "crpp/ui/svg.hpp"
#include "crpp/ui/textfield.hpp"

namespace crpp::ui {

inline constexpr float stage_w = 951;
inline constexpr float stage_h = 692;

enum class View : std::uint8_t { Setup, Home, Session };

enum class Cursor : std::uint8_t { Arrow, Hand, IBeam };

struct MemberView {
  std::uint32_t slot = 0;
  std::string nick;
  std::string emoji;  // Twemoji file id; empty = the default droplet
  bool offline = false;
};

// A message in the window's corner that never blocks the screen under it.
struct Notice {
  std::string text;
  std::string action;   // empty = no action button
  std::string dismiss;  // the label of the closing button
};

struct ChatEntry {
  std::uint32_t slot = 0;
  std::string cid, nick, emoji, text;
  long long time_ms = 0;  // unix epoch, milliseconds
};

class App {
 public:
  // An empty assets_dir draws with a system font until reload_assets.
  bool init(const std::filesystem::path& assets_dir);
  void reload_assets(const std::filesystem::path& assets_dir);
  [[nodiscard]] ID2D1Factory* factory() const { return gfx_.d2d(); }

  // --- state the shell sets ---
  void set_theme(ThemeId id) { theme_ = make_theme(id); }
  void set_nickname(std::string nickname) { nickname_ = std::move(nickname); }
  void set_hour(int hour) { hour_ = hour; }  // 0-23, for the greeting
  // The screen before assets are in place: progress, or an error with a retry.
  void show_setup(int percent, std::string error = {});
  void show_home();
  void show_session(std::string room_code);
  // Replaces the member list and room flags (from joined/members messages).
  void set_room(std::vector<MemberView> members, std::uint32_t me, std::optional<std::uint32_t> host,
                bool is_public, bool chat);
  // A member made a sound: their emoji ring lights up briefly.
  void pulse(std::uint32_t slot);
  void pulse_me() { pulse(me_); }
  void add_chat(ChatEntry entry);
  void clear_chat();
  void set_notice(std::optional<Notice> notice) { notice_ = std::move(notice); }
  void set_volume(int percent) { volume_ = percent; }
  [[nodiscard]] View view() const { return view_; }
  [[nodiscard]] std::size_t member_count() const { return members_.size(); }

  // --- what the user can ask for ---
  std::function<void()> on_create;
  std::function<void(std::string code)> on_join;
  std::function<void()> on_find_public;
  std::function<void()> on_settings;
  std::function<void()> on_leave;
  std::function<void(bool)> on_set_public;
  std::function<void(bool)> on_set_chat;
  std::function<void(int percent)> on_volume;
  std::function<void(bool)> on_mute;
  std::function<void(std::string)> on_copy;
  std::function<void(std::string text)> on_send_chat;
  // Runs once, when the notice's action button is pressed; the notice closes.
  std::function<void()> on_notice_action;
  std::function<void()> on_retry_setup;

  // --- drawing ---
  void paint(ID2D1RenderTarget* rt, float width, float height);

  // --- input; each returns true if the window should repaint ---
  bool mouse_move(float x, float y);
  bool mouse_leave();
  bool mouse_down(float x, float y, bool shift);
  bool mouse_up();
  bool right_click(float x, float y);
  bool wheel(float x, float y, float delta);  // delta in notches (+ = up)
  bool char_input(wchar_t c);
  bool key_down(unsigned vk, bool ctrl, bool shift);
  bool ime_composition(std::wstring_view composition, std::wstring_view result);
  bool blink();  // caret timer tick
  // True while a timed effect (member ring pulse) needs more frames.
  [[nodiscard]] bool animating() const { return !pulses_.empty(); }
  bool tick(unsigned long long now_ms);  // expires effects; true if the view changed
  [[nodiscard]] Cursor cursor() const;
  [[nodiscard]] Rect caret_rect() const { return caret_; }  // for IME placement
  [[nodiscard]] bool wants_blink() const { return focus_ != Focus::None; }

 private:
  // Hit ids: fixed values plus MemberBase + slot.
  enum Id : int {
    None, Create, Join, Code, Find, Settings, Leave, Mute, More, OptCode, OptPublic, OptChat, VolumeSlider, Popup, ChatInput, ChatSend, ChatArea, NoticeAction, NoticeDismiss, SetupRetry,
    MemberBase = 1000,
  };
  enum class Focus : std::uint8_t { None, Code, Chat };
  struct Hit {
    Rect rect;  // window coordinates, not stage ones
    int id;
  };
  struct SessionLayout {
    float members_x, members_y, members_h;  // window coordinates
    bool single_column;
    Rect chat;  // the chat panel, window coordinates
  };

  void paint_setup(Painter& p);
  void paint_home(Painter& p);
  void paint_session(Painter& p);
  void paint_chat(Painter& p, const SessionLayout& layout);
  void send_chat();
  void paint_corner_controls(Painter& p);
  void paint_session_controls(Painter& p);
  void paint_options_popup(Painter& p);
  void paint_mute_popup(Painter& p);
  void paint_notice(Painter& p);
  void paint_switch(Painter& p, Rect r, bool on, bool disabled, bool hot);
  [[nodiscard]] SessionLayout session_layout() const;
  [[nodiscard]] D2D1_MATRIX_3X2_F code_focus_scale() const;
  void add_hit(Rect stage_rect, int id);
  void add_hit_window(Rect window_rect, int id);
  [[nodiscard]] int hit_at(float x, float y) const;
  void set_volume_from(float x);
  [[nodiscard]] float to_stage_x(float x) const { return (x - ox_) / scale_; }
  [[nodiscard]] float to_stage_y(float y) const { return (y - oy_) / scale_; }
  [[nodiscard]] bool is_host() const { return host_ && *host_ == me_; }
  [[nodiscard]] bool speaking(std::uint32_t slot) const;

  Gfx gfx_;
  std::optional<EmojiCache> emoji_;
  Theme theme_ = make_theme(ThemeId::Cloudroom);
  ComPtr<ID2D1PathGeometry> cloud_, droplet_;
  ComPtr<ID2D1Geometry> session_cloud_;
  std::string nickname_;
  int hour_ = 12;
  View view_ = View::Home;
  std::optional<Notice> notice_;
  int setup_percent_ = 0;
  std::string setup_error_;

  float scale_ = 1, ox_ = 0, oy_ = 0, width_ = stage_w, height_ = stage_h;
  std::vector<Hit> hits_;
  int hover_ = None;
  Focus focus_ = Focus::None;
  bool caret_on_ = true;
  Rect caret_;
  TextField code_;
  TextField chat_input_;
  std::vector<ChatEntry> chat_log_;
  float chat_scroll_ = 0;
  bool chat_stick_ = true;
  float chat_content_h_ = 0, chat_area_h_ = 0;
  Rect chat_area_;

  // Session state.
  std::string room_code_;
  std::vector<MemberView> members_;
  std::uint32_t me_ = 0;
  std::optional<std::uint32_t> host_;
  bool public_ = false, chat_ = false;
  bool options_open_ = false, mute_popup_open_ = false;
  bool muted_ = false;
  int volume_ = 30;
  bool dragging_volume_ = false;
  Rect volume_track_;
  unsigned long long now_ms_ = 0;
  std::vector<std::pair<std::uint32_t, unsigned long long>> pulses_;  // slot, expiry (ms)
};

}  // namespace crpp::ui
