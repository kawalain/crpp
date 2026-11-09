// Runs a program on a hidden Win32 desktop and passes its exit code on, so that a scripted
// run of the app never reaches the user's desktop and bash can wait for a GUI program:
//
//   crpp_hidden_run <seconds> <exe> [args...]
//
// After <seconds> the program is ended and the exit code is 124, as `timeout` has it.
// Arguments are quoted for the C runtime's rules, so each reaches the program as one.
//
// The desktop outlives the run: a windowless second process holds it until no run has used it
// for two minutes. Each desktop made and destroyed leaves handles in dwm.exe and composition
// objects in the kernel that only a restart frees, about 2 and 7 on Windows 10 19044, so a
// desktop per run piled up thousands over a day of rendering scenes and slowed dwm.exe down.
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

namespace {

constexpr wchar_t desktop_name[] = L"crpp-hidden";
// Set by each run; the holder ends once it has not been set for hold_ms.
constexpr wchar_t hold_event_name[] = L"Local\\crpp-hidden-hold";
constexpr DWORD hold_ms = 120'000;
constexpr int timed_out = 124;

// CommandLineToArgvW's rules: backslashes are literal unless they precede a quote.
void append_quoted(std::wstring& line, const wchar_t* arg) {
  line += L'"';
  for (const wchar_t* p = arg;; ++p) {
    std::size_t slashes = 0;
    while (*p == L'\\') {
      ++slashes;
      ++p;
    }
    if (*p == 0) {
      line.append(slashes * 2, L'\\');
      break;
    }
    if (*p == L'"') line.append(slashes * 2 + 1, L'\\');
    else line.append(slashes, L'\\');
    line += *p;
  }
  line += L'"';
}

HANDLE handle_from(const wchar_t* text) { return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::wcstoull(text, nullptr, 10))); }

// The holder: keeps the inherited desktop and event until a wait for the event times out.
int hold(HANDLE desktop, HANDLE event) {
  while (WaitForSingleObject(event, hold_ms) == WAIT_OBJECT_0) {
  }
  CloseHandle(event);
  CloseDesktop(static_cast<HDESK>(desktop));
  return 0;
}

// Starts the holder with only the desktop and the event: an inherited pipe would keep bash
// waiting for its output until the holder ends.
void start_holder(HDESK desktop, HANDLE event) {
  SIZE_T size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
  std::vector<unsigned char> storage(size);
  auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
  if (!InitializeProcThreadAttributeList(attributes, 1, 0, &size)) return;
  HANDLE inherited[] = {desktop, event};
  if (UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof inherited, nullptr, nullptr)) {
    wchar_t self[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, self, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
      std::wstring line = L"crpp_hidden_run --hold " + std::to_wstring(reinterpret_cast<std::uintptr_t>(desktop)) + L' ' +
                          std::to_wstring(reinterpret_cast<std::uintptr_t>(event));
      STARTUPINFOEXW startup{};
      startup.StartupInfo.cb = sizeof startup;
      startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;  // and none of them
      startup.lpAttributeList = attributes;
      PROCESS_INFORMATION process{};
      const DWORD flags = EXTENDED_STARTUPINFO_PRESENT | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
      // Out of the caller's job where it allows that, so a job that ends with the caller does
      // not take the holder along.
      if (CreateProcessW(self, line.data(), nullptr, nullptr, TRUE, flags | CREATE_BREAKAWAY_FROM_JOB, nullptr, nullptr,
                         &startup.StartupInfo, &process) ||
          CreateProcessW(self, line.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr, &startup.StartupInfo, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
      }
    }
  }
  DeleteProcThreadAttributeList(attributes);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc == 4 && std::wcscmp(argv[1], L"--hold") == 0) return hold(handle_from(argv[2]), handle_from(argv[3]));
  if (argc < 3) {
    std::fputs("usage: crpp_hidden_run <seconds> <exe> [args...]\n", stderr);
    return 2;
  }
  const long seconds = std::wcstol(argv[1], nullptr, 10);
  if (seconds <= 0) {
    std::fputs("crpp_hidden_run: <seconds> must be a positive number\n", stderr);
    return 2;
  }
  // Inheritable for the holder only: the program below inherits nothing.
  SECURITY_ATTRIBUTES inheritable{sizeof inheritable, nullptr, TRUE};
  HANDLE hold_event = CreateEventW(&inheritable, FALSE, FALSE, hold_event_name);
  const bool held = hold_event != nullptr && GetLastError() == ERROR_ALREADY_EXISTS;
  HDESK desktop = CreateDesktopW(desktop_name, nullptr, nullptr, 0, GENERIC_ALL, &inheritable);
  if (desktop == nullptr) {
    std::fprintf(stderr, "crpp_hidden_run: CreateDesktop failed (%lu)\n", GetLastError());
    if (hold_event) CloseHandle(hold_event);
    return 2;
  }
  if (hold_event) {
    if (held) SetEvent(hold_event);
    else start_holder(desktop, hold_event);
  }
  std::wstring line;
  for (int i = 2; i < argc; ++i) {
    if (i > 2) line += L' ';
    append_quoted(line, argv[i]);
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof startup;
  std::wstring name = desktop_name;
  startup.lpDesktop = name.data();
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(argv[2], line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
    std::fprintf(stderr, "crpp_hidden_run: cannot start the program (%lu)\n", GetLastError());
    CloseDesktop(desktop);
    if (hold_event) CloseHandle(hold_event);
    return 2;
  }
  DWORD code = 0;
  if (WaitForSingleObject(process.hProcess, static_cast<DWORD>(seconds) * 1000) != WAIT_OBJECT_0) {
    TerminateProcess(process.hProcess, 1);
    WaitForSingleObject(process.hProcess, INFINITE);
    code = timed_out;
  } else {
    GetExitCodeProcess(process.hProcess, &code);
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  if (hold_event) {
    SetEvent(hold_event);  // the two minutes count from the end of this run
    CloseHandle(hold_event);
  }
  CloseDesktop(desktop);
  return static_cast<int>(code);
}
