#include "areas.hpp"
#include "shell.hpp"

namespace crpp::gui::area::os {

void wire(Shell&) {}

bool scene(Shell&, std::string_view) { return false; }

void pref_changed(Shell&, std::string_view) {}

bool on_message(Shell&, UINT, WPARAM, LPARAM, LRESULT&) { return false; }

}  // namespace crpp::gui::area::os
