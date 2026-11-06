#include "bridge.hpp"

#include <vector>

#include "shell.hpp"

using namespace crpp::ui;

namespace crpp::gui {

void Bridge::leaving() {
  const auto now = unix_ms();
  if (const auto resume = store_.resume()) store_.remember_seat(resume->code, resume->key, is_public_, true, now);
  store_.clear_resume();
  store_.mark_left(room_code_, now);
  room_code_.clear();
  if (on_room_changed) on_room_changed(false);
  (void)store_.save();
}

void Bridge::on_status(session::State s) {
  tasks_.post([this, s] {
    in_room_ = s == session::State::InRoom;
    state_ = s;
    if (on_state) on_state(s);
    if (s != session::State::Idle) return;
    const auto now = unix_ms();
    if (keep_seat_) {
      if (const auto resume = store_.resume()) store_.remember_seat(resume->code, resume->key, is_public_, false, now);
    }
    // Still set here means the Room ended without the user leaving.
    store_.mark_left(room_code_, now);
    room_code_.clear();
    store_.clear_resume();
    keep_seat_ = false;
    resume_code_.clear();
    if (on_room_changed) on_room_changed(false);
    app_.show_home();
    (void)store_.save();
  });
}

void Bridge::on_deliver(const wire::ServerMessage& m) {
  const wire::RoomState* room = nullptr;
  std::string code;
  std::uint32_t me = me_;
  if (const auto* j = std::get_if<wire::msg::Joined>(&m)) {
    room = &j->room;
    code = std::string(j->code);
    me = j->me;
  } else if (const auto* u = std::get_if<wire::msg::Members>(&m)) {
    room = &u->room;
  }
  if (const auto* c = std::get_if<wire::msg::Chat>(&m)) {
    ChatEntry entry{c->slot, std::string(c->cid), std::string(c->nick), c->emoji ? std::string(*c->emoji) : std::string{},
                    std::string(c->text), c->time};
    tasks_.post([this, entry = std::move(entry), slot = c->slot]() mutable {
      store_.record_chat(room_code_, {slot, entry.cid, entry.nick, entry.emoji, entry.text, entry.time_ms});
      if (on_chat) on_chat(entry);
      app_.room().chat().add(std::move(entry));
    });
    return;
  }
  if (const auto* b = std::get_if<wire::msg::ChatBlocked>(&m)) {
    tasks_.post([this, ms = b->ms] {
      if (on_chat_blocked) on_chat_blocked(ms);
    });
    return;
  }
  if (const auto* f = std::get_if<wire::msg::Profile>(&m)) {
    ProfileInfo info{f->slot,
                     std::string(f->nick),
                     f->emoji ? std::string(*f->emoji) : std::string{},
                     f->handle ? std::optional<std::string>(std::string(*f->handle)) : std::nullopt,
                     f->created,
                     f->stayed,
                     f->events};
    tasks_.post([this, info = std::move(info)] {
      if (on_profile) on_profile(info);
    });
    return;
  }
  if (!room) return;
  my_slot_.store(me, std::memory_order_relaxed);
  std::vector<MemberView> members;
  std::vector<sound::Member> sounds;
  std::vector<MemberIds> ids;
  for (const auto& info : room->members) {
    members.push_back({info.slot, std::string(info.nick), info.emoji ? std::string(*info.emoji) : std::string{},
                       info.offline, std::string(info.cid)});
    sounds.push_back({info.slot, std::string(info.key.value_or("")), std::string(info.mouse.value_or("")),
                      std::string(info.pen.value_or("")), std::string(info.cid)});
    ids.push_back({std::string(info.cid), std::string(info.uid.value_or(""))});
  }
  engine_.set_members(std::move(sounds));
  const auto host = room->host;
  const bool is_public = room->is_public, chat = room->chat;
  const std::uint64_t chat_lock = room->chat_lock.value_or(0);
  tasks_.post([=, this, members = std::move(members), ids = std::move(ids), code = std::move(code)]() mutable {
    me_ = me;
    is_public_ = is_public;
    if (!code.empty()) enter(code);
    if (on_member_ids) on_member_ids(ids);
    if (on_members) on_members(members);
    app_.room().set_room(std::move(members), me, host, is_public, chat, chat_lock);
  });
}

void Bridge::on_peer_input(std::span<const wire::ServerEvent> events) {
  std::vector<std::uint32_t> slots;
  const std::uint32_t me = my_slot_.load(std::memory_order_relaxed);
  for (const auto& e : events) {
    slots.push_back(e.slot);
    if (e.slot != me) engine_.play_peer(e.slot, e.kind);
  }
  tasks_.post([this, slots = std::move(slots)] {
    for (const auto s : slots) app_.room().pulse(s);
  });
}

void Bridge::on_failed(session::Failure reason, std::string_view detail) {
  tasks_.post([this, reason, detail = std::string(detail)] {
    state_ = session::State::Idle;  // a refused command reports no state of its own
    switch (reason) {
      case session::Failure::Unauthorized:
        if (on_unauthorized) on_unauthorized();
        [[fallthrough]];
      case session::Failure::ConnectFailed:
        keep_seat_ = true;  // the server may still hold it
        break;
      default:
        if (!resume_code_.empty()) store_.forget_seat(resume_code_);
        break;
    }
    app_.show_home();
    if (on_failure) on_failure(reason, detail);
  });
}

void Bridge::on_persist(const session::Persistent& p) {
  tasks_.post([this, code = std::string(p.code), key = std::string(p.key), ticket = std::string(p.ticket),
               gid = std::string(p.gid)] {
    if (!gid.empty()) store_.set("gid", store::Json(gid));
    // Ignored outside the entered Room, so a late message cannot undo leaving.
    if (!key.empty() && code == room_code_) store_.set_resume({code, key, ticket, std::nullopt});
    (void)store_.save();
  });
}

void Bridge::enter(const std::string& code) {
  const bool same_room = code == room_code_;  // a reconnect keeps what is on screen
  room_code_ = code;
  resume_code_.clear();
  keep_seat_ = false;
  store_.forget_seat(code);
  app_.show_session(code);
  if (same_room) return;
  if (on_room_changed) on_room_changed(true);
  app_.room().chat().clear();
  for (auto& r : store_.enter_room(code)) {
    app_.room().chat().add({r.slot.value_or(0), std::move(r.cid), std::move(r.nick), std::move(r.emoji), std::move(r.text), r.time_ms});
  }
}

}  // namespace crpp::gui
