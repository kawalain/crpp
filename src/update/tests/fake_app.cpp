// Stands in for the app in the replacement test. With `--updated-from <pid>` it says it
// came up, unless told not to, and stays a moment.
#include <windows.h>

#include <cstdlib>
#include <string_view>

#include "crpp/update/windows_platform.hpp"

int main(int argc, char** argv) {
  bool silent = false;
  unsigned long parent = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--crash") return 3;
    if (arg == "--silent") silent = true;
    if (arg == "--updated-from" && i + 1 < argc) parent = std::strtoul(argv[++i], nullptr, 10);
  }
  if (parent != 0 && !silent) crpp::update::report_started(parent);
  Sleep(silent ? 10000 : 1000);
  return 0;
}
