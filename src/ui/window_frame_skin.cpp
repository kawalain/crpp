// The window's frame, caption, menu bar and open menu drawn from a Skin Pack.
#include "crpp/skin/draw.hpp"
#include "crpp/ui/skin_canvas.hpp"
#include "crpp/ui/window_frame.hpp"

namespace crpp::ui {

namespace {

enum NumberId : int { Active, HasMinimize, HasClose, MenuShown, OpenMenu };
enum MenuNumberId : int { Items, MenuX, MenuY, Open };

constexpr skin::Name text_names[] = {{"title", 0}};
constexpr skin::Name number_names[] = {{"active", Active}, {"minimize", HasMinimize}, {"close", HasClose}, {"menu_shown", MenuShown}, {"open_menu", OpenMenu}};
constexpr skin::Name picture_names[] = {{"droplet", 0}};
constexpr skin::Name hit_names[] = {{"minimize", WindowFrame::Minimize}, {"close", WindowFrame::Close}, {"menu", WindowFrame::MenuBase}};
constexpr skin::Name note_names[] = {{"menu_0", 0}, {"menu_1", 1}, {"menu_2", 2}};

constexpr skin::Name menu_text_names[] = {{"item", 0}};
constexpr skin::Name menu_number_names[] = {{"items", Items}, {"menu_x", MenuX}, {"menu_y", MenuY}, {"open", Open}};
constexpr skin::Name menu_hit_names[] = {{"item", WindowFrame::ItemBase}};

}  // namespace

skin::ScreenSchema WindowFrame::schema() {
  return {.screen = "frame", .texts = text_names, .numbers = number_names, .pictures = picture_names, .hits = hit_names, .notes = note_names};
}

skin::ScreenSchema WindowFrame::menu_schema() { return {.screen = "frame_menu", .texts = menu_text_names, .numbers = menu_number_names, .hits = menu_hit_names}; }

void WindowFrame::paint_pack(Painter& p, const skin::Screen& screen) {
  // With one menu down, moving over another opens that one.
  if (!menu_shown_) open_ = -1;
  for (int i = 0; i < menus && open_ >= 0; ++i) {
    if (open_ != i && ui_.hot(this, MenuBase + i)) open_ = i;
  }

  class Shown final : public skin::Source {
   public:
    Shown(WindowFrame& frame, Painter& painter) : f_(frame), p_(painter) {}

    std::string_view text(int, int) override { return f_.title_; }

    float number(int id, int) override {
      switch (id) {
        case Active: return f_.active_ ? 1.0f : 0.0f;
        case HasMinimize: return f_.parts_.minimize ? 1.0f : 0.0f;
        case HasClose: return f_.parts_.close ? 1.0f : 0.0f;
        case MenuShown: return f_.menu_shown_ ? 1.0f : 0.0f;
        case OpenMenu: return static_cast<float>(f_.open_);
        default: return 0;
      }
    }

    void picture(int, int, skin::Box box, float, float) override { draw_svg(p_, f_.ui_.emoji_image(""), rect_of(box)); }
    void note(int id, int, skin::Box box) override {
      if (id >= 0 && id < menus) f_.titles_[id] = rect_of(box);
    }

   private:
    WindowFrame& f_;
    Painter& p_;
  } shown(*this, p);

  SkinCanvas canvas(ui_, p, this);
  skin::draw(*ui_.pack, screen, ui_.palette, {0, 0, ui_.width, ui_.height}, canvas, shown);
}

void WindowFrame::paint_menu_pack(Painter& p, const skin::Screen& screen) {
  class Shown final : public skin::Source {
   public:
    explicit Shown(WindowFrame& frame) : f_(frame), items_(frame.items_of(frame.open_)) {}

    std::string_view text(int, int index) override { return static_cast<std::size_t>(index) < items_.size() ? items_[static_cast<std::size_t>(index)].text : ""; }

    float number(int id, int) override {
      switch (id) {
        case Items: return static_cast<float>(items_.size());
        case MenuX: return f_.titles_[f_.open_].x;
        case MenuY: return f_.titles_[f_.open_].bottom();
        case Open: return static_cast<float>(f_.open_);
        default: return 0;
      }
    }

   private:
    WindowFrame& f_;
    std::vector<Item> items_;
  } shown(*this);

  SkinCanvas canvas(ui_, p, this);
  skin::draw(*ui_.pack, screen, ui_.palette, {0, 0, ui_.width, ui_.height}, canvas, shown);
}

}  // namespace crpp::ui
