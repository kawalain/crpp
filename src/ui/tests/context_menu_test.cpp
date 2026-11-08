// The context menu of text: what it offers where, its keys, and what its items do.
#include <optional>
#include <string>

#include "check.hpp"
#include "ui_harness.hpp"

using namespace crpp::ui;
using test::UiHarness;

namespace {

class FakeClipboard final : public Clipboard {
 public:
  bool set_text(std::wstring_view text) override {
    held = text;
    return true;
  }
  std::optional<std::wstring> text() override { return held; }
  bool has_text() override { return held.has_value(); }
  std::optional<std::wstring> held;
};

std::vector<MenuCommand> commands(const ContextMenu& menu) {
  std::vector<MenuCommand> list;
  for (const MenuItem& item : menu.items()) list.push_back(item.command);
  return list;
}

bool enabled(const ContextMenu& menu, MenuCommand command) {
  for (const MenuItem& item : menu.items()) {
    if (item.command == command) return item.enabled;
  }
  return false;
}

struct Room {
  UiHarness h;
  FakeClipboard clipboard;
  ChatPanel* chat = nullptr;

  explicit Room(const char* pack) {
    CHECK(h.start(951, 692, pack));
    h.app.ui().clipboard = &clipboard;
    h.app.show_session("ABC234");
    h.app.room().set_room({{0, "a", "", false}}, 0, 0, false, true);
    chat = &h.app.room().chat();
    chat->add({0, "c0", "a", "", "see https://example.com/x now", 1700000000000});
    h.render();
    h.render();
  }

  void right_click(const Surface& owner, int id) {
    const Ui::Hit* hit = h.app.ui().find_hit(&owner, id);
    CHECK(hit != nullptr);
    if (!hit) return;
    h.app.right_click(hit->rect.x + 2, hit->rect.y + hit->rect.h / 2);
    h.render();
  }
};

void a_field_offers_what_can_be_done_with_its_text(const char* pack) {
  Room r(pack);
  ContextMenu& menu = r.h.app.context_menu();
  CHECK(r.h.click(*r.chat, ChatPanel::Input));
  r.chat->input().set_text(L"hello");
  r.chat->input().select_all();
  r.right_click(*r.chat, ChatPanel::Input);
  CHECK(menu.is_open());
  CHECK((commands(menu) == std::vector<MenuCommand>{MenuCommand::Cut, MenuCommand::Copy, MenuCommand::Paste, MenuCommand::Delete,
                                                     MenuCommand::SelectAll}));
  // A right-click inside the selection keeps it; the clipboard is empty.
  CHECK(enabled(menu, MenuCommand::Copy) && enabled(menu, MenuCommand::Cut) && !enabled(menu, MenuCommand::Paste));
  // Its items are drawn by the pack, one hit each.
  CHECK(r.h.drawn(menu, ContextMenu::Item) && r.h.drawn(menu, ContextMenu::Item + 4));
  // Select All, by its access key; the menu goes.
  CHECK(r.h.app.key_down('A', false, false));
  CHECK(!menu.is_open());
  // Then the keys open it at the caret, on the first item that can act, and Enter cuts.
  r.h.render();
  CHECK(r.h.app.key_down(VK_APPS, false, false) && menu.is_open() && menu.chosen() == 0);
  CHECK(r.h.app.key_down(VK_RETURN, false, false));
  CHECK(r.clipboard.held == L"hello" && r.chat->input().text().empty());
  // Paste is there now, and the arrows skip what cannot act.
  r.h.render();
  CHECK(r.h.app.sys_key(VK_F10, true) && menu.is_open());
  CHECK(enabled(menu, MenuCommand::Paste) && menu.chosen() == 2);
  CHECK(r.h.app.key_down(VK_DOWN, false, false) && menu.chosen() == 2);  // the only one
  CHECK(r.h.app.char_input(L'x', false) && r.chat->input().text().empty());  // keys are the menu's
  CHECK(r.h.app.key_down(VK_RETURN, false, false) && r.chat->input().text() == L"hello");
}

void a_link_offers_its_address(const char* pack) {
  Room r(pack);
  ContextMenu& menu = r.h.app.context_menu();
  r.right_click(*r.chat, ChatPanel::LinkBase);
  CHECK(menu.is_open());
  CHECK((commands(menu) == std::vector<MenuCommand>{MenuCommand::CopyLink, MenuCommand::Copy, MenuCommand::SelectAll}));
  CHECK(!enabled(menu, MenuCommand::Copy));
  CHECK(r.h.click(menu, ContextMenu::Item));
  CHECK(!menu.is_open() && r.clipboard.held == L"https://example.com/x");
}

void messages_offer_copy_and_select_all(const char* pack) {
  Room r(pack);
  ContextMenu& menu = r.h.app.context_menu();
  r.right_click(*r.chat, ChatPanel::Text);
  CHECK((commands(menu) == std::vector<MenuCommand>{MenuCommand::Copy, MenuCommand::SelectAll}));
  // A greyed item does nothing, and the menu stays.
  CHECK(r.h.click(menu, ContextMenu::Item));
  CHECK(menu.is_open());
  CHECK(r.h.click(menu, ContextMenu::Item + 1));
  CHECK(!menu.is_open() && r.chat->selected_text() == L"see https://example.com/x now");
  // Inside what is selected, it stays selected, and Copy acts.
  r.right_click(*r.chat, ChatPanel::Text);
  CHECK(enabled(menu, MenuCommand::Copy));
  CHECK(r.h.app.key_down('C', false, false));
  CHECK(r.clipboard.held == L"see https://example.com/x now");
}

void escape_alt_and_a_click_elsewhere_close_it(const char* pack) {
  Room r(pack);
  ContextMenu& menu = r.h.app.context_menu();
  r.right_click(*r.chat, ChatPanel::Text);
  CHECK(menu.is_open() && r.h.app.key_down(VK_ESCAPE, false, false) && !menu.is_open());
  r.h.render();
  r.right_click(*r.chat, ChatPanel::Text);
  CHECK(r.h.app.sys_key(VK_MENU, false) && !menu.is_open());
  r.h.render();
  r.right_click(*r.chat, ChatPanel::Text);
  r.h.app.mouse_down(2, 2, false);
  r.h.app.mouse_up(2, 2);
  CHECK(!menu.is_open());
  r.h.render();
  r.right_click(*r.chat, ChatPanel::Text);
  CHECK(r.h.app.window_deactivated() && !menu.is_open());
  r.h.render();
  // Where there is no text, none opens.
  r.h.app.right_click(2, 2);
  CHECK(!menu.is_open());
}

void a_password_cannot_be_cut_or_copied() {
  UiHarness h;
  CHECK(h.start());
  FakeClipboard clipboard;
  h.app.ui().clipboard = &clipboard;
  Prompt prompt;
  prompt.password = true;
  prompt.value = "secret";
  h.app.show_prompt(prompt);
  h.render();
  CHECK(h.click(h.app.prompt(), PromptScreen::Field));
  CHECK(h.app.key_down('A', true, false));
  CHECK(h.app.context_key());
  ContextMenu& menu = h.app.context_menu();
  CHECK(menu.is_open() && !enabled(menu, MenuCommand::Cut) && !enabled(menu, MenuCommand::Copy) && enabled(menu, MenuCommand::Delete));
}

}  // namespace

int main() {
  for (const char* pack : {CRPP_TEST_SKINS "/cloud", CRPP_TEST_SKINS "/retro"}) {
    a_field_offers_what_can_be_done_with_its_text(pack);
    a_link_offers_its_address(pack);
    messages_offer_copy_and_select_all(pack);
    escape_alt_and_a_click_elsewhere_close_it(pack);
  }
  a_password_cannot_be_cut_or_copied();
  return crpp_test_result();
}
