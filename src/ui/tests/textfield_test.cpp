// What a field does with keys, clicks and pasted text, without a window.
#include <windows.h>

#include "check.hpp"
#include "crpp/ui/textfield.hpp"

using namespace crpp::ui;

namespace {

TextField with(std::wstring_view text, std::size_t caret) {
  TextField field;
  field.set_text(text);
  field.press(caret, 1, false);
  return field;
}

void ctrl_arrows_move_by_word() {
  TextField field = with(L"hello, big world", 0);
  CHECK(field.key_down(VK_RIGHT, true, false) && field.caret() == 5);  // the end of "hello"
  CHECK(field.key_down(VK_RIGHT, true, false) && field.caret() == 7);  // past "," and the space
  CHECK(field.key_down(VK_RIGHT, true, false) && field.caret() == 11);
  CHECK(field.key_down(VK_RIGHT, true, false) && field.caret() == 16);
  CHECK(field.key_down(VK_RIGHT, true, false) && field.caret() == 16);
  CHECK(field.key_down(VK_LEFT, true, false) && field.caret() == 11);
  CHECK(field.key_down(VK_LEFT, true, false) && field.caret() == 7);
  CHECK(field.key_down(VK_LEFT, true, false) && field.caret() == 5);
  CHECK(field.key_down(VK_LEFT, true, false) && field.caret() == 0);
  // With Shift it selects.
  CHECK(field.key_down(VK_RIGHT, true, true) && field.selected_text() == L"hello");
}

void korean_is_a_word_like_any_other() {
  TextField field = with(L"구름 위에서 만나요", 0);
  CHECK(field.key_down(VK_RIGHT, true, false) && field.caret() == 3);  // the start of the next word
  CHECK(field.key_down(VK_RIGHT, true, false) && field.caret() == 7);
}

void ctrl_backspace_and_delete_take_a_word() {
  TextField field = with(L"see you soon", 7);
  CHECK(field.key_down(VK_BACK, true, false) && field.text() == L"see  soon" && field.caret() == 4);
  CHECK(field.key_down(VK_DELETE, true, false) && field.text() == L"see soon");
  // A selection goes as it is, whatever the key.
  field.set_text(L"abc def");
  field.press(1, 1, false);
  field.press(5, 1, true);
  CHECK(field.key_down(VK_BACK, true, false) && field.text() == L"aef");
}

void a_word_never_splits_a_surrogate_pair() {
  // U+1F600 is two UTF-16 units.
  TextField field = with(L"a \U0001F600\U0001F600 b", 0);
  CHECK(field.key_down(VK_RIGHT, true, false) && field.caret() == 2);
  CHECK(field.key_down(VK_RIGHT, true, false) && field.caret() == 7);
  CHECK(field.key_down(VK_LEFT, true, false) && field.caret() == 2);
}

void a_double_click_selects_a_word_and_a_triple_its_paragraph() {
  TextField field;
  field.multiline = true;
  field.set_text(L"first line here\nsecond one");
  field.press(8, 2, false);
  CHECK(field.selected_text() == L"line");
  field.press(8, 3, false);
  CHECK(field.selected_text() == L"first line here");
  field.press(18, 3, false);
  CHECK(field.selected_text() == L"second one");
  // A double click on spaces takes them.
  field.set_text(L"a   b");
  field.press(2, 2, false);
  CHECK(field.selected_text() == L"   ");
}

void a_drag_after_a_double_click_goes_by_words() {
  TextField field = with(L"one two three four", 0);
  field.press(5, 2, false);  // "two"
  field.drag_to(10);         // inside "three"
  CHECK(field.selected_text() == L"two three");
  field.drag_to(1);  // back past where it began: the first word stays whole
  CHECK(field.selected_text() == L"one two");
  // A single click drags by characters.
  field.press(5, 1, false);
  field.drag_to(9);
  CHECK(field.selected_text() == L"wo t");
}

void a_single_line_field_pastes_lines_as_spaces() {
  TextField field;
  field.paste(L"one\r\ntwo\nthree\tfour\rfive");
  CHECK(field.text() == L"one two three four five");
}

void the_chat_input_keeps_line_breaks() {
  TextField field;
  field.multiline = true;
  field.paste(L"one\r\ntwo\tthree");
  CHECK(field.text() == L"one\ntwo three");
}

void a_compact_field_drops_spaces() {
  TextField field;
  field.compact = true;
  field.uppercase = true;
  field.max_length = 6;
  field.paste(L" ab c\r\n12 3 ");
  CHECK(field.text() == L"ABC123");
}

void a_paste_past_the_maximum_is_cut() {
  TextField field;
  field.max_length = 4;
  field.paste(L"abcdef");
  CHECK(field.text() == L"abcd");
}

void a_password_gives_no_text_to_copy() {
  TextField field = with(L"secret", 0);
  field.select_all();
  CHECK(field.selected_text() == L"secret");
  field.password = true;
  CHECK(field.selected_text().empty());
  CHECK(!field.can_copy());
  CHECK(field.can_delete());
}

}  // namespace

int main() {
  ctrl_arrows_move_by_word();
  korean_is_a_word_like_any_other();
  ctrl_backspace_and_delete_take_a_word();
  a_word_never_splits_a_surrogate_pair();
  a_double_click_selects_a_word_and_a_triple_its_paragraph();
  a_drag_after_a_double_click_goes_by_words();
  a_single_line_field_pastes_lines_as_spaces();
  the_chat_input_keeps_line_breaks();
  a_compact_field_drops_spaces();
  a_paste_past_the_maximum_is_cut();
  a_password_gives_no_text_to_copy();
  return crpp_test_result();
}
