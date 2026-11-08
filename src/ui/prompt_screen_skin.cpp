// The account prompt drawn from a Skin Pack.
#include "crpp/skin/draw.hpp"
#include "crpp/ui/prompt_screen.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

enum TextId : int { Heading, Label, Prefix, Hint, Extra2Text, ExtraText, LinkText };
enum NumberId : int { HasHeading, HasLabel, HasPrefix, HasLink, HasExtra, HasExtra2, Cancelable, Locked, Password, PasswordShown, HintError, Busy, SubmitOff, Valid };

constexpr skin::Name text_names[] = {{"heading", Heading}, {"label", Label}, {"prefix", Prefix}, {"hint", Hint}, {"extra2", Extra2Text}, {"extra", ExtraText}, {"link", LinkText}};
constexpr skin::Name number_names[] = {{"has_heading", HasHeading}, {"has_label", HasLabel}, {"has_prefix", HasPrefix}, {"has_link", HasLink}, {"has_extra", HasExtra}, {"has_extra2", HasExtra2}, {"cancelable", Cancelable},
                                       {"locked", Locked}, {"password", Password}, {"shown", PasswordShown}, {"hint_error", HintError}, {"busy", Busy}, {"submit_off", SubmitOff}, {"valid", Valid}};
constexpr skin::Name picture_names[] = {{"droplet", 0}};
constexpr skin::Name slot_names[] = {{"field", 0}};
constexpr skin::Name hit_names[] = {{"field", PromptScreen::Field}, {"submit", PromptScreen::Submit}, {"eye", PromptScreen::Eye}, {"back", PromptScreen::Back}, {"extra", PromptScreen::Extra}, {"extra2", PromptScreen::Extra2}, {"link", PromptScreen::Link}};

}  // namespace

skin::ScreenSchema PromptScreen::schema() {
  return {.screen = "prompt", .texts = text_names, .numbers = number_names, .pictures = picture_names, .hits = hit_names, .slots = slot_names};
}

void PromptScreen::paint_pack(Painter& p, const skin::Screen& screen, bool is_locked, bool hint_error) {
  p.set_base(D2D1::Matrix3x2F::Identity());

  class Shown final : public skin::Source {
   public:
    Shown(PromptScreen& prompt, Painter& painter, bool locked, bool error) : s_(prompt), p_(painter), locked_(locked), error_(error) {}

    std::string_view text(int id, int) override {
      switch (id) {
        case Heading: return s_.prompt_.heading;
        case Label: return s_.prompt_.label;
        case Prefix: return s_.prompt_.prefix;
        case Hint: return s_.hint_shown_;
        case Extra2Text: return s_.prompt_.extra2;
        case ExtraText: return s_.prompt_.extra;
        case LinkText: return s_.prompt_.link;
        default: return {};
      }
    }

    float number(int id, int) override {
      const auto yes = [](bool b) { return b ? 1.0f : 0.0f; };
      switch (id) {
        case HasHeading: return yes(!s_.prompt_.heading.empty());
        case HasLabel: return yes(!s_.prompt_.label.empty());
        case HasPrefix: return yes(!s_.prompt_.prefix.empty());
        case HasLink: return yes(!s_.prompt_.link.empty());
        case HasExtra: return yes(!s_.prompt_.extra.empty());
        case HasExtra2: return yes(!s_.prompt_.extra2.empty());
        case Cancelable: return yes(s_.prompt_.cancelable);
        case Locked: return yes(locked_);
        case Password: return yes(s_.prompt_.password);
        case PasswordShown: return yes(!s_.field_.password);
        case HintError: return yes(error_);
        case Busy: return yes(s_.busy_);
        case SubmitOff: return yes(!(s_.valid_ && !locked_) || s_.busy_);
        case Valid: return yes(s_.valid_);
        default: return 0;
      }
    }

    void picture(int, int, skin::Box box, float, float) override { draw_svg(p_, s_.ui_.emoji_image(""), rect_of(box)); }

    // Its text scrolls inside the box, to keep the caret in view.
    void slot(int, int, skin::Box at, const skin::SlotValues& values) override {
      const Rect box = rect_of(at);
      const TextStyle style = text_style_of(values.styles[0]);
      const bool focused = s_.ui_.focus == &s_.field_;
      const bool held = s_.ui_.pressed_owner == &s_ && s_.ui_.pressed_id == PromptScreen::Field;
      const Rect caret = s_.line_.paint(s_.ui_, p_, s_.field_, box, style, widen(s_.prompt_.placeholder), values.colors.first(5), focused, held);
      if (focused) s_.ui_.caret = caret;
    }

   private:
    PromptScreen& s_;
    Painter& p_;
    bool locked_, error_;
  } shown(*this, p, is_locked, hint_error);

  SkinCanvas canvas(ui_, p, this);
  canvas.in_window_coordinates();
  const Rect c = ui_.content;
  skin::draw(*ui_.pack, screen, ui_.palette, {c.x, c.y, c.w, c.h}, canvas, shown);
}

}  // namespace crpp::ui
