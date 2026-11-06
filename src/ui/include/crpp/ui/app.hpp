#pragma once
// The Cloudroom UI: screens, hit testing and input handling, independent of the
// window it is shown in. All coordinates passed in are window pixels.
//
// Layout follows the upstream web UI: a fixed 951x692 "stage" scaled to cover
// the window (the cloud art), plus a few controls fixed to the window corners.
#include <functional>
#include <string>
#include <vector>

#include "crpp/ui/gfx.hpp"
#include "crpp/ui/textfield.hpp"

namespace crpp::ui {

inline constexpr float stage_w = 951;
inline constexpr float stage_h = 692;

enum class View : std::uint8_t { Home };

enum class Cursor : std::uint8_t { Arrow, Hand, IBeam };

class App {
 public:
  bool init();
  [[nodiscard]] ID2D1Factory* factory() const { return gfx_.d2d(); }

  // --- state the shell sets ---
  void set_theme(ThemeId id) { theme_ = make_theme(id); }
  void set_nickname(std::string nickname) { nickname_ = std::move(nickname); }
  void set_hour(int hour) { hour_ = hour; }  // 0-23, for the greeting

  // --- what the user can ask for ---
  std::function<void()> on_create;
  std::function<void(std::string code)> on_join;
  std::function<void()> on_find_public;
  std::function<void()> on_settings;

  // --- drawing ---
  void paint(ID2D1RenderTarget* rt, float width, float height);

  // --- input; each returns true if the window should repaint ---
  bool mouse_move(float x, float y);
  bool mouse_leave();
  bool mouse_down(float x, float y, bool shift);
  bool char_input(wchar_t c);
  bool key_down(unsigned vk, bool ctrl, bool shift);
  bool ime_composition(std::wstring_view composition, std::wstring_view result);
  bool blink();  // caret timer tick
  [[nodiscard]] Cursor cursor() const;
  [[nodiscard]] Rect caret_rect() const { return caret_; }  // window pixels, for IME placement
  [[nodiscard]] bool wants_blink() const { return focus_ != Focus::None; }

 private:
  enum class Id : std::uint8_t { None, Create, Join, Code, Find, Settings };
  enum class Focus : std::uint8_t { None, Code };
  struct Hit {
    Rect rect;  // window pixels
    Id id;
  };

  void paint_home(Painter& p);
  [[nodiscard]] D2D1_MATRIX_3X2_F code_focus_scale() const;
  void add_hit(Rect stage_rect, Id id);
  void add_hit_window(Rect window_rect, Id id);
  [[nodiscard]] Id hit_at(float x, float y) const;
  [[nodiscard]] float to_stage_x(float x) const { return (x - ox_) / scale_; }
  [[nodiscard]] float to_stage_y(float y) const { return (y - oy_) / scale_; }

  Gfx gfx_;
  Theme theme_ = make_theme(ThemeId::Cloudroom);
  ComPtr<ID2D1PathGeometry> cloud_, droplet_;
  std::string nickname_;
  int hour_ = 12;

  float scale_ = 1, ox_ = 0, oy_ = 0, width_ = stage_w, height_ = stage_h;
  std::vector<Hit> hits_;
  Id hover_ = Id::None;
  Focus focus_ = Focus::None;
  bool caret_on_ = true;
  Rect caret_;
  TextField code_;
  bool create_hover_ = false;
};

}  // namespace crpp::ui
