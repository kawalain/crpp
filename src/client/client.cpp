#include "crpp/client/client.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <random>
#include <thread>
#include <utility>
#include <vector>

#include "crpp/transport/target.hpp"

namespace crpp::client {

namespace {

// One queued command or transport event, owning its data.
struct Item {
  enum class Type : std::uint8_t {
    Create, Join, JoinRandom, Resume, Leave, SetSounds, Chat, Profile, Nick, Visibility,
    ChatEnable, ChatDisable, SetGid,
    Connected, ConnectFailed, Closed, Text, Binary, Heard,
  };
  Type type{};
  std::uint32_t generation = 0;  // transport events only
  bool flag_a = false;
  bool flag_b = false;
  std::uint32_t number = 0;
  std::uint8_t present = 0;  // bit i: string i of `s` is present (optionals)
  std::array<std::string, 4> s;
  std::vector<std::uint8_t> bytes;
};

std::optional<std::string_view> opt(const Item& i, std::size_t k) {
  if ((i.present >> k & 1) == 0) return std::nullopt;
  return std::string_view(i.s[k]);
}

void put(Item& i, std::size_t k, const std::optional<std::string_view>& v) {
  if (!v) return;
  i.s[k] = std::string(*v);
  i.present = static_cast<std::uint8_t>(i.present | 1u << k);
}

}  // namespace

struct Client::Impl final : transport::Sink {
  Impl(transport::Transport& t, Config c, Observer& o)
      : transport(t), config(std::move(c)), observer(o) {
    const transport::Capabilities caps = transport.capabilities();
    if (!caps.can_ping) config.timing.ping_interval = std::chrono::milliseconds{0};
    if (!caps.reports_control_frames) config.timing.liveness_timeout = std::chrono::milliseconds{0};
    session = std::make_unique<session::Session>(config.timing, session::Jitter{&next_jitter, this});
  }

  // Driver thread only: Session asks from inside on().
  static double next_jitter(void* self) noexcept {
    auto& random = static_cast<Impl*>(self)->random;
    return static_cast<double>(random() - random.min()) / static_cast<double>(random.max() - random.min() + 1);
  }

  // ---- mailbox ----
  void push(Item item) {
    {
      std::lock_guard lock(mutex);
      queue.push_back(std::move(item));
    }
    cv.notify_one();
  }
  Item command(Item::Type type) {
    Item i;
    i.type = type;
    return i;
  }
  Item event(Item::Type type) {
    Item i;
    i.type = type;
    i.generation = generation.load();
    return i;
  }

  // ---- transport::Sink (transport thread) ----
  void connected() override { push(event(Item::Type::Connected)); }
  void connect_failed(bool unauthorized) override {
    Item i = event(Item::Type::ConnectFailed);
    i.flag_a = unauthorized;
    push(std::move(i));
  }
  void closed() override { push(event(Item::Type::Closed)); }
  void text(std::string_view json) override {
    Item i = event(Item::Type::Text);
    i.s[0] = std::string(json);
    push(std::move(i));
  }
  void binary(std::span<const std::uint8_t> b) override {
    Item i = event(Item::Type::Binary);
    i.bytes.assign(b.begin(), b.end());
    push(std::move(i));
  }
  void heard() override { push(event(Item::Type::Heard)); }

  // ---- driver thread ----
  void run() {
    std::deque<Item> batch;
    std::array<wire::Kind, max_pending_inputs> kinds{};
    for (;;) {
      std::size_t kind_count = 0;
      {
        std::unique_lock lock(mutex);
        const auto ready = [&] { return !queue.empty() || input_count > 0 || stopping; };
        if (!ready()) {
          if (const auto deadline = session->deadline()) {
            cv.wait_until(lock, *deadline, ready);
          } else {
            cv.wait(lock, ready);
          }
        }
        if (stopping) return;
        batch.swap(queue);
        kinds = inputs;
        kind_count = input_count;
        input_count = 0;
      }
      for (Item& item : batch) handle(item);
      batch.clear();
      for (std::size_t i = 0; i < kind_count; ++i) apply(session::event::Input{kinds[i]});
      if (const auto deadline = session->deadline(); deadline && session::Clock::now() >= *deadline) {
        apply(session::event::Tick{});
      }
    }
  }

  void handle(const Item& i) {
    using T = Item::Type;
    switch (i.type) {
      case T::Create: apply(session::event::Create{i.flag_a, i.flag_b}); return;
      case T::Join: apply(session::event::Join{i.s[0]}); return;
      case T::JoinRandom: apply(session::event::JoinRandom{}); return;
      case T::Resume: apply(session::event::Resume{i.s[0], i.s[1], opt(i, 2), i.flag_a}); return;
      case T::Leave: apply(session::event::Leave{}); return;
      case T::SetSounds:
        apply(session::event::SetSounds{opt(i, 0), opt(i, 1), opt(i, 2), opt(i, 3)});
        return;
      case T::Chat: apply(session::event::Forward{wire::cmd::Chat{i.s[0]}}); return;
      case T::Profile: apply(session::event::Forward{wire::cmd::Profile{i.number}}); return;
      case T::Nick: apply(session::event::Forward{wire::cmd::Nick{i.s[0]}}); return;
      case T::Visibility: apply(session::event::Forward{wire::cmd::Visibility{i.flag_a}}); return;
      case T::ChatEnable: apply(session::event::Forward{wire::cmd::ChatEnable{}}); return;
      case T::ChatDisable: apply(session::event::Forward{wire::cmd::ChatDisable{}}); return;
      case T::SetGid: session->set_gid(i.s[0]); return;
      default: break;
    }
    if (i.generation != generation.load()) return;  // from a connection we already closed
    switch (i.type) {
      case T::Connected: apply(session::event::Connected{}); return;
      case T::ConnectFailed: apply(session::event::ConnectFailed{i.flag_a}); return;
      case T::Closed: apply(session::event::Closed{}); return;
      case T::Text: apply(session::event::Text{i.s[0]}); return;
      case T::Binary: apply(session::event::Binary{i.bytes}); return;
      case T::Heard: apply(session::event::Heard{}); return;
      default: return;
    }
  }

  void apply(const session::Event& event) {
    for (const session::Action& action : session->on(event, session::Clock::now())) {
      perform(action);
    }
  }

  void perform(const session::Action& action) {
    using namespace session;
    if (std::holds_alternative<action::Connect>(action)) {
      connect();
    } else if (const auto* send = std::get_if<action::Send>(&action)) {
      std::array<char, wire::max_text_frame_bytes> out;
      if (const auto n = wire::encode_client_message(send->message, out)) {
        transport.send_text(std::string_view(out.data(), *n));
      }
    } else if (const auto* in = std::get_if<action::SendInput>(&action)) {
      const std::array<wire::Kind, 1> kinds{in->kind};
      std::array<std::uint8_t, 1> out;
      if (wire::encode_client_frame(kinds, out)) transport.send_binary(out);
    } else if (std::holds_alternative<action::Ping>(action)) {
      transport.ping();
    } else if (std::holds_alternative<action::Close>(action)) {
      ++generation;  // whatever is still queued from this connection is stale
      transport.close();
    } else if (const auto* status = std::get_if<action::Status>(&action)) {
      observer.on_status(status->state);
    } else if (const auto* deliver = std::get_if<action::Deliver>(&action)) {
      observer.on_deliver(deliver->message);
    } else if (const auto* peer = std::get_if<action::PeerInput>(&action)) {
      observer.on_peer_input(peer->events);
    } else if (const auto* failed = std::get_if<action::Failed>(&action)) {
      observer.on_failed(failed->reason, failed->detail);
    } else if (std::holds_alternative<action::Persist>(action)) {
      observer.on_persist(session->persistent());
    }
  }

  void connect() {
    const auto target = transport::make_target(config.server_url, config.nickname);
    ++generation;  // events of earlier connections are now stale
    if (!target) {
      push(event(Item::Type::ConnectFailed));
      return;
    }
    transport.open(*target, config.token, *this);
  }

  transport::Transport& transport;
  Config config;
  Observer& observer;
  std::unique_ptr<session::Session> session;
  std::minstd_rand random{std::random_device{}()};

  std::mutex mutex;
  std::condition_variable cv;
  std::deque<Item> queue;
  // Input takes no heap: it comes from the input thread on every key press.
  static constexpr std::size_t max_pending_inputs = 32;
  std::array<wire::Kind, max_pending_inputs> inputs{};
  std::size_t input_count = 0;
  bool stopping = false;
  std::atomic<std::uint32_t> generation{0};
  std::thread thread;
};

Client::Client(transport::Transport& transport, Config config, Observer& observer)
    : impl_(std::make_unique<Impl>(transport, std::move(config), observer)) {}

Client::~Client() { stop(); }

void Client::start() {
  if (impl_->thread.joinable()) return;
  {
    std::lock_guard lock(impl_->mutex);
    impl_->stopping = false;
  }
  impl_->thread = std::thread([this] { impl_->run(); });
}

void Client::stop() {
  {
    std::lock_guard lock(impl_->mutex);
    impl_->stopping = true;
  }
  impl_->cv.notify_all();
  if (impl_->thread.joinable()) impl_->thread.join();
  impl_->transport.close();
}

void Client::create(bool is_public, bool chat) {
  Item i = impl_->command(Item::Type::Create);
  i.flag_a = is_public;
  i.flag_b = chat;
  impl_->push(std::move(i));
}
void Client::join(std::string_view code) {
  Item i = impl_->command(Item::Type::Join);
  i.s[0] = std::string(code);
  impl_->push(std::move(i));
}
void Client::join_random() { impl_->push(impl_->command(Item::Type::JoinRandom)); }
void Client::resume(std::string_view code, std::string_view key, std::optional<std::string_view> ticket,
                    bool fallback_to_join) {
  Item i = impl_->command(Item::Type::Resume);
  i.s[0] = std::string(code);
  i.s[1] = std::string(key);
  put(i, 2, ticket);
  i.flag_a = fallback_to_join;
  impl_->push(std::move(i));
}
void Client::leave() { impl_->push(impl_->command(Item::Type::Leave)); }
void Client::set_sounds(const session::event::SetSounds& sounds) {
  Item i = impl_->command(Item::Type::SetSounds);
  put(i, 0, sounds.key);
  put(i, 1, sounds.mouse);
  put(i, 2, sounds.pen);
  put(i, 3, sounds.emoji);
  impl_->push(std::move(i));
}
void Client::input(wire::Kind kind) {
  {
    std::lock_guard lock(impl_->mutex);
    // A full list means the driver is far behind; Session would drop these anyway.
    if (impl_->input_count == impl_->inputs.size()) return;
    impl_->inputs[impl_->input_count++] = kind;
  }
  impl_->cv.notify_one();
}
void Client::send_chat(std::string_view text) {
  Item i = impl_->command(Item::Type::Chat);
  i.s[0] = std::string(text);
  impl_->push(std::move(i));
}
void Client::request_profile(std::uint32_t slot) {
  Item i = impl_->command(Item::Type::Profile);
  i.number = slot;
  impl_->push(std::move(i));
}
void Client::set_nick(std::string_view nick) {
  Item i = impl_->command(Item::Type::Nick);
  i.s[0] = std::string(nick);
  impl_->push(std::move(i));
}
void Client::set_visibility(bool is_public) {
  Item i = impl_->command(Item::Type::Visibility);
  i.flag_a = is_public;
  impl_->push(std::move(i));
}
void Client::set_chat_enabled(bool enabled) {
  impl_->push(impl_->command(enabled ? Item::Type::ChatEnable : Item::Type::ChatDisable));
}
void Client::restore_gid(std::string_view gid) {
  Item i = impl_->command(Item::Type::SetGid);
  i.s[0] = std::string(gid);
  impl_->push(std::move(i));
}

}  // namespace crpp::client
