#include "account_controller.hpp"

using namespace crpp::ui;

namespace crpp::gui {

using api::Flow;
using api::Step;

namespace {


}  // namespace

Prompt prompt_for(const Flow& flow) {
  const Step step = flow.step();
  const api::StepText& text = api::step_text(step);
  Prompt p;
  p.heading = text.heading;
  p.label = text.label;
  p.placeholder = text.placeholder;
  p.hint = text.hint;
  p.prefix = text.prefix;
  p.extra = text.extra;
  p.link = text.link;
  p.error = flow.error();
  p.value = flow.value();
  p.max_length = text.max_length;
  p.password = text.password;
  p.lowercase = text.lowercase;
  p.identifier = text.identifier;
  p.cancelable = flow.cancelable();
  if (step == Step::Nickname && !flow.identity().handle.empty()) p.extra2 = tr().account_use_handle;
  p.valid = [step](std::string_view value) { return api::step_valid(step, value); };
  if (step == Step::Nickname) p.hint_for = [](std::string_view value) { return api::nickname_length_hint(value); };
  p.lock_hint = [](long long seconds) { return api::lock_hint(seconds); };
  return p;
}

AccountController::AccountController(App& app, AccountHost& host) : app_(app), host_(host) {
  PromptScreen& prompt = app_.prompt();
  prompt.on_submit = [this](std::string value) { apply(flow_.submit(value)); };
  prompt.on_back = [this] { back(); };
  prompt.on_extra = [this] { random_nickname(); };
  prompt.on_extra2 = [this] { apply(flow_.use_handle()); };
  prompt.on_link = [this] { link(); };

  AccountTab& tab = app_.settings().account();
  tab.on_emoji = [this](std::string id) {
    host_.set_emoji(std::move(id));
    update_tab();
  };
  tab.on_change_nickname = [this] {
    start(Step::Nickname, host_.identity().nickname, {}, true, true);
  };
  tab.on_change_handle = [this] { start(Step::Handle, host_.identity().handle, {}, true, true); };
  tab.on_logout = [this] { logout(); };
  update_tab();
}

bool AccountController::begin_first_run() {
  const api::Identity identity = host_.identity();
  if (api::needs_first_run(identity, host_.guest_id())) {
    start(Step::Username, {}, {}, false, false);
    return true;
  }
  refresh_profile();
  return false;
}

void AccountController::refresh_profile() {
  const std::string token = host_.identity().token;
  if (token.empty()) return;
  host_.send(api::get_me(token), [this, token](const api::HttpResult& result) {
    api::Identity identity = host_.identity();
    // The person may have moved on while this was out.
    if (identity.token != token || app_.view() == View::Prompt) return;
    auto me = api::read_me(result);
    if (!me) {
      // Anything else says nothing about the account: the server may just be away.
      if (me.error().status == 401) token_rejected();
      else if (me.error().code == "network") host_.server_unreachable();
      return;
    }
    identity.username = std::move(me->username);
    identity.nickname = std::move(me->nickname);
    identity.handle = std::move(me->handle);
    host_.save_identity(identity);
    update_tab();
    if (identity.nickname.empty() && identity.handle.empty() && app_.base_view() == View::Home) {
      start(Step::Nickname, {}, {}, false, false);
    }
  });
}

void AccountController::token_rejected() {
  if (host_.identity().token.empty()) return;
  sign_out_locally();
  start(Step::Username, {}, tr().account_expired, false, false);
}

void AccountController::update_tab() {
  const api::Identity identity = host_.identity();
  app_.settings().account().set_profile(
      {identity.nickname, identity.handle, api::display_name(identity), host_.emoji(), identity.has_account()});
}

void AccountController::start(Step step, std::string value, std::string error, bool cancelable, bool from_settings) {
  from_settings_ = from_settings;
  flow_ = Flow(host_.identity());
  flow_.start(step, std::move(value), std::move(error), cancelable);
  show_step();
}

void AccountController::show_step() { app_.show_prompt(prompt_for(flow_)); }

void AccountController::apply(Flow::Next next) {
  if (flow_.take_switched()) host_.forget_seats();
  if (flow_.take_identity_changed()) {
    host_.save_identity(flow_.identity());
    update_tab();
  }
  PromptScreen& prompt = app_.prompt();
  switch (next.kind) {
    case Flow::Kind::Stay:
      break;
    case Flow::Kind::Show:
      show_step();
      break;
    case Flow::Kind::Error:
      prompt.set_busy(false);
      prompt.set_error(flow_.error());
      break;
    case Flow::Kind::Lock:
      prompt.set_busy(false);
      prompt.lock(next.lock_seconds);
      break;
    case Flow::Kind::Call:
      prompt.set_busy(true);
      host_.send(std::move(next.request), [this](const api::HttpResult& result) { apply(flow_.on_response(result)); });
      break;
    case Flow::Kind::Done:
      prompt.set_busy(false);
      from_settings_ = false;
      app_.close_prompt();
      break;
  }
}

void AccountController::back() {
  const bool to_settings = from_settings_;
  from_settings_ = false;
  app_.close_prompt();
  if (to_settings) app_.settings().open(SettingsPage::Account);
}

void AccountController::link() {
  if (flow_.step() != Step::Username) {
    apply(flow_.restart());
    return;
  }
  app_.dialogs().show({tr().account_skip_question, tr().ok, tr().cancel, false,
                       [this] { apply(flow_.continue_as_guest()); }, nullptr});
}

void AccountController::random_nickname() {
  const std::string current = app_.prompt().value();
  std::string name = host_.random_nickname();
  // One more try is enough: the word lists make thousands of names.
  if (name == current) name = host_.random_nickname();
  app_.prompt().set_value(name);
}

void AccountController::logout() {
  // A difference from upstream: it leaves the Room without saying so.
  const bool in_room = app_.base_view() == View::Session;
  app_.dialogs().show({in_room ? tr().account_logout_in_room : tr().account_logout_question, tr().account_logout, tr().cancel, false,
                       [this, in_room] {
                         if (in_room) host_.leave_room();
                         // Best effort: the token is forgotten here either way.
                         if (const std::string token = host_.identity().token; !token.empty()) {
                           host_.send(api::logout(token), [](const api::HttpResult&) {});
                         }
                         sign_out_locally();
                         start(Step::Username, {}, {}, false, false);
                       },
                       nullptr});
}

void AccountController::sign_out_locally() {
  host_.erase_personal_data();
  host_.save_identity({});
  update_tab();
}

}  // namespace crpp::gui
