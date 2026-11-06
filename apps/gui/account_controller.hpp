#pragma once
// Puts the account flow on screen: the prompt, the account tab and their dialogs.
// Everything outside the UI goes through AccountHost, so this runs in a test with no
// window, store or network.
#include <functional>
#include <string>
#include <string_view>

#include "crpp/api/flow.hpp"
#include "crpp/ui/app.hpp"

namespace crpp::gui {

class AccountHost {
 public:
  [[nodiscard]] virtual api::Identity identity() const = 0;
  [[nodiscard]] virtual std::string guest_id() const = 0;
  // Keeps it, and has the app go by it from now on.
  virtual void save_identity(const api::Identity& identity) = 0;
  // Someone else signed in here: the held Seats are not theirs.
  virtual void forget_seats() = 0;
  // Signing out leaves nothing of the person: Seats, chat cache and Guest ID.
  virtual void erase_personal_data() = 0;
  // Leaves the Room this client is in.
  virtual void leave_room() = 0;
  [[nodiscard]] virtual std::string random_nickname() = 0;
  [[nodiscard]] virtual std::string emoji() const = 0;
  virtual void set_emoji(std::string id) = 0;
  // Sends off the UI thread; `done` runs on the UI thread.
  virtual void send(api::Request request, std::function<void(api::HttpResult)> done) = 0;

 protected:
  ~AccountHost() = default;
};

// What a step looks like on the prompt.
[[nodiscard]] ui::Prompt prompt_for(const api::Flow& flow);

class AccountController {
 public:
  AccountController(ui::App& app, AccountHost& host);

  // Startup: true if the prompt took over the screen. Otherwise, an account's details
  // are refreshed from the server in the background.
  [[nodiscard]] bool begin_first_run();
  void refresh_profile();
  void token_rejected();
  // The account tab shows what is stored now.
  void update_tab();

 private:
  void start(api::Step step, std::string value, std::string error, bool cancelable, bool from_settings);
  void show_step();
  void apply(api::Flow::Next next);
  void back();
  void link();
  void random_nickname();
  void logout();
  void sign_out_locally();

  ui::App& app_;
  AccountHost& host_;
  api::Flow flow_{api::Identity{}};
  bool from_settings_ = false;
};

}  // namespace crpp::gui
