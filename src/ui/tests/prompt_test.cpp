#include "check.hpp"
#include "ui_harness.hpp"

using namespace crpp::ui;
using test::UiHarness;

namespace {

void type(UiHarness& h, std::wstring_view text) {
  for (const wchar_t c : text) h.app.char_input(c, false);
  h.render();
}

Prompt three_or_more() {
  Prompt p;
  p.label = "ID";
  p.hint = "힌트";
  p.valid = [](std::string_view v) { return v.size() >= 3; };
  p.lock_hint = [](long long s) { return "잠김 " + std::to_string(s); };
  return p;
}

void test_covers_the_screen_and_hides_the_droplet() {
  UiHarness h;
  CHECK(h.start());
  CHECK(h.drawn(h.app.settings(), SettingsPanel::Droplet));
  h.app.show_prompt(three_or_more());
  h.render();
  CHECK(h.app.view() == View::Prompt);
  CHECK(!h.drawn(h.app.settings(), SettingsPanel::Droplet));
  CHECK(h.drawn(h.app.prompt(), PromptScreen::Field));
  CHECK(h.app.ui().focus != nullptr);  // ready for typing
  h.app.close_prompt();
  h.render();
  CHECK(h.app.view() == View::Home);
  CHECK(h.drawn(h.app.settings(), SettingsPanel::Droplet));
}

void test_enter_submits_only_when_valid() {
  UiHarness h;
  CHECK(h.start());
  int submitted = 0;
  std::string value;
  h.app.prompt().on_submit = [&](std::string v) {
    ++submitted;
    value = std::move(v);
  };
  h.app.show_prompt(three_or_more());
  h.render();
  type(h, L"ab\r");
  CHECK(submitted == 0);
  CHECK(h.click(h.app.prompt(), PromptScreen::Submit));
  CHECK(submitted == 0);
  type(h, L"c\r");
  CHECK(submitted == 1 && value == "abc");
  CHECK(h.click(h.app.prompt(), PromptScreen::Submit));
  CHECK(submitted == 2);
}

void test_typing_clears_the_error() {
  UiHarness h;
  CHECK(h.start());
  h.app.show_prompt(three_or_more());
  h.render();
  type(h, L"abc");
  h.app.prompt().set_error("틀렸어요");
  h.render();
  CHECK(h.app.prompt().hint_shown() == "틀렸어요");
  h.render();  // drawing again keeps it
  CHECK(h.app.prompt().hint_shown() == "틀렸어요");
  type(h, L"d");
  CHECK(h.app.prompt().hint_shown() == "힌트");
}

void test_input_rules() {
  UiHarness h;
  CHECK(h.start());
  Prompt p = three_or_more();
  p.lowercase = true;
  p.identifier = true;
  p.max_length = 5;
  h.app.show_prompt(std::move(p));
  h.render();
  type(h, L"A한b_c.xyz");
  CHECK(h.app.prompt().value() == "ab_c.");
}

void test_password_eye() {
  UiHarness h;
  CHECK(h.start());
  Prompt p = three_or_more();
  CHECK(!p.password);
  h.app.show_prompt(p);
  h.render();
  CHECK(!h.drawn(h.app.prompt(), PromptScreen::Eye));
  p.password = true;
  h.app.show_prompt(p);
  h.render();
  type(h, L"secret");
  CHECK(!h.app.prompt().password_shown());
  CHECK(h.click(h.app.prompt(), PromptScreen::Eye));
  CHECK(h.app.prompt().password_shown());
  CHECK(h.app.ui().focus != nullptr);
  CHECK(h.click(h.app.prompt(), PromptScreen::Eye));
  CHECK(!h.app.prompt().password_shown());
  CHECK(h.app.prompt().value() == "secret");
}

void test_masked_password_is_not_copied() {
  // The clipboard is the machine's, so only the field's own state is checked: Ctrl+X
  // of a masked field must not cut anything.
  TextField field;
  field.password = true;
  field.set_text(L"secret");
  field.select_all();
  CHECK(field.key_down('X', true, false));
  CHECK(field.text() == L"secret");
}

void test_composing_replaces_the_selection() {
  TextField field;
  field.set_text(L"구름이");
  field.select_all();
  field.set_composition(L"ㅎ");
  CHECK(field.text().empty());
  CHECK(field.composing());
  // Going on with the same syllable changes nothing else.
  field.set_composition(L"하");
  field.set_composition(L"");
  field.insert(L"하");
  CHECK(field.text() == L"하");
}

void test_composing_what_the_field_refuses_keeps_the_selection() {
  TextField field;
  field.accept = [](wchar_t c) { return c < 0x80; };
  field.set_text(L"ABC");
  field.select_all();
  field.set_composition(L"ㅎ");
  CHECK(field.text() == L"ABC");
}

void test_dragging_selects_and_typing_replaces() {
  UiHarness h;
  CHECK(h.start());
  PromptScreen& s = h.app.prompt();
  Prompt p = three_or_more();
  p.value = "abcdef";
  h.app.show_prompt(p);
  h.render();
  const Ui::Hit* hit = h.app.ui().find_hit(&s, PromptScreen::Field);
  CHECK(hit != nullptr);
  if (!hit) return;
  const float y = hit->rect.y + hit->rect.h / 2;
  h.app.mouse_move(hit->rect.x + 2, y);
  h.app.mouse_down(hit->rect.x + 2, y, false);
  h.app.mouse_move(hit->rect.right() - 2, y);
  h.app.mouse_up(hit->rect.right() - 2, y);
  h.render();
  CHECK(h.app.ime_composition(L"", L"x"));
  CHECK(s.value() == "x");
}

void test_buttons_and_escape() {
  UiHarness h;
  CHECK(h.start());
  int back = 0, extra = 0, extra2 = 0, link = 0;
  PromptScreen& s = h.app.prompt();
  s.on_back = [&] { ++back; };
  s.on_extra = [&] { ++extra; };
  s.on_extra2 = [&] { ++extra2; };
  s.on_link = [&] { ++link; };
  Prompt p = three_or_more();
  h.app.show_prompt(p);
  h.render();
  CHECK(!h.drawn(s, PromptScreen::Back) && !h.drawn(s, PromptScreen::Extra) && !h.drawn(s, PromptScreen::Link));
  h.key(VK_ESCAPE);  // nothing to go back to
  CHECK(back == 0);

  p.cancelable = true;
  p.extra = "무작위 설정";
  p.extra2 = "사용자명 사용";
  p.link = "링크";
  h.app.show_prompt(p);
  h.render();
  CHECK(h.click(s, PromptScreen::Back) && back == 1);
  CHECK(h.click(s, PromptScreen::Extra) && extra == 1);
  CHECK(h.click(s, PromptScreen::Extra2) && extra2 == 1);
  CHECK(h.click(s, PromptScreen::Link) && link == 1);
  h.key(VK_ESCAPE);
  CHECK(back == 2);

  s.set_busy(true);  // while a request is out, nothing leaves the step
  h.render();
  CHECK(h.click(s, PromptScreen::Back) && back == 2);
  CHECK(h.click(s, PromptScreen::Link) && link == 1);
  h.key(VK_ESCAPE);
  CHECK(back == 2);
}

void test_busy_blocks_submit() {
  UiHarness h;
  CHECK(h.start());
  int submitted = 0;
  h.app.prompt().on_submit = [&](std::string) { ++submitted; };
  h.app.show_prompt(three_or_more());
  h.render();
  type(h, L"abc");
  h.app.prompt().set_busy(true);
  type(h, L"\r");
  CHECK(submitted == 0);
  h.app.prompt().set_busy(false);
  type(h, L"\r");
  CHECK(submitted == 1);
}

void test_lock_counts_down_without_polling() {
  UiHarness h;
  CHECK(h.start());
  int submitted = 0;
  PromptScreen& s = h.app.prompt();
  s.on_submit = [&](std::string) { ++submitted; };
  h.app.show_prompt(three_or_more());
  h.render();
  type(h, L"abc");
  s.lock(150);
  h.render();
  CHECK(s.locked());
  CHECK(s.value().empty());
  CHECK(h.app.ui().focus == nullptr);
  CHECK(s.hint_shown().starts_with("잠김 "));
  CHECK(!h.app.animating());  // nothing runs frames for it

  // 150 s means a wake when "3 minutes" becomes 2, 2 becomes 1, and at the end.
  int wakes = 0;
  while (const auto at = h.app.wake_at()) {
    CHECK(h.app.tick(at));
    ++wakes;
    if (wakes > 5) break;
  }
  CHECK(wakes == 3);
  CHECK(!s.locked());
  CHECK(h.app.ui().focus != nullptr);
  h.render();
  CHECK(s.hint_shown() == "힌트");
  type(h, L"abc\r");
  CHECK(submitted == 1);
}

void test_a_room_goes_on_under_the_prompt() {
  UiHarness h;
  CHECK(h.start());
  h.app.show_session("ABC234");
  h.render();
  h.app.show_prompt(three_or_more());
  CHECK(h.app.base_view() == View::Session);

  // The Room ended meanwhile: closing the prompt must not bring it back.
  h.app.show_home();
  CHECK(h.app.view() == View::Prompt && h.app.base_view() == View::Home);
  h.app.close_prompt();
  CHECK(h.app.view() == View::Home);

  h.app.show_prompt(three_or_more());
  h.app.show_session("ABC234");
  CHECK(h.app.view() == View::Prompt);
  h.app.close_prompt();
  CHECK(h.app.view() == View::Session);
}

void test_a_long_value_keeps_the_caret_in_the_field() {
  UiHarness h;
  CHECK(h.start());
  h.app.show_prompt(three_or_more());
  h.render();
  type(h, std::wstring(80, L'w'));
  const Ui::Hit* field = h.app.ui().find_hit(&h.app.prompt(), PromptScreen::Field);
  CHECK(field != nullptr);
  const Rect caret = h.app.caret_rect();
  CHECK(field && caret.x > field->rect.x && caret.x < field->rect.right());
  h.app.key_down(VK_HOME, false, false);
  h.render();
  CHECK(field && h.app.caret_rect().x > h.app.ui().find_hit(&h.app.prompt(), PromptScreen::Field)->rect.x);
}

}  // namespace

int main() {
  test_covers_the_screen_and_hides_the_droplet();
  test_enter_submits_only_when_valid();
  test_typing_clears_the_error();
  test_input_rules();
  test_password_eye();
  test_masked_password_is_not_copied();
  test_composing_replaces_the_selection();
  test_composing_what_the_field_refuses_keeps_the_selection();
  test_dragging_selects_and_typing_replaces();
  test_buttons_and_escape();
  test_busy_blocks_submit();
  test_lock_counts_down_without_polling();
  test_a_room_goes_on_under_the_prompt();
  test_a_long_value_keeps_the_caret_in_the_field();
  return crpp_test_result();
}
