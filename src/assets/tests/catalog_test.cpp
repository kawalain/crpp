#include "crpp/assets/catalog.hpp"

#include <string>

#include "check.hpp"

using namespace crpp::assets;

namespace {

const std::string key_line = "key " + std::string(128, 'a') + "\n";
const std::string head = "crpp-catalog 2\n" + key_line;
const std::string hash = std::string(64, 'b');

std::string file_line(std::string_view group, std::string_view path, std::string_view tail = "") {
  return "file " + std::string(group) + " " + std::string(path) + " 12 " + hash + std::string(tail) + "\n";
}

const std::string good = head +
                         "mirror https://mirror.test/s/\n"
                         "source twemoji https://raw.githubusercontent.com/jdecked/twemoji/abc/assets/svg/\n"
                         "skin cloud 1\n"
                         "skin-name cloud ko 구름\n"
                         "skin-name cloud en Cloud\n"
                         "theme cloud cumulus dark\n"
                         "theme-name cloud cumulus ko 뭉게구름\n"
                         "theme-name cloud cumulus en Cumulus\n"
                         "theme cloud pastel\n"
                         "skin retro 1 cloud\n"
                         "theme retro luna_blue dark\n"
                         "theme-name retro luna_blue en Luna  Blue\n"
                         "default-skin cloud\n" +
                         file_line("shared", "emoji/1f600.svg", " from twemoji 1f600.svg") +
                         file_line("shared", "emoji/1f507.svg", " from twemoji 1f507.svg nopick") +
                         file_line("shared", "emoji/01.svg") + file_line("shared", "emoji/1f400.svg") +
                         file_line("shared", "sounds/keyboard/a/down.wav") + file_line("skin:cloud", "skins/cloud/pack.cskin") +
                         file_line("skin:retro", "skins/retro/pack.cskin") + file_line("sound:soft", "sounds/keyboard/soft/x.wav");

void reads_every_kind_of_line() {
  const auto catalog = parse_catalog(good);
  CHECK(catalog.has_value());
  if (!catalog) return;
  CHECK(catalog->key[0] == 0xaa && catalog->key[63] == 0xaa);
  CHECK(catalog->grammar == 2 && catalog->mirror == "https://mirror.test/s/");
  CHECK(catalog->sources.size() == 1 && catalog->sources[0].name == "twemoji");
  CHECK(catalog->skins.size() == 2 && catalog->skins[1].parent == "cloud" && catalog->skins[1].format == 1);
  CHECK(catalog->default_skin == "cloud");
  const SkinPack& cloud = catalog->skins[0];
  CHECK(cloud.names.size() == 2 && cloud.names[0].first == "ko" && cloud.names[0].second == "구름" && cloud.names[1].second == "Cloud");
  CHECK(cloud.themes.size() == 2 && cloud.themes[0].dark && !cloud.themes[1].dark && cloud.themes[1].names.empty());
  CHECK(cloud.theme("cumulus") && cloud.theme("cumulus")->names[1].second == "Cumulus" && !cloud.theme("nothing"));
  CHECK(catalog->skins[1].theme("luna_blue")->names[0].second == "Luna  Blue");  // words keep their spaces
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
  refuses("crpp-catalog 3\n" + key_line, 1);
  CHECK(parse_catalog("crpp-catalog 1\n" + key_line).has_value());  // as an older exe kept it
  refuses(head + "mirror http://a/\n", 3);
  refuses(head + "mirror https://a/\nmirror https://b/\n", 4);
  refuses("crpp-catalog 1\n", 0);
  refuses("crpp-catalog 2\nkey abc\n", 2);
  refuses("crpp-catalog 1\r\n" + key_line, 1);
  refuses(head + "source x https://a/\r\n", 3);
  refuses(head + "file shared a.txt 12 " + hash + "  \n", 3);  // a double space is an empty field
  refuses(std::string(max_catalog_size + 1, '\n'), 0);
}

void skips_lines_of_a_later_kind() {
  const auto catalog = parse_catalog(head + "nonsense\nskin a 1\nsound-volume x 3\n");
  CHECK(catalog && catalog->skins.size() == 1);
}

void refuses_wrong_names_and_themes() {
  const std::string a = head + "skin a 1\n";
  refuses(head + "skin-name a ko A\n", 3);  // a skin listed further on
  refuses(a + "skin-name a ko\n", 4);
  refuses(a + "skin-name a KO A\n", 4);
  refuses(a + "skin-name a ko  A\n", 4);
  refuses(a + "skin-name a ko A \n", 4);
  refuses(a + "skin-name a ko A\tB\n", 4);
  refuses(a + "skin-name a ko " + std::string(65, 'x') + "\n", 4);
  refuses(a + "skin-name a ko A\nskin-name a ko B\n", 5);
  refuses(a + "theme a\n", 4);
  refuses(a + "theme a b light\n", 4);
  refuses(a + "theme a b dark x\n", 4);
  refuses(a + "theme a B\n", 4);
  refuses(a + "theme a b\ntheme a b\n", 5);
  refuses(a + "theme-name a b en B\n", 4);  // a theme not listed
  refuses(a + "theme a b\ntheme-name a b en\n", 5);
  CHECK(parse_catalog(a + "skin-name a ko " + std::string(64, 'x') + "\n").has_value());
}

// A name is read in the question about trusting a Repository: the marks that turn the direction
// of text could make it say something else than was written.
void a_name_holds_nothing_that_turns_text() {
  const std::string a = head + "skin a 1\n";
  CHECK(!parse_catalog(a + "skin-name a en safe\xE2\x80\xAE" "txt.exe\n"));
  CHECK(!parse_catalog(a + "skin-name a en \xE2\x81\xA6x\n"));
  CHECK(parse_catalog(a + "skin-name a en \xE2\x80\x94 dash\n").has_value());
}

// Each pack is looked up among those before it, so a catalog may list only so many.
void lists_only_so_many_packs() {
  std::string skins = head, sounds = head;
  for (std::size_t i = 0; i < max_packs; ++i) {
    skins += "skin s" + std::to_string(i) + " 1\n";
    sounds += "sound s" + std::to_string(i) + "\n";
  }
  CHECK(parse_catalog(skins).has_value());
  CHECK(parse_catalog(sounds).has_value());
  CHECK(!parse_catalog(skins + "skin one-more 1\n"));
  CHECK(!parse_catalog(sounds + "sound one-more\n"));
}

void refuses_paths_that_leave_the_folder() {
  for (const char* path : {"../a", "a/../b", "/a", "a//b", "a/./b", "a\\b", "a b", "%2e%2e/a", "a/", "."}) {
    CHECK(!parse_catalog(head + file_line("shared", path)));
  }
  CHECK(parse_catalog(head + file_line("shared", "a/b.c-d_E")).has_value());
  // Windows opens a device for these names, with any extension, in any folder.
  for (const char* path : {"skins/x/prn", "con", "a/CON.txt", "aux.svg", "a/nul", "com1.wav", "a/LPT9", "com0"}) {
    CHECK(!parse_catalog(head + file_line("shared", path)));
  }
  for (const char* path : {"console", "a/prn2", "com", "lpt10", "auxiliary.txt", "a/nulls.wav"}) {
    CHECK(parse_catalog(head + file_line("shared", path)).has_value());
  }
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

void an_address_names_a_page() {
  CHECK(page_of("github:someone/my.packs") == "https://github.com/someone/my.packs");
  CHECK(page_of("github:someone/packs@feature/new") == "https://github.com/someone/packs/tree/feature/new");
  CHECK(page_of("https://raw.githubusercontent.com/kawalain/crpp-repository/main/") == "https://github.com/kawalain/crpp-repository/tree/main");
  CHECK(page_of("https://raw.githubusercontent.com/someone/packs/main/sub/catalog.txt") == "https://github.com/someone/packs/tree/main/sub");
  CHECK(page_of("https://packs.example.org/a/catalog.txt") == "https://packs.example.org/a/");
  CHECK(page_of("github:someone").empty());
  CHECK(page_of("javascript:alert(1)").empty());
}

void an_address_names_a_folder() {
  CHECK(folder_of("https://packs.example.org/cloudroom") == "https://packs.example.org/cloudroom/");
  CHECK(folder_of("  https://packs.example.org/a/catalog.txt ") == "https://packs.example.org/a/");
  CHECK(folder_of("http://127.0.0.1:8765/") == "http://127.0.0.1:8765/");
  CHECK(folder_of("http://packs.example.org/").empty());  // plain HTTP only to this machine
  // What comes before an @ is a user name to WinHTTP, and the host is what follows it.
  CHECK(folder_of("http://localhost:1@packs.example.org/").empty());
  CHECK(folder_of("http://127.0.0.1/@packs.example.org/") == "http://127.0.0.1/@packs.example.org/");
  CHECK(folder_of("https://someone@packs.example.org/").empty());
  CHECK(folder_of("github:someone/my.packs") == "https://raw.githubusercontent.com/someone/my.packs/HEAD/");
  CHECK(folder_of("github:someone/packs@v0.2.0") == "https://raw.githubusercontent.com/someone/packs/v0.2.0/");
  CHECK(folder_of("github:someone/packs@feature/new") == "https://raw.githubusercontent.com/someone/packs/feature/new/");
  CHECK(folder_of("github:someone/packs@../x").empty());
  CHECK(folder_of("github:someone/..").empty());
  CHECK(folder_of("github:some_one/packs").empty());
  CHECK(folder_of("github:someone").empty());
  CHECK(folder_of("packs.example.org").empty());
  CHECK(folder_of("").empty());
}

void another_repository_offers_only_packs() {
  Catalog catalog;
  catalog.files = {{.group = "skin:aurora", .path = "skins/aurora/pack.cskin"}, {.group = "sound:soft", .path = "sounds/keyboard/soft/a.wav"},
                   {.group = "sound:soft", .path = "sounds/mouse/soft/left.wav"}};
  CHECK(offers_only_packs(catalog));
  CHECK((sound_kinds(catalog, "soft") == std::vector<std::string>{"keyboard", "mouse"}));
  catalog.files.push_back({.group = "sound:soft", .path = "sounds/soft/a.wav"});  // no kind
  CHECK(!offers_only_packs(catalog));
  catalog.files.back() = {.group = "sound:soft", .path = "sounds/drum/soft/a.wav"};
  CHECK(!offers_only_packs(catalog));
  catalog.files.push_back({.group = "skin:aurora", .path = "skins/other/pack.cskin"});
  CHECK(!offers_only_packs(catalog));
  catalog.files.back() = {.group = "shared", .path = "emoji/a.svg"};
  CHECK(!offers_only_packs(catalog));
  catalog.files.back() = {.group = "sound:soft", .path = "sounds/keyboard/softer/a.wav"};
  CHECK(!offers_only_packs(catalog));
}

// A pack's version, and the pack it extends in another catalog or pinned in this one.
void reads_versions_and_parents() {
  const std::string fp(64, 'c');
  const std::string text = head + "skin base 1\nskin-version base 0.1.0\nskin child 1 base\nparent child base version 0.1 sha256 " + hash +
                           "\nskin far 1\nskin-version far 0.12.3\nparent far retro fingerprint " + fp + " repository github:someone/skins\n" +
                           file_line("skin:base", "skins/base/pack.cskin");
  const auto catalog = parse_catalog(text);
  CHECK(catalog.has_value());
  if (!catalog) return;
  CHECK(catalog->skins[0].version == "0.1.0" && !catalog->skins[0].pin);
  const SkinPack& child = catalog->skins[1];
  CHECK(child.parent == "base" && child.version.empty() && child.pin && child.pin->id == "base" && child.pin->version == "0.1" && child.pin->sha256 == hash);
  const SkinPack& far = catalog->skins[2];
  CHECK(far.parent.empty() && far.pin && far.pin->id == "retro" && far.pin->fingerprint == fp && far.pin->repository == "github:someone/skins");
  CHECK((skin_chain(*catalog, "far") == std::vector<std::string>{"far"}));  // the rest is in another catalog
  CHECK(write_catalog(*catalog) == text);

  const std::string skins = head + "skin a 1\nskin b 1 a\n";
  refuses(skins + "skin-version a 1.0.0\n", 5);
  refuses(skins + "skin-version a 0.1\n", 5);
  refuses(skins + "skin-version a 0.1.0\nskin-version a 0.1.1\n", 6);
  refuses(skins + "skin-version c 0.1.0\n", 5);
  refuses(skins + "parent a a\n", 5);
  refuses(skins + "parent b c\n", 5);  // the skin line names another
  refuses(skins + "parent b a fingerprint " + fp + "\n", 5);  // one here has no other key
  refuses(skins + "parent a c fingerprint ABC\n", 5);
  refuses(skins + "parent a c repository github:a/b\n", 5);
  refuses(skins + "parent a c version 1.0\n", 5);
  refuses(skins + "parent a c version\n", 5);
  refuses(skins + "parent a c colour red\n", 5);
  refuses(skins + "parent a c version 0.1 version 0.2\n", 5);
  refuses(skins + "parent a c\nparent a c\n", 6);
}

void reads_sound_packs() {
  const std::string text = head + "sound soft\nsound-name soft ko 부드러운 소리\nsound-name soft en Soft\nsound plain\n" +
                           file_line("sound:soft", "sounds/keyboard/soft/a.wav");
  const auto catalog = parse_catalog(text);
  CHECK(catalog && catalog->sounds.size() == 2 && catalog->sound("soft") && !catalog->sound("none"));
  CHECK(catalog && catalog->sound("soft")->names.size() == 2 && catalog->sound("soft")->names[0].second == "부드러운 소리");
  CHECK(catalog && write_catalog(*catalog) == text);
  refuses(head + "sound soft\nsound soft\n", 4);
  refuses(head + "sound Soft\n", 3);
  refuses(head + "sound-name soft ko 소리\n", 3);
  refuses(head + "sound soft\nsound-name soft ko\n", 4);
}

void a_version_holds_to_a_pin() {
  CHECK(version_matches("", "") && version_matches("", "0.1.0"));
  CHECK(version_matches("0.3", "0.3.0") && version_matches("0.3", "0.3.12") && version_matches("0.3.2", "0.3.2"));
  CHECK(!version_matches("0.3", "0.30.1") && !version_matches("0.3", "0.4.0") && !version_matches("0.3.2", "0.3.20"));
  CHECK(!version_matches("0.3", "") && !version_matches("0.3.2", "0.3"));
}

// The digest covers the pack's own files only, in an order that does not depend on the catalog's.
void a_digest_lists_the_packs_own_files() {
  Catalog catalog;
  catalog.files = {{.group = "skin:a", .path = "skins/a/z.cskin", .size = 2, .sha256 = std::string(64, '2')},
                   {.group = "shared", .path = "emoji/a.svg", .size = 9, .sha256 = hash},
                   {.group = "skin:a", .path = "skins/a/Pack.cskin", .size = 1, .sha256 = std::string(64, '1')},
                   {.group = "skin:ab", .path = "skins/ab/pack.cskin", .size = 3, .sha256 = hash}};
  CHECK(digest_text(catalog, "a") == "skins/a/Pack.cskin 1 " + std::string(64, '1') + "\nskins/a/z.cskin 2 " + std::string(64, '2') + "\n");
  std::swap(catalog.files[0], catalog.files[2]);
  CHECK(digest_text(catalog, "a") == "skins/a/Pack.cskin 1 " + std::string(64, '1') + "\nskins/a/z.cskin 2 " + std::string(64, '2') + "\n");
  CHECK(digest_text(catalog, "none").empty());
}

}  // namespace

int main() {
  a_name_holds_nothing_that_turns_text();
  lists_only_so_many_packs();
  reads_every_kind_of_line();
  writes_what_it_reads();
  a_signature_covers_what_comes_before_it();
  refuses_what_is_not_a_catalog();
  skips_lines_of_a_later_kind();
  refuses_wrong_names_and_themes();
  refuses_paths_that_leave_the_folder();
  refuses_unsure_fields();
  checks_what_lines_refer_to();
  follows_a_skin_to_its_parents();
  lists_the_picker_and_the_sounds();
  an_address_names_a_folder();
  an_address_names_a_page();
  another_repository_offers_only_packs();
  reads_versions_and_parents();
  a_version_holds_to_a_pin();
  reads_sound_packs();
  a_digest_lists_the_packs_own_files();
  return crpp_test_result();
}
