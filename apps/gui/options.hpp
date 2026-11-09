#pragma once
// The command line, read in any order. Free of the window, so the tests can run it.
//
// An option that takes a value may stand anywhere: `--data <dir>` first is the same as last. What
// is left, in the order written, says what to do: `--shot <png> [size] [hour] [scene] [--at <ms>]
// [notice]`, `--smoke <png>`, `--minimized`, or nothing for the app itself. A word that looks like
// an option and is none is an error, never ignored: a misspelt `--data` would otherwise open the
// real app on the user's desktop and data.
#include <algorithm>
#include <array>
#include <expected>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace crpp::gui {

struct Options {
  // By name without the dashes: data, update-url, updated-from, asset-url, skin-dir, locale, theme, dpi.
  std::map<std::string, std::string, std::less<>> named;
  // The program's name, then what says what to do, as main and run_shot index it.
  std::vector<std::string> words;

  [[nodiscard]] std::string_view value(std::string_view name) const {
    const auto found = named.find(name);
    return found == named.end() ? std::string_view{} : std::string_view(found->second);
  }
  [[nodiscard]] std::string_view word(std::size_t index) const { return index < words.size() ? std::string_view(words[index]) : std::string_view{}; }
};

inline constexpr std::array<std::string_view, 8> valued_options{"--data", "--update-url", "--updated-from", "--asset-url", "--skin-dir", "--locale", "--theme", "--dpi"};
// Words with dashes that are not options with a value: they say what to do, where they stand.
inline constexpr std::array<std::string_view, 4> command_words{"--shot", "--smoke", "--minimized", "--at"};

// `args` as main gets them, the program's name first. The error names what was wrong.
[[nodiscard]] inline std::expected<Options, std::string> read_options(std::span<const char* const> args) {
  Options options;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view word = args[i];
    if (i == 0 || !word.starts_with("--")) {
      options.words.emplace_back(word);
      continue;
    }
    if (std::ranges::find(valued_options, word) != valued_options.end()) {
      if (i + 1 >= args.size()) return std::unexpected(std::string(word) + " needs a value");
      if (!options.named.emplace(std::string(word.substr(2)), args[i + 1]).second) return std::unexpected(std::string(word) + " is given twice");
      ++i;
      continue;
    }
    if (std::ranges::find(command_words, word) == command_words.end()) return std::unexpected("no option " + std::string(word));
    options.words.emplace_back(word);
  }
  return options;
}

}  // namespace crpp::gui
