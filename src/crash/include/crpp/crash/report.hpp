#pragma once
// The Crash report: what the program knew when it ended abnormally, as text anyone may read. It names
// the build, the cause, the stack as module+offset and the last diagnostic events; never a path, a
// name, an address of the user's or anything typed. Writing it allocates nothing, as the crash
// handler writes it.
//
// Its lines, which crpp_symbolize reads back:
//   crpp crash report
//   version: <x.y.z> (<commit>)
//   build: official | development
//   windows: <major.minor.build>
//   cause: <what>[ (0x<code>)][, <reading|writing|executing> 0x<address>]
//   thread: main | other
//   uptime: <seconds> s
//   stack:
//     <n>  <module>+0x<offset>[  (and <count> more like it)]
//   modules:
//     <module>  <stamp> <size>  <pdb> <GUID and age, as symbol servers file a PDB>
//   events:
//     -<seconds> s  <event>[: <detail>]
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "crpp/crash/breadcrumbs.hpp"
#include "crpp/crash/writer.hpp"

namespace crpp::crash {

enum class Cause : std::uint8_t { Exception, Abort, Terminate, PureCall, InvalidParameter };

// An image in the process, as its own headers describe it.
struct Module {
  std::string_view name;  // its file name, as it calls itself
  std::uint32_t stamp = 0;  // TimeDateStamp
  std::uint32_t size = 0;   // SizeOfImage
  std::string_view pdb;     // the PDB's file name from its CodeView record; empty without one
  std::array<std::uint8_t, 16> guid{};  // the PDB's GUID, as a GUID lies in memory
  std::uint32_t age = 0;
};

inline constexpr std::uint16_t no_module = 0xffff;

// A return address: an offset into modules[module], or an address in no module. A recursion is one
// frame with the times it came again right below itself.
struct Frame {
  std::uint64_t offset = 0;
  std::uint16_t module = no_module;
  std::uint32_t repeats = 0;
};

// What the running program says of itself.
struct Build {
  std::string_view version;
  std::string_view hash;
  bool official = false;
  std::string_view windows;  // "10.0.19044"
};

struct Crash {
  Cause cause = Cause::Exception;
  std::uint32_t code = 0;  // the exception's
  // Of an access violation: 0 reading, 1 writing, 8 executing, -1 none.
  int access = -1;
  std::uint64_t address = 0;
  bool main_thread = false;
  std::uint64_t uptime_ms = 0;
  std::span<const Frame> frames{};
  std::span<const Module> modules{};
};

// The name of a Windows exception code, or empty.
[[nodiscard]] std::string_view exception_name(std::uint32_t code) noexcept;

// The whole report. `now_ms` is on the clock the breadcrumbs were noted on.
void write_report(Writer& out, const Build& build, const Crash& crash, const Breadcrumbs& crumbs, std::uint64_t now_ms) noexcept;

// A crash that Windows ended the program for before it could write its own report, from the
// event "Application Error" (ID 1000) of the Application log.
struct EventCrash {
  std::string app;
  std::string app_version;
  std::uint32_t app_stamp = 0;
  std::string module;
  std::string module_version;
  std::uint32_t module_stamp = 0;
  std::uint32_t code = 0;
  std::uint64_t offset = 0;
  std::uint32_t pid = 0;
  std::uint64_t created = 0;  // the process's creation time, as a FILETIME
};

// The event's XML as EvtRender gives it, in UTF-8; nothing if it is not one of these.
[[nodiscard]] std::optional<EventCrash> parse_application_error(std::string_view xml);
// The XPath query for those events since a FILETIME.
[[nodiscard]] std::string event_query(std::uint64_t since_filetime);
// The report of one; `build` is the program that writes it, maybe not the one that crashed.
[[nodiscard]] std::string event_report(const Build& build, const EventCrash& crash);

// The file of a report the handler writes, not shown yet: new-<UTC yyyymmdd-hhmmss>-<pid>.txt.
struct Stamp {
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
};
void write_report_name(Writer& out, const Stamp& at, std::uint32_t pid) noexcept;
inline constexpr std::string_view new_prefix = "new-";

// What to do with the files of the crash folder at a start: show the newest new report, keep every
// report as a shown one, and only the newest `kept` of those.
inline constexpr std::size_t kept_reports = 5;
struct Tidy {
  std::string show;  // a new report to show; empty for none
  std::vector<std::pair<std::string, std::string>> rename;
  std::vector<std::string> remove;
};
[[nodiscard]] Tidy tidy_reports(std::span<const std::string> names);

// The mark of a run under way, running-<pid>.txt, which a clean end removes. A start that finds one
// of a process gone asks Windows' event log how it ended.
struct Marker {
  std::uint32_t pid = 0;
  std::uint64_t created = 0;  // FILETIME, which tells the process from a later one with its id
};
inline constexpr std::string_view marker_prefix = "running-";
void write_marker(Writer& out, const Marker& marker) noexcept;
[[nodiscard]] std::optional<Marker> parse_marker(std::string_view text) noexcept;

// The issue that reports it: a new issue from the crash form, filled with the report and a title.
// A report too long for an address loses its last lines; the whole is on the clipboard.
[[nodiscard]] std::string issue_title(std::string_view report);
[[nodiscard]] std::string issue_url(std::string_view repository_page, std::string_view report, std::size_t limit = 7500);

}  // namespace crpp::crash
