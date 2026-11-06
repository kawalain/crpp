#pragma once
// Finds the notification sounds in a pack folder: each .wav is one, in the order of
// its number prefix, named by pack.json under its key (upstream's discoverNotifications).
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include "crpp/store/json.hpp"

namespace crpp::gui {

struct FoundSound {
  std::string stem, id, name;  // id: pref::notification_id(stem)
  int order = INT_MAX;
};

template <typename IdOf>
[[nodiscard]] std::vector<FoundSound> find_notification_sounds(const std::filesystem::path& folder, IdOf id_of) {
  std::string pack_text;
  {
    std::ifstream in(folder / "pack.json", std::ios::binary);
    pack_text.assign(std::istreambuf_iterator<char>(in), {});
  }
  const auto pack = store::Json::parse(pack_text);

  std::vector<FoundSound> found;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
    if (entry.path().extension() != ".wav") continue;
    FoundSound sound;
    const auto stem8 = entry.path().stem().u8string();
    sound.stem.assign(stem8.begin(), stem8.end());
    sound.id = id_of(sound.stem);
    sound.name = sound.id;
    if (pack) {
      if (const store::Json* item = pack->find(sound.id)) {
        if (const store::Json* name = item->find("name")) {
          if (const auto text = name->as_string()) sound.name = std::string(*text);
        }
      }
    }
    if (std::isdigit(static_cast<unsigned char>(sound.stem.front()))) sound.order = std::atoi(sound.stem.c_str());
    found.push_back(std::move(sound));
  }
  std::ranges::sort(found, [](const FoundSound& a, const FoundSound& b) {
    return a.order != b.order ? a.order < b.order : a.stem < b.stem;
  });
  return found;
}

}  // namespace crpp::gui
