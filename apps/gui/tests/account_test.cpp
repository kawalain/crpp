// The first-run flow and the account tab, through the real screens with a fake server.
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "account_controller.hpp"
#include "check.hpp"
#include "ui_harness.hpp"

using namespace crpp;
using namespace crpp::gui;
using namespace crpp::ui;
using ui::test::UiHarness;

namespace {

class FakeHost final : public AccountHost {
 public:
  api::Identity stored;
  std::string gid, stored_emoji;
  int seats_forgotten = 0, erased = 0, left_room = 0, saved = 0;
  std::vector<api::Request> requests;
  std::deque<std::move_only_function<void(api::HttpResult) const>> pending;

  [[nodiscard]] api::Identity identity() const override { return stored; }
  [[nodiscard]] std::string guest_id() const override { return gid; }
  void save_identity(const api::Identity& identity) override {
    stored = identity;
    ++saved;
  }
  void forget_seats() override { ++seats_forgotten; }
  void erase_personal_data() override {
    ++erased;
    gid.clear();
  }
  void leave_room() override { ++left_room; }
  void server_unreachable() override { ++unreachable; }
  int unreachable = 0;
  [[nodiscard]] std::string random_nickname() override { return "졸린 곰"; }
  [[nodiscard]] std::string emoji() const override { return stored_emoji; }
  void set_emoji(std::string id) override { stored_emoji = std::move(id); }
  void send(api::Request request, std::move_only_function<void(api::HttpResult) const> done) override {
    requests.push_back(std::move(request));
    pending.push_back(std::move(done));
  }

  // The server: each one keeps its own account, as the store does.
  std::string server = "cr.nanliburusu.com";
  std::map<std::string, api::Identity> kept;
  std::vector<std::string> checked;
  std::move_only_function<void(ServerCheck) const> checking;
  [[nodiscard]] std::string server_shown() const override { return server; }
  void check_server(std::string typed, std::move_only_function<void(ServerCheck) const> done) override {
    checked.push_back(std::move(typed));
    checking = std::move(done);
  }
  void change_server(const std::string& address) override {
    kept[server] = stored;
    server = address.empty() ? "cr.nanliburusu.com" : address;
    stored = kept[server];
    gid.clear();
  }
  // Answers the check that is out.
  void server_answer(UiHarness& h, ServerCheck check) {
    CHECK(static_cast<bool>(checking));
    if (!checking) return;
    std::exchange(checking, nullptr)(std::move(check));
    h.render();
  }

  // Answers what is out, as the UI thread would receive it.
  void answer(UiHarness& h, api::HttpResult result) {
    CHECK(!pending.empty());
    if (pending.empty()) return;
    auto done = std::move(pending.front());
    pending.pop_front();
    done(std::move(result));
    h.render();
  }
  void answer(UiHarness& h, int status, std::string body) { answer(h, api::Response{status, std::move(body)}); }
};

struct Rig {
  UiHarness h;
  FakeHost host;
  std::optional<AccountController> controller;

  Rig() {
    CHECK(h.start());
    controller.emplace(h.app, host);
  }
  PromptScreen& prompt() { return h.app.prompt(); }
  AccountTab& tab() { return h.app.settings().account(); }
  void type(std::wstring_view text) {
    for (const wchar_t c : text) h.app.char_input(c, false);
    h.render();
  }
  void submit(std::wstring_view text) {
    type(text);
    type(L"\r");
  }
  void open_tab() {
    h.app.settings().open(SettingsPage::Account);
    h.render();
  }
};

api::Identity account(std::string nickname = "구름", std::string handle = "cloud") {
  api::Identity id;
  id.username = "alice";
  id.token = "TOKEN";
  id.nickname = std::move(nickname);
  id.handle = std::move(handle);
  return id;
}

api::Identity guest(std::string nickname = "구름") {
  api::Identity id;
  id.nickname = std::move(nickname);
  id.is_guest = true;
  return id;
}

void test_stored_identity_goes_straight_home() {
  {
    Rig r;
    r.host.stored = guest();
    CHECK(!r.controller->begin_first_run());
    CHECK(r.h.app.view() == View::Home);
    CHECK(r.host.requests.empty());  // a Guest has nothing to refresh
  }
  {
    Rig r;
    r.host.gid = "guest-id";
    CHECK(!r.controller->begin_first_run());
    CHECK(r.h.app.view() == View::Home);
  }
  {
    Rig r;
    r.host.stored = account();
    CHECK(!r.controller->begin_first_run());
    CHECK(r.h.app.view() == View::Home);
    CHECK(r.host.requests.size() == 1 && r.host.requests[0].path == "api/me");
  }
  {
    Rig r;
    CHECK(r.controller->begin_first_run());
    CHECK(r.h.app.view() == View::Prompt);
    CHECK(r.host.requests.empty());  // nothing is sent until the user asks
    CHECK(r.prompt().prompt().label == "ID");
  }
}

void test_register_walks_all_steps_to_home() {
  Rig r;
  (void)r.controller->begin_first_run();
  r.h.render();

  r.submit(L"ALICE");  // typed letters become lowercase
  CHECK(r.host.requests.back().path == "api/username?name=alice");
  CHECK(r.prompt().busy());
  r.host.answer(r.h, 200, R"({"valid":true,"available":true})");
  CHECK(r.prompt().prompt().heading == "계정 만들기");
  CHECK(r.prompt().prompt().password);
  CHECK(r.prompt().value().empty());

  r.submit(L"passw0rd!");
  CHECK(r.prompt().prompt().heading == "비밀번호 확인");
  CHECK(r.host.requests.size() == 1);  // that step stays on this device

  r.submit(L"typo1234");
  CHECK(r.prompt().hint_shown() == "비밀번호가 일치하지 않아요.");
  CHECK(r.prompt().value() == "typo1234");
  r.type(std::wstring(8, L'\b'));
  for (int i = 0; i < 8; ++i) r.h.app.key_down(VK_BACK, false, false);
  r.submit(L"passw0rd!");
  CHECK(r.host.requests.back().path == "api/register");
  r.host.answer(r.h, 200, R"({"token":"T","username":"alice"})");
  CHECK(r.prompt().prompt().heading == "사용자명 정하기");
  CHECK(r.prompt().prompt().prefix == "@");
  CHECK(r.host.stored.token == "T");  // kept from here on, whatever happens next
  CHECK(r.host.seats_forgotten == 1);

  r.submit(L"cloud");
  CHECK(r.host.requests.back().token == "T");
  r.host.answer(r.h, 200, R"({"valid":true,"available":true})");
  CHECK(r.host.requests.back().path == "api/handle");
  r.host.answer(r.h, 200, R"({"username":"alice","handle":"cloud"})");
  CHECK(r.prompt().prompt().heading == "별명 정하기");
  CHECK(r.prompt().prompt().extra == "랜덤으로 정하기");
  CHECK(r.prompt().prompt().extra2 == "사용자명으로 쓰기");

  CHECK(r.h.click(r.prompt(), PromptScreen::Extra));
  CHECK(r.prompt().value() == "졸린 곰");
  r.type(L"\r");
  CHECK(r.host.requests.back().path == "api/nickname");
  r.host.answer(r.h, 200, R"({"username":"alice","nickname":"졸린 곰","handle":"cloud"})");
  CHECK(r.h.app.view() == View::Home);
  CHECK(r.host.stored.nickname == "졸린 곰" && r.host.stored.handle == "cloud");
}

void test_use_handle_needs_no_nickname() {
  Rig r;
  r.host.stored = account("", "cloud");
  r.tab();
  CHECK(!r.controller->begin_first_run());
  r.host.answer(r.h, 200, R"({"username":"alice","handle":"cloud"})");
  CHECK(r.h.app.view() == View::Home);  // has a handle, so nothing is asked

  r.open_tab();
  CHECK(r.h.click(r.tab(), AccountTab::Nickname));
  CHECK(r.h.click(r.prompt(), PromptScreen::Extra2));
  CHECK(r.host.requests.back().body == R"({"nickname":""})");
  r.host.answer(r.h, 200, R"({"username":"alice","handle":"cloud"})");
  CHECK(r.h.app.view() == View::Home);
  CHECK(r.host.stored.nickname.empty());
}

void test_login_with_lockout() {
  Rig r;
  (void)r.controller->begin_first_run();
  r.h.render();
  r.submit(L"alice");
  r.host.answer(r.h, 200, R"({"valid":true,"available":false})");
  CHECK(r.prompt().prompt().heading == "로그인");

  r.submit(L"wrong");
  r.host.answer(r.h, 401, R"({"error":"invalid_credentials","attempts":4,"max":5})");
  CHECK(r.prompt().hint_shown() == "비밀번호가 맞지 않아요. (4/5)");
  CHECK(r.prompt().value() == "wrong");
  CHECK(!r.prompt().busy());

  r.type(L"\r");
  r.host.answer(r.h, 429, R"({"error":"locked","retryAfter":180})");
  CHECK(r.prompt().locked());
  CHECK(r.prompt().value().empty());
  CHECK(r.prompt().hint_shown() == "비밀번호를 너무 많이 틀렸어요. 3분 뒤에 다시 시도해 주세요.");
  CHECK(r.host.requests.size() == 3);
  r.type(L"x\r");  // nothing takes it
  CHECK(r.host.requests.size() == 3);

  while (const auto at = r.h.app.wake_at()) (void)r.h.app.tick(at);
  r.h.render();
  CHECK(!r.prompt().locked());
  r.submit(L"right");
  r.host.answer(r.h, 200, R"({"token":"T","username":"alice","nickname":"구름"})");
  CHECK(r.h.app.view() == View::Home);
  CHECK(r.host.stored.token == "T" && r.host.stored.nickname == "구름");
}

void test_network_failure_stays_on_the_step() {
  Rig r;
  (void)r.controller->begin_first_run();
  r.h.render();
  r.submit(L"alice");
  r.host.answer(r.h, std::unexpected(api::HttpError::Network));
  CHECK(r.h.app.view() == View::Prompt);
  CHECK(r.prompt().prompt().label == "ID");
  CHECK(r.prompt().hint_shown() == "요청을 처리하지 못했어요.");
  CHECK(r.prompt().value() == "alice");
  CHECK(!r.prompt().busy());
}

void test_guest_path_through_its_dialog() {
  Rig r;
  (void)r.controller->begin_first_run();
  r.h.render();
  CHECK(r.h.click(r.prompt(), PromptScreen::Link));
  CHECK(r.h.app.dialogs().is_open());

  CHECK(r.h.click(r.h.app.dialogs(), Dialogs::Cancel));
  CHECK(r.prompt().prompt().label == "ID");
  CHECK(!r.host.stored.is_guest);

  CHECK(r.h.click(r.prompt(), PromptScreen::Link));
  CHECK(r.h.click(r.h.app.dialogs(), Dialogs::Confirm));
  CHECK(r.prompt().prompt().heading == "별명 정하기");
  CHECK(r.prompt().prompt().extra2.empty());  // no handle to use
  CHECK(r.host.stored.is_guest);
  CHECK(r.host.seats_forgotten == 1);

  r.submit(L"구름");
  CHECK(r.h.app.view() == View::Home);
  CHECK(r.host.stored.nickname == "구름");
  CHECK(r.host.requests.empty());  // a Guest's name is theirs alone
}

void test_links_go_back_to_the_first_step() {
  Rig r;
  (void)r.controller->begin_first_run();
  r.h.render();
  r.submit(L"alice");
  r.host.answer(r.h, 200, R"({"valid":true,"available":true})");
  CHECK(r.prompt().prompt().link == "← ID 다시 입력하기");
  CHECK(r.h.click(r.prompt(), PromptScreen::Link));
  CHECK(r.prompt().prompt().label == "ID");
  CHECK(!r.h.app.dialogs().is_open());
}

void test_tab_shows_what_each_person_can_do() {
  {
    Rig r;
    r.host.stored = guest();
    r.controller->update_tab();
    r.open_tab();
    CHECK(r.h.drawn(r.tab(), AccountTab::Emoji));
    CHECK(r.h.drawn(r.tab(), AccountTab::Nickname));
    CHECK(!r.h.drawn(r.tab(), AccountTab::Handle));
    CHECK(r.h.drawn(r.tab(), AccountTab::Logout));
  }
  {
    Rig r;
    r.host.stored = account();
    r.controller->update_tab();
    r.open_tab();
    CHECK(r.h.drawn(r.tab(), AccountTab::Handle));
  }
}

void test_icon_goes_through_the_picker() {
  Rig r;
  r.host.stored = guest();
  r.host.stored_emoji = "1f431";
  r.controller->update_tab();
  r.open_tab();
  CHECK(r.h.click(r.tab(), AccountTab::Emoji));
  CHECK(r.h.app.emoji_picker().is_open());
}

void test_cancel_returns_to_the_tab() {
  Rig r;
  r.host.stored = account();
  r.controller->update_tab();
  r.open_tab();
  CHECK(r.h.click(r.tab(), AccountTab::Handle));
  CHECK(r.h.app.view() == View::Prompt);
  CHECK(!r.h.app.settings().is_open());
  CHECK(r.prompt().value() == "cloud");
  CHECK(r.h.click(r.prompt(), PromptScreen::Back));
  CHECK(r.h.app.view() == View::Home);
  CHECK(r.h.app.settings().is_open());
  CHECK(r.h.app.settings().page() == SettingsPage::Account);

  CHECK(r.h.click(r.tab(), AccountTab::Nickname));
  r.h.key(VK_ESCAPE);
  CHECK(r.h.app.settings().is_open());
  CHECK(r.h.app.view() == View::Home);
}

void test_changing_names_inside_a_room_does_not_leave_it() {
  Rig r;
  r.host.stored = account();
  r.controller->update_tab();
  r.h.app.show_session("ABC234");
  r.open_tab();

  CHECK(r.h.click(r.tab(), AccountTab::Nickname));
  CHECK(!r.h.app.dialogs().is_open());  // upstream asks to leave first
  r.h.app.key_down('A', true, false);
  r.submit(L"새 별명");
  r.host.answer(r.h, 200, R"({"username":"alice","nickname":"새 별명","handle":"cloud"})");
  CHECK(r.h.app.view() == View::Session);
  CHECK(r.host.stored.nickname == "새 별명");
  CHECK(r.host.left_room == 0);

  r.open_tab();
  CHECK(r.h.click(r.tab(), AccountTab::Handle));
  r.h.app.key_down('A', true, false);
  r.submit(L"newhandle");
  r.host.answer(r.h, 200, R"({"valid":true,"available":true})");
  r.host.answer(r.h, 200, R"({"username":"alice","nickname":"새 별명","handle":"newhandle"})");
  CHECK(r.h.app.view() == View::Session);
  CHECK(r.host.stored.handle == "newhandle");
  CHECK(r.host.left_room == 0);
}

void test_logout_from_home() {
  Rig r;
  r.host.stored = account();
  r.controller->update_tab();
  r.open_tab();
  CHECK(r.h.click(r.tab(), AccountTab::Logout));
  CHECK(r.h.app.dialogs().is_open());
  CHECK(r.h.app.dialogs().text() == tr().account_logout_question);  // an account comes back by signing in
  CHECK(r.h.click(r.h.app.dialogs(), Dialogs::Cancel));
  CHECK(r.host.stored.token == "TOKEN");
  CHECK(r.h.app.view() == View::Home);

  CHECK(r.h.click(r.tab(), AccountTab::Logout));
  CHECK(r.h.click(r.h.app.dialogs(), Dialogs::Confirm));
  CHECK(r.host.requests.back().path == "api/logout" && r.host.requests.back().token == "TOKEN");
  CHECK(r.host.stored.token.empty() && r.host.stored.nickname.empty() && r.host.stored.username.empty());
  CHECK(r.host.erased == 1);
  CHECK(r.host.left_room == 0);
  CHECK(r.h.app.view() == View::Prompt);
  CHECK(r.prompt().prompt().label == "ID");
  CHECK(!r.prompt().prompt().cancelable);
  // Signed out even if the server never hears of it.
  r.host.answer(r.h, std::unexpected(api::HttpError::Network));
  CHECK(r.prompt().prompt().label == "ID");
  CHECK(api::needs_first_run(r.host.stored, r.host.gid));
}

void test_logout_inside_a_room_says_so_and_leaves() {
  Rig r;
  r.host.stored = guest();
  r.host.gid = "guest-id";
  r.controller->update_tab();
  r.h.app.show_session("ABC234");
  r.open_tab();
  CHECK(r.h.click(r.tab(), AccountTab::Logout));
  // A Guest ID cannot come back: the question says it goes, and starts on Cancel.
  CHECK(r.h.app.dialogs().text() == std::string(tr().account_logout_guest) + "\n" + tr().account_logout_in_room);
  CHECK(r.h.app.dialogs().opening_stop() == Dialogs::Cancel);
  CHECK(r.h.click(r.h.app.dialogs(), Dialogs::Confirm));
  CHECK(r.host.left_room == 1);
  CHECK(r.host.requests.empty());  // a Guest has no token to revoke
  CHECK(r.host.gid.empty());
  CHECK(r.h.app.view() == View::Prompt);

  // The session reaches Idle later, and the prompt stays; finishing it lands on Home.
  r.h.app.show_home();
  CHECK(r.h.app.view() == View::Prompt);
  CHECK(r.h.app.base_view() == View::Home);
}

void test_rejected_token_asks_to_sign_in_again() {
  Rig r;
  r.host.stored = account();
  r.controller->token_rejected();
  r.h.render();
  CHECK(r.host.stored.token.empty());
  CHECK(r.h.app.view() == View::Prompt);
  CHECK(r.prompt().hint_shown() == "로그인이 만료되었어요. 다시 로그인해 주세요.");

  Rig none;
  none.host.stored = guest();
  none.controller->token_rejected();
  CHECK(none.h.app.view() == View::Home);  // a Guest has no token to reject
}

void test_refresh_at_startup() {
  {  // The server knows better.
    Rig r;
    r.host.stored = account("옛 별명", "");
    CHECK(!r.controller->begin_first_run());
    r.host.answer(r.h, 200, R"({"username":"alice","nickname":"새 별명","handle":"cloud"})");
    CHECK(r.host.stored.nickname == "새 별명" && r.host.stored.handle == "cloud");
    CHECK(r.h.app.view() == View::Home);
  }
  {  // The token is dead.
    Rig r;
    r.host.stored = account();
    CHECK(!r.controller->begin_first_run());
    r.host.answer(r.h, 401, R"({"error":"unauthorized"})");
    CHECK(r.h.app.view() == View::Prompt);
    CHECK(r.host.stored.token.empty());
  }
  {  // The server is away: nothing changes.
    Rig r;
    r.host.stored = account();
    CHECK(!r.controller->begin_first_run());
    r.host.answer(r.h, std::unexpected(api::HttpError::Network));
    CHECK(r.h.app.view() == View::Home);
    CHECK(r.host.stored.token == "TOKEN");
    CHECK(r.host.unreachable == 1);
  }
  {  // An account with no name at all has to get one.
    Rig r;
    r.host.stored = account("", "");
    CHECK(!r.controller->begin_first_run());
    r.host.answer(r.h, 200, R"({"username":"alice"})");
    CHECK(r.h.app.view() == View::Prompt);
    CHECK(r.prompt().prompt().heading == "별명 정하기");
    CHECK(!r.prompt().prompt().cancelable);
  }
  {  // The answer arrives after the person signed out.
    Rig r;
    r.host.stored = account();
    CHECK(!r.controller->begin_first_run());
    r.host.stored = guest();
    r.host.answer(r.h, 401, R"({"error":"unauthorized"})");
    CHECK(r.h.app.view() == View::Home);
    CHECK(r.host.stored.is_guest);
  }
}

}  // namespace

// The first step's button leads to the server step, which checks the address, asks to trust it and
// switches; the new server has no account yet, so the first step comes back.
void test_the_first_run_can_choose_a_server() {
  Rig r;
  (void)r.controller->begin_first_run();
  r.h.render();
  CHECK(r.prompt().prompt().extra == "서버 바꾸기");
  CHECK(r.h.click(r.prompt(), PromptScreen::Extra));
  CHECK(r.prompt().prompt().heading == "서버");
  CHECK(r.prompt().value() == "cr.nanliburusu.com");
  CHECK(r.prompt().prompt().cancelable);

  r.prompt().set_value("");
  r.submit(L"http://packs.example.org");
  CHECK(r.host.checked == std::vector<std::string>{"http://packs.example.org"});
  CHECK(r.prompt().busy());
  r.host.server_answer(r.h, {false, "", "", "안 돼요"});
  CHECK(!r.prompt().busy());
  CHECK(r.prompt().hint_shown() == "안 돼요");
  CHECK(r.host.server == "cr.nanliburusu.com");

  r.prompt().set_value("");
  r.submit(L"cr.example.com");
  r.host.server_answer(r.h, {true, "https://cr.example.com/", "cr.example.com", ""});
  CHECK(r.h.app.dialogs().is_open());
  CHECK(r.h.click(r.h.app.dialogs(), Dialogs::Cancel));
  CHECK(r.host.server == "cr.nanliburusu.com");
  CHECK(r.prompt().prompt().heading == "서버");
  CHECK(!r.prompt().busy());

  CHECK(r.h.click(r.prompt(), PromptScreen::Submit));
  r.host.server_answer(r.h, {true, "https://cr.example.com/", "cr.example.com", ""});
  CHECK(r.h.click(r.h.app.dialogs(), Dialogs::Confirm));
  CHECK(r.host.server == "https://cr.example.com/");
  CHECK(r.h.app.view() == View::Prompt);
  CHECK(r.prompt().prompt().label == "ID");

  // Back from the server step is the first step again.
  CHECK(r.h.click(r.prompt(), PromptScreen::Extra));
  CHECK(r.h.click(r.prompt(), PromptScreen::Back));
  CHECK(r.prompt().prompt().label == "ID");
}

// From the system tab: the default server needs no trust, and the account kept for it comes back.
void test_the_system_tab_switches_servers() {
  Rig r;
  r.host.stored = account();
  r.host.kept["https://cr.example.com/"] = guest("저기");
  (void)r.controller->begin_first_run();
  r.h.render();
  SystemTab& tab = r.h.app.settings().system();
  CHECK(tab.server().current == "cr.nanliburusu.com");
  CHECK(tab.on_apply_server != nullptr);
  tab.on_apply_server("cr.example.com");
  CHECK(tab.server().state == SystemTab::ServerState::State::Checking);
  r.host.server_answer(r.h, {true, "https://cr.example.com/", "cr.example.com", ""});
  CHECK(r.h.click(r.h.app.dialogs(), Dialogs::Confirm));
  CHECK(r.host.server == "https://cr.example.com/");
  CHECK(tab.server().state == SystemTab::ServerState::State::None);
  CHECK(r.h.app.view() == View::Home);  // that server's Guest is still there
  CHECK(r.host.requests.size() == 1);  // the first account's only: a Guest has no profile to refresh

  tab.on_apply_server("");
  r.host.server_answer(r.h, {true, "", "cr.nanliburusu.com", ""});
  CHECK(!r.h.app.dialogs().is_open());
  CHECK(r.host.server == "cr.nanliburusu.com");
  CHECK(r.host.stored.token == "TOKEN");
  CHECK(r.host.requests.size() == 2);  // the account's profile, from the server it belongs to

  tab.on_apply_server("nope");
  r.host.server_answer(r.h, {false, "", "", "안 돼요"});
  CHECK(tab.server().state == SystemTab::ServerState::State::Failed);
  CHECK(tab.server().reason == "안 돼요");
}

int main() {
  test_stored_identity_goes_straight_home();
  test_register_walks_all_steps_to_home();
  test_use_handle_needs_no_nickname();
  test_login_with_lockout();
  test_network_failure_stays_on_the_step();
  test_guest_path_through_its_dialog();
  test_links_go_back_to_the_first_step();
  test_tab_shows_what_each_person_can_do();
  test_icon_goes_through_the_picker();
  test_cancel_returns_to_the_tab();
  test_changing_names_inside_a_room_does_not_leave_it();
  test_logout_from_home();
  test_logout_inside_a_room_says_so_and_leaves();
  test_rejected_token_asks_to_sign_in_again();
  test_refresh_at_startup();
  test_the_first_run_can_choose_a_server();
  test_the_system_tab_switches_servers();
  return crpp_test_result();
}
