#include "crpp/crash/report.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>

namespace crpp::crash {

namespace {

struct Named {
  std::uint32_t code;
  std::string_view name;
};

constexpr Named exception_names[] = {
    {0xc0000005, "access violation"},
    {0xc0000006, "in-page error"},
    {0xc0000008, "invalid handle"},
    {0xc000001d, "illegal instruction"},
    {0xc0000025, "noncontinuable exception"},
    {0xc0000026, "invalid disposition"},
    {0xc000008c, "array bounds exceeded"},
    {0xc000008d, "float denormal operand"},
    {0xc000008e, "float divide by zero"},
    {0xc000008f, "float inexact result"},
    {0xc0000090, "float invalid operation"},
    {0xc0000091, "float overflow"},
    {0xc0000092, "float stack check"},
    {0xc0000093, "float underflow"},
    {0xc0000094, "integer divide by zero"},
    {0xc0000095, "integer overflow"},
    {0xc0000096, "privileged instruction"},
    {0xc00000fd, "stack overflow"},
    {0xc0000374, "heap corruption"},
    {0xc0000409, "fail fast or stack buffer overrun"},
    {0xc000041d, "exception in a user callback"},
    {0xc0000420, "assertion failure"},
    {0x80000002, "datatype misalignment"},
    {0x80000003, "breakpoint"},
    {0x80000004, "single step"},
    {0xe06d7363, "C++ exception"},
};

void write_cause(Writer& out, const Crash& crash) noexcept {
  out.text("cause: ");
  switch (crash.cause) {
    case Cause::Abort:
      out.text("abort (a failed check of the standard library, or abort())");
      break;
    case Cause::Terminate:
      out.text("std::terminate");
      break;
    case Cause::PureCall:
      out.text("pure virtual function call");
      break;
    case Cause::InvalidParameter:
      out.text("invalid parameter to the C runtime");
      break;
    case Cause::Exception: {
      const std::string_view name = exception_name(crash.code);
      out.text(name.empty() ? "exception" : name).text(" (0x").hex(crash.code, 8).ch(')');
      if (crash.access >= 0) {
        out.text(crash.access == 0 ? ", reading 0x" : crash.access == 1 ? ", writing 0x" : ", executing 0x").hex(crash.address, 16);
      }
      break;
    }
  }
  out.ch('\n');
}

// GUID and age as a symbol server files a PDB: Data1, Data2, Data3 as numbers, then Data4's bytes.
void write_pdb_key(Writer& out, const Module& module) noexcept {
  const auto& g = module.guid;
  const auto le = [&](std::size_t at, std::size_t bytes) {
    std::uint64_t value = 0;
    for (std::size_t i = bytes; i-- > 0;) value = value << 8 | g[at + i];
    return value;
  };
  out.hex(le(0, 4), 8, true).hex(le(4, 2), 4, true).hex(le(6, 2), 4, true);
  for (std::size_t i = 8; i < 16; ++i) out.hex(g[i], 2, true);
  out.hex(module.age, 1, true);
}

template <class T>
std::optional<T> hex_number(std::string_view text) {
  T value{};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
  if (error != std::errc{} || end != text.data() + text.size() || text.empty()) return std::nullopt;
  return value;
}

std::string decode_entities(std::string_view text) {
  std::string out;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '&') {
      out += text[i];
      continue;
    }
    constexpr std::pair<std::string_view, char> entities[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
    bool known = false;
    for (const auto& [entity, c] : entities) {
      if (text.substr(i).starts_with(entity)) {
        out += c;
        i += entity.size() - 1;
        known = true;
        break;
      }
    }
    if (!known) out += '&';
  }
  return out;
}

// The contents of each <Data> of <EventData>, in order.
std::vector<std::string> event_data(std::string_view xml) {
  std::vector<std::string> values;
  const std::size_t from = xml.find("<EventData>");
  const std::size_t to = xml.find("</EventData>");
  if (from == std::string_view::npos || to == std::string_view::npos || to < from) return values;
  std::string_view rest = xml.substr(from, to - from);
  for (std::size_t at = rest.find("<Data"); at != std::string_view::npos; at = rest.find("<Data")) {
    rest.remove_prefix(at + 5);
    const std::size_t close = rest.find('>');
    if (close == std::string_view::npos) break;
    if (close > 0 && rest[close - 1] == '/') {  // <Data/>
      values.emplace_back();
      rest.remove_prefix(close + 1);
      continue;
    }
    rest.remove_prefix(close + 1);
    const std::size_t end = rest.find("</Data>");
    if (end == std::string_view::npos) break;
    values.push_back(decode_entities(rest.substr(0, end)));
    rest.remove_prefix(end + 7);
  }
  return values;
}

std::string percent_encoded(std::string_view text) {
  std::string out;
  for (const char c : text) {
    const auto u = static_cast<unsigned char>(c);
    if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      constexpr char digits[] = "0123456789ABCDEF";
      out += '%';
      out += digits[u >> 4];
      out += digits[u & 0xf];
    }
  }
  return out;
}

// The text after "<key>" on the line that starts with it.
std::string_view line_value(std::string_view report, std::string_view key) {
  for (std::size_t at = 0; at < report.size();) {
    const std::size_t end = std::min(report.find('\n', at), report.size());
    const std::string_view line = report.substr(at, end - at);
    if (line.starts_with(key)) return line.substr(key.size());
    at = end + 1;
  }
  return {};
}

}  // namespace

std::string_view exception_name(std::uint32_t code) noexcept {
  for (const Named& named : exception_names) {
    if (named.code == code) return named.name;
  }
  return {};
}

void write_report(Writer& out, const Build& build, const Crash& crash, const Breadcrumbs& crumbs, std::uint64_t now_ms) noexcept {
  out.text("crpp crash report\n");
  out.text("version: ").text(build.version).text(" (").text(build.hash).text(")\n");
  out.text("build: ").text(build.official ? "official" : "development").ch('\n');
  out.text("windows: ").text(build.windows).ch('\n');
  write_cause(out, crash);
  out.text("thread: ").text(crash.main_thread ? "main" : "other").ch('\n');
  out.text("uptime: ").seconds(crash.uptime_ms).text(" s\n");
  out.text("stack:\n");
  for (std::size_t i = 0; i < crash.frames.size(); ++i) {
    const Frame& frame = crash.frames[i];
    out.text("  ").dec(i).text("  ");
    if (frame.module < crash.modules.size()) out.text(crash.modules[frame.module].name).text("+0x").hex(frame.offset);
    else out.text("0x").hex(frame.offset, 16);
    if (frame.repeats != 0) out.text("  (and ").dec(frame.repeats).text(" more like it)");
    out.ch('\n');
  }
  if (crash.frames.empty()) out.text("  (none)\n");
  out.text("modules:\n");
  for (const Module& module : crash.modules) {
    out.text("  ").text(module.name).text("  ").hex(module.stamp, 8).ch(' ').hex(module.size);
    if (!module.pdb.empty()) {
      out.text("  ").text(module.pdb).ch(' ');
      write_pdb_key(out, module);
    }
    out.ch('\n');
  }
  out.text("events:\n");
  const std::size_t count = crumbs.size();
  for (std::size_t i = 0; i < count; ++i) {
    const Breadcrumbs::Crumb& crumb = crumbs.at(i);
    if (crumb.what == nullptr) continue;
    out.text("  -").seconds(now_ms >= crumb.ms ? now_ms - crumb.ms : 0).text(" s  ").text({crumb.what, crumb.what_size});
    if (crumb.detail_size != 0) out.text(": ").text({crumb.detail, std::min<std::size_t>(crumb.detail_size, Breadcrumbs::detail_bytes)});
    out.ch('\n');
  }
  if (count == 0) out.text("  (none)\n");
}

std::optional<EventCrash> parse_application_error(std::string_view xml) {
  if (xml.find("Name='Application Error'") == std::string_view::npos && xml.find("Name=\"Application Error\"") == std::string_view::npos) return std::nullopt;
  const std::size_t id = xml.find("<EventID");
  if (id == std::string_view::npos) return std::nullopt;
  const std::size_t id_text = xml.find('>', id);
  if (id_text == std::string_view::npos || !xml.substr(id_text + 1).starts_with("1000<")) return std::nullopt;
  // AppName, AppVersion, AppTimeStamp, ModuleName, ModuleVersion, ModuleTimeStamp, ExceptionCode,
  // FaultingOffset, ProcessId, ProcessCreationTime, and then paths, which are not kept.
  const std::vector<std::string> data = event_data(xml);
  if (data.size() < 10) return std::nullopt;
  const auto app_stamp = hex_number<std::uint32_t>(data[2]);
  const auto module_stamp = hex_number<std::uint32_t>(data[5]);
  const auto code = hex_number<std::uint32_t>(data[6]);
  const auto offset = hex_number<std::uint64_t>(data[7]);
  const auto pid = hex_number<std::uint32_t>(data[8].starts_with("0x") ? std::string_view(data[8]).substr(2) : std::string_view(data[8]));
  const auto created = hex_number<std::uint64_t>(data[9]);
  if (!app_stamp || !module_stamp || !code || !offset || !pid || !created) return std::nullopt;
  return EventCrash{data[0], data[1], *app_stamp, data[3], data[4], *module_stamp, *code, *offset, *pid, *created};
}

std::string event_query(std::uint64_t since_filetime) {
  using namespace std::chrono;
  constexpr std::uint64_t unix_epoch = 116444736000000000ull;  // 1970-01-01 as a FILETIME
  const std::uint64_t since = since_filetime > unix_epoch ? since_filetime - unix_epoch : 0;
  const sys_time<milliseconds> at{milliseconds(since / 10000)};
  const sys_days day = floor<days>(at);
  const year_month_day date{day};
  const hh_mm_ss time{at - day};
  char buffer[32];
  Writer out(buffer);
  out.dec(static_cast<std::uint64_t>(static_cast<int>(date.year())), 4).ch('-').dec(static_cast<unsigned>(date.month()), 2).ch('-').dec(static_cast<unsigned>(date.day()), 2);
  out.ch('T').dec(static_cast<std::uint64_t>(time.hours().count()), 2).ch(':').dec(static_cast<std::uint64_t>(time.minutes().count()), 2).ch(':');
  out.dec(static_cast<std::uint64_t>(time.seconds().count()), 2).ch('.').dec(static_cast<std::uint64_t>(time.subseconds().count()), 3).ch('Z');
  return "*[System[Provider[@Name='Application Error'] and (EventID=1000) and TimeCreated[@SystemTime>='" + std::string(out.written()) + "']]]";
}

std::string event_report(const Build& build, const EventCrash& crash) {
  std::string text(4096, '\0');
  Writer out(text);
  out.text("crpp crash report\n");
  out.text("version: ").text(crash.app_version).text(" (as Windows' event log names it)\n");
  out.text("reported by: ").text(build.version).text(" (").text(build.hash).text("), ").text(build.official ? "official" : "development").ch('\n');
  out.text("windows: ").text(build.windows).ch('\n');
  const std::string_view name = exception_name(crash.code);
  out.text("cause: ").text(name.empty() ? "exception" : name).text(" (0x").hex(crash.code, 8).text("), ended by Windows\n");
  out.text("stack:\n  0  ").text(crash.module).text("+0x").hex(crash.offset).ch('\n');
  out.text("modules:\n");
  out.text("  ").text(crash.module).text("  ").hex(crash.module_stamp, 8).text("  version ").text(crash.module_version).ch('\n');
  if (crash.module != crash.app) out.text("  ").text(crash.app).text("  ").hex(crash.app_stamp, 8).text("  version ").text(crash.app_version).ch('\n');
  out.text("note: Windows ended the program before it could write a report of its own, so this holds only what Windows recorded.\n");
  text.resize(out.written().size());
  return text;
}

void write_report_name(Writer& out, const Stamp& at, std::uint32_t pid) noexcept {
  const auto two = [&](int value) { out.dec(static_cast<std::uint64_t>(value), 2); };
  out.text(new_prefix).dec(static_cast<std::uint64_t>(at.year), 4);
  two(at.month);
  two(at.day);
  out.ch('-');
  two(at.hour);
  two(at.minute);
  two(at.second);
  out.ch('-').dec(pid).text(".txt");
}

Tidy tidy_reports(std::span<const std::string> names) {
  Tidy tidy;
  std::vector<std::string> kept;
  for (const std::string& name : names) {
    if (!name.ends_with(".txt") || name.starts_with(marker_prefix)) continue;
    if (name.starts_with(new_prefix)) {
      if (name > tidy.show) tidy.show = name;
      std::string shown = name.substr(new_prefix.size());
      tidy.rename.emplace_back(name, shown);
      kept.push_back(std::move(shown));
    } else {
      kept.push_back(name);
    }
  }
  // In the order the catalog sorts its names too, which the exe then holds once: oldest first.
  std::ranges::sort(kept);
  kept.erase(std::unique(kept.begin(), kept.end()), kept.end());
  for (std::size_t i = 0; i + kept_reports < kept.size(); ++i) tidy.remove.push_back(kept[i]);
  return tidy;
}

void write_marker(Writer& out, const Marker& marker) noexcept { out.dec(marker.pid).ch(' ').hex(marker.created, 16).ch('\n'); }

std::optional<Marker> parse_marker(std::string_view text) noexcept {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.remove_suffix(1);
  const std::size_t space = text.find(' ');
  if (space == std::string_view::npos) return std::nullopt;
  Marker marker;
  const std::string_view pid = text.substr(0, space);
  const auto [pid_end, pid_error] = std::from_chars(pid.data(), pid.data() + pid.size(), marker.pid);
  const std::string_view created = text.substr(space + 1);
  const auto [created_end, created_error] = std::from_chars(created.data(), created.data() + created.size(), marker.created, 16);
  if (pid_error != std::errc{} || pid_end != pid.data() + pid.size() || pid.empty()) return std::nullopt;
  if (created_error != std::errc{} || created_end != created.data() + created.size() || created.empty()) return std::nullopt;
  return marker;
}

std::string issue_title(std::string_view report) {
  std::string_view cause = line_value(report, "cause: ");
  cause = cause.substr(0, std::min(cause.find(" ("), cause.find(',')));
  std::string title = "크래시: " + std::string(cause.empty() ? "unknown" : cause);
  // The first frame: "  0  crpp.exe+0x1a2b".
  const std::size_t stack = report.find("\nstack:\n  0  ");
  if (stack != std::string_view::npos) {
    const std::string_view rest = report.substr(stack + 13);
    const std::string_view frame = rest.substr(0, rest.find('\n'));
    if (!frame.empty() && frame != "(none)") title += ", " + std::string(frame);
  }
  return title;
}

std::string issue_url(std::string_view repository_page, std::string_view report, std::size_t limit) {
  const std::string head = std::string(repository_page) + "/issues/new?template=crash.yml&title=" + percent_encoded(issue_title(report)) + "&report=";
  std::string body(report);
  std::string url = head + percent_encoded(body);
  constexpr std::string_view cut = "(cut here; the whole report is on the clipboard)\n";
  while (url.size() > limit) {
    // Whole lines from the end: the events, then modules, then the deepest frames.
    if (body.ends_with(cut)) body.resize(body.size() - cut.size());
    if (!body.empty() && body.back() == '\n') body.pop_back();
    const std::size_t last = body.rfind('\n');
    if (last == std::string::npos) {
      body.clear();
    } else {
      body.resize(last + 1);
    }
    body += cut;
    url = head + percent_encoded(body);
    if (body == cut) break;
  }
  return url;
}

}  // namespace crpp::crash
