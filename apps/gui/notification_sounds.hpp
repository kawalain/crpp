#pragma once
// Finds the notification sounds in a pack folder: each .wav is one, in the order of
// their names, named by pack.json under its key.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include "crpp/store/json.hpp"

namespace crpp::gui {

struct FoundSound {
  std::string stem, id, name;  // id: pref::notification_id(stem)
};

template <typename IdOf>
[[nodiscard]] std::vector<FoundSound> find_notification_sounds(const std::filesystem::path& folder, IdOf id_of,
                                                               std::span<const std::string> expected_stems = {}) {
  std::string pack_text;
  {
    std::ifstream in(folder / "pack.json", std::ios::binary);
    pack_text.assign(std::istreambuf_iterator<char>(in), {});
  }
  const auto pack = store::Json::parse(pack_text);

  std::set<std::string> stems(expected_stems.begin(), expected_stems.end());
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
    if (entry.path().extension() != ".wav") continue;
    const auto stem8 = entry.path().stem().u8string();
    stems.emplace(stem8.begin(), stem8.end());
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
