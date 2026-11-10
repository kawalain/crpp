// The Crash report's text, the breadcrumbs, the event log's events and the crash folder's files.
#include <string>
#include <vector>

#include "check.hpp"
#include "crpp/crash/breadcrumbs.hpp"
#include "crpp/crash/report.hpp"
#include "crpp/crash/writer.hpp"

using namespace crpp::crash;

namespace {

void writer_numbers() {
  char buffer[64];
  Writer out(buffer);
  out.hex(0x1a2b).ch(' ').hex(0x5, 4).ch(' ').hex(0xabc, 1, true).ch(' ').dec(0).ch(' ').dec(7, 3).ch(' ').seconds(12345).ch(' ').seconds(99);
  CHECK(out.written() == "1a2b 0005 ABC 0 007 12.3 0.0");
  CHECK(!out.full());
  char small[4];
  Writer tight(small);
  tight.text("abcdef");
  CHECK(tight.written() == "abcd");
  CHECK(tight.full());
  char wide[20];
  Writer max(wide);
  max.hex(~0ull);
  CHECK(max.written() == "ffffffffffffffff");
}

void breadcrumbs_keep_the_last() {
  Breadcrumbs crumbs;
  CHECK(crumbs.size() == 0);
  crumbs.note(10, "connecting");
  crumbs.note(20, "in room", "line\nbreak");
  CHECK(crumbs.size() == 2);
  CHECK(std::string_view(crumbs.at(0).what, crumbs.at(0).what_size) == "connecting");
  CHECK(std::string_view(crumbs.at(1).detail, crumbs.at(1).detail_size) == "line break");
  for (std::uint64_t i = 0; i < 40; ++i) crumbs.note(100 + i, "leaving");
  CHECK(crumbs.size() == Breadcrumbs::capacity);
  CHECK(crumbs.at(0).ms == 100 + 40 - Breadcrumbs::capacity);
  CHECK(crumbs.at(Breadcrumbs::capacity - 1).ms == 139);
}

void breadcrumbs_cut_on_a_character() {
  Breadcrumbs crumbs;
  // 109 bytes of ASCII and then a three-byte character that does not fit whole.
  const std::string detail = std::string(109, 'a') + "가";
  crumbs.note(1, "skin: not loaded", detail);
  CHECK(crumbs.at(0).detail_size == 109);
  const std::string fits = std::string(107, 'a') + "가";
  crumbs.note(2, "skin: not loaded", fits);
  CHECK(crumbs.at(1).detail_size == 110);
}

void report_text() {
  Breadcrumbs crumbs;
  crumbs.note(1000, "connecting");
  crumbs.note(4000, "skin: not loaded", "retro: bad file");
  const Module modules[] = {
      {"crpp.exe", 0x6aee79a0, 0x1a3000, "crpp.pdb", {0x78, 0x56, 0x34, 0x12, 0xbc, 0x9a, 0xf0, 0xde, 1, 2, 3, 4, 5, 6, 7, 8}, 1},
      {"ntdll.dll", 0x12345678, 0x1f8000, "", {}, 0},
  };
  const Frame frames[] = {{0x1a2b3c, 0, 0}, {0x1a0000, 0, 812}, {0x9f00, 1, 0}, {0x7ff600001234, no_module, 0}};
  Crash crash;
  crash.code = 0xc0000005;
  crash.access = 1;
  crash.address = 0x10;
  crash.main_thread = true;
  crash.uptime_ms = 734200;
  crash.frames = frames;
  crash.modules = modules;
  char buffer[4096];
  Writer out(buffer);
  write_report(out, {"0.4.0", "abc1234", true, "10.0.19044"}, crash, crumbs, 5500);
  const std::string_view expected =
      "crpp crash report\n"
      "version: 0.4.0 (abc1234)\n"
      "build: official\n"
      "windows: 10.0.19044\n"
      "cause: access violation (0xc0000005), writing 0x0000000000000010\n"
      "thread: main\n"
      "uptime: 734.2 s\n"
      "stack:\n"
      "  0  crpp.exe+0x1a2b3c\n"
      "  1  crpp.exe+0x1a0000  (and 812 more like it)\n"
      "  2  ntdll.dll+0x9f00\n"
      "  3  0x00007ff600001234\n"
      "modules:\n"
      "  crpp.exe  6aee79a0 1a3000  crpp.pdb 123456789ABCDEF001020304050607081\n"
      "  ntdll.dll  12345678 1f8000\n"
      "events:\n"
      "  -4.5 s  connecting\n"
      "  -1.5 s  skin: not loaded: retro: bad file\n";
  CHECK(out.written() == expected);
}

void report_causes() {
  const Breadcrumbs none;
  const auto cause_of = [&](Crash crash) {
    char buffer[1024];
    Writer out(buffer);
    write_report(out, {"0.0.0", "x", false, "10.0.1"}, crash, none, 0);
    const std::string_view text = out.written();
    const std::size_t at = text.find("cause: ");
    return std::string(text.substr(at, text.find('\n', at) - at));
  };
  CHECK(cause_of({.cause = Cause::Abort}) == "cause: abort (a failed check of the standard library, or abort())");
  CHECK(cause_of({.cause = Cause::PureCall}) == "cause: pure virtual function call");
  CHECK(cause_of({.cause = Cause::Terminate}) == "cause: std::terminate");
  CHECK(cause_of({.cause = Cause::InvalidParameter}) == "cause: invalid parameter to the C runtime");
  CHECK(cause_of({.code = 0xc00000fd}) == "cause: stack overflow (0xc00000fd)");
  CHECK(cause_of({.code = 0xe0434352}) == "cause: exception (0xe0434352)");
  CHECK(cause_of({.code = 0xc0000005, .access = 8, .address = 0xdead}) == "cause: access violation (0xc0000005), executing 0x000000000000dead");
}

void report_never_overflows() {
  Breadcrumbs crumbs;
  for (int i = 0; i < 40; ++i) crumbs.note(static_cast<std::uint64_t>(i), "assets: no answer", std::string(200, 'x'));
  std::vector<Frame> frames(64, Frame{0x1234, 0, 0});
  const Module modules[] = {{"crpp.exe", 1, 2, "crpp.pdb", {}, 1}};
  char buffer[512];
  Writer out(buffer);
  write_report(out, {"0.4.0", "abc1234", true, "10.0.19044"}, {.frames = frames, .modules = modules}, crumbs, 100);
  CHECK(out.full());
  CHECK(out.written().size() == sizeof buffer);
}

// As EvtRender gave it on Windows 10 19044 for a crash of a development build; the paths are
// someone's own, so they are replaced here.
constexpr std::string_view event_xml =
    "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'><System><Provider Name='Application Error'/>"
    "<EventID Qualifiers='0'>1000</EventID><Version>0</Version><Level>2</Level><Task>100</Task><Opcode>0</Opcode>"
    "<Keywords>0x80000000000000</Keywords><TimeCreated SystemTime='2026-11-07T01:07:11.8886750Z'/><EventRecordID>10512</EventRecordID>"
    "<Correlation/><Execution ProcessID='0' ThreadID='0'/><Channel>Application</Channel><Computer>pc</Computer><Security/></System>"
    "<EventData><Data>crpp.exe</Data><Data>0.0.0.0</Data><Data>6aee79a0</Data><Data>crpp.exe</Data><Data>0.0.0.0</Data>"
    "<Data>6aee79a0</Data><Data>c0000005</Data><Data>00000000000b3960</Data><Data>16a4</Data><Data>01dd6e5451b9766c</Data>"
    "<Data>C:\\x\\crpp.exe</Data><Data>C:\\x\\crpp.exe</Data><Data>f4da9aab-fc7a-47b4-8927-febf3048dc5d</Data><Data></Data><Data></Data>"
    "</EventData></Event>";

void events_of_windows() {
  const auto crash = parse_application_error(event_xml);
  CHECK(crash.has_value());
  if (!crash) return;
  CHECK(crash->app == "crpp.exe");
  CHECK(crash->app_version == "0.0.0.0");
  CHECK(crash->app_stamp == 0x6aee79a0);
  CHECK(crash->module == "crpp.exe");
  CHECK(crash->code == 0xc0000005);
  CHECK(crash->offset == 0xb3960);
  CHECK(crash->pid == 0x16a4);
  CHECK(crash->created == 0x01dd6e5451b9766cull);
  // Named data, as other versions of Windows write it, and an empty one.
  std::string named(event_xml);
  for (std::size_t at = named.find("<Data>"); at != std::string::npos; at = named.find("<Data>", at)) named.replace(at, 6, "<Data Name='X'>");
  named.replace(named.find("<Data Name='X'></Data></EventData>"), 22, "<Data Name='X'/>");
  const auto again = parse_application_error(named);
  CHECK(again.has_value() && again->pid == 0x16a4 && again->offset == 0xb3960);
  // Another provider, another event and too little data are none of these.
  std::string other(event_xml);
  other.replace(other.find("Application Error"), 17, "Application Hang");
  CHECK(!parse_application_error(other));
  std::string hang(event_xml);
  hang.replace(hang.find(">1000<"), 6, ">1002<");
  CHECK(!parse_application_error(hang));
  CHECK(!parse_application_error("<Event><System><Provider Name='Application Error'/><EventID>1000</EventID></System><EventData><Data>a</Data></EventData></Event>"));
  // Escaped characters come back as themselves.
  std::string escaped(event_xml);
  escaped.replace(escaped.find("<Data>0.0.0.0</Data>"), 20, "<Data>1&amp;2</Data>");
  CHECK(parse_application_error(escaped)->app_version == "1&2");
}

void event_query_names_the_time() {
  // The creation time of the process in the event above.
  CHECK(event_query(0x01dd6e5451b9766cull).ends_with("TimeCreated[@SystemTime>='2026-11-07T01:07:11.840Z']]]"));
  CHECK(event_query(116444736000000000ull).ends_with("'1970-01-01T00:00:00.000Z']]]"));
  CHECK(event_query(116444736000000000ull + 86400ull * 10000000ull + 12345678ull).ends_with("'1970-01-02T00:00:01.234Z']]]"));
  CHECK(event_query(0).starts_with("*[System[Provider[@Name='Application Error'] and (EventID=1000)"));
}

void event_reports() {
  const auto crash = parse_application_error(event_xml);
  const std::string text = event_report({"0.4.0", "abc1234", true, "10.0.19044"}, *crash);
  CHECK(text.starts_with("crpp crash report\nversion: 0.0.0.0 (as Windows' event log names it)\nreported by: 0.4.0 (abc1234), official\n"));
  CHECK(text.find("cause: access violation (0xc0000005), ended by Windows\n") != std::string::npos);
  CHECK(text.find("stack:\n  0  crpp.exe+0xb3960\n") != std::string::npos);
  CHECK(text.find("C:\\") == std::string::npos);  // no path
}

void report_names() {
  char buffer[64];
  Writer out(buffer);
  write_report_name(out, {2026, 11, 10, 9, 5, 7}, 4660);
  CHECK(out.written() == "new-20261110-090507-4660.txt");
}

void tidying() {
  const std::vector<std::string> names{
      "20261101-000000-1.txt", "20261102-000000-1.txt", "20261103-000000-1.txt", "20261104-000000-1.txt",
      "new-20261106-000000-9.txt", "new-20261105-000000-8.txt", "running-12.txt", "notes.md",
  };
  const Tidy tidy = tidy_reports(names);
  CHECK(tidy.show == "new-20261106-000000-9.txt");
  CHECK(tidy.rename.size() == 2);
  CHECK(tidy.rename[0].first == "new-20261106-000000-9.txt" && tidy.rename[0].second == "20261106-000000-9.txt");
  CHECK(tidy.remove.size() == 1 && tidy.remove[0] == "20261101-000000-1.txt");
  const Tidy quiet = tidy_reports(std::vector<std::string>{"20261101-000000-1.txt", "running-3.txt"});
  CHECK(quiet.show.empty() && quiet.rename.empty() && quiet.remove.empty());
}

void markers() {
  char buffer[64];
  Writer out(buffer);
  write_marker(out, {4660, 0x01dd6e5451b9766cull});
  CHECK(out.written() == "4660 01dd6e5451b9766c\n");
  const auto marker = parse_marker(out.written());
  CHECK(marker && marker->pid == 4660 && marker->created == 0x01dd6e5451b9766cull);
  CHECK(parse_marker("4660 01dd6e5451b9766c\r\n").has_value());
  CHECK(!parse_marker(""));
  CHECK(!parse_marker("4660"));
  CHECK(!parse_marker("x 12"));
  CHECK(!parse_marker("12 zz"));
}

void issues() {
  const std::string report =
      "crpp crash report\nversion: 0.4.0 (abc1234)\ncause: access violation (0xc0000005), writing 0x0000000000000010\n"
      "stack:\n  0  crpp.exe+0x1a2b3c\n  1  ntdll.dll+0x9f00\nevents:\n  -1.5 s  connecting\n";
  CHECK(issue_title(report) == "크래시: access violation, crpp.exe+0x1a2b3c");
  CHECK(issue_title("crpp crash report\ncause: abort (a failed check)\nstack:\n  (none)\n") == "크래시: abort");
  const std::string url = issue_url("https://github.com/kawalain/crpp", report);
  CHECK(url.starts_with("https://github.com/kawalain/crpp/issues/new?template=crash.yml&title=%ED%81%AC%EB%9E%98%EC%8B%9C%3A%20access%20violation"));
  CHECK(url.find("&report=crpp%20crash%20report%0Aversion%3A%200.4.0") != std::string::npos);
  CHECK(url.find(' ') == std::string::npos && url.find('\n') == std::string::npos);
  // Too long: whole lines go from the end, and it says so.
  std::string long_report = report;
  for (int i = 0; i < 400; ++i) long_report += "  -1.5 s  assets: no answer\n";
  const std::string cut = issue_url("https://github.com/kawalain/crpp", long_report, 2000);
  CHECK(cut.size() <= 2000);
  CHECK(cut.ends_with("%28cut%20here%3B%20the%20whole%20report%20is%20on%20the%20clipboard%29%0A"));
  CHECK(cut.find("crpp.exe%2B0x1a2b3c") != std::string::npos);
}

}  // namespace

int main() {
  writer_numbers();
  breadcrumbs_keep_the_last();
  breadcrumbs_cut_on_a_character();
  report_text();
  report_causes();
  report_never_overflows();
  events_of_windows();
  event_query_names_the_time();
  event_reports();
  report_names();
  tidying();
  markers();
  issues();
  return crpp_test_result();
}
