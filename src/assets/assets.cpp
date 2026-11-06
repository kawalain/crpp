#include "crpp/assets/assets.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unordered_map>

namespace crpp::assets {

namespace fs = std::filesystem;

namespace {

constexpr const char* installed_name = ".installed";

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

std::vector<std::uint8_t> read_file(const fs::path& path) {
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

}  // namespace

std::vector<const Entry*> missing(const Manifest& manifest, const fs::path& dir) {
  const auto installed = read_installed(dir);
  std::vector<const Entry*> result;
  for (const Entry& entry : manifest.entries) {
    const auto it = installed.find(std::string(entry.path));
    if (it == installed.end() || it->second != entry.sha256 || !has_size(dir / entry.path, entry.size)) {
      result.push_back(&entry);
    }
  }
  return result;
}

std::expected<void, Error> install(const Manifest& manifest, const fs::path& dir, Platform& platform,
                                   const OnProgress& progress) {
  const auto needed = missing(manifest, dir);
  std::uint64_t total = 0, done = 0;
  for (const Entry* entry : needed) total += entry->size;

  for (const Entry* entry : needed) {
    const fs::path path = dir / entry->path;
    // It may have arrived in a run that failed later.
    const bool have = has_size(path, entry->size) && platform.sha256(read_file(path)) == entry->sha256;
    if (!have) {
      const auto bytes = platform.fetch(*entry, [&](std::uint64_t received) {
        if (progress) progress(done + std::min(received, entry->size), total);
      });
      if (!bytes) return std::unexpected(bytes.error());
      if (bytes->size() != entry->size || platform.sha256(*bytes) != entry->sha256) {
        return std::unexpected(Error::BadContent);
      }
      if (!replace_file(path, as_text(*bytes))) return std::unexpected(Error::Disk);
    }
    done += entry->size;
    if (progress) progress(done, total);
  }

  auto stale = read_installed(dir);
  std::ostringstream list;
  for (const Entry& entry : manifest.entries) {
    list << entry.sha256 << ' ' << entry.path << '\n';
    stale.erase(std::string(entry.path));
  }
  if (!replace_file(dir / installed_name, list.str())) return std::unexpected(Error::Disk);
  for (const auto& [old_path, hash] : stale) {
    std::error_code ec;
    fs::remove(dir / old_path, ec);  // what could not be removed only takes up space
  }
  return {};
}

}  // namespace crpp::assets
