#include "crpp/ui/context_menu.hpp"

#include <windows.h>

#include "crpp/locale/locale.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

enum TextId : int { Label, Shortcut };
enum NumberId : int { Items, Enabled, Chosen, PointX, PointY, Access };
constexpr skin::Name text_names[] = {{"label", Label}, {"shortcut", Shortcut}};
constexpr skin::Name number_names[] = {{"items", Items}, {"enabled", Enabled}, {"chosen", Chosen}, {"point_x", PointX}, {"point_y", PointY}, {"access", Access}};
constexpr skin::Name hit_names[] = {{"card", ContextMenu::Card}, {"item", ContextMenu::Item}};

// The Locale's wording, with & before the access key as Windows writes it.
const char* wording(MenuCommand command) {
  switch (command) {
    case MenuCommand::CopyLink: return tr().menu_copy_link;
    case MenuCommand::Cut: return tr().menu_cut;
    case MenuCommand::Copy: return tr().menu_copy;
    case MenuCommand::Paste: return tr().menu_paste;
    case MenuCommand::Delete: return tr().menu_delete;
    case MenuCommand::SelectAll: return tr().menu_select_all;
  }
  return "";
}

}  // namespace

skin::ScreenSchema ContextMenu::schema() { return {.screen = "context_menu", .texts = text_names, .numbers = number_names, .hits = hit_names}; }

std::string ContextMenu::label(MenuCommand command) { return locale::label(wording(command)); }

wchar_t ContextMenu::access_key(MenuCommand command) { return static_cast<wchar_t>(locale::access_key(wording(command))); }

const char* ContextMenu::shortcut(MenuCommand command) {
  switch (command) {
    case MenuCommand::Cut: return "Ctrl+X";
    case MenuCommand::Copy: return "Ctrl+C";
    case MenuCommand::Paste: return "Ctrl+V";
    case MenuCommand::Delete: return "Del";
    case MenuCommand::SelectAll: return "Ctrl+A";
    case MenuCommand::CopyLink: return "";
  }
  return "";
}

void ContextMenu::open(Surface* target, std::vector<MenuItem> items, float x, float y, bool from_keyboard) {
  if (!target || items.empty()) return;
  target_ = target;
  items_ = std::move(items);
  labels_.clear();
  for (const MenuItem& item : items_) labels_.push_back(label(item.command));
  x_ = x;
  y_ = y;
  window_ = ui_.window;
  width_ = ui_.width;
  height_ = ui_.height;
  chosen_ = -1;
  from_keyboard_ = from_keyboard;
  if (from_keyboard) choose(-1, 1);
}

void ContextMenu::close() {
  target_ = nullptr;
  items_.clear();
  labels_.clear();
  chosen_ = -1;
}

void ContextMenu::choose(int from, int step) {
  const int n = static_cast<int>(items_.size());
  for (int i = 1; i <= n; ++i) {
    const int at = ((from + step * i) % n + n) % n;
    if (items_[static_cast<std::size_t>(at)].enabled) {
      chosen_ = at;
      return;
    }
  }
}

void ContextMenu::run(std::size_t index) {
  if (index >= items_.size() || !items_[index].enabled) return;
  Surface* const target = target_;
  const MenuCommand command = items_[index].command;
  close();
  target->menu_command(command);
}

void ContextMenu::before_click(const Surface* owner, int) {
  if (is_open() && owner != this) close();
}

void ContextMenu::click(int id, float, float, bool) {
  if (id >= Item) run(static_cast<std::size_t>(id - Item));
}

bool ContextMenu::escape() {
  if (!is_open()) return false;
  close();
  return true;
}

bool ContextMenu::key(unsigned vk, bool, bool) {
  if (!is_open()) return false;
  const int n = static_cast<int>(items_.size());
  switch (vk) {
    case VK_UP: choose(chosen_ < 0 ? n : chosen_, -1); return true;
    case VK_DOWN: choose(chosen_, 1); return true;
    case VK_HOME: choose(-1, 1); return true;
    case VK_END: choose(n, -1); return true;
    case VK_RETURN:
    case VK_SPACE:
      if (chosen_ >= 0) run(static_cast<std::size_t>(chosen_));
      return true;
    case VK_ESCAPE:
    case VK_MENU:
    case VK_F10:
    case VK_APPS: close(); return true;
    default: break;
  }
  // An access key runs its item at once.
  for (std::size_t i = 0; i < items_.size(); ++i) {
    if (access_key(items_[i].command) == static_cast<wchar_t>(vk)) {
      run(i);
      return true;
    }
  }
  return true;
}

void ContextMenu::paint(Painter& p) {
  if (!is_open() || ui_.window != window_) return;
  if (ui_.width != width_ || ui_.height != height_) {  // the window changed size: it has nothing to point at now
    close();
    return;
  }
  if (const skin::Screen* screen = ui_.pack_screen("context_menu")) paint_pack(p, *screen);
}

void ContextMenu::paint_pack(Painter& p, const skin::Screen& screen) {
  class Shown final : public skin::Source {
   public:
    explicit Shown(ContextMenu& menu) : m_(menu) {}
    std::string_view text(int id, int index) override {
      const auto at = static_cast<std::size_t>(index);
      if (at >= m_.items_.size()) return {};
      return id == Label ? std::string_view(m_.labels_[at]) : std::string_view(shortcut(m_.items_[at].command));
    }
    float number(int id, int index) override {
      const auto at = static_cast<std::size_t>(index);
      switch (id) {
        case Items: return static_cast<float>(m_.items_.size());
        case Enabled: return at < m_.items_.size() && m_.items_[at].enabled ? 1.0f : 0.0f;
        case Chosen: return static_cast<float>(m_.chosen_);
        case PointX: return m_.x_;
        case Access: return at < m_.items_.size() && m_.underlined() ? static_cast<float>(locale::access_index(wording(m_.items_[at].command))) : -1.0f;
        default: return m_.y_;
      }
    }

   private:
    ContextMenu& m_;
  } shown(*this);
  p.set_base(D2D1::Matrix3x2F::Identity());
  draw_layer(ui_, p, this, screen, {0, 0, ui_.width, ui_.height}, shown);
}

}  // namespace crpp::ui
