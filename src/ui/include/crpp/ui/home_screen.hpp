#pragma once
// The main menu: create a Room, join one by code, or find a public one.
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "crpp/skin/pack.hpp"
#include "crpp/ui/hover.hpp"
#include "crpp/ui/surface.hpp"

namespace crpp::ui {

class HomeScreen final : public Surface {
 public:
  explicit HomeScreen(Ui& ui);

  // What a Skin Pack's screen for Home can name.
  [[nodiscard]] static skin::ScreenSchema schema();
  [[nodiscard]] static skin::ScreenSchema return_schema();

  enum Id : int { Create, Join, Code, Find, ReturnBackdrop, ReturnCard, ReturnConfirm, ReturnRandom, SeatBase = 100 };

  std::function<void()> on_create;
  std::function<void(std::string code)> on_join;
  std::function<void()> on_find_public;
  // "돌아가기" in the question the search button asks when a Seat is held.
  std::function<void()> on_return;
  // A Room is being entered: the buttons do nothing and nothing about them changes.
  std::function<bool()> busy;
  // How many ms the latest held Seat still lasts, if there is one. Read on every
  // paint, so nobody has to report a Seat coming or going.
  std::function<std::optional<std::uint64_t>()> held_seat_ms;
  // The Rooms with a Seat to go back to, and the user's own picture: read on every paint, by
  // a Skin that shows them.
  std::function<std::vector<std::string>()> held_seats;
  std::function<std::string()> my_emoji;

  void set_nickname(std::string nickname) { nickname_ = std::move(nickname); }
  void set_hour(int hour) { hour_ = hour; }  // 0-23, for the greeting
  // A Room could not be entered: says so above the button that tried, or at the top
  // of the window when no button did.
  void show_error(std::string text);
  // A Room was entered: the question and the typed code do not outlive Home.
  void left_home();
  [[nodiscard]] bool asking_return() const { return return_open_; }
  [[nodiscard]] bool returning() const { return seat_wake_ != 0; }

  void paint(Painter& p);
  // The return question, above the settings droplet like every modal.
  void paint_return(Painter& p);
  [[nodiscard]] bool accepts(int id, float x, float y) const override;
  void click(int id, float x, float y, bool shift) override;
  void drag(float x, float y) override;
  void enter(bool shift) override;
  bool escape() override;
  [[nodiscard]] Cursor cursor(int id) const override;
  [[nodiscard]] bool is_link(int id) const override { return id == Create || id == Find || id >= SeatBase; }
  [[nodiscard]] bool animating() const override;
  bool tick(unsigned long long now_ms) override;
  [[nodiscard]] unsigned long long wake_at() const override;

 private:
  [[nodiscard]] bool is_busy() const { return busy && busy(); }
  void paint_luna(Painter& p);
  void paint_pack(Painter& p, const skin::Screen& screen);
  void paint_return_pack(Painter& p, const skin::Screen& screen);
  [[nodiscard]] TextStyle luna_code_style() const;
  [[nodiscard]] TextStyle field_style() const;
  void join();
  void find();
  void refresh_seat(unsigned long long now);
  [[nodiscard]] bool moving(unsigned long long now) const;
  [[nodiscard]] bool tip_wanted(unsigned long long now) const;

  std::string nickname_;
  // How the pack last drew the code.
  TextStyle pack_code_style_;
  // Where the Skin's layout last put these; a click and a bubble go by them.
  float code_x_ = 469 + 18;
  Rect join_rect_{469 + 221, 522 + 10, 52, 60}, find_rect_{759, 522, 92, 80};
  int hour_ = 12;
  TextField code_;
  Id last_action_ = Create;
  unsigned long long seat_wake_ = 0;  // when the latest held Seat expires; 0 = none
  bool return_open_ = false;
  unsigned long long return_opened_at_ = 0;
  bool was_moving_ = false;
  unsigned long long tip_muted_until_ = 0;  // a bubble is where the search label would be
  // The transitions in style.css.
  Tween create_hover_{160};
  Tween entry_focus_{150};
  Tween enter_hover_{150};
  Tween search_color_{150};
  Tween search_scale_{140};
  Tween seat_light_{150};
  Tween search_tip_{tip_ms};
};

}  // namespace crpp::ui
