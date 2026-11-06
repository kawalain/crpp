#include "crpp/sound/catalog.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace crpp::sound {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
  std::ranges::transform(s, s.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
  return s;
}

// A file for the sound is <stem>.wav or <stem>_<digits>.wav (a variation of it).
bool stem_matches(std::string_view file, std::string_view stem) {
  if (!file.ends_with(".wav")) return false;
  file.remove_suffix(4);
  if (!file.starts_with(stem)) return false;
  file.remove_prefix(stem.size());
  if (file.empty()) return true;
  if (file.front() != '_') return false;
  file.remove_prefix(1);
  return !file.empty() && std::ranges::all_of(file, [](char c) { return c >= '0' && c <= '9'; });
}

std::vector<std::string_view> matching(const std::vector<std::string>& files, const std::string& stem) {
  std::vector<std::string_view> found;
  for (const std::string& file : files) {
    if (stem_matches(lower(file), stem)) found.push_back(file);
  }
  return found;
}

std::string lower_name(std::string_view name) { return lower(std::string(name)); }

// The Korean name in a pack.json ({"name": {"ko": "...", "en": "..."}}); these files
// come with the app and hold nothing else, so this is no JSON parser.
std::string pack_name(const fs::path& json, std::string fallback) {
  std::ifstream in(json, std::ios::binary);
  const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  auto at = text.find("\"ko\"");
  if (at == std::string::npos || (at = text.find(':', at)) == std::string::npos ||
      (at = text.find('"', at)) == std::string::npos) {
    return fallback;
  }
  const auto end = text.find('"', at + 1);
  return end == std::string::npos ? fallback : text.substr(at + 1, end - at - 1);
}

bool numbered(std::string_view text) {
  return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

}  // namespace

std::optional<std::string_view> sound_name(wire::Kind kind) noexcept {
  switch (static_cast<std::uint8_t>(kind)) {
    case 0: return "left";
    case 1: return "middle";
    case 2: return "wheel";
    case 3: return "right";
    case 4: return "side_back";
    case 5: return "side_forward";
    case 8: return "left_release";
    case 9: return "middle_release";
    case 10: return "right_release";
    case 11: return "left_hold";
    case 12: return "middle_hold";
    case 13: return "right_hold";
    case 16: return "key_normal";
    case 17: return "mod_tab";
    case 18: return "mod_caps";
    case 19: return "mod_shift_hold";
    case 20: return "mod_ctrl_hold";
    case 21: return "mod_alt_hold";
    case 22: return "mod_enter";
    case 23: return "mod_esc";
    case 24: return "mod_shift";
    case 25: return "mod_ctrl";
    case 26: return "mod_alt";
    case 32: return "pen_tap";
    case 33: return "pen_stroke";
    case 40: return "alpha_R1";
    case 41: return "alpha_R2";
    case 42: return "alpha_R3";
    case 43: return "alpha_R4";
    case 44: return "mod_space";
    case 45: return "mod_backspace";
    case 46: return "mod_shift_release";
    case 47: return "mod_ctrl_release";
    case 48: return "mod_alt_release";
    case 49: return "any";
    default: return std::nullopt;
  }
}

std::optional<fs::path> notification_file(const fs::path& root, std::string_view name) {
  const std::string wanted = lower_name(name) + ".wav";
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(root / "Notifications", ec)) {
    const std::string file = lower(entry.path().filename().string());
    if (file == wanted) return entry.path();
    const auto underscore = file.find('_');
    if (underscore != std::string::npos && numbered(std::string_view(file).substr(0, underscore)) &&
        std::string_view(file).substr(underscore + 1) == wanted) {
      return entry.path();
    }
  }
  return std::nullopt;
}

Device device_of(wire::Kind kind) noexcept {
  const auto k = static_cast<std::uint8_t>(kind);
  if (k < 16) return Device::Mouse;
  if (k < 32 || k >= 40) return Device::Keyboard;
  return Device::Pen;
}

Catalog Catalog::scan(const fs::path& root) {
  Catalog catalog;
  const std::array<std::pair<const char*, Device>, 3> folders{
      {{"Device_Keyboard", Device::Keyboard}, {"Device_Mouse", Device::Mouse}, {"Device_Pen", Device::Pen}}};
  for (const auto& [folder, device] : folders) {
    std::vector<fs::path> dirs;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root / folder, ec)) {
      if (entry.is_directory(ec) && fs::is_regular_file(entry.path() / "pack.json", ec)) dirs.push_back(entry.path());
    }
    std::ranges::sort(dirs);  // the number in front of the name is the display order
    for (const fs::path& dir : dirs) {
      std::string id = lower(dir.filename().string());
      if (const auto underscore = id.find('_');
          underscore != std::string::npos && underscore > 0 &&
          std::all_of(id.begin(), id.begin() + static_cast<std::ptrdiff_t>(underscore), [](char c) { return c >= '0' && c <= '9'; })) {
        id.erase(0, underscore + 1);
      }
      std::string name = pack_name(dir / "pack.json", id);
      Pack pack{device, std::move(id), std::move(name), dir, {}};
      for (const auto& file : fs::directory_iterator(dir, ec)) {
        if (file.is_regular_file(ec)) pack.files.push_back(file.path().filename().string());
      }
      catalog.packs_.push_back(std::move(pack));
    }
  }
  return catalog;
}

std::vector<std::string> Catalog::packs(Device device) const {
  std::vector<std::string> ids;
  for (const Pack& pack : packs_) {
    if (pack.device == device) ids.push_back(pack.id);
  }
  return ids;
}

std::vector<PackInfo> Catalog::choices(Device device) const {
  std::vector<PackInfo> found;
  for (const Pack& pack : packs_) {
    if (pack.device != device) continue;
    if (std::ranges::any_of(pack.files, [](const std::string& file) { return lower(file).ends_with(".wav"); })) {
      found.push_back({pack.id, pack.name});
    }
  }
  return found;
}

const Catalog::Pack* Catalog::find(Device device, std::string_view id) const noexcept {
  const Pack* fallback = nullptr;
  for (const Pack& pack : packs_) {
    if (pack.device != device) continue;
    if (!fallback) fallback = &pack;
    if (pack.id == id) return &pack;
  }
  return fallback;
}

std::optional<fs::path> Catalog::pick(wire::Kind kind, std::string_view pack_id, Random& random) const {
  const auto name_opt = sound_name(kind);
  if (!name_opt) return std::nullopt;
  const std::string_view name = *name_opt;
  const Pack* pack = find(device_of(kind), pack_id);
  if (!pack) return std::nullopt;
  const std::string prefix = pack->id + "_";
  const auto files_of = [&](std::string_view sound) { return matching(pack->files, prefix + lower_name(sound)); };

  std::vector<std::string_view> found;
  const auto paired = [&](std::string_view base) {
    return !files_of(std::string(base) + "_hold").empty() && !files_of(std::string(base) + "_release").empty();
  };
  if (name.ends_with("_release")) {
    const std::string_view base = name.substr(0, name.size() - 8);
    if (!paired(base)) return std::nullopt;  // a press without a pair has one plain sound
    found = files_of(name);
  } else if (name.ends_with("_hold")) {
    const std::string_view base = name.substr(0, name.size() - 5);
    found = files_of(paired(base) ? name : base);
  } else if (name != "any") {
    found = files_of(name);
  }

  // Letter keys carry no row, or the pack lacks this row: any letter sound will do.
  const bool letter = name == "any" || name.starts_with("alpha_R");
  if (found.empty() && letter) {
    std::array<std::string_view, 3> rows{"alpha_R1", "alpha_R2", "alpha_R3"};
    for (std::size_t i = rows.size() - 1; i > 0; --i) std::swap(rows[i], rows[random.below(i + 1)]);
    for (const std::string_view row : rows) {
      found = files_of(row);
      if (!found.empty()) break;
    }
  }
  if (found.empty()) return std::nullopt;
  const std::string_view chosen = found[random.below(found.size())];
  return pack->dir / fs::path(std::string(chosen));
}

}  // namespace crpp::sound
