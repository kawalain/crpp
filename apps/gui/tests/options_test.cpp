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

int main() {
  an_option_stands_anywhere();
  what_to_do_keeps_its_order();
  a_word_that_is_no_option_is_an_error();
  return crpp_test_result();
}
