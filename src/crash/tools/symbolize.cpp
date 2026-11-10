// crpp_symbolize: a Crash report with each frame of crpp.exe named as a function and a source line,
// from the PDB its Release published beside the exe.
//   crpp_symbolize <report.txt> <crpp.pdb> [<other.pdb> ...]
// A PDB serves the module of the report that names it by file name, and only when its GUID and age
// are the report's: a PDB of another build would name the wrong lines. Prints the report with the
// names after each frame, and says why a module could not be read.
#include <windows.h>

#include <dbghelp.h>
#include <fcntl.h>
#include <io.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct ModuleLine {
  std::string pdb;
  std::string key;  // GUID and age, as the report writes them
  std::uint32_t size = 0;
};

struct Loaded {
  DWORD64 base = 0;
};

std::vector<std::string_view> lines_of(std::string_view text) {
  std::vector<std::string_view> lines;
  while (!text.empty()) {
    const std::size_t end = text.find('\n');
    lines.push_back(text.substr(0, end));
    if (end == std::string_view::npos) break;
    text.remove_prefix(end + 1);
  }
  return lines;
}

std::vector<std::string_view> words_of(std::string_view line) {
  std::vector<std::string_view> words;
  for (std::size_t at = 0; at < line.size();) {
    while (at < line.size() && line[at] == ' ') ++at;
    const std::size_t end = std::min(line.find(' ', at), line.size());
    if (end > at) words.push_back(line.substr(at, end - at));
    at = end;
  }
  return words;
}

template <class T>
std::optional<T> hex(std::string_view text) {
  if (text.starts_with("0x")) text.remove_prefix(2);
  T value{};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
  if (error != std::errc{} || end != text.data() + text.size() || text.empty()) return std::nullopt;
  return value;
}

std::string key_of(const IMAGEHLP_MODULEW64& info) {
  char text[64];
  const GUID& g = info.PdbSig70;
  std::snprintf(text, sizeof text, "%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%lX", g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1],
                g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7], info.PdbAge);
  return text;
}

// The source as the repository names it, not as the machine that built it did.
std::string short_path(std::string path) {
  for (const char* top : {"\\src\\", "\\apps\\", "\\tests\\", "\\include\\", "/src/", "/apps/", "/tests/", "/include/"}) {
    if (const std::size_t at = path.find(top); at != std::string::npos) return path.substr(at + 1);
  }
  return path;
}

std::string narrow(std::wstring_view text) {
  std::string out(text.size() * 3, '\0');
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), static_cast<int>(out.size()), nullptr, nullptr);
  out.resize(size > 0 ? static_cast<std::size_t>(size) : 0);
  return out;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  // The report's own line ends, not the console's.
  (void)_setmode(_fileno(stdout), _O_BINARY);
  if (argc < 3) {
    std::fprintf(stderr, "usage: crpp_symbolize <report.txt> <crpp.pdb> [<other.pdb> ...]\n");
    return 2;
  }
  std::string report;
  {
    std::ifstream in(std::filesystem::path(argv[1]), std::ios::binary);
    if (!in) {
      std::fprintf(stderr, "cannot read %ls\n", argv[1]);
      return 1;
    }
    report.assign(std::istreambuf_iterator<char>(in), {});
  }
  const std::vector<std::string_view> lines = lines_of(report);

  // The modules the report lists: "<name>  <stamp> <size>  <pdb> <key>".
  std::map<std::string, ModuleLine, std::less<>> modules;
  bool in_modules = false;
  for (const std::string_view line : lines) {
    if (!line.starts_with("  ")) in_modules = line == "modules:";
    if (!in_modules || !line.starts_with("  ")) continue;
    const auto words = words_of(line);
    if (words.size() < 5) continue;
    modules[std::string(words[0])] = {std::string(words[3]), std::string(words[4]), hex<std::uint32_t>(words[2]).value_or(0)};
  }

  // Any value will do as the session's handle, as long as it is not a process of ours.
  const HANDLE session = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x5c4a5c4a));
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
  if (!SymInitializeW(session, nullptr, FALSE)) {
    std::fprintf(stderr, "dbghelp did not start: %lu\n", GetLastError());
    return 1;
  }
  std::map<std::string, Loaded, std::less<>> loaded;
  DWORD64 next_base = 0x10000000;
  int status = 0;
  for (int i = 2; i < argc; ++i) {
    const std::filesystem::path pdb = argv[i];
    const std::string file = narrow(pdb.filename().wstring());
    const auto module = std::ranges::find_if(modules, [&](const auto& entry) { return _stricmp(entry.second.pdb.c_str(), file.c_str()) == 0; });
    if (module == modules.end()) {
      std::fprintf(stderr, "%s: the report names no module with this PDB\n", file.c_str());
      status = 1;
      continue;
    }
    const DWORD size = module->second.size != 0 ? module->second.size : 0x10000000;
    const DWORD64 base = SymLoadModuleExW(session, nullptr, pdb.c_str(), nullptr, next_base, size, nullptr, 0);
    if (base == 0) {
      std::fprintf(stderr, "%s: dbghelp could not load it: %lu\n", file.c_str(), GetLastError());
      status = 1;
      continue;
    }
    IMAGEHLP_MODULEW64 info{sizeof info};
    if (!SymGetModuleInfoW64(session, base, &info) || key_of(info) != module->second.key) {
      std::fprintf(stderr, "%s: not the PDB of this build (%s, the report wants %s)\n", file.c_str(), key_of(info).c_str(), module->second.key.c_str());
      SymUnloadModule64(session, base);
      status = 1;
      continue;
    }
    loaded[module->first] = {base};
    next_base += (static_cast<DWORD64>(size) + 0xffff) & ~DWORD64{0xffff};
  }

  // Each frame "<n>  <module>+0x<offset>[  (and ...)]", named where its module was loaded.
  bool in_stack = false;
  for (const std::string_view line : lines) {
    std::string out(line);
    if (!line.starts_with("  ")) in_stack = line == "stack:";
    if (in_stack && line.starts_with("  ")) {
      const auto words = words_of(line);
      const std::size_t plus = words.size() >= 2 ? words[1].rfind("+0x") : std::string_view::npos;
      if (plus != std::string_view::npos) {
        const auto found = loaded.find(words[1].substr(0, plus));
        const auto offset = hex<std::uint64_t>(words[1].substr(plus + 1));
        if (found != loaded.end() && offset) {
          // A return address is the instruction after the call: its line is the call's one byte before.
          const bool first = words[0] == "0";
          const DWORD64 address = found->second.base + *offset;
          alignas(SYMBOL_INFOW) unsigned char buffer[sizeof(SYMBOL_INFOW) + 512 * sizeof(wchar_t)]{};
          auto* symbol = reinterpret_cast<SYMBOL_INFOW*>(buffer);
          symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
          symbol->MaxNameLen = 512;
          DWORD64 displacement = 0;
          if (SymFromAddrW(session, first ? address : address - 1, &displacement, symbol)) {
            char text[32];
            std::snprintf(text, sizeof text, "+0x%llx", static_cast<unsigned long long>(first ? displacement : displacement + 1));
            out += "  " + narrow(symbol->Name) + text;
          }
          IMAGEHLP_LINEW64 where{sizeof where};
          DWORD column = 0;
          if (SymGetLineFromAddrW64(session, first ? address : address - 1, &column, &where)) {
            out += "  " + short_path(narrow(where.FileName)) + ":" + std::to_string(where.LineNumber);
          }
        }
      }
    }
    std::printf("%s\n", out.c_str());
  }
  SymCleanup(session);
  return status;
}
