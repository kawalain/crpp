#include "crpp/transport/target.hpp"

namespace crpp::transport {

namespace {

constexpr bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
constexpr bool is_digit(char c) { return c >= '0' && c <= '9'; }

constexpr bool is_unreserved(char c) {
  return is_alpha(c) || is_digit(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

bool equals_ignore_case(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const char x = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
    if (x != b[i]) return false;
  }
  return true;
}

void append_percent_encoded(std::string& out, std::string_view s) {
  static constexpr char hex[] = "0123456789ABCDEF";
  for (const char c : s) {
    if (is_unreserved(c)) {
      out += c;
    } else {
      const auto u = static_cast<unsigned char>(c);
      out += '%';
      out += hex[u >> 4];
      out += hex[u & 0xF];
    }
  }
}

}  // namespace

std::expected<Target, TargetError> make_target(std::string_view url, std::string_view nickname) {
  std::string_view scheme = "https";
  if (const auto sep = url.find("://"); sep != std::string_view::npos) {
    scheme = url.substr(0, sep);
    url.remove_prefix(sep + 3);
  }
  Target t;
  if (equals_ignore_case(scheme, "https")) {
    t.secure = true;
  } else if (!equals_ignore_case(scheme, "http")) {
    return std::unexpected(TargetError::UnsupportedScheme);
  }

  // The query and fragment of the base URL are dropped, as upstream does.
  url = url.substr(0, url.find_first_of("?#"));
  const auto slash = url.find('/');
  std::string_view authority = url.substr(0, slash);
  std::string_view base_path =
      slash == std::string_view::npos ? std::string_view{} : url.substr(slash);
  if (authority.empty() || authority.find('@') != std::string_view::npos) {
    return std::unexpected(TargetError::Invalid);
  }

  std::string_view host;
  std::string_view port_text;
  if (authority.front() == '[') {
    const auto close = authority.find(']');
    if (close == std::string_view::npos || close == 1) return std::unexpected(TargetError::Invalid);
    host = authority.substr(1, close - 1);
    const auto rest = authority.substr(close + 1);
    if (!rest.empty()) {
      if (rest.front() != ':') return std::unexpected(TargetError::Invalid);
      port_text = rest.substr(1);
    }
  } else {
    const auto colon = authority.find(':');
    host = authority.substr(0, colon);
    if (colon != std::string_view::npos) port_text = authority.substr(colon + 1);
  }
  if (host.empty()) return std::unexpected(TargetError::Invalid);
  for (const char c : host) {
    if (c <= ' ' || c == '[' || c == ']' || c == '\\') return std::unexpected(TargetError::Invalid);
  }

  unsigned port = t.secure ? 443 : 80;
  if (!port_text.empty()) {
    port = 0;
    for (const char c : port_text) {
      if (!is_digit(c)) return std::unexpected(TargetError::Invalid);
      port = port * 10 + static_cast<unsigned>(c - '0');
      if (port > 65535) return std::unexpected(TargetError::Invalid);
    }
    if (port == 0) return std::unexpected(TargetError::Invalid);
  }
  t.host = std::string(host);
  t.port = static_cast<std::uint16_t>(port);

  while (!base_path.empty() && base_path.back() == '/') base_path.remove_suffix(1);
  t.path_and_query = std::string(base_path);
  t.path_and_query += "/ws?nick=";
  append_percent_encoded(t.path_and_query, nickname);
  return t;
}

std::expected<Target, TargetError> make_health_target(std::string_view url) {
  auto target = make_target(url, "");
  if (!target) return target;
  target->path_and_query.resize(target->path_and_query.size() - std::string_view("ws?nick=").size());
  target->path_and_query += "health";
  return target;
}

}  // namespace crpp::transport
