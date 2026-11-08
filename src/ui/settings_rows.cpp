#include "crpp/ui/settings_rows.hpp"

#include <algorithm>

namespace crpp::ui {

void place_dropdown(const Ui& ui, Dropdown& dropdown, Rect button) {
  if (!ui.pack) return;
  const auto n = static_cast<float>(dropdown.options.size());
  const skin::Pack& k = *ui.pack;
  const float pad = k.constant("list_pad"), item_h = k.constant("list_item_h"), gap = k.constant("list_gap"), max_h = k.constant("list_max_h");
  const float list_h = std::min(max_h, n * item_h + (n - 1) * gap + 2 * pad);
  const float below = ui.dropdown_view.bottom() - button.bottom();
  const float above = button.y - ui.dropdown_view.y;
  dropdown.up = below < list_h + 8 && above > below;
}

}  // namespace crpp::ui
