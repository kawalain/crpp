#include <memory>

#include "account_controller.hpp"
#include "areas.hpp"
#include "crpp/api/winhttp_client.hpp"
#include "crpp/api/worker.hpp"
#include "crpp/transport/target.hpp"
#include "crpp/transport/winhttp_health.hpp"
#include "shell.hpp"

using namespace crpp::ui;

namespace crpp::gui::area::account {

namespace {

store::Json optional_text(const std::string& text) { return text.empty() ? store::Json{} : store::Json(text); }

// A server as the user reads it: without https://, which every one but this machine's has.
std::string shown(std::string_view normalized) {
  if (normalized.starts_with("https://")) normalized.remove_prefix(8);
  return std::string(normalized);
}

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

  [[nodiscard]] std::string random_nickname() override { return gui::random_nickname(official_files(s_), locale::current().words_folder); }

  [[nodiscard]] std::string emoji() const override { return s_.store->string_or("emoji", ""); }

  void set_emoji(std::string id) override {
    s_.store->set("emoji", store::Json(id));
    (void)s_.store->save();
    sound::tell_server_my_sounds(s_);
  }

  void send(api::Request request, std::move_only_function<void(api::HttpResult) const> done) override {
    if (!http_) http_ = std::make_shared<api::WinHttpClient>(s_.store->string_or("serverUrl", store::default_server_url));
    worker_.post([http = http_, request = std::move(request), done = std::move(done)]() mutable -> api::Worker::Done {
      return [result = http->send(request), done = std::move(done)] { done(result); };
    });
  }

  [[nodiscard]] std::string server_shown() const override { return shown(s_.store->server()); }

  void check_server(std::string typed, std::move_only_function<void(ServerCheck) const> done) override {
    const std::string fallback = store::normalize_server(store::default_server_url);
    if (typed.find_first_not_of(" \t") == std::string::npos) {
      done({true, "", shown(fallback), ""});
      return;
    }
    auto address = transport::server_address(typed);
    if (!address) {
      done({false, "", "", address.error() == transport::AddressProblem::NotSecure ? tr().server_not_secure : tr().server_invalid});
      return;
    }
    const std::string normalized = store::normalize_server(*address);
    if (normalized == fallback) {
      done({true, "", shown(fallback), ""});
      return;
    }
    // WinHTTP refuses a certificate that does not hold and, here, any redirect: either is no answer.
    worker_.post([address = *address, normalized, done = std::move(done)]() mutable -> api::Worker::Done {
      std::optional<std::string> body;
      if (const auto target = transport::make_health_target(address)) body = transport::WinHttpHealth().ask(*target);
      const bool answered = body.has_value(), cloudroom = body && transport::is_cloudroom_health(*body);
      // The wording is read on the UI thread, where the language changes.
      return [address, normalized, answered, cloudroom, done = std::move(done)] {
        ServerCheck check{cloudroom, address, shown(normalized), ""};
        if (!answered) check.problem = tr().server_unreachable;
        else if (!cloudroom) check.problem = tr().server_not_cloudroom;
        done(check);
      };
    });
  }

  void change_server(const std::string& address) override {
    if (s_.app.base_view() == View::Session) leave_room();
    store::Store& data = *s_.store;
    data.switch_server(address);
    (void)data.save();
    s_.config.server_url = data.string_or("serverUrl", store::default_server_url);
    s_.config.token = data.string_or("token", "");
    http_.reset();  // a request still out keeps its own
    const api::Identity id = identity();
    const std::string name = api::display_name(id);
    s_.app.home().set_nickname(name);
    s_.config.nickname = name;
    area::room::names_changed(s_);
    reconnect();
    connection::count_public_rooms(s_);
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
  } else if (name == "prompt-server") {
    api::Flow flow({});
    flow.start(api::Step::Server, "cr.example.com", tr().server_not_cloudroom, true);
    s.app.show_prompt(prompt_for(flow));
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
