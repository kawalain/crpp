#pragma once
// Turns client events into UI and store changes. The store and the UI are only
// touched on the UI thread, inside posted tasks and the methods marked so.
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "crpp/client/client.hpp"
#include "crpp/sound/engine.hpp"
#include "crpp/store/store.hpp"
#include "crpp/ui/app.hpp"

namespace crpp::gui {

class UiTasks;

// A member's profile, as the server answered it.
struct ProfileInfo {
  std::uint32_t slot = 0;
  std::string nick, emoji;
  std::optional<std::string> handle;
  std::optional<std::int64_t> created;
  std::int64_t stayed = 0;
  std::uint64_t events = 0;
};

class Bridge final : public client::Observer {
 public:
  Bridge(ui::App& app, UiTasks& tasks, store::Store& store, sound::Engine& engine)
      : app_(app), tasks_(tasks), store_(store), engine_(engine) {}

  // All of these run on the UI thread.
  // Called when the token is rejected; the client cannot go on with it.
  std::function<void()> on_unauthorized;
  // Input is observed only while in a Room.
  std::function<void(bool in_room)> on_room_changed;
  // Every connection state, for what the user is shown while reconnecting.
  std::function<void(session::State)> on_state;
  // The Room could not be entered or was lost; the app has gone back to Home.
  std::function<void(session::Failure, const std::string& detail)> on_failure;
  // The chat rate limit: nothing can be sent for this long.
  std::function<void(std::uint64_t ms)> on_chat_blocked;
  std::function<void(const ProfileInfo&)> on_profile;
  // A chat message arrived, just before it is added to the panel.
  std::function<void(const ui::ChatEntry&)> on_chat;
  // The member list changed, just before the screen is updated.
  std::function<void(const std::vector<ui::MemberView>&)> on_members;

  // UI thread: remember which held Seat a resume is for, to forget it if refused.
  void expect_resume(std::string code) { resume_code_ = std::move(code); }

  // UI thread: a Room has been entered and not yet ended.
  [[nodiscard]] bool in_room() const { return !room_code_.empty(); }

  // UI thread: whether a message can be sent at all right now.
  [[nodiscard]] bool can_send() const { return in_room_; }

  // UI thread: the Session takes a command to enter a Room only when idle. What it
  // would ignore must not change what the user is told or which Seat is expected.
  [[nodiscard]] bool idle() const { return state_ == session::State::Idle; }
  void entering() { state_ = session::State::Connecting; }

  // UI thread: the user leaves. The Seat stays held for a while.
  void leaving();

  void on_status(session::State s) override;
  void on_deliver(const wire::ServerMessage& m) override;
  void on_peer_input(std::span<const wire::ServerEvent> events) override;
  void on_failed(session::Failure reason, std::string_view detail) override;
  void on_persist(const session::Persistent& p) override;

 private:
  void enter(const std::string& code);

  ui::App& app_;
  UiTasks& tasks_;
  store::Store& store_;
  sound::Engine& engine_;
  std::atomic<std::uint32_t> my_slot_{0xFFFFFFFF};
  std::uint32_t me_ = 0;
  bool is_public_ = false;
  bool keep_seat_ = false;
  bool in_room_ = false;
  std::string room_code_;
  std::string resume_code_;
  session::State state_ = session::State::Idle;
};

}  // namespace crpp::gui
