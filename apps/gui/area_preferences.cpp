#include "areas.hpp"
#include "shell.hpp"

namespace crpp::gui::area::preferences {

void wire(Shell&) {}

bool scene(Shell&, std::string_view) { return false; }

void pref_changed(Shell&, std::string_view) {}

}  // namespace crpp::gui::area::preferences
