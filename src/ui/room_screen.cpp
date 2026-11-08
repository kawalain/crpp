#include "crpp/ui/room_screen.hpp"

#include <windows.h>

#include <algorithm>

#include "crpp/ui/emoji_picker.hpp"

namespace crpp::ui {

RoomScreen::RoomScreen(Ui& ui) : Surface(ui), chat_panel_(ui), profile_(ui), menu_(ui) {
  profile_.on_request = [this](std::uint32_t slot) {
    if (on_request_profile) on_request_profile(slot);
  };
  profile_.on_change_icon = [this] { change_icon(); };
  menu_.on_profile = [this](std::uint32_t slot) { profile_.request(slot); };
  menu_.on_volume = [this](std::uint32_t slot, int percent) { set_member_volume(slot, percent); };
}

void RoomScreen::enter_room(std::string room_code) {
  room_code_ = std::move(room_code);
  options_open_ = mute_popup_open_ = false;
}

void RoomScreen::reset() {
  members_.clear();
  came_later_.clear();
  members_scroll_ = {};
  chat_panel_.clear();
  options_open_ = mute_popup_open_ = false;
  pulses_.clear();
  copy_until_ = hint_muted_until_ = 0;
  lock_pending_ = false;
  blocked_id_ = -1;
  close_member_popups();
  member_volumes_.clear();
  // A question about this Room means nothing once it is gone.
  if (dialog_open_) ui_.dialogs->close();
  dialog_open_ = false;
}

void RoomScreen::close_member_popups() {
  // The picker is modal over the card, so an open card means it is the card's.
  if (profile_.is_open()) ui_.emoji_picker->close();
  profile_.close();
  menu_.close();
}

void RoomScreen::set_room(std::vector<MemberView> members, std::uint32_t me, std::optional<std::uint32_t> host,
                          bool is_public, bool chat, std::uint64_t chat_lock_ms) {
  // Those already here at the first list are not arriving; everyone after it is.
  if (!members_.empty() && ui_.animations) {
    for (const MemberView& member : members) {
      const auto same = [&](const MemberView& known) { return known.slot == member.slot && known.cid == member.cid; };
      if (std::ranges::none_of(members_, same)) {
        std::erase_if(came_later_, [&](const auto& c) { return c.first == member.slot; });
        came_later_.emplace_back(member.slot, ++arrivals_counted_);
      }
    }
  }
  members_ = std::move(members);
  std::erase_if(came_later_, [&](const auto& c) { return std::ranges::find(members_, c.first, &MemberView::slot) == members_.end(); });
  me_ = me;
  host_ = host;
  public_ = is_public;
  chat_ = chat;
  chat_lock_at_ = clock_ms() + chat_lock_ms;
  lock_pending_ = chat && chat_lock_ms > 0;
}

float RoomScreen::Pulse::level(unsigned long long now, bool animations) const {
  if (!animations) return now < until ? 1.0f : 0.0f;
  const auto risen = [&](unsigned long long at) {
    return rise_from + (1 - rise_from) * ease(static_cast<float>(at - std::min(at, start)) / fade_ms);
  };
  if (now < until) return risen(now);
  return risen(until) * (1 - ease(static_cast<float>(now - until) / fade_ms));
}

bool RoomScreen::Pulse::moving(unsigned long long now, bool animations) const {
  if (!animations) return false;
  return now < start + static_cast<unsigned long long>(fade_ms) || now >= until;
}

void RoomScreen::pulse(std::uint32_t slot) {
  // Nothing shows for someone muted (.mute-others .on::before), so nothing has to be drawn.
  if (ui_.prefs && ui_.prefs->get(pref::mute_others)) return;
  const auto member = std::ranges::find(members_, slot, &MemberView::slot);
  if (member != members_.end() && slot != me_ && member_volume(member->cid) == 0) return;
  const auto now = clock_ms();
  const auto until = now + static_cast<unsigned long long>(ui_.motion.speaking_hold_ms);
  const float fade_ms = ui_.motion.speaking_fade_ms;
  for (auto& p : pulses_) {
    if (p.slot != slot) continue;
    // Still up, it stays up with nothing to draw; on its way down, it turns back.
    if (now < p.until) {
      p.until = until;
    } else {
      p = {slot, p.level(now, ui_.animations), now, until, true, fade_ms};
      new_pulse_ = true;
    }
    return;
  }
  pulses_.push_back({slot, 0, now, until, true, fade_ms});
  new_pulse_ = true;
}

float RoomScreen::speaking_level(std::uint32_t slot) const {
  for (const auto& p : pulses_) {
    if (p.slot == slot) return p.level(ui_.now_ms, ui_.animations);
  }
  return 0;
}

void RoomScreen::add_area(Rect& area, std::uint32_t slot) const {
  for (const auto& [member, avatar] : member_discs_) {
    if (member != slot) continue;
    if (area.w <= 0) {
      area = avatar;
    } else {
      const float right = std::max(area.right(), avatar.right()), bottom = std::max(area.bottom(), avatar.bottom());
      area.x = std::min(area.x, avatar.x);
      area.y = std::min(area.y, avatar.y);
      area.w = right - area.x;
      area.h = bottom - area.y;
    }
  }
}

std::optional<Rect> RoomScreen::only_moving_area() const {
  if (needs_full_ || others_moving()) return std::nullopt;
  Rect area = ended_area_;
  for (const auto& p : pulses_) add_area(area, p.slot);
  if (area.w <= 0) return std::nullopt;
  return area;
}

void RoomScreen::show_profile(ProfileView profile) {
  const bool mine = profile.slot == me_;
  profile_.answer(std::move(profile), mine);
}

void RoomScreen::change_icon() {
  const std::string& emoji = profile_.profile().emoji;
  ui_.emoji_picker->show(emoji.empty() ? "1f4a7" : emoji, [this](std::string id) {
    profile_.set_emoji(id);
    if (on_pick_emoji) on_pick_emoji(id);
  });
}

int RoomScreen::member_volume(const std::string& cid) const {
  const auto it = std::ranges::find(member_volumes_, cid, [](const auto& v) -> const std::string& { return v.first; });
  return it == member_volumes_.end() ? 100 : it->second;
}

void RoomScreen::set_member_volume(std::uint32_t slot, int percent) {
  const auto member = std::ranges::find(members_, slot, &MemberView::slot);
  if (member == members_.end()) return;
  const int before = member_volume(member->cid);
  std::erase_if(member_volumes_, [&](const auto& v) { return v.first == member->cid; });
  if (percent != 100) member_volumes_.emplace_back(member->cid, percent);
  if (on_member_volume) on_member_volume(member->cid, percent);
  if ((before == 0) != (percent == 0) && on_member_muted) on_member_muted(member->cid, percent == 0);
}

bool RoomScreen::chat_locked() const { return chat_ && clock_ms() >= chat_lock_at_; }

bool RoomScreen::others_moving() const {
  return tip_.animating(ui_) || copy_tip_.animating(ui_) || profile_.animating() ||
         menu_.animating() || (chat_ && chat_panel_.animating());
}

bool RoomScreen::animating() const {
  return others_moving() || std::ranges::any_of(pulses_, [&](const Pulse& p) { return p.moving(ui_.now_ms, ui_.animations); });
}

bool RoomScreen::tick(unsigned long long now_ms) {
  // These change only the avatars, so they do not ask for the whole window.
  bool pulse_moved = false;
  std::erase_if(pulses_, [&](Pulse& p) {
    const auto end = p.until + (ui_.animations ? static_cast<unsigned long long>(p.fade_ms) : 0);
    if (now_ms >= end) {
      add_area(ended_area_, p.slot);
      pulse_moved = true;
      return true;
    }
    // Reaching full strength and starting to fall both still need a frame.
    const bool moving = p.moving(now_ms, ui_.animations);
    if (moving || p.was_moving) pulse_moved = true;
    p.was_moving = moving;
    return false;
  });
  bool changed = false;
  if (tip_.animating(ui_) || copy_tip_.animating(ui_) || menu_.animating()) changed = true;
  changed = profile_.tick(now_ms) || changed;
  // Not drawn without chat, so its link tip would wait for ever for a frame that shows the hover.
  if (chat_) changed = chat_panel_.tick(now_ms) || changed;
  // The chat switch locks 5 minutes after chat was turned on.
  if (lock_pending_ && now_ms >= chat_lock_at_) {
    lock_pending_ = false;
    changed = true;
  }
  // A waiting copy tip or muted hint: one repaint when its time comes.
  if (copy_until_ != 0 && now_ms >= copy_until_) {
    copy_until_ = 0;
    changed = true;
  }
  if (hint_muted_until_ != 0 && now_ms >= hint_muted_until_) {
    hint_muted_until_ = 0;
    changed = true;
  }
  needs_full_ = needs_full_ || changed;
  return changed || pulse_moved;
}

unsigned long long RoomScreen::wake_at() const {
  unsigned long long at = 0;
  for (const auto wake : {copy_until_, hint_muted_until_, lock_pending_ ? chat_lock_at_ : 0ULL, profile_.wake_at(),
                          chat_panel_.wake_at()}) {
    if (wake != 0 && (at == 0 || wake < at)) at = wake;
  }
  // A held disc draws nothing until its fall begins.
  for (const auto& p : pulses_) {
    if (at == 0 || p.until < at) at = p.until;
  }
  return at;
}

void RoomScreen::paint(Painter& p) {
  if (const skin::Screen* screen = ui_.pack_screen("room")) paint_pack(p, *screen);
}

void RoomScreen::paint_overlay(Painter& p) {
  p.set_base(D2D1::Matrix3x2F::Identity());
  if (ui_.scales_stage()) profile_.paint(p);  // otherwise it has a window of its own
  menu_.paint(p);
}

// .switch.pinned: a small bar across the knob.
std::optional<HoverTip::Spec> RoomScreen::tip_for_hover() const {
  if (ui_.hover_owner != this) return std::nullopt;
  const int id = ui_.hover_id;
  if ((id == Mute || id == More) && !ui_.scales_stage()) return std::nullopt;  // they say it themselves
  HoverTip::Spec spec;
  spec.shakes = blocked_times_;
  if (id == Leave && controls_shown_) {
    spec.text = tr().room_tip_leave;
    spec.anchor = fab_rects_[0];
    spec.place = HoverTip::Place::Left;
  } else if (id == Mute && controls_shown_ && !mute_popup_open_) {
    spec.text = ui_.prefs->get(pref::mute_others) ? tr().unmute : tr().mute;
    spec.anchor = fab_rects_[1];
  } else if (id == More && controls_shown_ && !options_open_) {
    spec.text = tr().room_tip_more;
    spec.anchor = fab_rects_[2];
    spec.place = HoverTip::Place::Right;
  } else if (id == OptCode && options_open_ && hint_muted_until_ == 0) {
    spec.text = tr().room_tip_copy;
    spec.anchor = code_rect_;
    spec.place = HoverTip::Place::Right;
  } else if ((id == OptPublic || id == OptChat) && options_open_) {
    const Rect sw = id == OptPublic ? public_rect_ : chat_rect_;
    spec.place = HoverTip::Place::Right;
    if (!is_host()) {
      // Above the whole row (.host-tip).
      spec.text = tr().room_tip_host_only;
      spec.look = HoverTip::Look::Small;
      spec.anchor = {sw.x, sw.y - 7, sw.w, sw.h + 14};
      return spec;
    }
    // An off switch has nothing to pin (.switch:has(input:not(:checked)) > .pin-tip).
    if (!(id == OptPublic ? public_ : chat_)) return std::nullopt;
    spec.anchor = sw;
    if (blocked_id_ == id) {
      spec.text = blocked_text_;
    } else if (id == OptChat && chat_locked()) {
      spec.text = tr().room_tip_chat_locked;
    } else {
      spec.text = ui_.prefs->get(id == OptPublic ? pref::pin_public : pref::pin_chat)
                      ? tr().room_tip_unpin
                      : tr().room_tip_pin;
    }
  } else if (id >= MemberBase) {
    const auto slot = static_cast<std::uint32_t>(id - MemberBase);
    const auto member = std::ranges::find(members_, slot, &MemberView::slot);
    const auto rect = std::ranges::find(member_rects_, slot, [](const auto& r) { return r.first; });
    if (member == members_.end() || rect == member_rects_.end()) return std::nullopt;
    if (host_ && *host_ == slot) spec.text = tr().room_tip_host;
    if (member->offline) spec.text += (spec.text.empty() ? std::string{} : " · ") + std::string(tr().room_tip_offline);
    if (spec.text.empty()) return std::nullopt;
    // .member > .tip: 6 from the left, its bottom 2 inside the row.
    spec.anchor = {rect->second.x + 6, rect->second.y, rect->second.w, rect->second.h};
    spec.place = HoverTip::Place::Left;
    spec.look = HoverTip::Look::Member;
    spec.gap = -2;
  } else {
    return std::nullopt;
  }
  return spec;
}

void RoomScreen::paint_tips(Painter& p) {
  // A refusal stays on the switch until the pointer leaves it.
  if (blocked_id_ >= 0 && !ui_.hot(this, blocked_id_)) blocked_id_ = -1;
  const auto want = tip_for_hover();
  tip_.paint(ui_, p, want ? &*want : nullptr);
  HoverTip::Spec copied;
  const bool copying = options_open_ && copy_until_ != 0;
  if (copying) {
    copied.text = copy_text_;
    copied.anchor = code_rect_;
    copied.place = HoverTip::Place::Right;
    copied.look = HoverTip::Look::Copied;
  }
  copy_tip_.paint(ui_, p, copying ? &copied : nullptr);
}

void RoomScreen::set_volume_from(float x) {
  volume_ = slider_value(volume_track_, x, 0, 100);
  if (on_volume) on_volume(volume_);
}

void RoomScreen::copy_code() {
  const bool copied = on_copy && on_copy(room_code_);
  copy_text_ = copied ? tr().room_copied : tr().room_copy_failed;
  const auto now = clock_ms();
  copy_until_ = now + 1000;
  // Right after copying the hint stays away, even under the cursor.
  hint_muted_until_ = now + 5000;
}

void RoomScreen::ask(Dialog dialog) {
  dialog_open_ = true;
  dialog.on_confirm = [this, then = std::move(dialog.on_confirm)] {
    dialog_open_ = false;
    if (then) then();
  };
  dialog.on_cancel = [this, then = std::move(dialog.on_cancel)] {
    dialog_open_ = false;
    if (then) then();
  };
  ui_.dialogs->show(std::move(dialog));
}

void RoomScreen::ask_leave() {
  options_open_ = false;
  // A leaving Host hands the Room to someone, if anyone else is there.
  const bool others = std::ranges::any_of(members_, [&](const MemberView& m) { return m.slot != me_; });
  const bool delegates = is_host() && others;
  ask({delegates ? tr().room_leave_delegates : tr().room_leave_question, tr().room_leave, tr().cancel,
       delegates,
       [this] {
         if (on_leave) on_leave();
       },
       nullptr});
}

// The public and chat notices show once each. The first answer to either brings the
// tip about pinning, once.
void RoomScreen::notice_answered(pref::Bool seen) {
  ui_.prefs->set(seen, true);
  if (ui_.prefs->get(pref::pin_tip_seen)) return;
  ui_.prefs->set(pref::pin_tip_seen, true);
  ask({tr().room_pin_notice, tr().ok, "", false, nullptr, nullptr});
}

void RoomScreen::block_switch(int id, const char* text) {
  blocked_id_ = id;
  blocked_text_ = text;
  blocked_at_ = clock_ms();
  ++blocked_times_;
}

void RoomScreen::toggle_public() {
  if (!is_host()) return;
  if (public_ && ui_.prefs->get(pref::pin_public)) {
    block_switch(OptPublic, tr().room_tip_pin_blocked);
    return;
  }
  if (!public_ && !ui_.prefs->get(pref::public_notice_seen)) {
    ask({tr().room_public_notice, tr().room_public_confirm, tr().cancel, false,
         [this] {
           notice_answered(pref::public_notice_seen);
           if (on_set_public) on_set_public(true);
         },
         [this] { notice_answered(pref::public_notice_seen); }});
    return;
  }
  if (on_set_public) on_set_public(!public_);
}

void RoomScreen::toggle_chat() {
  if (!is_host()) return;
  if (chat_locked()) {
    block_switch(OptChat, tr().room_tip_chat_locked);
    return;
  }
  if (chat_ && ui_.prefs->get(pref::pin_chat)) {
    block_switch(OptChat, tr().room_tip_pin_blocked);
    return;
  }
  if (!chat_ && !ui_.prefs->get(pref::chat_notice_seen)) {
    ask({tr().room_chat_notice,
         tr().room_chat_confirm, tr().cancel, true,
         [this] {
           notice_answered(pref::chat_notice_seen);
           if (on_set_chat) on_set_chat(true);
         },
         [this] { notice_answered(pref::chat_notice_seen); }});
    return;
  }
  if (on_set_chat) on_set_chat(!chat_);
}

// Pinned, a new Room starts with this on. Off is the default, so only an on switch
// can be pinned; anyone can pin, and any pin can be removed.
void RoomScreen::toggle_pin(pref::Bool key, bool on) {
  const bool pinned = ui_.prefs->get(key);
  if (!pinned && !on) return;
  ui_.prefs->set(key, !pinned);
}

void RoomScreen::before_click(const Surface* owner, int id) {
  menu_.before_click(owner, id);
  // Popups close when anything outside them is clicked, except a notice's buttons,
  // which leave the switches in view.
  const bool own = owner == this;
  const bool in_options = own && (id == Popup || id == OptCode || id == OptPublic || id == OptChat || id == More);
  const bool in_mute = own && (id == Popup || id == VolumeSlider || id == Mute);
  if (options_open_ && !in_options && owner != ui_.dialogs) options_open_ = false;
  if (mute_popup_open_ && !in_mute) mute_popup_open_ = false;
}

void RoomScreen::click(int id, float x, float y, bool) {
  const auto hold_thumb = [&] {
    ui_.capture = this;
    members_held_ = true;
    members_grab_y_ = y;
    members_grab_offset_ = members_scroll_.offset;
  };
  switch (id) {
    case MembersBar:
      if (const Rect thumb = members_scroll_.thumb(members_track_); y >= thumb.y && y < thumb.bottom()) hold_thumb();
      else members_scroll_.step_towards(members_track_, y);
      break;
    case MembersThumb:
      hold_thumb();
      break;
    case MembersUp:
    case MembersDown:
    case MembersTrack:
      members_scroll_.step_towards(members_track_, y);
      break;
    case Leave:
      ask_leave();
      break;
    case More:
      options_open_ = !options_open_;
      mute_popup_open_ = false;
      break;
    case Mute:
      ui_.prefs->set(pref::mute_others, !ui_.prefs->get(pref::mute_others));
      if (on_mute) on_mute(ui_.prefs->get(pref::mute_others));
      break;
    case OptCode:
      copy_code();
      break;
    case OptPublic:
      toggle_public();
      break;
    case OptChat:
      toggle_chat();
      break;
    case VolumeSlider:
      ui_.capture = this;
      set_volume_from(x);
      break;
    default:
      if (id >= MemberBase) profile_.request(static_cast<std::uint32_t>(id - MemberBase));
      break;
  }
}

bool RoomScreen::right_click(int id, float x, float y) {
  if (id >= MemberBase) {
    const auto slot = static_cast<std::uint32_t>(id - MemberBase);
    const auto member = std::ranges::find(members_, slot, &MemberView::slot);
    if (member == members_.end()) return false;
    options_open_ = false;
    menu_.open(slot, slot == me_, member_volume(member->cid), x, y);
    return true;
  }
  if (id == OptPublic || id == OptChat) {
    toggle_pin(id == OptPublic ? pref::pin_public : pref::pin_chat, id == OptPublic ? public_ : chat_);
    return true;
  }
  // Right-clicking the speaker opens the volume popup (main.js contextmenu).
  if (id != Mute) return false;
  mute_popup_open_ = !mute_popup_open_;
  options_open_ = false;
  return true;
}

void RoomScreen::drag(float x, float y) {
  if (members_held_) {
    const float travel = std::max(1.0f, members_track_.h - members_scroll_.thumb(members_track_).h);
    members_scroll_.jump_to(members_grab_offset_ + (y - members_grab_y_) * members_scroll_.max_offset() / travel);
    return;
  }
  set_volume_from(x);
}

bool RoomScreen::escape() {
  if (menu_.escape()) return true;
  if (mute_popup_open_) {
    mute_popup_open_ = false;
    return true;
  }
  return profile_.escape();
}

bool RoomScreen::wheel(float x, float y, float delta) {
  if (profile_.is_open() && ui_.scales_stage()) return true;  // modal
  if (chat_ && chat_panel_.wheel(x, y, delta)) return true;
  if (!members_area_.contains(x, y) || members_scroll_.max_offset() <= 0) return false;
  members_scroll_.wheel(ui_, delta);
  return true;
}

Cursor RoomScreen::cursor(int id) const { return id == Popup ? Cursor::Arrow : Cursor::Hand; }

}  // namespace crpp::ui
