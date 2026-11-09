#include "crpp/files/tree.hpp"

#include <fstream>
#include <random>

#include "check.hpp"

using crpp::files::Tree;
namespace fs = std::filesystem;

namespace {

void finds_by_path_and_lists_a_folder() {
  Tree tree;
  tree.add("skins/cloud/pack.cskin", "store/aa");
  tree.add("skins/cloud/screens/home.cskin", "store/bb");
  tree.add("skins/retro/pack.cskin", "store/cc");
  tree.add("skins/a-b/pack.cskin", "store/dd");
  tree.add("emoji/1f600.svg", "store/ee");
  CHECK(tree.find("skins/cloud/pack.cskin") && *tree.find("skins/cloud/pack.cskin") == "store/aa");
  CHECK(!tree.find("skins/cloud"));
  CHECK((tree.names_in("skins/") == std::vector<std::string>{"a-b", "cloud", "retro"}));
  CHECK((tree.names_in("skins/cloud/") == std::vector<std::string>{"pack.cskin", "screens"}));
  CHECK(tree.names_in("sounds/").empty());
  const Tree cloud = tree.under("skins/cloud/");
  CHECK(cloud.size() == 2 && cloud.find("screens/home.cskin") && *cloud.find("pack.cskin") == "store/aa");
  // A later one wins.
  Tree other;
  other.add("emoji/1f600.svg", "store/ff");
  tree.merge(other);
  CHECK(*tree.find("emoji/1f600.svg") == "store/ff");
}

void reads_a_folder_and_its_files() {
  const fs::path dir = fs::temp_directory_path() / ("crpp_files_test_" + std::to_string(std::random_device{}()));
  fs::create_directories(dir / "keyboard/plastic");
  std::ofstream(dir / "keyboard/plastic/pack.json", std::ios::binary) << "{}";
  const Tree tree = Tree::of_folder(dir, "sounds/");
  CHECK(tree.size() == 1 && tree.read("sounds/keyboard/plastic/pack.json", 10) == "{}");
  CHECK(!tree.read("sounds/keyboard/plastic/pack.json", 1));  // more than the limit
  CHECK(!tree.read("sounds/none", 10));
  CHECK(Tree::of_folder(dir / "none").empty());
  std::error_code ec;
  fs::remove_all(dir, ec);
}

}  // namespace

int main() {
  finds_by_path_and_lists_a_folder();
  reads_a_folder_and_its_files();
  return crpp_test_result();
}
