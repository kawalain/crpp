#include "areas.hpp"
#include "shell.hpp"

namespace crpp::gui::area::preferences {

void wire(Shell&) {}

bool scene(Shell& s, std::string_view name) {
  if (name != "settings") return false;
  s.app.settings().open(ui::SettingsPage::Sound);
  s.app.settings().set_version("v0.0.0");
  return true;
}

void pref_changed(Shell&, std::string_view) {}

bool notification_enabled(Shell&, std::string_view) { return true; }

}  // namespace crpp::gui::area::preferences
