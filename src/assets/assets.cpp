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

// The temporary name means a stop halfway leaves the old file or none.
bool replace_file(const fs::path& path, std::string_view bytes) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  if (ec) return false;
  fs::path temp = path;
  temp += ".part";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) return false;
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

std::vector<std::string> wanted_groups(const Catalog& catalog, const fs::path& dir) {
  const auto installed = read_installed(dir);
  std::vector<std::string> groups{"shared"};
  const auto add = [&](const std::string& group) {
    if (std::ranges::find(groups, group) == groups.end()) groups.push_back(group);
  };
  for (const std::string& id : skin_chain(catalog, catalog.default_skin)) add(skin_group(id));
  for (const File& file : catalog.files) {
    if (file.group.starts_with("sound:")) add(file.group);
    else if (file.group.starts_with("skin:") && installed.contains(file.path)) {
      for (const std::string& id : skin_chain(catalog, std::string_view(file.group).substr(5))) add(skin_group(id));
    }
  }
  return groups;
}

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
                                   const fs::path& dir, Platform& platform, const OnProgress& progress) {
  const auto needed = missing(catalog, groups, dir);
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
    if (!replace_file(path, as_text(*bytes))) return fail(Error::Disk);
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

}  // namespace crpp::assets
