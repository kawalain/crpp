#include "crpp/assets/assets.hpp"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace crpp::assets {

namespace fs = std::filesystem;

namespace {

constexpr const char* installed_name = ".installed";

// Eight took 9 s against 70 s one at a time from a cold CDN. More scale further, but
// this is a public service met on every first run.
constexpr std::size_t max_workers = 8;

// A passing failure is tried twice more, after these waits.
constexpr std::chrono::milliseconds retry_waits[] = {std::chrono::milliseconds(500), std::chrono::milliseconds(1500)};

// Lines of "<sha256> <path>", written last, so it only lists finished files.
std::unordered_map<std::string, std::string> read_installed(const fs::path& dir) {
  std::unordered_map<std::string, std::string> installed;
  std::ifstream in(dir / installed_name);
  std::string hash, path;
  while (in >> hash >> path) installed[path] = hash;
  return installed;
}

bool has_size(const fs::path& path, std::uint64_t size) {
  std::error_code ec;
  const auto actual = fs::file_size(path, ec);
  return !ec && actual == size;
}

std::vector<std::uint8_t> read_file(const fs::path& path, std::uint64_t limit) {
  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  if (ec || size > limit) return {};
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool write_plainly(const fs::path& path, std::string_view bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out.close();
  return static_cast<bool>(out);
}

// The temporary name means a stop halfway leaves the old file or none. With a platform the bytes
// are on the disk first; the lists written without one are made again when they do not read.
bool replace_file(const fs::path& path, std::string_view bytes, Platform* platform = nullptr) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  if (ec) return false;
  fs::path temp = path;
  temp += ".part";
  const bool wrote = platform ? platform->write_file(temp, {reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()}) : write_plainly(temp, bytes);
  if (!wrote) {
    fs::remove(temp, ec);
    return false;
  }
  fs::rename(temp, path, ec);
  if (ec) fs::remove(temp, ec);
  return has_size(path, bytes.size());
}

std::string_view as_text(std::span<const std::uint8_t> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::span<const std::uint8_t> as_bytes(std::string_view text) {
  return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

template <typename Get>
auto with_retries(Platform& platform, const Get& get) {
  auto result = get();
  for (const auto wait : retry_waits) {
    if (result || result.error() != Error::Network) break;
    platform.wait(wait);
    result = get();
  }
  return result;
}

std::expected<Catalog, Error> checked(std::string_view text, const update::PublicKey& key, Platform& platform) {
  auto catalog = parse_catalog(text);
  if (!catalog) return std::unexpected(Error::BadContent);
  if (catalog->key != key || !catalog->signature ||
      !platform.verify(key, as_bytes(text.substr(0, catalog->signed_bytes)), *catalog->signature)) {
    return std::unexpected(Error::BadSignature);
  }
  return std::move(*catalog);
}

}  // namespace

bool Platform::write_file(const fs::path& path, std::span<const std::uint8_t> bytes) { return write_plainly(path, as_text(bytes)); }

std::expected<Fetched, Error> fetch_catalog(const Repository& repository, Platform& platform) {
  std::optional<Error> first;
  for (const std::string* folder : {&repository.url, &repository.mirror}) {
    if (folder->empty()) continue;
    const std::string url = *folder + std::string(catalog_name);
    const auto bytes = with_retries(platform, [&] { return platform.fetch(url, max_catalog_size, nullptr); });
    std::expected<Catalog, Error> catalog = std::unexpected(bytes ? Error::BadContent : bytes.error());
    if (bytes) catalog = checked(as_text(*bytes), repository.key, platform);
    if (catalog) return Fetched{std::move(*catalog), std::string(as_text(*bytes)), folder == &repository.mirror};
    if (!first) first = catalog.error();
  }
  return std::unexpected(first.value_or(Error::Network));
}

std::string fingerprint(const update::PublicKey& key, Platform& platform) { return platform.sha256(key); }

std::string own_digest(const Catalog& catalog, std::string_view skin, Platform& platform) {
  return platform.sha256(as_bytes(digest_text(catalog, skin)));
}

Chain pack_chain(std::span<const Shelf> shelves, std::size_t official, std::size_t start, std::string_view pack) {
  Chain chain;
  std::size_t shelf = start;
  std::string id(pack);
  const ParentPin* pin = nullptr;
  while (true) {
    if (chain.links.size() == max_chain ||
        std::ranges::any_of(chain.links, [&](const Chain::Link& link) { return link.shelf == shelf && link.pack == id; })) {
      chain.gap = Chain::Gap::TooDeep;
      return chain;
    }
    const Catalog* catalog = shelf < shelves.size() ? shelves[shelf].catalog : nullptr;
    const SkinPack* skin = catalog ? catalog->skin(id) : nullptr;
    if (!skin) {
      chain.gap = Chain::Gap::Missing;
      return chain;
    }
    chain.links.push_back({shelf, id, pin});
    pin = skin->pin ? &*skin->pin : nullptr;
    if (!skin->parent.empty()) {
      id = skin->parent;  // in the same catalog
    } else if (!pin) {
      return chain;
    } else if (!pin->fingerprint.empty()) {
      const auto it = std::ranges::find(shelves, pin->fingerprint, &Shelf::fingerprint);
      if (it == shelves.end()) {
        chain.gap = Chain::Gap::Repository;
        chain.wanted = pin;
        return chain;
      }
      shelf = static_cast<std::size_t>(it - shelves.begin());
      id = pin->id;
    } else {
      // The same Repository would have listed it on the skin line: the official one has it.
      if (shelf == official || official >= shelves.size()) {
        chain.gap = Chain::Gap::Missing;
        return chain;
      }
      shelf = official;
      id = pin->id;
    }
  }
}

PinCheck check_pin(const ParentPin& pin, const Catalog& catalog, std::string_view pack, Platform& platform) {
  const SkinPack* skin = catalog.skin(pack);
  if (!pin.sha256.empty() && (!skin || own_digest(catalog, pack, platform) != pin.sha256)) return PinCheck::Digest;
  if (!pin.version.empty() && (!skin || !version_matches(pin.version, skin->version))) return PinCheck::Version;
  return PinCheck::Holds;
}

std::expected<Fetched, Error> fetch_new_catalog(const std::string& folder, Platform& platform) {
  const std::string url = folder + std::string(catalog_name);
  const auto bytes = with_retries(platform, [&] { return platform.fetch(url, max_catalog_size, nullptr); });
  if (!bytes) return std::unexpected(bytes.error());
  const auto stated = parse_catalog(as_text(*bytes));
  if (!stated) return std::unexpected(Error::BadContent);
  auto catalog = checked(as_text(*bytes), stated->key, platform);
  if (!catalog) return std::unexpected(catalog.error());
  if (!offers_only_packs(*catalog)) return std::unexpected(Error::BadContent);
  return Fetched{std::move(*catalog), std::string(as_text(*bytes)), false};
}

std::optional<Catalog> load_catalog(const fs::path& dir, const update::PublicKey& key, Platform& platform) {
  const auto bytes = read_file(dir / catalog_name, max_catalog_size);
  auto catalog = checked(as_text(bytes), key, platform);
  if (!catalog) return std::nullopt;
  return std::move(*catalog);
}

std::optional<Catalog> load_local_catalog(const fs::path& dir) {
  const auto bytes = read_file(dir / catalog_name, max_catalog_size);
  auto catalog = parse_catalog(as_text(bytes));
  if (!catalog) return std::nullopt;
  return std::move(*catalog);
}

bool save_catalog(const fs::path& dir, std::string_view text) {
  // Read at every start, and mostly unchanged: the disk is written only for a new one.
  const auto kept = read_file(dir / catalog_name, max_catalog_size);
  if (as_text(kept) == text) return true;
  return replace_file(dir / catalog_name, text);
}

namespace {

void add_group(std::vector<std::string>& groups, const std::string& group) {
  if (std::ranges::find(groups, group) == groups.end()) groups.push_back(group);
}

// Every Sound pack, or only those with a file installed, and every Skin with a file installed.
std::vector<std::string> groups_in_use(const Catalog& catalog, const fs::path& dir, std::vector<std::string> groups, bool every_sound) {
  const auto installed = read_installed(dir);
  for (const File& file : catalog.files) {
    if (file.group.starts_with("sound:")) {
      if (every_sound || installed.contains(file.path)) add_group(groups, file.group);
    } else if (file.group.starts_with("skin:") && installed.contains(file.path)) {
      for (const std::string& id : skin_chain(catalog, std::string_view(file.group).substr(5))) add_group(groups, skin_group(id));
    }
  }
  return groups;
}

}  // namespace

std::vector<std::string> wanted_groups(const Catalog& catalog, const fs::path& dir) {
  std::vector<std::string> groups{"shared"};
  for (const std::string& id : skin_chain(catalog, catalog.default_skin)) add_group(groups, skin_group(id));
  return groups_in_use(catalog, dir, std::move(groups), true);
}

std::vector<std::string> kept_groups(const Catalog& catalog, const fs::path& dir) { return groups_in_use(catalog, dir, {}, false); }

std::vector<const File*> missing(const Catalog& catalog, std::span<const std::string> groups, const fs::path& dir) {
  const auto installed = read_installed(dir);
  std::vector<const File*> result;
  for (const File& file : catalog.files) {
    if (std::ranges::find(groups, file.group) == groups.end()) continue;
    const auto it = installed.find(file.path);
    if (it == installed.end() || it->second != file.sha256 || !has_size(dir / file.path, file.size)) result.push_back(&file);
  }
  return result;
}

std::expected<void, Error> install(const Catalog& catalog, std::string_view base, std::span<const std::string> groups,
                                   const fs::path& dir, Platform& platform, const OnProgress& progress,
                                   const std::atomic<bool>* cancel, std::span<const std::string> held) {
  const auto is_held = [&](std::string_view path) {
    return std::ranges::any_of(held, [&](const std::string& h) { return h == path || (h.ends_with('/') && path.starts_with(h)); });
  };
  auto needed = missing(catalog, groups, dir);
  std::erase_if(needed, [&](const File* file) { return is_held(file->path); });
  Progress state{.files = needed.size()};
  for (const File* file : needed) state.bytes += file->size;

  std::mutex mutex;  // guards state, failure and stop, and keeps progress calls in turn
  std::optional<Error> failure;
  bool stop = false;
  std::atomic<bool> base_down{base.empty()};
  std::vector<char> arrived(needed.size(), 0);
  const auto add_done = [&](std::uint64_t bytes, bool file_done) {
    const std::lock_guard lock(mutex);
    state.bytes_done += bytes;
    if (file_done) ++state.files_done;
    if (progress) progress(state);
  };
  const auto fail = [&](Error error) {
    const std::lock_guard lock(mutex);
    if (!failure) failure = error;
    if (error == Error::Network || error == Error::Disk) stop = true;
  };
  const auto stopped = [&] {
    if (cancel && cancel->load()) return true;
    const std::lock_guard lock(mutex);
    return stop;
  };
  const auto install_one = [&](std::size_t index) {
    const File& file = *needed[index];
    const fs::path path = dir / file.path;
    // It may have arrived in a run that failed later, or by an older version.
    if (has_size(path, file.size) && platform.sha256(read_file(path, file.size)) == file.sha256) {
      add_done(file.size, true);
      arrived[index] = 1;
      return;
    }
    std::uint64_t reported = 0;
    const auto on_bytes = [&](std::uint64_t received) {
      const std::uint64_t capped = std::min(received, file.size);
      if (capped > reported) add_done(capped - reported, false);
      reported = std::max(reported, capped);
    };
    const auto get = [&](const std::string& url) -> std::expected<std::vector<std::uint8_t>, Error> {
      auto bytes = with_retries(platform, [&] { return platform.fetch(url, file.size, on_bytes); });
      if (bytes && (bytes->size() != file.size || platform.sha256(*bytes) != file.sha256)) return std::unexpected(Error::BadContent);
      return bytes;
    };
    std::expected<std::vector<std::uint8_t>, Error> bytes = std::unexpected(Error::Network);
    if (!base_down) {
      bytes = get(std::string(base) + file.path);
      if (!bytes && bytes.error() == Error::Network) base_down = true;
    }
    if (const SourceUrl* source = catalog.source(file.source); !bytes && source != nullptr) {
      const auto other = get(source->url + file.remote);
      // The Repository's answer says more than a source that did not have it either.
      if (other || bytes.error() == Error::Network) bytes = other;
    }
    if (!bytes) return fail(bytes.error());
    if (!replace_file(path, as_text(*bytes), &platform)) return fail(Error::Disk);
    add_done(file.size - reported, true);
    arrived[index] = 1;
  };

  std::atomic<std::size_t> next{0};
  const auto work = [&] {
    while (!stopped()) {
      const std::size_t index = next.fetch_add(1);
      if (index >= needed.size()) return;
      install_one(index);
    }
  };
  {
    std::vector<std::jthread> workers;
    for (std::size_t i = 0; i < std::min(max_workers, needed.size()); ++i) workers.emplace_back(work);
  }
  std::unordered_set<std::string_view> absent;
  for (std::size_t i = 0; i < needed.size(); ++i) {
    if (!arrived[i]) absent.insert(needed[i]->path);
  }
  auto stale = read_installed(dir);
  std::unordered_set<std::string_view> needed_paths;
  for (const File* file : needed) needed_paths.insert(file->path);
  std::ostringstream list;
  for (const File& file : catalog.files) {
    // What was not needed is listed as before, with the hash it has: of a group not asked for
    // this time, it may be older than the catalog, and is then missing until it is fetched.
    const auto it = stale.find(file.path);
    if (needed_paths.contains(file.path)) {
      if (!absent.contains(file.path)) list << file.sha256 << ' ' << file.path << '\n';
    } else if (it != stale.end()) {
      list << it->second << ' ' << file.path << '\n';
    }
    if (it != stale.end()) stale.erase(it);
  }
  // A held file the catalog no longer lists stays, and stays listed.
  for (auto it = stale.begin(); it != stale.end();) {
    if (!is_held(it->first)) {
      ++it;
      continue;
    }
    list << it->second << ' ' << it->first << '\n';
    it = stale.erase(it);
  }
  if (!replace_file(dir / installed_name, list.str())) return std::unexpected(Error::Disk);
  for (const auto& [old_path, hash] : stale) {
    // The list is ours, but a path that leaves the folder is never followed.
    if (old_path.find("..") != std::string::npos || old_path.find_first_of(":\\") != std::string::npos || old_path.starts_with('/')) continue;
    std::error_code ec;
    fs::remove(dir / old_path, ec);  // what could not be removed only takes up space
  }
  if (failure) return std::unexpected(*failure);
  return {};
}

void hold_skin(std::vector<std::string>& held, std::string_view id, const Catalog* installed_by) {
  held.push_back("skins/" + std::string(id) + "/");
  if (!installed_by) return;
  const std::string group = skin_group(id);
  for (const File& file : installed_by->files) {
    if (file.group == group) held.push_back(file.path);
  }
}

void hold_unreadable(std::vector<std::string>& held, const Catalog& latest, const Catalog* installed_by, int min_format,
                     int max_format) {
  for (const SkinPack& skin : latest.skins) {
    if (skin.format >= min_format && skin.format <= max_format) continue;
    hold_skin(held, skin.id, installed_by);
    // What the latest lists of it outside its folder is not fetched either.
    const std::string group = skin_group(skin.id);
    for (const File& file : latest.files) {
      if (file.group == group) held.push_back(file.path);
    }
  }
}

std::expected<void, Error> uninstall(const Catalog& catalog, std::span<const std::string> groups, const fs::path& dir) {
  std::unordered_set<std::string> going;
  for (const File& file : catalog.files) {
    if (std::ranges::find(groups, file.group) != groups.end()) going.insert(file.path);
  }
  std::vector<std::string> folders;
  for (const std::string& group : groups) {
    if (group.starts_with("skin:")) folders.push_back("skins/" + group.substr(5) + "/");
  }
  const auto installed = read_installed(dir);
  std::ostringstream list;
  std::vector<std::string> removed;
  // In the catalog's order, as install writes it, then the rest.
  for (const File& file : catalog.files) {
    if (const auto it = installed.find(file.path); it != installed.end() && !going.contains(file.path) &&
                                                   !std::ranges::any_of(folders, [&](const std::string& f) { return file.path.starts_with(f); })) {
      list << it->second << ' ' << file.path << '\n';
    }
  }
  for (const auto& [path, hash] : installed) {
    const bool gone = going.contains(path) || std::ranges::any_of(folders, [&](const std::string& f) { return path.starts_with(f); });
    if (gone) removed.push_back(path);
    else if (!std::ranges::any_of(catalog.files, [&](const File& file) { return file.path == path; })) list << hash << ' ' << path << '\n';
  }
  // Listed last, as a file that goes is no longer installed even if removing it fails.
  if (!replace_file(dir / installed_name, list.str())) return std::unexpected(Error::Disk);
  for (const std::string& path : removed) {
    if (path.find("..") != std::string::npos || path.find_first_of(":\\") != std::string::npos || path.starts_with('/')) continue;
    std::error_code ec;
    fs::remove(dir / path, ec);
    // And the folders it leaves empty, up to the assets folder.
    for (fs::path folder = (dir / path).parent_path(); folder != dir && folder.native().size() > dir.native().size(); folder = folder.parent_path()) {
      if (!fs::is_empty(folder, ec) || ec || !fs::remove(folder, ec)) break;
    }
  }
  return {};
}

}  // namespace crpp::assets
