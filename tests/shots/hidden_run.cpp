// Runs a program on a hidden Win32 desktop and passes its exit code on, so that a scripted
// run of the app never reaches the user's desktop and bash can wait for a GUI program:
//
//   crpp_hidden_run <seconds> <exe> [args...]
//
// After <seconds> the program is ended and the exit code is 124, as `timeout` has it. The
// desktop is created on the first call and kept while any process uses it. Arguments are
// quoted for the C runtime's rules, so each reaches the program as one.
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

constexpr wchar_t desktop_name[] = L"crpp-hidden";
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

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) {
    std::fputs("usage: crpp_hidden_run <seconds> <exe> [args...]\n", stderr);
    return 2;
  }
  const long seconds = std::wcstol(argv[1], nullptr, 10);
  if (seconds <= 0) {
    std::fputs("crpp_hidden_run: <seconds> must be a positive number\n", stderr);
    return 2;
  }
  HDESK desktop = CreateDesktopW(desktop_name, nullptr, nullptr, 0, GENERIC_ALL, nullptr);
  if (desktop == nullptr) {
    std::fprintf(stderr, "crpp_hidden_run: CreateDesktop failed (%lu)\n", GetLastError());
    return 2;
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
  CloseDesktop(desktop);
  return static_cast<int>(code);
}
