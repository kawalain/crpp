#include <string>

#include "check.hpp"
#include "crpp/api/flow.hpp"

using namespace crpp::api;
using Kind = Flow::Kind;

namespace {

HttpResult reply(int status, std::string body) { return Response{status, std::move(body)}; }

Identity with_token(std::string token, std::string nickname = {}, std::string handle = {}) {
  Identity id;
  id.token = std::move(token);
  id.nickname = std::move(nickname);
  id.handle = std::move(handle);
  return id;
}

void test_who_sees_the_first_run() {
  Identity none;
  CHECK(needs_first_run(none, ""));
  Identity blank;
  blank.nickname = "   ";
  CHECK(needs_first_run(blank, ""));
  Identity guest;
  guest.is_guest = true;  // chose Guest, but never named themselves
  CHECK(needs_first_run(guest, ""));
  Identity named;
  named.nickname = "구름";
  CHECK(!needs_first_run(named, ""));
  CHECK(!needs_first_run(none, "guest-id"));
  CHECK(!needs_first_run(with_token("T"), ""));
}

void test_display_name() {
  CHECK(display_name({}) == "게스트");
  CHECK(display_name(with_token("", "", "cloud")) == "@cloud");
  CHECK(display_name(with_token("", " 구름 ", "cloud")) == "구름");
}

void test_validity() {
  CHECK(step_valid(Step::Username, "abc"));
  CHECK(step_valid(Step::Username, "a.b_c9"));
  CHECK(!step_valid(Step::Username, "ab"));
  CHECK(!step_valid(Step::Username, "ABC"));
  CHECK(!step_valid(Step::Username, std::string(33, 'a')));
  CHECK(step_valid(Step::Handle, std::string(16, 'a')));
  CHECK(!step_valid(Step::Handle, std::string(17, 'a')));
  CHECK(!step_valid(Step::Password, "short1"));
  CHECK(step_valid(Step::Password, "longenough"));
  CHECK(!step_valid(Step::Password, std::string(33, 'a')));
  CHECK(!step_valid(Step::Login, ""));
  CHECK(step_valid(Step::Login, std::string(128, 'a')));
  CHECK(!step_valid(Step::Login, std::string(129, 'a')));
  CHECK(step_valid(Step::Nickname, "구름 이"));
  CHECK(!step_valid(Step::Nickname, "  "));
  CHECK(!step_valid(Step::Nickname, "a\xE2\x80\xAE" "b"));  // U+202E, which reorders text
  CHECK(!step_valid(Step::Nickname, "a\x07"));
  std::string long_name;
  for (int i = 0; i < 128; ++i) long_name += "가";
  CHECK(step_valid(Step::Nickname, long_name));
  CHECK(!nickname_length_hint(long_name).empty());
  CHECK(nickname_length_hint("가").empty());
  CHECK(!step_valid(Step::Nickname, long_name + "가"));
}

void test_new_account_walks_every_step() {
  Flow f(Identity{});
  f.start(Step::Username);

  auto next = f.submit("alice");
  CHECK(next.kind == Kind::Call && next.request.path == "api/username?name=alice");
  CHECK(f.busy());
  CHECK(f.submit("alice").kind == Kind::Stay);  // no second request while one is out
  next = f.on_response(reply(200, R"({"valid":true,"available":true})"));
  CHECK(next.kind == Kind::Show && f.step() == Step::Password && f.value().empty());

  next = f.submit("onlyletters");
  CHECK(next.kind == Kind::Error && f.error() == "영문과 숫자를 함께 넣어 주세요." && f.step() == Step::Password);
  next = f.submit("한글비밀번호12345");
  CHECK(next.kind == Kind::Error && f.error() == "비밀번호에 사용할 수 없는 문자가 있어요.");
  next = f.submit("passw0rd!");
  CHECK(next.kind == Kind::Show && f.step() == Step::PasswordConfirm && f.error().empty());

  next = f.submit("different1");
  CHECK(next.kind == Kind::Error && f.error() == "비밀번호가 일치하지 않아요.");
  next = f.submit("passw0rd!");
  CHECK(next.kind == Kind::Call && next.request.path == "api/register");
  CHECK(next.request.body == R"({"username":"alice","password":"passw0rd!"})");
  next = f.on_response(reply(200, R"({"token":"T","username":"alice"})"));
  CHECK(next.kind == Kind::Show && f.step() == Step::Handle);
  CHECK(f.identity().token == "T" && f.identity().username == "alice" && !f.identity().is_guest);
  CHECK(f.take_identity_changed());
  CHECK(!f.take_identity_changed());
  CHECK(f.take_switched());

  next = f.submit("cloud");
  CHECK(next.kind == Kind::Call && next.request.path == "api/handle?name=cloud" && next.request.token == "T");
  next = f.on_response(reply(200, R"({"valid":true,"available":true})"));
  CHECK(next.kind == Kind::Call && next.request.method == Method::Put && next.request.body == R"({"handle":"cloud"})");
  next = f.on_response(reply(200, R"({"username":"alice","handle":"cloud"})"));
  CHECK(next.kind == Kind::Show && f.step() == Step::Nickname && f.identity().handle == "cloud");

  next = f.submit(" 구름 ");
  CHECK(next.kind == Kind::Call && next.request.body == R"({"nickname":"구름"})");
  next = f.on_response(reply(200, R"({"username":"alice","nickname":"구름","handle":"cloud"})"));
  CHECK(next.kind == Kind::Done && f.identity().nickname == "구름");
}

void test_login_of_a_known_account() {
  Flow f(Identity{});
  f.start(Step::Username);
  (void)f.submit("alice");
  CHECK(f.on_response(reply(200, R"({"valid":true,"available":false})")).kind == Kind::Show);
  CHECK(f.step() == Step::Login);

  auto next = f.submit("wrong");
  CHECK(next.request.path == "api/login");
  next = f.on_response(reply(401, R"({"error":"invalid_credentials","attempts":2,"max":5})"));
  CHECK(next.kind == Kind::Error && f.error() == "비밀번호가 맞지 않아요. (2/5)" && f.value() == "wrong");

  (void)f.submit("right");
  next = f.on_response(reply(200, R"({"token":"T","username":"alice","nickname":"구름"})"));
  CHECK(next.kind == Kind::Done && f.identity().token == "T" && f.identity().nickname == "구름");
  CHECK(f.take_switched());
}

void test_login_of_an_account_without_a_name_asks_for_one() {
  Flow f(Identity{});
  f.start(Step::Login);
  (void)f.submit("right");
  CHECK(f.on_response(reply(200, R"({"token":"T","username":"alice"})")).kind == Kind::Show);
  CHECK(f.step() == Step::Nickname);
}

void test_lockout() {
  Flow f(Identity{});
  f.start(Step::Login);
  (void)f.submit("wrong");
  auto next = f.on_response(reply(429, R"({"error":"locked","retryAfter":600})"));
  CHECK(next.kind == Kind::Lock && next.lock_seconds == 600 && f.value().empty() && f.error().empty());

  (void)f.submit("wrong");
  next = f.on_response(reply(429, R"({"error":"locked"})"));
  CHECK(next.kind == Kind::Lock && next.lock_seconds == 180);

  CHECK(lock_hint(3600) == "비밀번호를 너무 많이 틀렸어요. 1시간 뒤에 다시 시도해 주세요.");
  CHECK(lock_hint(3599) == "비밀번호를 너무 많이 틀렸어요. 60분 뒤에 다시 시도해 주세요.");
  CHECK(lock_hint(61) == "비밀번호를 너무 많이 틀렸어요. 2분 뒤에 다시 시도해 주세요.");
  CHECK(lock_hint(1) == "비밀번호를 너무 많이 틀렸어요. 1분 뒤에 다시 시도해 주세요.");
  CHECK(lock_hint(0) == "비밀번호를 너무 많이 틀렸어요. 1분 뒤에 다시 시도해 주세요.");
}

void test_failures_stay_on_the_step() {
  Flow f(with_token("T"));
  f.start(Step::Handle);
  (void)f.submit("cloud");
  CHECK(f.on_response(reply(200, R"({"valid":true,"available":false})")).kind == Kind::Error);
  CHECK(f.error() == "쓸 수 없는 사용자명이에요." && !f.busy());

  (void)f.submit("cloud");
  (void)f.on_response(reply(200, R"({"valid":true,"available":true})"));
  CHECK(f.on_response(reply(409, R"({"error":"handle_taken"})")).kind == Kind::Error);
  CHECK(f.error() == "쓸 수 없는 사용자명이에요." && f.identity().handle.empty());

  (void)f.submit("cloud");
  CHECK(f.on_response(std::unexpected(HttpError::Network)).kind == Kind::Error);
  CHECK(f.error() == "요청을 처리하지 못했어요." && f.value() == "cloud");
}

void test_guest() {
  Identity old = with_token("OLD", "", "old");
  old.username = "old";
  Flow f(old);
  f.start(Step::Username);
  const auto next = f.continue_as_guest();
  CHECK(next.kind == Kind::Show && f.step() == Step::Nickname);
  CHECK(f.identity().is_guest && f.identity().token.empty() && f.identity().username.empty() && f.identity().handle.empty());
  CHECK(!f.identity().has_account());
  CHECK(f.take_switched());

  // A Guest's nickname stays on this device: no request.
  CHECK(f.submit("구름").kind == Kind::Done);
  CHECK(f.identity().nickname == "구름");
  CHECK(f.take_identity_changed());
}

void test_use_handle_clears_the_nickname() {
  Flow f(with_token("T", "구름", "cloud"));
  f.start(Step::Nickname, "구름");
  auto next = f.use_handle();
  CHECK(next.kind == Kind::Call && next.request.body == R"({"nickname":""})");
  next = f.on_response(reply(200, R"({"username":"a","handle":"cloud"})"));
  CHECK(next.kind == Kind::Done && f.identity().nickname.empty());
  CHECK(display_name(f.identity()) == "@cloud");

  Flow none(with_token("T"));
  none.start(Step::Nickname);
  CHECK(none.use_handle().kind == Kind::Stay);
}

void test_an_old_server_without_the_nickname_route() {
  Flow f(with_token("T"));
  f.start(Step::Nickname);
  (void)f.submit("구름");
  CHECK(f.on_response(reply(404, "")).kind == Kind::Done);
  CHECK(f.identity().nickname == "구름");
}

void test_cancelable_steps_end_the_flow() {
  Flow f(with_token("T", "", "old"));
  f.start(Step::Handle, "old", "", true);
  CHECK(f.cancelable());
  (void)f.submit("cloud");
  (void)f.on_response(reply(200, R"({"valid":true,"available":true})"));
  CHECK(f.on_response(reply(200, R"({"username":"a","handle":"cloud"})")).kind == Kind::Done);
  CHECK(f.identity().handle == "cloud");
}

void test_restart_forgets_what_was_typed() {
  Flow f(Identity{});
  f.start(Step::Username);
  (void)f.submit("alice");
  (void)f.on_response(reply(200, R"({"valid":true,"available":true})"));
  (void)f.submit("passw0rd!");
  CHECK(f.step() == Step::PasswordConfirm);
  CHECK(f.restart().kind == Kind::Show);
  CHECK(f.step() == Step::Username && f.value().empty());
  f.start(Step::PasswordConfirm);
  CHECK(f.submit("passw0rd!").kind == Kind::Error);  // the first entry is gone
}

}  // namespace

int main() {
  test_who_sees_the_first_run();
  test_display_name();
  test_validity();
  test_new_account_walks_every_step();
  test_login_of_a_known_account();
  test_login_of_an_account_without_a_name_asks_for_one();
  test_lockout();
  test_failures_stay_on_the_step();
  test_guest();
  test_use_handle_clears_the_nickname();
  test_an_old_server_without_the_nickname_route();
  test_cancelable_steps_end_the_flow();
  test_restart_forgets_what_was_typed();
  return crpp_test_result();
}
