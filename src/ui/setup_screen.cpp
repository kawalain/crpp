#include "crpp/ui/setup_screen.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

enum TextId : int { Message, PercentText, Detail };
enum NumberId : int { Percent, Failed };
constexpr skin::Name text_names[] = {{"message", Message}, {"percent_text", PercentText}, {"detail", Detail}};
constexpr skin::Name number_names[] = {{"percent", Percent}, {"failed", Failed}};
constexpr skin::Name hit_names[] = {{"retry", SetupScreen::Retry}, {"quit", SetupScreen::Quit}, {"backdrop", SetupScreen::Backdrop}, {"card", SetupScreen::Card}};

// Its own colours, which no pack chooses.
constexpr Color plain_back = rgb(0x66ccff), plain_ink = rgb(0x2e5f80), plain_track = rgb(0xd9f2ff), plain_card = rgb(0xFFFFFF);

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
  paint_plain(p, shown_percent, detail);
}

// The app's own screen, as plain as can be, in the system's font: it is what shows before any Skin
// Pack is there, so nothing of a pack can draw it.
void SetupScreen::paint_plain(Painter& p, int shown_percent, const std::string& detail) {
  const Rect window{0, 0, ui_.width, ui_.height};
  p.fill_rect(window, plain_back);
  const float w = std::min(380.0f, ui_.width - 32);
  const float x = (ui_.width - w) / 2;
  const TextStyle message{.size = 18, .weight = 700, .line_height = 26, .align = Align::Center, .wrap = true, .keep_words = true};
  const TextStyle note{.size = 15, .line_height = 22, .align = Align::Center};
  if (!status_.failed) {
    const float y = ui_.height / 2 - 60;
    p.text(std::string_view(tr().setup_message), {x, y, w, 26}, message, plain_ink);
    const Rect track{x, y + 46, w, 8};
    p.fill_round(track, 4, plain_track);
    if (shown_percent > 0) p.fill_round({track.x, track.y, track.w * static_cast<float>(shown_percent) / 100, track.h}, 4, plain_ink);
    p.text(std::to_string(shown_percent) + "%", {x, y + 64, w, 22}, note, plain_ink);
    if (!detail.empty()) p.text(detail, {x, y + 88, w, 22}, note, plain_ink);
    return;
  }
  // A question with two answers, on a card.
  constexpr float pad = 24, gap = 20, button_h = 36, button_w = 120, button_gap = 10;
  DWRITE_TEXT_METRICS size{};
  if (const auto layout = ui_.gfx.layout(widen(tr().setup_failed), message, w - 2 * pad)) layout->GetMetrics(&size);
  const float h = pad + size.height + gap + button_h + pad;
  const Rect card{x, (ui_.height - h) / 2, w, h};
  ui_.add_hit_window(this, window, Backdrop);
  p.fill_round(card, 12, plain_card);
  ui_.add_hit_window(this, card, Card);
  p.text(std::string_view(tr().setup_failed), {card.x + pad, card.y + pad, w - 2 * pad, size.height}, message, plain_ink);
  const float left = card.x + (w - 2 * button_w - button_gap) / 2, top = card.bottom() - pad - button_h;
  const TextStyle label{.size = 15, .weight = 700, .line_height = button_h, .align = Align::Center};
  const auto button = [&](Rect r, const char* text, bool primary, int id) {
    const bool hot = ui_.hot(this, id);
    if (primary) {
      p.fill_round(r, 8, hot ? mix(plain_ink, plain_card, 85) : plain_ink);
    } else {
      p.fill_round(r, 8, hot ? plain_track : plain_card);
      p.stroke_round(r, 8, plain_ink, 1.5f);
    }
    p.text(std::string_view(text), r, label, primary ? plain_card : plain_ink);
    ui_.add_hit_window(this, r, id);
  };
  button({left, top, button_w, button_h}, tr().setup_retry, true, Retry);
  button({left + button_w + button_gap, top, button_w, button_h}, tr().setup_quit, false, Quit);
}

}  // namespace crpp::ui
