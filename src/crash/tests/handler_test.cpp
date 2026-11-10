// Crashes copies of itself in every way the handler catches, and reads the reports they leave; and one
// that only Windows sees, whose report comes from the event log at the next start.
#include <windows.h>

#include <winevt.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "check.hpp"
#include "crpp/crash/breadcrumbs.hpp"
#include "crpp/crash/handler.hpp"
#include "crpp/crash/report.hpp"
#include "crpp/win/handles.hpp"

using namespace crpp;

namespace {

struct Case {
  const wchar_t* kind;
  DWORD exit_code;
  std::string_view cause;
  std::string_view thread;
};

constexpr Case cases[] = {
    {L"access", 0xc0000005, "cause: access violation (0xc0000005), writing 0x0000000000000000\n", "thread: main\n"},
    {L"thread", 0xc0000005, "cause: access violation (0xc0000005), writing 0x0000000000000000\n", "thread: other\n"},
    {L"stl", 3, "cause: abort (", "thread: main\n"},
    {L"abort", 3, "cause: abort (", "thread: main\n"},
    {L"terminate", 3, "cause: std::terminate\n", "thread: main\n"},
    {L"purecall", 3, "cause: pure virtual function call\n", "thread: main\n"},
    {L"invalid", 3, "cause: invalid parameter to the C runtime\n", "thread: main\n"},
    {L"overflow", 0xc00000fd, "cause: stack overflow (0xc00000fd)\n", "thread: main\n"},
};

std::filesystem::path self() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return path;
}

// Runs a copy of this test that crashes as `kind` with its reports in `dir`, and gives its exit code.
DWORD crash_child(const wchar_t* kind, const std::filesystem::path& dir) {
  std::wstring command = L"\"" + self().wstring() + L"\" --crash " + kind + L" \"" + dir.wstring() + L"\"";
  STARTUPINFOW startup{sizeof startup};
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) return 0xffffffff;
  const win::Handle thread(process.hThread);
  const win::Handle child(process.hProcess);
  if (WaitForSingleObject(child.get(), 30000) != WAIT_OBJECT_0) {
    TerminateProcess(child.get(), 1);
    return 0xfffffffe;
  }
  DWORD code = 0;
  GetExitCodeProcess(child.get(), &code);
  return code;
}

std::vector<std::string> names_in(const std::filesystem::path& dir) {
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) names.push_back(entry.path().filename().string());
  return names;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}

void caught(const Case& c, const std::filesystem::path& root) {
  const std::filesystem::path dir = root / c.kind;
  const DWORD code = crash_child(c.kind, dir);
  if (code != c.exit_code) std::fprintf(stderr, "%ls: exit code %08lx\n", c.kind, code);
  CHECK(code == c.exit_code);
  const std::vector<std::string> names = names_in(dir);
  CHECK(names.size() == 1);  // the report, and no mark of the run left
  if (names.size() != 1) return;
  CHECK(names[0].starts_with(crash::new_prefix));
  const std::string report = read_file(dir / names[0]);
  const auto has = [&](std::string_view part) {
    const bool found = report.find(part) != std::string::npos;
    if (!found) std::fprintf(stderr, "%ls: no \"%.*s\" in\n%s\n", c.kind, static_cast<int>(part.size()), part.data(), report.c_str());
    return found;
  };
  CHECK(report.starts_with("crpp crash report\nversion: 0.0.0 (test)\nbuild: development\nwindows: 10."));
  CHECK(has(c.cause));
  CHECK(has(c.thread));
  CHECK(has("crpp_crash_handler_test.exe+0x"));
  CHECK(has("\n  crpp_crash_handler_test.exe  "));
  CHECK(has("ntdll.dll"));
  CHECK(has("events:\n  -"));
  CHECK(has(" s  connecting: before the crash\n"));
  CHECK(report.find(root.filename().string()) == std::string::npos);  // no path
  if (std::wstring_view(c.kind) == L"overflow") CHECK(has(" more like it)\n"));
  // Shown once, then kept as shown.
  const auto shown = crash::take_new_report(dir);
  CHECK(shown && *shown == report);
  CHECK(!crash::take_new_report(dir));
  CHECK(names_in(dir) == std::vector<std::string>{names[0].substr(crash::new_prefix.size())});
}

// Whether Windows Error Reporting runs here: switched off, it records no crash in the event log.
bool error_reporting_on() {
  const auto off = [](HKEY root, const wchar_t* key) {
    DWORD value = 0, size = sizeof value;
    return RegGetValueW(root, key, L"Disabled", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS && value != 0;
  };
  constexpr const wchar_t* settings = L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting";
  constexpr const wchar_t* policy = L"SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Error Reporting";
  return !off(HKEY_LOCAL_MACHINE, settings) && !off(HKEY_LOCAL_MACHINE, policy) && !off(HKEY_CURRENT_USER, settings) && !off(HKEY_CURRENT_USER, policy);
}

// The newest crash Windows recorded of any program, read without the code under test.
std::optional<crash::EventCrash> newest_recorded_crash() {
  const EVT_HANDLE results = EvtQuery(nullptr, L"Application", L"*[System[Provider[@Name='Application Error'] and (EventID=1000)]]",
                                      EvtQueryChannelPath | EvtQueryReverseDirection);
  if (!results) return std::nullopt;
  std::optional<crash::EventCrash> found;
  EVT_HANDLE event = nullptr;
  DWORD returned = 0;
  if (EvtNext(results, 1, &event, 1000, 0, &returned) && returned == 1) {
    std::vector<wchar_t> xml(16384);
    DWORD used = 0, properties = 0;
    if (EvtRender(nullptr, event, EvtRenderEventXml, static_cast<DWORD>(xml.size() * sizeof(wchar_t)), xml.data(), &used, &properties)) {
      std::string utf8(xml.size() * 3, '\0');
      const int size = WideCharToMultiByte(CP_UTF8, 0, xml.data(), -1, utf8.data(), static_cast<int>(utf8.size()), nullptr, nullptr);
      utf8.resize(size > 0 ? static_cast<std::size_t>(size - 1) : 0);
      found = crash::parse_application_error(utf8);
    }
    EvtClose(event);
  }
  EvtClose(results);
  return found;
}

// The mark a run that Windows ended leaves turns into a report from the event log, and goes.
void reports_from_the_event_log(const std::filesystem::path& root) {
  // Where Windows records crashes: one that ends in __fastfail, which no handler sees.
  if (error_reporting_on()) {
    const std::filesystem::path dir = root / L"fastfail";
    const DWORD code = crash_child(L"fastfail", dir);
    CHECK(code == 0xc0000409);
    const std::vector<std::string> names = names_in(dir);
    CHECK(names.size() == 1 && names[0].starts_with(crash::marker_prefix));
    if (names.size() != 1) return;
    const std::string marker = read_file(dir / names[0]);
    // Windows writes its event as it reports the crash; give it a moment.
    std::optional<std::string> report;
    for (int attempt = 0; attempt < 20 && !report; ++attempt) {
      if (!std::filesystem::exists(dir / names[0])) std::ofstream(dir / names[0], std::ios::binary) << marker;
      crash::collect_unreported(dir);
      report = crash::take_new_report(dir);
      if (!report) Sleep(500);
    }
    CHECK(report.has_value());
    if (!report) return;
    CHECK(report->find("cause: fail fast or stack buffer overrun (0xc0000409), ended by Windows\n") != std::string::npos);
    CHECK(report->find("crpp_crash_handler_test.exe") != std::string::npos);
    CHECK(report->find("reported by: 0.0.0 (parent), development\n") != std::string::npos);
    for (const std::string& name : names_in(dir)) CHECK(!name.starts_with(crash::marker_prefix));
  } else {
    std::fprintf(stderr, "Windows Error Reporting is off here, so no __fastfail is recorded; reading a crash recorded before\n");
  }
  // Wherever the log holds a crash of any program: a mark of that process finds it.
  const auto recorded = newest_recorded_crash();
  if (!recorded) {
    std::fprintf(stderr, "the event log holds no crash to read\n");
    return;
  }
  const std::filesystem::path dir = root / L"recorded";
  std::filesystem::create_directories(dir);
  std::ofstream(dir / ("running-" + std::to_string(recorded->pid) + ".txt"), std::ios::binary)
      << recorded->pid << ' ' << std::hex << recorded->created << '\n';
  crash::collect_unreported(dir);
  const auto report = crash::take_new_report(dir);
  CHECK(report.has_value());
  if (!report) return;
  CHECK(report->find("ended by Windows\n") != std::string::npos);
  CHECK(report->find("stack:\n  0  " + recorded->module + "+0x") != std::string::npos);
  CHECK(report->find(":\\") == std::string::npos);  // no path
}

// A mark of a process still running is left alone; one of no process is taken away.
void marks_of_others(const std::filesystem::path& root) {
  const std::filesystem::path dir = root / L"marks";
  std::filesystem::create_directories(dir);
  // A pid that is no process's, and a mark that cannot be read.
  std::ofstream(dir / "running-1.txt", std::ios::binary) << "1 0000000000000001\n";
  std::ofstream(dir / "running-x.txt", std::ios::binary) << "garbage";
  crash::collect_unreported(dir);
  CHECK(names_in(dir).empty());
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc == 4 && std::wstring_view(argv[1]) == L"--crash") {
    crash::install({argv[3], "0.0.0", "test", false});
    crash::breadcrumbs.note(crash::now_ms(), "connecting", "before the crash");
    crash::begin_run();
    std::string kind;
    for (const wchar_t* at = argv[2]; *at != 0; ++at) kind += static_cast<char>(*at);  // ASCII
    crash::crash_on_purpose(kind);
    return 0;
  }
  const std::filesystem::path root = std::filesystem::temp_directory_path() / ("crpp-crash-test-" + std::to_string(GetCurrentProcessId()));
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  crash::install({root, "0.0.0", "parent", false});
  if (argc == 2 && std::wstring_view(argv[1]) == L"--event-log") {
    reports_from_the_event_log(root);
  } else {
    for (const Case& c : cases) caught(c, root);
    marks_of_others(root);
  }
  std::filesystem::remove_all(root, ec);
  return crpp_test_result();
}
