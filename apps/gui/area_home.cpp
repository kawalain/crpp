#include "areas.hpp"
#include "shell.hpp"

using namespace crpp::ui;

namespace crpp::gui::area::home {

void wire(Shell& s) {
  Shell* sp = &s;
  s.app.home().on_create = [sp] { sp->client->create(false, false); };
  s.app.home().on_join = [sp](std::string code) {
    if (const auto seat = sp->store->find_seat(code, unix_ms())) {
      sp->bridge->expect_resume(code);
      sp->client->resume(code, seat->key, std::nullopt, true);
    } else {
      sp->client->join(code);
    }
  };
}

bool scene(Shell&, std::string_view name) { return name == "home"; }

}  // namespace crpp::gui::area::home
