#include "crpp/api/flow.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace crpp::api {

namespace {

// Decodes the code point at i and moves past it. A byte that does not start a whole
// sequence stands for itself.
char32_t next_code_point(std::string_view s, std::size_t& i) {
  const auto lead = static_cast<unsigned char>(s[i]);
  const std::size_t extra = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
  if (extra == 0 || i + extra >= s.size()) {
    ++i;
    return lead;
  }
  char32_t c = extra == 3 ? lead & 0x07u : extra == 2 ? lead & 0x0Fu : lead & 0x1Fu;
  for (std::size_t k = 1; k <= extra; ++k) c = (c << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
  i += extra + 1;
  return c;
}

bool is_space(char32_t c) {
  return (c >= 9 && c <= 13) || c == 32 || c == 0x85 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
         c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF;
}

std::string_view trimmed(std::string_view s) {
  while (!s.empty()) {
    std::size_t i = 0;
    if (!is_space(next_code_point(s, i))) break;
    s.remove_prefix(i);
  }
  while (!s.empty()) {
    std::size_t start = s.size() - 1;  // a code point starts at a non-continuation byte
    while (start > 0 && (static_cast<unsigned char>(s[start]) & 0xC0) == 0x80) --start;
    std::size_t i = start;
    if (!is_space(next_code_point(s, i))) break;
    s.remove_suffix(s.size() - start);
  }
  return s;
}

std::size_t code_points(std::string_view s) {
  std::size_t count = 0;
  for (std::size_t i = 0; i < s.size(); ++count) (void)next_code_point(s, i);
  return count;
}

std::size_t utf16_units(std::string_view s) {
  std::size_t count = 0;
  for (std::size_t i = 0; i < s.size();) count += next_code_point(s, i) >= 0x10000 ? 2 : 1;
  return count;
}

bool name_chars(std::string_view s) {
  return std::ranges::all_of(s, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.'; });
}

bool nickname_valid(std::string_view value) {
  const std::string_view s = trimmed(value);
  if (s.empty() || code_points(s) > 128) return false;
  for (std::size_t i = 0; i < s.size();) {
    const char32_t c = next_code_point(s, i);
    if (c <= 0x1F || (c >= 0x7F && c <= 0x9F) || (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069)) return false;
  }
  return true;
}

constexpr std::array<StepText, 6> step_texts{{
    {"", "ID", "ID 입력", "3~32자, 영문, 숫자, 밑줄, 마침표만 사용할 수 있습니다.", "", "", "계정을 만들고 싶지 않아요.", 32, false, true, false},
    {"계정 생성", "비밀번호", "비밀번호 입력", "8~32자, 영문과 숫자를 포함해야 하며 특수문자도 사용할 수 있습니다.", "", "", "← ID 다시 입력", 32, true, false, false},
    {"비밀번호 확인", "비밀번호", "비밀번호 재입력", "입력한 비밀번호를 한 번 더 입력해주세요.", "", "", "← 처음부터 다시", 32, true, false, false},
    {"로그인", "비밀번호", "비밀번호 입력", "계정 비밀번호를 입력해주세요.", "", "", "← ID 다시 입력", 128, true, false, false},
    {"사용자명 설정", "사용자명", "사용자명 입력", "3~16자, 영문, 숫자, 밑줄, 마침표만 사용할 수 있습니다.", "@", "", "", 16, false, true, true},
    {"별명 설정", "별명", "별명 입력", "별명은 중복 설정 가능합니다.", "", "무작위 설정", "", 512, false, false, false},
}};

}  // namespace

std::string display_name(const Identity& identity) {
  if (const auto nick = trimmed(identity.nickname); !nick.empty()) return std::string(nick);
  return identity.handle.empty() ? "게스트" : "@" + identity.handle;
}

bool needs_first_run(const Identity& identity, std::string_view guest_id) {
  return trimmed(identity.nickname).empty() && guest_id.empty() && identity.token.empty();
}

bool step_valid(Step step, std::string_view value) {
  switch (step) {
    case Step::Username:
      return value.size() >= 3 && value.size() <= 32 && name_chars(value);
    case Step::Password:
      return utf16_units(value) >= 8 && utf16_units(value) <= 32;
    case Step::Handle:
      return value.size() >= 3 && value.size() <= 16 && name_chars(value);
    case Step::PasswordConfirm:
    case Step::Login:
      return !value.empty() && utf16_units(value) <= 128;
    case Step::Nickname:
      return nickname_valid(value);
  }
  return false;
}

std::string nickname_length_hint(std::string_view value) {
  return code_points(trimmed(value)) >= 128 ? "별명은 최대 128자까지 설정할 수 있습니다." : "";
}

std::string failure_message(Step step, const Failure& failure) {
  const std::string_view code = failure.code;
  if (code == "invalid_credentials" && step == Step::Login && failure.attempts > 0) {
    return "비밀번호가 올바르지 않습니다. (" + std::to_string(failure.attempts) + "/" +
           std::to_string(failure.max_attempts > 0 ? failure.max_attempts : 5) + ")";
  }
  if (code == "username_taken") return "이미 사용 중인 ID입니다.";
  if (code == "invalid_username") return "ID 형식이 올바르지 않습니다.";
  if (code == "invalid_password") return "비밀번호는 8~32자의 영문, 숫자, 특수문자만 사용할 수 있어요.";
  if (code == "weak_password") return "영문과 숫자를 포함해주세요.";
  if (code == "handle_taken") return "사용할 수 없는 사용자명입니다.";
  if (code == "invalid_handle") return "사용자명 형식이 올바르지 않습니다.";
  if (code == "invalid_credentials") return "ID 또는 비밀번호가 올바르지 않습니다.";
  if (code == "rate_limited") return "요청이 너무 많습니다. 잠시 후 다시 시도해주세요.";
  if (code == "unauthorized") return "로그인이 만료되었어요. 다시 로그인해주세요.";
  return "요청을 처리하지 못했습니다.";
}

std::string lock_hint(long long seconds) {
  seconds = std::max(1LL, seconds);
  const std::string label = seconds >= 3600 ? "1시간" : std::to_string((seconds + 59) / 60) + "분";
  return "비밀번호를 너무 많이 틀렸습니다. " + label + " 뒤 다시 시도해주세요.";
}

const StepText& step_text(Step step) { return step_texts[static_cast<std::size_t>(step)]; }

void Flow::start(Step step, std::string value, std::string error_text, bool cancelable) {
  step_ = step;
  value_ = std::move(value);
  error_ = std::move(error_text);
  cancelable_ = cancelable;
  call_ = CallKind::None;
  if (step != Step::PasswordConfirm) pending_password_.clear();
}

Flow::Next Flow::call(CallKind kind, Request request) {
  call_ = kind;
  return {Kind::Call, std::move(request), 0};
}

Flow::Next Flow::show(Step step) {
  start(step);
  return {Kind::Show, {}, 0};
}

Flow::Next Flow::error(std::string text) {
  error_ = std::move(text);
  return {Kind::Error, {}, 0};
}

Flow::Next Flow::submit(std::string_view value) {
  if (busy() || !step_valid(step_, value)) return {};
  value_ = std::string(value);
  error_.clear();
  switch (step_) {
    case Step::Username:
      return call(CallKind::CheckUsername, check_username(value));
    case Step::Password: {
      const bool printable = std::ranges::all_of(value, [](char c) { return c >= 0x21 && c <= 0x7E; });
      if (!printable) return error("비밀번호에 사용할 수 없는 문자가 있어요.");
      const bool letter = std::ranges::any_of(value, [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); });
      const bool digit = std::ranges::any_of(value, [](char c) { return c >= '0' && c <= '9'; });
      if (!letter || !digit) return error("영문과 숫자를 포함해주세요.");
      pending_password_ = std::string(value);
      return show(Step::PasswordConfirm);
    }
    case Step::PasswordConfirm:
      if (value != pending_password_) return error("비밀번호가 일치하지 않아요.");
      return call(CallKind::Register, register_account(pending_username_, value));
    case Step::Login:
      return call(CallKind::Login, login(pending_username_, value));
    case Step::Handle:
      pending_value_ = std::string(value);
      return call(CallKind::CheckHandle, check_handle(identity_.token, value));
    case Step::Nickname:
      return finish_nickname(std::string(trimmed(value)));
  }
  return {};
}

Flow::Next Flow::finish_nickname(std::string nickname) {
  if (identity_.token.empty()) {
    identity_.nickname = std::move(nickname);
    identity_changed_ = true;
    return {Kind::Done, {}, 0};
  }
  Request request = set_nickname(identity_.token, nickname);
  pending_value_ = std::move(nickname);
  return call(CallKind::SetNickname, std::move(request));
}

void Flow::signed_in(Auth auth) {
  identity_ = {std::move(auth.me.username), std::move(auth.token), std::move(auth.me.nickname), std::move(auth.me.handle), false};
  identity_changed_ = true;
  switched_ = true;
  pending_password_.clear();
}

Flow::Next Flow::on_response(const HttpResult& result) {
  const CallKind kind = std::exchange(call_, CallKind::None);
  const auto failed = [&](const Failure& failure) { return error(failure_message(step_, failure)); };
  switch (kind) {
    case CallKind::None:
      return {};
    case CallKind::CheckUsername: {
      const auto answer = read_availability(result);
      if (!answer) return failed(answer.error());
      if (!answer->valid) return error("ID 형식이 올바르지 않습니다.");
      pending_username_ = value_;
      return show(answer->available ? Step::Password : Step::Login);
    }
    case CallKind::Register: {
      auto auth = read_auth(result);
      if (!auth) return failed(auth.error());
      signed_in(std::move(*auth));
      return show(Step::Handle);
    }
    case CallKind::Login: {
      auto auth = read_auth(result);
      if (!auth) {
        if (auth.error().code != "locked") return failed(auth.error());
        value_.clear();
        error_.clear();
        return {Kind::Lock, {}, auth.error().retry_after > 0 ? auth.error().retry_after : 180};
      }
      signed_in(std::move(*auth));
      if (!identity_.nickname.empty() || !identity_.handle.empty()) return {Kind::Done, {}, 0};
      return show(Step::Nickname);
    }
    case CallKind::CheckHandle: {
      const auto answer = read_availability(result);
      if (!answer) return failed(answer.error());
      if (!answer->valid) return error("사용자명 형식이 올바르지 않습니다.");
      if (!answer->available) return error("사용할 수 없는 사용자명입니다.");
      return call(CallKind::SetHandle, set_handle(identity_.token, pending_value_));
    }
    case CallKind::SetHandle: {
      const auto me = read_me(result);
      if (!me) return failed(me.error());
      identity_.handle = me->handle.empty() ? pending_value_ : me->handle;
      identity_changed_ = true;
      if (cancelable_) return {Kind::Done, {}, 0};
      return show(Step::Nickname);
    }
    case CallKind::SetNickname: {
      // A server that predates this request keeps the nickname on this device only.
      if (const auto done = read_status(result); !done && done.error().status != 404 && done.error().status != 405) {
        return failed(done.error());
      }
      identity_.nickname = pending_value_;
      identity_changed_ = true;
      return {Kind::Done, {}, 0};
    }
  }
  return {};
}

Flow::Next Flow::continue_as_guest() {
  if (busy()) return {};
  identity_.username.clear();
  identity_.token.clear();
  identity_.handle.clear();
  identity_.is_guest = true;
  identity_changed_ = true;
  switched_ = true;
  return show(Step::Nickname);
}

Flow::Next Flow::use_handle() {
  if (busy() || identity_.handle.empty()) return {};
  return finish_nickname({});
}

Flow::Next Flow::restart() {
  if (busy()) return {};
  pending_username_.clear();
  return show(Step::Username);
}

bool Flow::take_identity_changed() { return std::exchange(identity_changed_, false); }

bool Flow::take_switched() { return std::exchange(switched_, false); }

}  // namespace crpp::api
