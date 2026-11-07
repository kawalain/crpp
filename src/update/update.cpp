#include "crpp/update/update.hpp"

#include <algorithm>
#include <charconv>

#include "crpp/store/json.hpp"

namespace crpp::update {

namespace {

constexpr std::size_t max_manifest_size = 1024;
constexpr std::size_t max_releases_size = 8 * 1024 * 1024;
constexpr std::string_view github_json = "application/vnd.github+json";

int hex_digit(char c) noexcept {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;  // lowercase only: one text for one value, so nothing signed can vary
}

// Exactly out.size() * 2 lowercase hex digits.
bool parse_hex(std::string_view hex, std::span<std::uint8_t> out) noexcept {
  if (hex.size() != out.size() * 2) return false;
  for (std::size_t i = 0; i < out.size(); ++i) {
    const int high = hex_digit(hex[i * 2]), low = hex_digit(hex[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    out[i] = static_cast<std::uint8_t>(high * 16 + low);
  }
  return true;
}

bool safe_file_name(std::string_view name) noexcept {
  if (name.empty() || name.front() == '.') return false;
  return std::ranges::all_of(name, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
  });
}

// One "key value" line. Moves `text` past it.
std::optional<std::string_view> take_line(std::string_view& text, std::string_view key) noexcept {
  const auto end = text.find('\n');
  if (end == std::string_view::npos) return std::nullopt;
  const std::string_view line = text.substr(0, end);
  if (line.size() <= key.size() || !line.starts_with(key) || line[key.size()] != ' ') return std::nullopt;
  text.remove_prefix(end + 1);
  return line.substr(key.size() + 1);
}

std::string text_of(const store::Json& object, std::string_view key) {
  const store::Json* value = object.find(key);
  const auto text = value ? value->as_string() : std::nullopt;
  return text ? std::string(*text) : std::string{};
}

bool flag_of(const store::Json& object, std::string_view key) {
  const store::Json* value = object.find(key);
  return value && value->as_bool().value_or(false);
}

std::filesystem::path with_suffix(std::filesystem::path path, const char* suffix) {
  path += suffix;
  return path;
}

}  // namespace

std::string_view error_name(Error error) noexcept {
  switch (error) {
    case Error::Network:
      return "network";
    case Error::Unavailable:
      return "unavailable";
    case Error::BadContent:
      return "bad_content";
    case Error::BadSignature:
      return "bad_signature";
    case Error::NoKey:
      return "no_key";
    case Error::Disk:
      return "disk";
    case Error::Launch:
      return "launch";
    case Error::RollbackFailed:
      return "rollback_failed";
  }
  return "unknown";
}

std::optional<Version> parse_version(std::string_view text) noexcept {
  if (text.starts_with('v')) text.remove_prefix(1);
  std::array<std::uint32_t, 3> parts{};
  for (std::size_t i = 0; i < parts.size(); ++i) {
    const char* const end = text.data() + text.size();
    const auto [next, code] = std::from_chars(text.data(), end, parts[i]);
    const auto digits = static_cast<std::size_t>(next - text.data());
    if (code != std::errc{} || (digits > 1 && text.front() == '0')) return std::nullopt;
    text.remove_prefix(digits);
    if (i + 1 < parts.size()) {
      if (!text.starts_with('.')) return std::nullopt;
      text.remove_prefix(1);
    }
  }
  if (!text.empty()) return std::nullopt;
  return Version{parts[0], parts[1], parts[2]};
}

std::string to_string(Version version) {
  return std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
}

std::optional<PublicKey> parse_public_key(std::string_view hex) noexcept {
  PublicKey key{};
  if (!parse_hex(hex, key)) return std::nullopt;
  return key;
}

std::expected<Manifest, Error> parse_manifest(std::span<const std::uint8_t> bytes) {
  if (bytes.size() > max_manifest_size) return std::unexpected(Error::BadContent);
  const std::string_view whole(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  std::string_view rest = whole;
  const auto format = take_line(rest, "crpp-update");
  const auto version_text = take_line(rest, "version");
  const auto asset = take_line(rest, "asset");
  const auto size_text = take_line(rest, "size");
  const auto hash = take_line(rest, "sha256");
  if (format != "1" || !version_text || !asset || !size_text || !hash) return std::unexpected(Error::BadContent);
  const std::size_t signed_bytes = whole.size() - rest.size();
  const auto signature_text = take_line(rest, "signature");
  if (!signature_text || !rest.empty()) return std::unexpected(Error::BadContent);

  Manifest manifest;
  manifest.signed_bytes = signed_bytes;
  const auto version = parse_version(*version_text);
  if (!version || version_text->starts_with('v')) return std::unexpected(Error::BadContent);
  manifest.version = *version;
  if (!safe_file_name(*asset)) return std::unexpected(Error::BadContent);
  manifest.asset = std::string(*asset);
  const char* const size_end = size_text->data() + size_text->size();
  const auto [next, code] = std::from_chars(size_text->data(), size_end, manifest.size);
  if (code != std::errc{} || next != size_end || manifest.size == 0 || manifest.size > max_exe_size) {
    return std::unexpected(Error::BadContent);
  }
  std::array<std::uint8_t, 32> digest{};
  if (!parse_hex(*hash, digest)) return std::unexpected(Error::BadContent);
  manifest.sha256 = std::string(*hash);
  if (!parse_hex(*signature_text, manifest.signature)) return std::unexpected(Error::BadContent);
  return manifest;
}

std::expected<std::vector<Release>, Error> parse_releases(std::string_view json) {
  const auto document = store::Json::parse(json);
  if (!document) return std::unexpected(Error::BadContent);
  const auto* list = document->as_array();
  if (!list) return std::unexpected(Error::BadContent);
  std::vector<Release> releases;
  for (const store::Json& item : *list) {
    if (flag_of(item, "draft") || flag_of(item, "prerelease")) continue;
    const std::string tag = text_of(item, "tag_name");
    const auto version = parse_version(tag);
    if (!version || !tag.starts_with('v')) continue;
    releases.push_back({*version, text_of(item, "name"), text_of(item, "body"), text_of(item, "published_at")});
  }
  std::ranges::stable_sort(releases, [](const Release& a, const Release& b) { return a.version > b.version; });
  return releases;
}

std::string dump_releases(std::span<const Release> releases) {
  store::Json::Array list;
  list.reserve(releases.size());
  for (const Release& release : releases) {
    store::Json item = store::Json::object();
    item.set("tag_name", store::Json(release.tag()));
    item.set("name", store::Json(release.name));
    item.set("body", store::Json(release.body));
    item.set("published_at", store::Json(release.published_at));
    list.push_back(std::move(item));
  }
  return store::Json(std::move(list)).dump();
}

std::string korean_date(std::string_view published_at) {
  // YYYY-MM-DD at the start; the time of day is not shown.
  if (published_at.size() < 10 || published_at[4] != '-' || published_at[7] != '-') return {};
  for (const std::size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u}) {
    if (published_at[i] < '0' || published_at[i] > '9') return {};
  }
  std::string text(published_at.substr(0, 4));
  text += "년 ";
  text += published_at.substr(5, 2);
  text += "월 ";
  text += published_at.substr(8, 2);
  text += "일";
  return text;
}

Source published_source() {
  return {"https://api.github.com/repos/kawalain/crpp/releases?per_page=30",
          "https://github.com/kawalain/crpp/releases/download/"};
}

std::expected<CheckResult, Error> check(const Source& source, Version current, const std::optional<PublicKey>& key,
                                        Platform& platform) {
  if (!key) return std::unexpected(Error::NoKey);
  const auto reply = platform.get(source.releases_url, max_releases_size, github_json, nullptr);
  if (!reply) return std::unexpected(reply.error());
  if (reply->size() > max_releases_size) return std::unexpected(Error::BadContent);
  auto releases = parse_releases({reinterpret_cast<const char*>(reply->data()), reply->size()});
  if (!releases) return std::unexpected(releases.error());

  CheckResult result;
  result.releases = std::move(*releases);
  if (result.releases.empty() || result.releases.front().version <= current) return result;

  const Release& newest = result.releases.front();
  const auto proved = [&]() -> std::expected<Manifest, Error> {
    const auto bytes = platform.get(source.download_base + newest.tag() + "/" + std::string(manifest_asset),
                                    max_manifest_size, {}, nullptr);
    if (!bytes) return std::unexpected(bytes.error());
    auto manifest = parse_manifest(*bytes);
    if (!manifest) return std::unexpected(manifest.error());
    if (!platform.verify(*key, std::span(*bytes).first(manifest->signed_bytes), manifest->signature)) {
      return std::unexpected(Error::BadSignature);
    }
    // Signed, so ours, but it must be this release: an old manifest must not pass as new.
    if (manifest->version != newest.version) return std::unexpected(Error::BadContent);
    return manifest;
  }();
  if (proved) result.update = Available{newest, *proved};
  else result.problem = proved.error();
  return result;
}

std::expected<std::vector<std::uint8_t>, Error> download(const Source& source, const Available& available, Platform& platform,
                                                         const Platform::OnBytes& on_bytes) {
  const Manifest& manifest = available.manifest;
  auto bytes = platform.get(source.download_base + available.release.tag() + "/" + manifest.asset,
                            static_cast<std::size_t>(manifest.size), {}, on_bytes);
  if (!bytes) return std::unexpected(bytes.error());
  if (bytes->size() != manifest.size || platform.sha256(*bytes) != manifest.sha256) return std::unexpected(Error::BadContent);
  return bytes;
}

std::expected<void, Error> self_replace(const std::filesystem::path& exe, std::span<const std::uint8_t> bytes,
                                        std::string_view sha256, Platform& platform) {
  const auto fresh = with_suffix(exe, ".new"), old = with_suffix(exe, ".old");
  (void)platform.remove_file(fresh);
  (void)platform.remove_file(old);  // if this one stays, move_file below says so

  // Read back: a full disk or a scanner can leave something else than was written.
  const auto written = platform.write_file(fresh, bytes)
                           ? platform.read_file(fresh, bytes.size())
                           : std::expected<std::vector<std::uint8_t>, Error>(std::unexpected(Error::Disk));
  if (!written || platform.sha256(*written) != sha256) {
    (void)platform.remove_file(fresh);
    return std::unexpected(Error::Disk);
  }
  if (!platform.move_file(exe, old)) {
    (void)platform.remove_file(fresh);
    return std::unexpected(Error::Disk);
  }
  if (!platform.move_file(fresh, exe)) {
    if (!platform.move_file(old, exe)) return std::unexpected(Error::RollbackFailed);
    (void)platform.remove_file(fresh);
    return std::unexpected(Error::Disk);
  }
  if (!platform.launch(exe)) {
    // Replaces the exe that would not start.
    if (!platform.move_file(old, exe)) return std::unexpected(Error::RollbackFailed);
    return std::unexpected(Error::Launch);
  }
  return {};
}

void remove_leftovers(const std::filesystem::path& exe, Platform& platform) {
  (void)platform.remove_file(with_suffix(exe, ".new"));
  (void)platform.remove_file(with_suffix(exe, ".old"));
}

bool notes_due_when_noticed(std::string_view shown, std::string_view available) noexcept { return shown != available; }

bool notes_due_at_start(std::string_view seen, std::string_view shown, std::string_view current) noexcept {
  return !seen.empty() && seen != current && shown != current;
}

}  // namespace crpp::update
