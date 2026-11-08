// The window's frame, caption, menu bar and open menu drawn from a Skin Pack.
#include "crpp/skin/draw.hpp"
#include "crpp/ui/skin_canvas.hpp"
#include "crpp/ui/window_frame.hpp"

namespace crpp::ui {

namespace {

enum TextId : int { Title, MenuFile, MenuTools, MenuHelp };
enum NumberId : int { Active, HasMinimize, HasClose, MenuShown, OpenMenu, KeyMenu, AccessFile, AccessTools, AccessHelp };
enum MenuNumberId : int { Items, MenuX, MenuY, Open, Chosen, Access };

// A menu's words are given without the mark of their access key.
constexpr skin::Name text_names[] = {{"title", Title}, {"menu_file", MenuFile}, {"menu_tools", MenuTools}, {"menu_help", MenuHelp}};
constexpr skin::Name number_names[] = {{"active", Active}, {"minimize", HasMinimize}, {"close", HasClose}, {"menu_shown", MenuShown}, {"open_menu", OpenMenu}, {"key_menu", KeyMenu},
                                       {"access_file", AccessFile}, {"access_tools", AccessTools}, {"access_help", AccessHelp}};
constexpr skin::Name picture_names[] = {{"droplet", 0}};
constexpr skin::Name hit_names[] = {{"minimize", WindowFrame::Minimize}, {"close", WindowFrame::Close}, {"menu", WindowFrame::MenuBase}};
constexpr skin::Name note_names[] = {{"menu_0", 0}, {"menu_1", 1}, {"menu_2", 2}};

constexpr skin::Name menu_text_names[] = {{"item", 0}};
constexpr skin::Name menu_number_names[] = {{"items", Items}, {"menu_x", MenuX}, {"menu_y", MenuY}, {"open", Open}, {"chosen", Chosen}, {"access", Access}};
constexpr skin::Name menu_hit_names[] = {{"item", WindowFrame::ItemBase}};

}  // namespace

skin::ScreenSchema WindowFrame::schema() {
  return {.screen = "frame", .texts = text_names, .numbers = number_names, .pictures = picture_names, .hits = hit_names, .notes = note_names};
}

skin::ScreenSchema WindowFrame::menu_schema() { return {.screen = "frame_menu", .texts = menu_text_names, .numbers = menu_number_names, .hits = menu_hit_names}; }

void WindowFrame::paint_pack(Painter& p, const skin::Screen& screen) {
  // With one menu down, moving onto another opens that one; resting there leaves the keys free to move on.
  if (!menu_shown_) leave_menus();
  int hot = -1;
  for (int i = 0; i < menus; ++i) {
    if (ui_.hot(this, MenuBase + i)) hot = i;
  }
  if (hot != hot_title_) {
    hot_title_ = hot;
    if (hot >= 0 && open_ >= 0 && open_ != hot) open_menu(hot, -1);
  }
  for (int i = 0; i < menus; ++i) labels_[i] = locale::label(title_of(i));

  class Shown final : public skin::Source {
   public:
    Shown(WindowFrame& frame, Painter& painter) : f_(frame), p_(painter) {}

    std::string_view text(int id, int) override { return id >= MenuFile && id <= MenuHelp ? f_.labels_[id - MenuFile] : f_.title_; }

    float number(int id, int) override {
      switch (id) {
        case Active: return f_.active_ ? 1.0f : 0.0f;
        case HasMinimize: return f_.parts_.minimize ? 1.0f : 0.0f;
        case HasClose: return f_.parts_.close ? 1.0f : 0.0f;
        case MenuShown: return f_.menu_shown_ ? 1.0f : 0.0f;
        case OpenMenu: return static_cast<float>(f_.open_);
        case KeyMenu: return f_.open_ < 0 ? static_cast<float>(f_.bar_) : -1.0f;
        case AccessFile:
        case AccessTools:
        case AccessHelp: return f_.underlined() ? static_cast<float>(locale::access_index(title_of(id - AccessFile))) : -1.0f;
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
  // Moving onto an item takes the keyboard's place there.
  int hot = -1;
  for (int i = 0; i < 10; ++i) {
    if (ui_.hot(this, ItemBase + open_ * 10 + i)) hot = i;
  }
  if (hot != hot_item_) {
    hot_item_ = hot;
    if (hot >= 0) item_ = hot;
  }

  class Shown final : public skin::Source {
   public:
    explicit Shown(WindowFrame& frame) : f_(frame) {
      for (const Item& item : frame.items_of(frame.open_)) {
        labels_.push_back(locale::label(item.text));
        access_.push_back(frame.underlined() ? locale::access_index(item.text) : -1);
      }
    }

    std::string_view text(int, int index) override { return static_cast<std::size_t>(index) < labels_.size() ? std::string_view(labels_[static_cast<std::size_t>(index)]) : ""; }

    float number(int id, int index) override {
      switch (id) {
        case Items: return static_cast<float>(labels_.size());
        case MenuX: return f_.titles_[f_.open_].x;
        case MenuY: return f_.titles_[f_.open_].bottom();
        case Open: return static_cast<float>(f_.open_);
        case Chosen: return static_cast<float>(f_.item_);
        case Access: return static_cast<std::size_t>(index) < access_.size() ? static_cast<float>(access_[static_cast<std::size_t>(index)]) : -1.0f;
        default: return 0;
      }
    }

   private:
    WindowFrame& f_;
    std::vector<std::string> labels_;
    std::vector<int> access_;
  } shown(*this);

  SkinCanvas canvas(ui_, p, this);
  skin::draw(*ui_.pack, screen, ui_.palette, {0, 0, ui_.width, ui_.height}, canvas, shown);
}

}  // namespace crpp::ui
