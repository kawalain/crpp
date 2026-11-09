#include <memory>

#include "account_controller.hpp"
#include "areas.hpp"
#include "crpp/api/winhttp_client.hpp"
#include "crpp/api/worker.hpp"
#include "shell.hpp"

using namespace crpp::ui;

namespace crpp::gui::area::account {

namespace {

store::Json optional_text(const std::string& text) { return text.empty() ? store::Json{} : store::Json(text); }

class Host final : public AccountHost {
 public:
  explicit Host(Shell& s) : s_(s), worker_([&s](api::Worker::Done done) { s.tasks.post(std::move(done)); }) {}

  [[nodiscard]] api::Identity identity() const override {
    const store::Store& data = *s_.store;
    return {data.string_or("username", ""), data.string_or("token", ""), data.string_or("nickname", ""),
            data.string_or("handle", ""), data.bool_or("isGuest", false)};
  }

  [[nodiscard]] std::string guest_id() const override { return s_.store->string_or("gid", ""); }

  void save_identity(const api::Identity& id) override {
    store::Store& data = *s_.store;
    data.set("username", optional_text(id.username));
    data.set("token", optional_text(id.token));
    data.set("nickname", store::Json(id.nickname));
    data.set("handle", optional_text(id.handle));
    data.set("isGuest", store::Json(id.is_guest));
    (void)data.save();
    const std::string name = api::display_name(id);
    s_.app.home().set_nickname(name);
    s_.config.nickname = name;
    area::room::names_changed(s_);
    if (s_.client) s_.client->set_nick(name);
    if (id.token != s_.config.token) {
      s_.config.token = id.token;
      reconnect();
    }
  }

  void forget_seats() override {
    s_.store->forget_all_seats();
    s_.store->clear_resume();
    (void)s_.store->save();
  }

  void erase_personal_data() override {
    forget_seats();
    s_.store->clear_chat();
    s_.store->set("gid", store::Json{});
    (void)s_.store->save();
  }

  void server_unreachable() override { connection::server_unreachable(s_); }
  void leave_room() override {
    s_.bridge->leaving();
    s_.client->leave();
    leaving_ = true;
  }

  [[nodiscard]] std::string random_nickname() override { return gui::random_nickname(s_.assets_dir / locale::current().words_folder); }

  [[nodiscard]] std::string emoji() const override { return s_.store->string_or("emoji", ""); }

  void set_emoji(std::string id) override {
    s_.store->set("emoji", store::Json(id));
    (void)s_.store->save();
    sound::tell_server_my_sounds(s_);
  }

  void send(api::Request request, std::function<void(api::HttpResult)> done) override {
    if (!http_) http_ = std::make_shared<api::WinHttpClient>(s_.store->string_or("serverUrl", store::default_server_url));
    worker_.post([http = http_, request = std::move(request), done = std::move(done)]() -> api::Worker::Done {
      return [result = http->send(request), done] { done(result); };
    });
  }

  // The session came to rest: a leave that was waiting for that has gone out.
  void session_idle() {
    leaving_ = false;
    if (std::exchange(reconnect_after_leave_, false)) reconnect();
  }

 private:
  // The token is fixed for a Client's life. Stopping it at once would drop a leave
  // that has not gone out, and the Room would see this member go offline instead.
  void reconnect() {
    if (!s_.client) return;  // startup has not got that far: it uses the new token
    if (leaving_) {
      reconnect_after_leave_ = true;
      return;
    }
    s_.input->stop();  // it calls into the client being replaced
    s_.client->stop();
    start_client(s_);
    if (!s_.smoke_path.empty()) s_.client->create(false, false);
  }

  Shell& s_;
  std::shared_ptr<api::WinHttpClient> http_;  // shared with a request that outlives the app's exit
  bool leaving_ = false, reconnect_after_leave_ = false;
  api::Worker worker_;
};

struct State {
  explicit State(Shell& s) : host(s), controller(s.app, host) {}
  Host host;
  AccountController controller;
};

State& state(Shell& s) { return *static_cast<State*>(s.account.get()); }

}  // namespace

void wire(Shell& s) {
  s.account = std::make_shared<State>(s);
  Shell* sp = &s;
  // Chained: the connection area, wired earlier, has its own use for this.
  s.bridge->on_state = [sp, earlier = std::move(s.bridge->on_state)](session::State st) {
    if (earlier) earlier(st);
    if (st == session::State::Idle) state(*sp).host.session_idle();
  };
}

bool scene(Shell& s, std::string_view name) {
  const bool dark = name.ends_with("-dark");
  if (dark) name.remove_suffix(5);
  const auto show = [&](api::Step step, api::Identity identity, std::string value, std::string error) {
    api::Flow flow(std::move(identity));
    flow.start(step, std::move(value), std::move(error));
    s.app.show_prompt(prompt_for(flow));
  };
  api::Identity account;
  account.token = "t";
  account.handle = "cloud";
  if (name == "prompt-id") {
    show(api::Step::Username, {}, "", "");
  } else if (name == "prompt-password") {
    show(api::Step::Password, {}, "passw0rd", "");
  } else if (name == "prompt-nickname") {
    show(api::Step::Nickname, account, "크롬이", "");
  } else if (name == "prompt-error") {
    show(api::Step::Login, {}, "wrong", locale::fill(tr().account_wrong_password, {"2", "5"}));
  } else if (name == "prompt-locked") {
    show(api::Step::Login, {}, "", "");
    s.app.prompt().lock(600);
  } else if (name == "prompt-handle") {
    show(api::Step::Handle, account, "cloud", "");
  } else if (name == "settings-account" || name == "settings-account-guest") {
    const bool guest = name == "settings-account-guest";
    s.app.settings().account().set_profile(
        {"크롬이", guest ? "" : "cloud", "크롬이", "1f431", !guest});
    s.app.settings().open(SettingsPage::Account);
  } else {
    return false;
  }
  if (dark) s.app.set_theme("darkcloud");
  return true;
}

bool begin_first_run(Shell& s) {
  if (!s.smoke_path.empty()) return false;  // a smoke run goes straight into a Room
  return state(s).controller.begin_first_run();
}

void token_rejected(Shell& s) { state(s).controller.token_rejected(); }

}  // namespace crpp::gui::area::account
