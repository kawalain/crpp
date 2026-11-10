#pragma once
// Windows' side of the Crash report: the handlers that write one, the mark of a run under way, and
// what Windows' event log says of a run that could not write its own.
//
// Caught here, on any thread: an exception nothing handled (an access violation, a stack overflow),
// abort(), which a failed check of the hardened standard library ends in, std::terminate, a pure
// virtual call and an invalid parameter to the C runtime. The handler walks the stack of the thread
// that crashed with the unwind tables every x64 image carries, names each frame module+offset from
// the images' own headers, writes the report from memory set aside before and ends the process. It
// allocates nothing, loads nothing and takes no lock of the loader, so a crash anywhere can write it.
//
// Not caught: what Windows ends at once (Control Flow Guard, a CET shadow stack, __fastfail, a
// corrupt heap). For those Windows writes the event "Application Error" (1000), and the next start
// turns it into a report from the mark the run left behind.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace crpp::crash {

struct Setup {
  std::filesystem::path dir;  // the crash folder, made when first written to
  std::string_view version;   // these two must live as long as the program
  std::string_view hash;
  bool official = false;
};

// As early as possible, on the main thread. Costs nothing until a crash: the memory the handler
// writes into is only touched then.
void install(const Setup& setup);

// The mark of this run in the crash folder, for a run of the app, not a shot. end_run removes it at
// a clean end, as the handler does once its report is written.
void begin_run();
void end_run();

// Marks of runs that are gone without a clean end or a report: what Windows' event log recorded of
// them becomes a report. The event log's DLL is loaded only when there is such a mark.
void collect_unreported(const std::filesystem::path& dir);

// The newest report not shown yet, from now on counted as shown; only the newest kept_reports stay.
[[nodiscard]] std::optional<std::string> take_new_report(const std::filesystem::path& dir);

// The clock the breadcrumbs are noted on: milliseconds since Windows started.
[[nodiscard]] std::uint64_t now_ms() noexcept;

// Crashes as asked, to see a report written: access, thread (an access violation on another
// thread), stl (an index out of range), abort, terminate, purecall, invalid (a parameter to the C
// runtime), overflow (the stack) and fastfail (which only Windows sees). Anything else returns.
void crash_on_purpose(std::string_view kind);

}  // namespace crpp::crash
