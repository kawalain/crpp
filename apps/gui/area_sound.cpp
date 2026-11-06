#include "areas.hpp"
#include "shell.hpp"

namespace crpp::gui::area::sound {

void wire(Shell& s) {
  Shell* sp = &s;
  s.app.room().on_mute = [sp](bool muted) {
    sp->audio.muted = muted;
    sp->engine->set_settings(sp->audio);
  };
  s.app.room().on_volume = [sp](int percent) {
    sp->audio.volume = percent;
    sp->engine->set_settings(sp->audio);
    sp->store->set("volume", store::Json(percent));
  };
}

bool scene(Shell&, std::string_view) { return false; }

void pref_changed(Shell&, std::string_view) {}

void set_member_volume(Shell&, std::string_view, int) {}

void set_member_muted(Shell&, std::string_view, bool) {}

void play_notification(Shell&, std::string_view) {}

}  // namespace crpp::gui::area::sound
