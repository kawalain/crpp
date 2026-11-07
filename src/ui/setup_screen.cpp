#include "crpp/ui/setup_screen.hpp"

#include <algorithm>
#include <string>

#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

enum TextId : int { Message, PercentText };
constexpr skin::Name text_names[] = {{"message", Message}, {"percent_text", PercentText}};
constexpr skin::Name number_names[] = {{"percent", 0}};

}  // namespace

skin::ScreenSchema SetupScreen::schema() { return {.screen = "setup", .texts = text_names, .numbers = number_names}; }

void SetupScreen::paint(Painter& p) {
  const Theme& t = ui_.theme;
  p.set_base(D2D1::Matrix3x2F::Identity());
  if (const skin::Screen* screen = ui_.pack_screen("setup")) {
    class Shown final : public skin::Source {
     public:
      explicit Shown(int percent) : percent_(percent), text_(std::to_string(percent) + "%") {}
      std::string_view text(int id, int) override { return id == Message ? std::string_view(tr().setup_message) : std::string_view(text_); }
      float number(int, int) override { return static_cast<float>(percent_); }

     private:
      int percent_;
      std::string text_;
    } shown(percent_);
    draw_screen(ui_, p, this, *screen, {0, 0, ui_.width, ui_.height}, shown);
    return;
  }
  const float w = std::min(380.0f, ui_.width - 32);
  const float x = (ui_.width - w) / 2;
  const float y = ui_.height / 2 - 60;
  const Color ink = t.ink_strong;
  const TextStyle message{.size = 18, .weight = 700, .line_height = 26, .align = Align::Center, .wrap = true};
  p.text(std::string_view(tr().setup_message), {x, y, w, 26}, message, ink);
  const Rect track{x, y + 46, w, 8};
  ui_.skin().progress(p, t, track, percent_);
  const TextStyle percent{.size = 15, .line_height = 22, .align = Align::Center};
  p.text(std::to_string(percent_) + "%", {x, y + 64, w, 22}, percent, ink);
}

}  // namespace crpp::ui
