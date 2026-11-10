#include "options.hpp"

#include <algorithm>
#include <initializer_list>

#include "check.hpp"

using namespace crpp::gui;

namespace {

std::expected<Options, std::string> read(std::initializer_list<const char*> args) {
  const std::vector<const char*> all(args);
  return read_options(all);
}

// --data first once opened the real app on the user's desktop, as it was only read at the end.
void an_option_stands_anywhere() {
  const auto last = read({"crpp", "--shot", "out.png", "951x692", "14", "home", "--data", "d"});
  const auto first = read({"crpp", "--data", "d", "--shot", "out.png", "951x692", "14", "home"});
  const auto middle = read({"crpp", "--shot", "out.png", "--data", "d", "951x692", "--theme", "classic", "14", "home"});
  CHECK(last.has_value() && first.has_value() && middle.has_value());
  CHECK(last->value("data") == "d" && first->value("data") == "d" && middle->value("data") == "d");
  CHECK(last->words == first->words && first->words == middle->words);
  CHECK(first->word(1) == "--shot" && first->word(2) == "out.png" && first->word(5) == "home");
  CHECK(middle->value("theme") == "classic");
  CHECK(first->value("theme").empty());
}

void what_to_do_keeps_its_order() {
  const auto motion = read({"crpp", "--shot", "o.png", "951x692", "14", "motion-screen", "--at", "120", "--dpi", "144"});
  CHECK(motion.has_value());
  CHECK(motion->word(6) == "--at" && motion->word(7) == "120");
  CHECK(motion->value("dpi") == "144");
  CHECK(read({"crpp", "--crash", "access", "--data", "d"})->value("crash") == "access");
  const auto plain = read({"crpp"});
  CHECK(plain.has_value() && plain->words.size() == 1 && plain->named.empty());
  CHECK(read({"crpp", "--minimized"})->word(1) == "--minimized");
  CHECK(plain->word(1).empty());
}

// Never ignored: what follows would run as if the option were not there.
void a_word_that_is_no_option_is_an_error() {
  CHECK(!read({"crpp", "--dta", "d"}));
  CHECK(!read({"crpp", "--shot", "o.png", "--data"}));
  CHECK(!read({"crpp", "--data", "a", "--data", "b"}));
  CHECK(read({"crpp", "--dta", "d"}).error().find("--dta") != std::string::npos);
}

}  // namespace

// Sizes, hours and the like are the whole word or nothing: "14abc" is not 14.
void a_number_is_the_whole_word() {
  CHECK(number_or(std::string_view("14"), 0) == 14);
  CHECK(number_or(std::string_view("14abc"), 0) == 0);
  CHECK(number_or(std::string_view(""), 7) == 7);
  CHECK(number_or(std::string_view("-3"), 0u) == 0u);
  CHECK(number_or(std::string_view("144"), 96u) == 144u);
  CHECK(number_or(std::string_view("dpi"), 96u) == 96u);
}

int main() {
  an_option_stands_anywhere();
  a_number_is_the_whole_word();
  what_to_do_keeps_its_order();
  a_word_that_is_no_option_is_an_error();
  return crpp_test_result();
}
