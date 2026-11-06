#pragma once
// The account prompt's steps: which one follows which, what each one accepts, and
// what the server's answers mean for them. Pure: it asks for requests and is told
// their results, and the caller does the showing and the sending.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "crpp/api/api.hpp"

namespace crpp::api {

struct Identity {
  std::string username, token, nickname, handle;
  bool is_guest = false;

  [[nodiscard]] bool has_account() const { return !is_guest && !token.empty(); }
};

// The name others see: the nickname, else "@handle", else "게스트".
[[nodiscard]] std::string display_name(const Identity& identity);

// Who sees the first-run prompt: only someone with no stored identity at all. A
// saved nickname, Guest ID or token goes straight to Home.
[[nodiscard]] bool needs_first_run(const Identity& identity, std::string_view guest_id);

enum class Step : std::uint8_t { Username, Password, PasswordConfirm, Login, Handle, Nickname };

// Whether the value could be submitted at this step (the arrow lights up).
[[nodiscard]] bool step_valid(Step step, std::string_view value);
// Replaces the nickname step's hint at the length limit; empty below it.
[[nodiscard]] std::string nickname_length_hint(std::string_view value);
// What the prompt shows for a failed request at this step.
[[nodiscard]] std::string failure_message(Step step, const Failure& failure);
// "비밀번호를 너무 많이 틀렸습니다. ..." for this many seconds left.
[[nodiscard]] std::string lock_hint(long long seconds);

// The wording and input rules of one step.
struct StepText {
  const char* heading;
  const char* label;
  const char* placeholder;
  const char* hint;
  const char* prefix;
  const char* extra;  // the button on the right under the field
  const char* link;   // the link below that
  std::size_t max_length;
  bool password;
  bool lowercase;   // typed letters become lowercase
  bool identifier;  // only a-z, 0-9, '_' and '.' can be typed
};
[[nodiscard]] const StepText& step_text(Step step);

class Flow {
 public:
  explicit Flow(Identity identity) : identity_(std::move(identity)) {}

  enum class Kind : std::uint8_t {
    Stay,   // nothing to show; the prompt stays as it is
    Show,   // the current step changed: show step(), value() and error()
    Error,  // stay on this step, with error()
    Lock,   // login is locked for lock_seconds; the field empties
    Call,   // send `request` and pass its result to on_response
    Done,   // the flow is over
  };
  struct Next {
    Kind kind = Kind::Stay;
    Request request;       // Call
    int lock_seconds = 0;  // Lock
  };

  // Enters a step. A cancelable one has a way back and ends the flow on success.
  void start(Step step, std::string value = {}, std::string error = {}, bool cancelable = false);

  [[nodiscard]] Next submit(std::string_view value);
  [[nodiscard]] Next on_response(const HttpResult& result);
  // "확인" in the dialog of "계정을 만들고 싶지 않아요.": on as a Guest.
  [[nodiscard]] Next continue_as_guest();
  // "사용자명 사용": no nickname, so others see the handle.
  [[nodiscard]] Next use_handle();
  // The link under the card on the steps that have one: back to the first step.
  [[nodiscard]] Next restart();

  [[nodiscard]] Step step() const { return step_; }
  [[nodiscard]] const std::string& value() const { return value_; }
  [[nodiscard]] const std::string& error() const { return error_; }
  [[nodiscard]] bool cancelable() const { return cancelable_; }
  [[nodiscard]] bool busy() const { return call_ != CallKind::None; }
  [[nodiscard]] const Identity& identity() const { return identity_; }

  // Each is true once after the identity changed and must be saved; `switched` means
  // it is another person now, whose held Seats these are not.
  [[nodiscard]] bool take_identity_changed();
  [[nodiscard]] bool take_switched();

 private:
  enum class CallKind : std::uint8_t { None, CheckUsername, Register, Login, CheckHandle, SetHandle, SetNickname };

  Next call(CallKind kind, Request request);
  Next show(Step step);
  Next error(std::string text);
  Next finish_nickname(std::string nickname);
  void signed_in(Auth auth);

  Identity identity_;
  Step step_ = Step::Username;
  std::string value_, error_;
  bool cancelable_ = false;
  CallKind call_ = CallKind::None;
  std::string pending_username_, pending_password_, pending_value_;
  bool identity_changed_ = false, switched_ = false;
};

}  // namespace crpp::api
