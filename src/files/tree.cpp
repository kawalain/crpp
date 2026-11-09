#include "crpp/files/tree.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

namespace crpp::files {

namespace fs = std::filesystem;

std::optional<std::string> ascii(const fs::path& path) {
  std::string out;
  for (const auto c : path.generic_wstring()) {
    if (c <= 0 || c >= 0x80) return std::nullopt;
    out += static_cast<char>(c);
  }
  return out;
}

Tree Tree::of_folder(const fs::path& dir, std::string_view prefix) {
  Tree tree;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    const fs::path relative = fs::relative(it->path(), dir, ec);
    if (ec) continue;
    // A catalog's paths are ASCII, so a file named otherwise is none of them.
    if (const auto name = ascii(relative)) tree.add(std::string(prefix) + *name, it->path());
  }
  return tree;
}

void Tree::add(std::string path, fs::path real) { files_.insert_or_assign(std::move(path), std::move(real)); }

void Tree::merge(const Tree& other) {
  for (const auto& [path, real] : other.files_) files_.insert_or_assign(path, real);
}

const fs::path* Tree::find(std::string_view path) const {
  const auto it = files_.find(path);
  return it == files_.end() ? nullptr : &it->second;
}

Tree Tree::under(std::string_view folder) const {
  Tree tree;
  for (auto it = files_.lower_bound(folder); it != files_.end() && it->first.starts_with(folder); ++it) {
    tree.files_.emplace_hint(tree.files_.end(), it->first.substr(folder.size()), it->second);
  }
  return tree;
}

std::vector<std::string> Tree::names_in(std::string_view folder) const {
  std::vector<std::string> names;
  for (auto it = files_.lower_bound(folder); it != files_.end() && it->first.starts_with(folder); ++it) {
    const std::string_view rest = std::string_view(it->first).substr(folder.size());
    const std::string_view name = rest.substr(0, rest.find('/'));
    if (!name.empty() && (names.empty() || names.back() != name)) names.emplace_back(name);
  }
  // "a-b/x" sorts before "a/x", so a folder's entries are together but its name may not be in order.
  std::ranges::sort(names);
  names.erase(std::ranges::unique(names).begin(), names.end());
  return names;
}

std::optional<std::string> Tree::read(std::string_view path, std::size_t limit) const {
  const fs::path* real = find(path);
  if (!real) return std::nullopt;
  std::error_code ec;
  const auto size = fs::file_size(*real, ec);
  if (ec || size > limit) return std::nullopt;
  std::ifstream in(*real, std::ios::binary);
  if (!in) return std::nullopt;
  return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace crpp::files
