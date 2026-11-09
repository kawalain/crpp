#pragma once
// What the program calls a downloaded file, its path in a catalog such as
// "skins/cloud/pack.cskin", and where that file really is. Installed files are kept under their
// hashes (ADR 0010), and a development build reads its source folders, so whatever reads a pack,
// a sound or an emoji asks a Tree rather than a folder. A Tree is a plain value: copying it to
// another thread shares nothing.
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace crpp::files {

class Tree {
 public:
  // Every regular file under `dir`, at its path relative to it with '/' between names, after
  // `prefix`. A folder that is not there adds nothing.
  [[nodiscard]] static Tree of_folder(const std::filesystem::path& dir, std::string_view prefix = {});

  // A later add of the same path replaces the earlier one.
  void add(std::string path, std::filesystem::path real);
  void merge(const Tree& other);

  [[nodiscard]] bool empty() const noexcept { return files_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return files_.size(); }
  [[nodiscard]] const std::filesystem::path* find(std::string_view path) const;
  // The entries under a folder ("skins/cloud/"), without it in front.
  [[nodiscard]] Tree under(std::string_view folder) const;
  // The names right under a folder, files and folders alike, each once and sorted.
  [[nodiscard]] std::vector<std::string> names_in(std::string_view folder) const;
  // Its bytes, if it is there and at most `limit` long.
  [[nodiscard]] std::optional<std::string> read(std::string_view path, std::size_t limit) const;

  // Every path, sorted.
  [[nodiscard]] const std::map<std::string, std::filesystem::path, std::less<>>& entries() const noexcept { return files_; }

 private:
  std::map<std::string, std::filesystem::path, std::less<>> files_;
};

}  // namespace crpp::files
