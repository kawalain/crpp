#include "crpp/ui/setup_screen.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

enum TextId : int { Message, PercentText, Detail };
enum NumberId : int { Percent, Failed };
constexpr skin::Name text_names[] = {{"message", Message}, {"percent_text", PercentText}, {"detail", Detail}};
constexpr skin::Name number_names[] = {{"percent", Percent}, {"failed", Failed}};
constexpr skin::Name hit_names[] = {{"retry", SetupScreen::Retry}, {"quit", SetupScreen::Quit}, {"backdrop", SetupScreen::Backdrop}, {"card", SetupScreen::Card}};

// One decimal, as the font alone is 13 MB.
std::string megabytes(std::uint64_t bytes) {
  char text[32];
  std::snprintf(text, sizeof text, "%.1f", static_cast<double>(bytes) / (1024.0 * 1024.0));
  return text;
}

std::string detail_of(const SetupScreen::Status& s) {
  if (s.files == 0) return {};
  const std::string done = std::to_string(s.files_done), files = std::to_string(s.files);
  const std::string got = megabytes(s.bytes_done), all = megabytes(s.bytes);
  return locale::fill(tr().setup_detail, {done, files, got, all});
}

}  // namespace

skin::ScreenSchema SetupScreen::schema() {
  return {.screen = "setup", .texts = text_names, .numbers = number_names, .hits = hit_names};
}

int SetupScreen::percent() const {
  if (status_.bytes == 0) return status_.files == 0 ? 0 : static_cast<int>(status_.files_done * 100 / status_.files);
  return static_cast<int>(std::min<std::uint64_t>(100, status_.bytes_done * 100 / status_.bytes));
}

void SetupScreen::click(int id, float, float, bool) {
  if (id == Retry && on_retry) on_retry();
  else if (id == Quit && on_quit) on_quit();
}

void SetupScreen::paint(Painter& p) {
  const Theme& t = ui_.theme;
  p.set_base(D2D1::Matrix3x2F::Identity());
  const int shown_percent = percent();
  const std::string detail = detail_of(status_);
  if (const skin::Screen* screen = ui_.pack_screen("setup")) {
    class Shown final : public skin::Source {
     public:
      Shown(const Status& status, int percent, const std::string& detail)
          : status_(status), percent_(percent), percent_text_(std::to_string(percent) + "%"), detail_(detail) {}
      std::string_view text(int id, int) override {
        if (id == Message) return status_.failed ? tr().setup_failed : tr().setup_message;
        return id == Detail ? std::string_view(detail_) : std::string_view(percent_text_);
      }
      float number(int id, int) override { return id == Failed ? (status_.failed ? 1.0f : 0.0f) : static_cast<float>(percent_); }

     private:
      const Status& status_;
      int percent_;
      std::string percent_text_;
      const std::string& detail_;
    } shown(status_, shown_percent, detail);
    draw_screen(ui_, p, this, *screen, {0, 0, ui_.width, ui_.height}, shown);
    return;
  }
  // The app's own screen, in the system's font: it is what shows before any Skin Pack is there.
  const float w = std::min(380.0f, ui_.width - 32);
  const float x = (ui_.width - w) / 2;
  const float y = ui_.height / 2 - 60;
  const Color ink = t.ink_strong;
  const TextStyle message{.size = 18, .weight = 700, .line_height = 26, .align = Align::Center, .wrap = true};
  const TextStyle note{.size = 15, .line_height = 22, .align = Align::Center};
  if (status_.failed) {  // as a question with two answers
    const auto [pad_top, pad_x, pad_bottom, gap, button_h, button_w, button_gap, width, wide_width, margin, text_size, text_line, align] =
        ui_.skin().dialog_metrics();
    const float card_w = std::min(wide_width, ui_.width - margin);
    const TextStyle body{.size = text_size, .weight = 700, .line_height = text_line, .align = align, .wrap = true, .keep_words = true};
    const float text_w = card_w - 2 * pad_x;
    DWRITE_TEXT_METRICS size{};
    if (const auto layout = ui_.gfx.layout(widen(tr().setup_failed), body, text_w)) layout->GetMetrics(&size);
    const Rect card = paint_modal(ui_, this, p, card_w, pad_top + size.height + gap + button_h + pad_bottom, 1, Backdrop, Card);
    p.text(std::string_view(tr().setup_failed), {card.x + pad_x, card.y + pad_top, text_w, size.height}, body, t.hl_text);
    const float left = card.x + (card_w - 2 * button_w - button_gap) / 2, top = card.bottom() - pad_bottom - button_h;
    paint_modal_button(ui_, this, p, {left, top, button_w, button_h}, tr().setup_retry, true, 1, Retry);
    paint_modal_button(ui_, this, p, {left + button_w + button_gap, top, button_w, button_h}, tr().setup_quit, false, 1, Quit);
    return;
  }
  p.text(std::string_view(tr().setup_message), {x, y, w, 26}, message, ink);
  const Rect track{x, y + 46, w, 8};
  ui_.skin().progress(p, t, track, shown_percent);
  p.text(std::to_string(shown_percent) + "%", {x, y + 64, w, 22}, note, ink);
  if (!detail.empty()) p.text(detail, {x, y + 88, w, 22}, note, ink);
}

}  // namespace crpp::ui
