#include "areas.hpp"
#include "shell.hpp"

namespace crpp::gui::area::account {

void wire(Shell&) {}

bool scene(Shell&, std::string_view) { return false; }

bool begin_first_run(Shell&) { return false; }

}  // namespace crpp::gui::area::account
