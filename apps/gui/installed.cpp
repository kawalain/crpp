// Where the downloaded files are, by the paths the catalogs give them (files::Tree).
#include "shell.hpp"

namespace crpp::gui {

files::Tree official_files(const Shell& s) {
  files::Tree tree = files::Tree::of_folder(s.assets_dir);
  // Beside a development exe the packs are a folder of their own.
  if (s.skins_dir != s.assets_dir / "skins") tree.merge(files::Tree::of_folder(s.skins_dir, "skins/"));
  return tree;
}

files::Tree added_files(const AddedRepository& added) { return files::Tree::of_folder(added.dir); }

files::Tree official_sounds(const Shell& s) {
  files::Tree sounds = official_files(s).under("sounds/");
  for (const std::string& path : s.expected_sounds) {
    if (!sounds.find(path)) sounds.add(path, s.assets_dir / "sounds" / std::filesystem::path(path));
  }
  return sounds;
}

}  // namespace crpp::gui
