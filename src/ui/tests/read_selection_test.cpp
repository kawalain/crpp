// A selection over text that is only read, in numbered blocks.
#include <map>
#include <string>

#include "check.hpp"
#include "crpp/ui/read_selection.hpp"

using namespace crpp::ui;

namespace {

std::map<std::size_t, std::wstring> chat{{7, L"good morning"}, {8, L"see you at noon"}, {9, L"ok"}};

std::wstring_view text_of(std::size_t block) {
  const auto it = chat.find(block);
  return it == chat.end() ? std::wstring_view{} : std::wstring_view(it->second);
}

void a_drag_across_messages_copies_their_bodies_by_line() {
  ReadSelection s;
  s.press({7, 5}, 1, false, text_of);
  s.drag_to({9, 1}, text_of);
  CHECK(s.text(text_of) == L"morning\nsee you at noon\no");
  CHECK((s.in_block(8, 15) == std::pair<std::size_t, std::size_t>{0, 15}));
  CHECK((s.in_block(7, 12) == std::pair<std::size_t, std::size_t>{5, 12}));
  CHECK(!s.in_block(6, 4));
  // Dragged back above where it began, it selects from there instead.
  s.drag_to({7, 0}, text_of);
  CHECK(s.text(text_of) == L"good ");
}

void a_double_click_takes_a_word_and_drags_by_words() {
  ReadSelection s;
  s.press({8, 5}, 2, false, text_of);
  CHECK(s.text(text_of) == L"you");
  s.drag_to({9, 1}, text_of);
  CHECK(s.text(text_of) == L"you at noon\nok");
  s.press({8, 1}, 3, false, text_of);
  CHECK(s.text(text_of) == L"see you at noon");
}

void shift_extends_from_where_it_began() {
  ReadSelection s;
  s.press({7, 0}, 1, false, text_of);
  s.drag_to({7, 4}, text_of);
  s.press({8, 3}, 1, true, text_of);
  CHECK(s.text(text_of) == L"good morning\nsee");
}

void a_message_that_left_is_passed_over() {
  ReadSelection s;
  s.select({7, 0}, {8, 3});
  chat.erase(7);
  CHECK(s.text(text_of) == L"see");
  chat[7] = L"good morning";
}

void an_empty_selection_has_nothing() {
  ReadSelection s;
  s.press({8, 3}, 1, false, text_of);
  CHECK(s.empty() && s.text(text_of).empty() && !s.in_block(8, 15));
}

}  // namespace

int main() {
  a_drag_across_messages_copies_their_bodies_by_line();
  a_double_click_takes_a_word_and_drags_by_words();
  shift_extends_from_where_it_began();
  a_message_that_left_is_passed_over();
  an_empty_selection_has_nothing();
  return crpp_test_result();
}
