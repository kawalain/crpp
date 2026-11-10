#include <windows.h>

#include <winevt.h>
#include <werapi.h>

#include <io.h>
#include <signal.h>
#include <stdlib.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <system_error>
#include <thread>
#include <vector>

#include "crpp/crash/breadcrumbs.hpp"
#include "crpp/crash/handler.hpp"
#include "crpp/crash/report.hpp"
#include "crpp/crash/writer.hpp"
#include "crpp/win/handles.hpp"

namespace crpp::crash {

namespace {

// What the handler needs, known before any crash.
struct State {
  wchar_t dir[1024]{};
  std::size_t dir_size = 0;
  wchar_t marker[1100]{};
  bool marked = false;
  std::uint64_t exe_base = 0;
  char exe_name[96]{};
  std::size_t exe_name_size = 0;
  char windows[32]{};
  std::size_t windows_size = 0;
  std::string_view version;
  std::string_view hash;
  bool official = false;
  DWORD main_thread = 0;
  std::uint64_t started_ms = 0;
};

constinit State state;
constinit std::atomic<DWORD> crashing{0};

// What the handler fills: here rather than on a stack that may be nearly used up.
constexpr std::size_t max_frames = 64;
constexpr std::size_t max_modules = 24;
constinit CONTEXT context{};
constinit Frame frames[max_frames]{};
constinit Module modules[max_modules]{};
constinit std::uint64_t module_bases[max_modules]{};
constinit char text[16 * 1024]{};

std::string_view bounded(const char* from, std::size_t limit) noexcept {
  std::size_t size = 0;
  while (size < limit && size < 128 && from[size] != '\0') ++size;
  return {from, size};
}

std::string_view file_name_of(std::string_view path) noexcept {
  const std::size_t slash = path.find_last_of("\\/");
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

// The image's own headers, read where it lies in memory: its name from its exports, its stamp and
// size, and its PDB from its CodeView record. False when they cannot be read.
bool read_module(std::uint64_t base, Module& out) noexcept {
#ifdef _MSC_VER
  __try {
#endif
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + static_cast<std::uint64_t>(dos->e_lfanew));
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    out.stamp = nt->FileHeader.TimeDateStamp;
    out.size = nt->OptionalHeader.SizeOfImage;
    const IMAGE_DATA_DIRECTORY& exports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (exports.VirtualAddress != 0 && exports.Size >= sizeof(IMAGE_EXPORT_DIRECTORY) && exports.VirtualAddress < out.size) {
      const auto* directory = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + exports.VirtualAddress);
      if (directory->Name != 0 && directory->Name < out.size) out.name = bounded(reinterpret_cast<const char*>(base + directory->Name), out.size - directory->Name);
    }
    const IMAGE_DATA_DIRECTORY& debug = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (debug.VirtualAddress != 0 && debug.VirtualAddress < out.size) {
      const auto* entries = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + debug.VirtualAddress);
      const std::size_t count = std::min<std::size_t>(debug.Size / sizeof(IMAGE_DEBUG_DIRECTORY), 16);
      for (std::size_t i = 0; i < count; ++i) {
        const IMAGE_DEBUG_DIRECTORY& entry = entries[i];
        if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW || entry.AddressOfRawData == 0 || entry.SizeOfData < 25 || entry.AddressOfRawData >= out.size) continue;
        const auto* record = reinterpret_cast<const unsigned char*>(base + entry.AddressOfRawData);
        if (std::memcmp(record, "RSDS", 4) != 0) continue;
        std::memcpy(out.guid.data(), record + 4, 16);
        std::memcpy(&out.age, record + 20, 4);
        out.pdb = file_name_of(bounded(reinterpret_cast<const char*>(record + 24), entry.SizeOfData - 24));
        break;
      }
    }
    return true;
#ifdef _MSC_VER
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
#endif
}

std::uint16_t module_of(std::uint64_t base, std::size_t& count) noexcept {
  for (std::size_t i = 0; i < count; ++i) {
    if (module_bases[i] == base) return static_cast<std::uint16_t>(i);
  }
  if (count == max_modules) return no_module;
  Module module;
  if (!read_module(base, module)) return no_module;
  // An exe exports nothing to be named by; a DLL with no exports goes by its PDB.
  if (module.name.empty() && base == state.exe_base) module.name = {state.exe_name, state.exe_name_size};
  if (module.name.empty()) module.name = module.pdb.empty() ? std::string_view("?") : module.pdb;
  module_bases[count] = base;
  modules[count] = module;
  return static_cast<std::uint16_t>(count++);
}

// From `ctx` outwards along the unwind tables, into `frames`.
std::size_t walk(CONTEXT& ctx, std::size_t& module_count) noexcept {
  std::size_t n = 0;
#if defined(_M_X64)
#ifdef _MSC_VER
  __try {
#endif
    // Deep enough for a recursion that used up the stack, whose frames fold into one.
    for (std::size_t depth = 0; depth < 1u << 17 && ctx.Rip != 0; ++depth) {
      const DWORD64 pc = ctx.Rip;
      const DWORD64 sp = ctx.Rsp;
      void* image = nullptr;
      RtlPcToFileHeader(reinterpret_cast<void*>(pc), &image);
      const auto base = reinterpret_cast<std::uint64_t>(image);
      const std::uint16_t module = base != 0 ? module_of(base, module_count) : no_module;
      const std::uint64_t offset = module == no_module ? pc : pc - base;
      if (n > 0 && frames[n - 1].module == module && frames[n - 1].offset == offset) {
        ++frames[n - 1].repeats;
      } else {
        if (n == max_frames) break;
        frames[n++] = Frame{offset, module, 0};
      }
      DWORD64 function_base = 0;
      if (PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(pc, &function_base, nullptr)) {
        void* handler_data = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, function_base, pc, function, &ctx, &handler_data, &establisher, nullptr);
      } else {
        // A leaf function, which keeps nothing on the stack but its return address.
        ctx.Rip = *reinterpret_cast<const DWORD64*>(ctx.Rsp);
        ctx.Rsp += 8;
      }
      if (ctx.Rsp < sp || (ctx.Rsp == sp && ctx.Rip == pc)) break;
    }
#ifdef _MSC_VER
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
#endif
#else
  (void)ctx;
  (void)module_count;
#endif
  return n;
}

void write_file(std::string_view report) noexcept {
  SYSTEMTIME now{};
  GetSystemTime(&now);
  char name[64];
  Writer out(name);
  write_report_name(out, {now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond}, GetCurrentProcessId());
  wchar_t path[std::size(State{}.dir) + 80]{};
  std::copy_n(state.dir, state.dir_size, path);
  path[state.dir_size] = L'\\';
  const std::string_view written = out.written();
  for (std::size_t i = 0; i < written.size(); ++i) path[state.dir_size + 1 + i] = static_cast<wchar_t>(written[i]);
  CreateDirectoryW(state.dir, nullptr);
  const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD done = 0;
  WriteFile(file, report.data(), static_cast<DWORD>(report.size()), &done, nullptr);
  FlushFileBuffers(file);
  CloseHandle(file);
}

[[noreturn]] void report_and_end(Cause cause, const CONTEXT* from, const EXCEPTION_RECORD* record, UINT exit_code) noexcept {
  const DWORD me = GetCurrentThreadId();
  DWORD first = 0;
  if (!crashing.compare_exchange_strong(first, me)) {
    // A crash while writing the report ends it here; another thread's waits for the first to end
    // the process.
    if (first == me) TerminateProcess(GetCurrentProcess(), exit_code);
    for (;;) Sleep(INFINITE);
  }
  if (from != nullptr) {
    context = *from;
  } else {
    RtlCaptureContext(&context);
  }
  std::size_t module_count = 0;
  const std::size_t frame_count = walk(context, module_count);
  Crash crash;
  crash.cause = cause;
  if (record != nullptr) {
    crash.code = record->ExceptionCode;
    if ((crash.code == EXCEPTION_ACCESS_VIOLATION || crash.code == EXCEPTION_IN_PAGE_ERROR) && record->NumberParameters >= 2) {
      crash.access = static_cast<int>(record->ExceptionInformation[0]);
      crash.address = record->ExceptionInformation[1];
    }
  }
  crash.main_thread = me == state.main_thread;
  const std::uint64_t now = now_ms();
  crash.uptime_ms = now - state.started_ms;
  crash.frames = {frames, frame_count};
  crash.modules = {modules, module_count};
  Writer out(text);
  write_report(out, {state.version, state.hash, state.official, {state.windows, state.windows_size}}, crash, breadcrumbs, now);
  if (state.dir_size != 0) write_file(out.written());
  if (state.marked) DeleteFileW(state.marker);
  TerminateProcess(GetCurrentProcess(), exit_code);
  __fastfail(FAST_FAIL_FATAL_APP_EXIT);
}

LONG WINAPI on_exception(EXCEPTION_POINTERS* pointers) {
  report_and_end(Cause::Exception, pointers->ContextRecord, pointers->ExceptionRecord, pointers->ExceptionRecord->ExceptionCode);
}

// The exit code abort() itself would give.
constexpr UINT abort_code = 3;

void __cdecl on_abort(int) { report_and_end(Cause::Abort, nullptr, nullptr, abort_code); }
[[noreturn]] void on_terminate() noexcept { report_and_end(Cause::Terminate, nullptr, nullptr, abort_code); }
void __cdecl on_purecall() { report_and_end(Cause::PureCall, nullptr, nullptr, abort_code); }
void __cdecl on_invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
  report_and_end(Cause::InvalidParameter, nullptr, nullptr, abort_code);
}

void copy_utf8(std::wstring_view from, char* to, std::size_t capacity, std::size_t& size) {
  const int written = WideCharToMultiByte(CP_UTF8, 0, from.data(), static_cast<int>(from.size()), to, static_cast<int>(capacity), nullptr, nullptr);
  size = written > 0 ? static_cast<std::size_t>(written) : 0;
}

std::uint64_t as_number(const FILETIME& time) noexcept { return static_cast<std::uint64_t>(time.dwHighDateTime) << 32 | time.dwLowDateTime; }

std::uint64_t creation_of(HANDLE process) noexcept {
  FILETIME created{}, exited{}, kernel{}, user{};
  return GetProcessTimes(process, &created, &exited, &kernel, &user) ? as_number(created) : 0;
}

bool still_running(const Marker& marker) noexcept {
  const win::Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, marker.pid));
  if (!process) return false;
  DWORD code = 0;
  return GetExitCodeProcess(process.get(), &code) && code == STILL_ACTIVE && creation_of(process.get()) == marker.created;
}

Build this_build() noexcept { return {state.version, state.hash, state.official, {state.windows, state.windows_size}}; }

// A function of a DLL by its name, as the type it has. Through a function without parameters, which
// no compiler warns a cast from.
template <class Function>
Function function_of(HMODULE module, const char* name) noexcept {
  return reinterpret_cast<Function>(reinterpret_cast<void (*)()>(GetProcAddress(module, name)));
}

// The event log's functions, from wevtapi.dll loaded for the purpose.
class EventLog {
 public:
  EventLog() : library_(LoadLibraryExW(L"wevtapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
    if (!library_) return;
    query_ = function_of<decltype(query_)>(library_.get(), "EvtQuery");
    next_ = function_of<decltype(next_)>(library_.get(), "EvtNext");
    render_ = function_of<decltype(render_)>(library_.get(), "EvtRender");
    close_ = function_of<decltype(close_)>(library_.get(), "EvtClose");
  }

  // The crash of this process recorded since it was created, if Windows recorded one.
  std::optional<EventCrash> crash_of(const Marker& marker) const {
    if (!query_ || !next_ || !render_ || !close_) return std::nullopt;
    const std::string query = event_query(marker.created);
    const std::wstring wide(query.begin(), query.end());  // ASCII
    const Closing results(close_, query_(nullptr, L"Application", wide.c_str(), EvtQueryChannelPath | EvtQueryReverseDirection));
    if (!results.handle) return std::nullopt;
    std::vector<wchar_t> buffer(8192);
    for (;;) {
      EVT_HANDLE events[16]{};
      DWORD returned = 0;
      if (!next_(results.handle, static_cast<DWORD>(std::size(events)), events, 1000, 0, &returned) || returned == 0) return std::nullopt;
      std::optional<EventCrash> found;
      for (DWORD i = 0; i < returned; ++i) {
        const Closing event(close_, events[i]);
        if (found) continue;
        DWORD used = 0, properties = 0;
        if (!render_(nullptr, event.handle, EvtRenderEventXml, static_cast<DWORD>(buffer.size() * sizeof(wchar_t)), buffer.data(), &used, &properties)) {
          if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) continue;
          buffer.resize(used / sizeof(wchar_t) + 1);
          if (!render_(nullptr, event.handle, EvtRenderEventXml, static_cast<DWORD>(buffer.size() * sizeof(wchar_t)), buffer.data(), &used, &properties)) continue;
        }
        const std::wstring_view xml(buffer.data(), wcsnlen(buffer.data(), used / sizeof(wchar_t)));
        std::string utf8(xml.size() * 3, '\0');
        std::size_t size = 0;
        copy_utf8(xml, utf8.data(), utf8.size(), size);
        utf8.resize(size);
        if (auto crash = parse_application_error(utf8); crash && crash->pid == marker.pid && crash->created == marker.created) found = std::move(crash);
      }
      if (found) return found;
    }
  }

 private:
  using Query = EVT_HANDLE(WINAPI*)(EVT_HANDLE, LPCWSTR, LPCWSTR, DWORD);
  using Next = BOOL(WINAPI*)(EVT_HANDLE, DWORD, EVT_HANDLE*, DWORD, DWORD, PDWORD);
  using Render = BOOL(WINAPI*)(EVT_HANDLE, EVT_HANDLE, DWORD, DWORD, PVOID, PDWORD, PDWORD);
  using Close = BOOL(WINAPI*)(EVT_HANDLE);

  // A handle of the event log, closed by the function loaded with the others.
  struct Closing {
    Close close;
    EVT_HANDLE handle;
    Closing(Close c, EVT_HANDLE h) : close(c), handle(h) {}
    Closing(const Closing&) = delete;
    Closing& operator=(const Closing&) = delete;
    ~Closing() {
      if (handle) close(handle);
    }
  };

  win::Library library_;
  Query query_ = nullptr;
  Next next_ = nullptr;
  Render render_ = nullptr;
  Close close_ = nullptr;
};

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4717)  // recursive on all control paths, as it is meant to be
#endif
int overflow(int depth) {
  volatile char pad[4096];
  pad[0] = static_cast<char>(depth);
  return overflow(depth + 1) + pad[0];
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

struct Base {
  Base() { call(this); }
  virtual ~Base() = default;
  virtual void f() = 0;
  static void call(Base* self);
};
void Base::call(Base* self) { self->f(); }
struct Derived final : Base {
  void f() override {}
};

void access_violation() {
  volatile int* volatile nowhere = nullptr;
  *nowhere = 1;
}

}  // namespace

void install(const Setup& setup) {
  const std::wstring dir = setup.dir.wstring();
  state.dir_size = std::min(dir.size(), std::size(state.dir) - 1);
  std::copy_n(dir.data(), state.dir_size, state.dir);
  state.version = setup.version;
  state.hash = setup.hash;
  state.official = setup.official;
  state.main_thread = GetCurrentThreadId();
  state.started_ms = now_ms();
  const HMODULE exe = GetModuleHandleW(nullptr);
  state.exe_base = reinterpret_cast<std::uint64_t>(exe);
  wchar_t path[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(exe, path, MAX_PATH);
  std::wstring_view name(path, length);
  if (const auto slash = name.find_last_of(L"\\/"); slash != std::wstring_view::npos) name.remove_prefix(slash + 1);
  copy_utf8(name, state.exe_name, std::size(state.exe_name), state.exe_name_size);
  // RtlGetVersion tells the truth whatever the manifest says.
  using GetVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
  OSVERSIONINFOW version{};
  version.dwOSVersionInfoSize = sizeof version;
  if (const auto get = function_of<GetVersion>(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"); get && get(&version) == 0) {
    Writer out(state.windows);
    out.dec(version.dwMajorVersion).ch('.').dec(version.dwMinorVersion).ch('.').dec(version.dwBuildNumber);
    state.windows_size = out.written().size();
  }

  SetUnhandledExceptionFilter(on_exception);
  // No message box and no report to Microsoft from abort(): the handler writes its own and ends.
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  signal(SIGABRT, on_abort);
  std::set_terminate(on_terminate);  // this thread's; elsewhere terminate aborts
  _set_purecall_handler(on_purecall);
  _set_invalid_parameter_handler(on_invalid_parameter);
}

void begin_run() {
  if (state.dir_size == 0) return;
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(std::wstring_view(state.dir, state.dir_size)), ec);
  char name[48];
  Writer out(name);
  out.text(marker_prefix).dec(GetCurrentProcessId()).text(".txt");
  const std::string_view written = out.written();
  std::copy_n(state.dir, state.dir_size, state.marker);
  state.marker[state.dir_size] = L'\\';
  for (std::size_t i = 0; i < written.size(); ++i) state.marker[state.dir_size + 1 + i] = static_cast<wchar_t>(written[i]);
  state.marker[state.dir_size + 1 + written.size()] = L'\0';
  char content[48];
  Writer body(content);
  write_marker(body, {GetCurrentProcessId(), creation_of(GetCurrentProcess())});
  const win::Handle file = win::own_handle(CreateFileW(state.marker, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!file) return;
  DWORD done = 0;
  state.marked = WriteFile(file.get(), body.written().data(), static_cast<DWORD>(body.written().size()), &done, nullptr) != 0;
}

void end_run() {
  if (!state.marked) return;
  state.marked = false;
  DeleteFileW(state.marker);
}

void collect_unreported(const std::filesystem::path& dir) {
  std::error_code ec;
  std::optional<EventLog> log;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    const std::string name = entry.path().filename().string();
    if (!name.starts_with(marker_prefix) || !name.ends_with(".txt")) continue;
    const auto marker = parse_marker(read_file(entry.path()));
    if (marker && (marker->pid == GetCurrentProcessId() || still_running(*marker))) continue;
    if (marker) {
      if (!log) log.emplace();
      if (const auto crash = log->crash_of(*marker)) {
        const std::string report = event_report(this_build(), *crash);
        SYSTEMTIME now{};
        GetSystemTime(&now);
        char file[64];
        Writer out(file);
        write_report_name(out, {now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond}, marker->pid);
        std::ofstream(dir / std::string(out.written()), std::ios::binary) << report;
      }
    }
    std::filesystem::remove(entry.path(), ec);
  }
}

std::optional<std::string> take_new_report(const std::filesystem::path& dir) {
  std::error_code ec;
  std::vector<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) names.push_back(entry.path().filename().string());
  const Tidy tidy = tidy_reports(names);
  std::optional<std::string> shown;
  if (!tidy.show.empty()) shown = read_file(dir / tidy.show);
  for (const auto& [from, to] : tidy.rename) std::filesystem::rename(dir / from, dir / to, ec);
  for (const std::string& name : tidy.remove) std::filesystem::remove(dir / name, ec);
  if (shown && shown->empty()) shown.reset();
  return shown;
}

std::uint64_t now_ms() noexcept { return GetTickCount64(); }

void crash_on_purpose(std::string_view kind) {
  if (kind == "access") access_violation();
  if (kind == "thread") std::thread(access_violation).join();
  if (kind == "stl") {
    std::vector<int> few(2);
    volatile std::size_t index = 3;
    few[index] = 1;
  }
  if (kind == "abort") std::abort();
  if (kind == "terminate") std::terminate();
  if (kind == "purecall") Derived{};
  if (kind == "invalid") (void)_close(-1);
  if (kind == "overflow") (void)overflow(0);
  if (kind == "fastfail") {
    // Windows' own report of it shows no dialog, for a test.
    WerSetFlags(WER_FAULT_REPORTING_NO_UI);
    __fastfail(FAST_FAIL_FATAL_APP_EXIT);
  }
}

}  // namespace crpp::crash
