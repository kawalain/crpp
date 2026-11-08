#include "crpp/assets/catalog.hpp"

#include <string>

#include "check.hpp"

using namespace crpp::assets;

namespace {

const std::string key_line = "key " + std::string(128, 'a') + "\n";
const std::string head = "crpp-catalog 1\n" + key_line;
const std::string hash = std::string(64, 'b');

std::string file_line(std::string_view group, std::string_view path, std::string_view tail = "") {
  return "file " + std::string(group) + " " + std::string(path) + " 12 " + hash + std::string(tail) + "\n";
}

const std::string good = head +
                         "source twemoji https://raw.githubusercontent.com/jdecked/twemoji/abc/assets/svg/\n"
                         "skin cloud 1\n"
                         "skin retro 1 cloud\n"
                         "default-skin cloud\n" +
                         file_line("shared", "emoji/1f600.svg", " from twemoji 1f600.svg") +
                         file_line("shared", "emoji/1f507.svg", " from twemoji 1f507.svg nopick") +
                         file_line("shared", "emoji/01.svg") + file_line("shared", "emoji/1f400.svg") +
                         file_line("shared", "sounds/keyboard/a/down.wav") + file_line("skin:cloud", "skins/cloud/pack.jsonc") +
                         file_line("skin:retro", "skins/retro/pack.jsonc") + file_line("sound:soft", "sounds/keyboard/soft/x.wav");

void reads_every_kind_of_line() {
  const auto catalog = parse_catalog(good);
  CHECK(catalog.has_value());
  if (!catalog) return;
  CHECK(catalog->key[0] == 0xaa && catalog->key[63] == 0xaa);
  CHECK(catalog->sources.size() == 1 && catalog->sources[0].name == "twemoji");
  CHECK(catalog->skins.size() == 2 && catalog->skins[1].parent == "cloud" && catalog->skins[1].format == 1);
  CHECK(catalog->default_skin == "cloud");
  CHECK(catalog->files.size() == 8);
  CHECK(catalog->files[0].source == "twemoji" && catalog->files[0].remote == "1f600.svg" && catalog->files[0].pick);
  CHECK(!catalog->files[1].pick);
  CHECK(catalog->files[2].source.empty() && catalog->files[2].size == 12 && catalog->files[2].sha256 == hash);
  CHECK(!catalog->signature);
}

void writes_what_it_reads() {
  const auto catalog = parse_catalog(good);
  CHECK(catalog && write_catalog(*catalog) == good);
}

void a_signature_covers_what_comes_before_it() {
  const std::string signed_text = good + "signature " + std::string(128, 'c') + "\n";
  const auto catalog = parse_catalog(signed_text);
  CHECK(catalog && catalog->signature && catalog->signed_bytes == good.size() && (*catalog->signature)[0] == 0xcc);
  CHECK(!parse_catalog(signed_text + file_line("shared", "a.txt")));
}

void refuses(const std::string& text, std::size_t line) {
  const auto catalog = parse_catalog(text);
  CHECK(!catalog);
  if (!catalog) CHECK(catalog.error().line == line);
}

void refuses_what_is_not_a_catalog() {
  refuses("", 0);
  refuses("crpp-catalog 1", 0);  // no line break at the end
  refuses("crpp-catalog 2\n" + key_line, 1);
  refuses("crpp-catalog 1\n", 0);
  refuses("crpp-catalog 1\nkey abc\n", 2);
  refuses("crpp-catalog 1\r\n" + key_line, 1);
  refuses(head + "source x https://a/\r\n", 3);
  refuses(head + "nonsense\n", 3);
  refuses(head + "file shared a.txt 12 " + hash + "  \n", 3);  // a double space is an empty field
  refuses(std::string(max_catalog_size + 1, '\n'), 0);
}

void refuses_paths_that_leave_the_folder() {
  for (const char* path : {"../a", "a/../b", "/a", "a//b", "a/./b", "a\\b", "a b", "%2e%2e/a", "a/", "."}) {
    CHECK(!parse_catalog(head + file_line("shared", path)));
  }
  CHECK(parse_catalog(head + file_line("shared", "a/b.c-d_E")).has_value());
}

void refuses_unsure_fields() {
  refuses(head + "file shared a 012 " + hash + "\n", 3);
  refuses(head + "file shared a -1 " + hash + "\n", 3);
  refuses(head + "file shared a 99999999999 " + hash + "\n", 3);
  refuses(head + "file shared a 12 " + std::string(64, 'B') + "\n", 3);
  refuses(head + "file other a 12 " + hash + "\n", 3);
  refuses(head + "file skin:Cloud a 12 " + hash + "\n", 3);
  refuses(head + file_line("shared", "a", " from nowhere a"), 3);
  refuses(head + "source a https://x/\n" + file_line("shared", "a", " from a"), 4);
  refuses(head + file_line("shared", "a", " nopick extra"), 3);
  refuses(head + file_line("shared", "a") + file_line("sound:x", "a"), 4);
  refuses(head + "source a http://example.com/\n", 3);  // plain HTTP only to this machine
  refuses(head + "source a https://example.com\n", 3);
  refuses(head + "source a https://x/\nsource a https://y/\n", 4);
  refuses(head + "skin a 0\n", 3);
  refuses(head + "skin a 1 a\n", 3);
  refuses(head + "skin a 1\nskin a 1\n", 4);
  refuses(head + "default-skin a\ndefault-skin a\n", 4);
  CHECK(parse_catalog(head + "source a http://127.0.0.1:8080/x/\n").has_value());
}

void checks_what_lines_refer_to() {
  refuses(head + "skin a 1 b\n", 0);
  refuses(head + "skin a 1 b\nskin b 1 a\n", 0);
  refuses(head + "default-skin a\n", 0);
  refuses(head + file_line("skin:a", "a"), 0);
}

void follows_a_skin_to_its_parents() {
  const auto catalog = parse_catalog(good);
  CHECK((catalog && skin_chain(*catalog, "retro") == std::vector<std::string>{"retro", "cloud"}));
  CHECK((catalog && skin_chain(*catalog, "cloud") == std::vector<std::string>{"cloud"}));
  CHECK(catalog && skin_chain(*catalog, "none").empty());
}

void lists_the_picker_and_the_sounds() {
  const auto catalog = parse_catalog(good);
  CHECK((catalog && picker_emoji(*catalog) == std::vector<std::string>{"1f400", "1f600", "01"}));
  CHECK((catalog && sound_files(*catalog) == std::vector<std::string>{"keyboard/a/down.wav", "keyboard/soft/x.wav"}));
}

}  // namespace

int main() {
  reads_every_kind_of_line();
  writes_what_it_reads();
  a_signature_covers_what_comes_before_it();
  refuses_what_is_not_a_catalog();
  refuses_paths_that_leave_the_folder();
  refuses_unsure_fields();
  checks_what_lines_refer_to();
  follows_a_skin_to_its_parents();
  lists_the_picker_and_the_sounds();
  return crpp_test_result();
}
