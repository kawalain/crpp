// Where the downloaded files are, by the paths the catalogs give them (files::Tree).
#include "shell.hpp"

namespace crpp::gui {

files::Tree official_files(const Shell& s) {
  if (!s.assets_beside_exe) return assets::installed_tree(s.assets_dir, s.files_dir);
  // Beside a development exe: its folders as they are, the packs in one of their own.
  files::Tree tree = files::Tree::of_folder(s.assets_dir);
  tree.merge(files::Tree::of_folder(s.skins_dir, "skins/"));
  return tree;
}

files::Tree added_files(const Shell& s, const AddedRepository& added) { return assets::installed_tree(added.dir, s.files_dir); }

files::Tree official_sounds(const Shell& s) {
  files::Tree sounds = official_files(s).under("sounds/");
  if (!s.catalog) return sounds;
  // Where each will be once it is here.
  for (const assets::File& file : s.catalog->files) {
    if (!file.path.starts_with("sounds/")) continue;
    const std::string path = file.path.substr(7);
    if (sounds.find(path)) continue;
    sounds.add(path, s.assets_beside_exe ? s.assets_dir / std::filesystem::path(file.path) : s.files_dir / file.sha256);
  }
  return sounds;
}

}  // namespace crpp::gui
