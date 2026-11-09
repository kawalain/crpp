#pragma once
// Finds the notification sounds in a pack folder: each .wav is one, in the order of
// their names, named by pack.json under its key.
#include <set>
#include <span>
#include <string>
#include <vector>

#include "crpp/files/tree.hpp"
#include "crpp/store/json.hpp"

namespace crpp::gui {

struct FoundSound {
  std::string stem, id, name;  // id: pref::notification_id(stem)
};

template <typename IdOf>
[[nodiscard]] std::vector<FoundSound> find_notification_sounds(const files::Tree& folder, IdOf id_of,
                                                               std::span<const std::string> expected_stems = {}) {
  const std::string pack_text = folder.read("pack.json", 64 * 1024).value_or(std::string());
  const auto pack = store::Json::parse(pack_text);

  std::set<std::string> stems(expected_stems.begin(), expected_stems.end());
  for (const std::string& name : folder.names_in("")) {
    if (name.size() > 4 && name.ends_with(".wav")) stems.insert(name.substr(0, name.size() - 4));
  }
  std::vector<FoundSound> found;
  for (const std::string& stem : stems) {
    FoundSound sound;
    sound.stem = stem;
    sound.id = id_of(sound.stem);
    sound.name = sound.id;
    if (pack) {
      if (const store::Json* item = pack->find(sound.id)) {
        if (const store::Json* name = item->find("name")) {
          if (const auto text = name->as_string()) sound.name = std::string(*text);
        }
      }
    }
    found.push_back(std::move(sound));
  }
  return found;
}

}  // namespace crpp::gui
