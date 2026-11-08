#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "check.hpp"
#include "crpp/locale/locale.hpp"

using namespace crpp::locale;

namespace {

std::set<char> placeholders(std::string_view text) {
  std::set<char> found;
  for (std::size_t i = 0; i + 2 < text.size(); ++i) {
    if (text[i] == '{' && text[i + 2] == '}') found.insert(text[i + 1]);
  }
  return found;
}

void test_every_locale_has_every_text() {
  for (const Locale& locale : all_locales()) {
    const auto texts = strings_as_array(*locale.strings);
    for (std::size_t i = 0; i < texts.size(); ++i) {
      if (texts[i] == nullptr || *texts[i] == '\0') {
        std::fprintf(stderr, "%s lacks text %zu\n", std::string(locale.id).c_str(), i);
        CHECK(false);
      }
    }
  }
}

void test_a_text_has_the_same_placeholders_in_every_locale() {
  const auto reference = strings_as_array(*all_locales()[0].strings);
  for (const Locale& locale : all_locales()) {
    const auto texts = strings_as_array(*locale.strings);
    for (std::size_t i = 0; i < texts.size(); ++i) {
      if (placeholders(texts[i]) != placeholders(reference[i])) {
        std::fprintf(stderr, "%s differs at text %zu: %s\n", std::string(locale.id).c_str(), i, texts[i]);
        CHECK(false);
      }
    }
  }
}

void test_locales_have_what_the_list_needs() {
  std::set<std::string_view> ids;
  for (const Locale& locale : all_locales()) {
    CHECK(!locale.id.empty() && !locale.name.empty() && !locale.tag.empty() && !locale.words_folder.empty());
    CHECK(ids.insert(locale.id).second);
  }
  CHECK(find_locale("ko") != nullptr);
  CHECK(find_locale("en") != nullptr);
  CHECK(find_locale("xx") == nullptr);
  CHECK(find_locale("") == nullptr);
}

void test_the_saved_choice_wins_then_the_os_language_then_english() {
  CHECK(choose_locale("en", "ko-KR").id == "en");
  CHECK(choose_locale("", "ko-KR").id == "ko");
  CHECK(choose_locale("", "KO").id == "ko");
  CHECK(choose_locale("", "en-GB").id == "en");
  CHECK(choose_locale("", "fr-FR").id == "en");
  CHECK(choose_locale("xx", "ko_KR").id == "ko");
  CHECK(choose_locale("", "kok-IN").id == "en");  // Konkani is not Korean
  CHECK(choose_locale("", "").id == "en");
}

void test_fill_replaces_what_it_can() {
  CHECK(fill("{0} {1}:{2}", {"오후", "3", "07"}) == "오후 3:07");
  CHECK(fill("{1}:{2} {0}", {"PM", "3", "07"}) == "3:07 PM");
  CHECK(fill("no placeholders", {"x"}) == "no placeholders");
  CHECK(fill("{0}{0}", {"a"}) == "aa");
  CHECK(fill("{3}", {"a"}) == "{3}");
  CHECK(fill("{", {"a"}) == "{");
  CHECK(fill("", {}) == "");
}

void test_access_keys() {
  CHECK(label("&File") == "File");
  CHECK(access_key("&File") == 'F');
  CHECK(label("파일(&F)") == "파일(F)");
  CHECK(access_key("파일(&F)") == 'F');
  CHECK(access_key("E&xit") == 'X');
  CHECK(label("Salt && Pepper") == "Salt & Pepper");
  CHECK(access_key("Salt && &Pepper") == 'P');
  CHECK(access_key("Plain") == 0);
  CHECK(label("end&") == "end&");
  CHECK(access_key("end&") == 0);
  CHECK(access_index("&File") == 0);
  CHECK(access_index("E&xit") == 1);
  CHECK(access_index("파일(&F)") == 3);
  CHECK(access_index("Salt && &Pepper") == 7);
  CHECK(access_index("😀&A") == 2);
  CHECK(access_index("Plain") == -1);
}

void test_korean_formats() {
  set_current(*find_locale("ko"));
  CHECK(format_clock(0, 5, false) == "오전 12:05");
  CHECK(format_clock(15, 7, false) == "오후 3:07");
  CHECK(format_clock(12, 0, false) == "오후 12:00");
  CHECK(format_clock(15, 7, true) == "15:07");
  CHECK(format_stay(59) == "1분 미만");
  CHECK(format_stay(3900) == "1시간 5분");
  CHECK(format_times(1) == "1회");
  CHECK(format_times(1234567) == "1,234,567회");
  CHECK(format_release_date("2026-11-04T01:02:03Z") == "2026년 11월 04일");
  CHECK(format_release_date("2026-1-04").empty());
  CHECK(format_release_date("").empty());
  CHECK(format_release_date("soon").empty());
  CHECK(format_release_date("20261104T090000").empty());
}

void test_english_formats() {
  set_current(*find_locale("en"));
  CHECK(format_clock(0, 5, false) == "12:05 AM");
  CHECK(format_clock(15, 7, false) == "3:07 PM");
  CHECK(format_clock(15, 7, true) == "15:07");
  CHECK(format_stay(59) == "Under a minute");
  CHECK(format_stay(60) == "1 minute");
  CHECK(format_stay(3900) == "1 hour 5 minutes");
  CHECK(format_stay((525600 + 2 * 43200) * 60LL) == "1 year 2 months");
  CHECK(format_times(1) == "1 time");
  CHECK(format_times(1000) == "1,000 times");
  CHECK(format_release_date("2026-11-04") == "2026-11-04");
  set_current(*find_locale("ko"));
}

}  // namespace

void keys_name_every_field_once() {
  std::vector<std::string_view> names;
  const auto fields = strings_as_array(tr());
  for (std::size_t i = 0; i < strings_count; ++i) {
    const std::string_view name = key_name(i);
    CHECK(key_index(name) == i);
    CHECK(key_text(i) == fields[i]);  // the same order as the struct
    names.push_back(name);
  }
  std::ranges::sort(names);
  CHECK(std::ranges::adjacent_find(names) == names.end());
  CHECK(!key_index("no_such_key").has_value());
}

int main() {
  test_every_locale_has_every_text();
  test_a_text_has_the_same_placeholders_in_every_locale();
  test_locales_have_what_the_list_needs();
  test_the_saved_choice_wins_then_the_os_language_then_english();
  test_fill_replaces_what_it_can();
  test_access_keys();
  test_korean_formats();
  test_english_formats();
  keys_name_every_field_once();
  return crpp_test_result();
}
