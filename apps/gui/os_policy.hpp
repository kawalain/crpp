#pragma once
// What the OS integration decides, apart from the calls that carry it out. Pure.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace crpp::gui::os_policy {

// Hiding without an icon would leave no way back to the window, so without one a
// close exits. That also holds where there is no taskbar, as on a hidden desktop.
[[nodiscard]] constexpr bool close_hides(bool minimize_to_tray, bool icon_present) {
  return minimize_to_tray && icon_present;
}

// --minimized: the same rule; otherwise the taskbar button is the way back.
[[nodiscard]] constexpr bool start_hidden(bool minimize_to_tray, bool icon_present) {
  return close_hides(minimize_to_tray, icon_present);
}

// The Room stays connected while the window is away, and input timing keeps being
// shared. Said once, the first time it can surprise someone.
[[nodiscard]] constexpr bool hide_needs_hint(bool in_room, bool already_shown) { return in_room && !already_shown; }

// A shown window has its render resources; hidden and minimized ones do not.
[[nodiscard]] constexpr bool window_hidden(bool visible, bool iconic) { return !visible || iconic; }

struct ChatFacts {
  bool mine = false;
  bool muted = false;  // the sender, by this user's member volume
  bool hidden = false;
  bool focused = false;
  bool tray_notify = true;
  bool taskbar_flash = true;
  bool flashing = false;  // a flash from an earlier message is still running
};

struct ChatReaction {
  bool sound = false;
  bool notify = false;
  bool flash = false;
};

[[nodiscard]] constexpr ChatReaction chat_reaction(const ChatFacts& c) {
  if (c.mine) return {};
  const bool away = c.hidden || !c.focused;
  return {.sound = !c.muted, .notify = away && c.tray_notify && !c.muted, .flash = away && c.taskbar_flash && !c.flashing};
}

// Who a member is across list updates.
[[nodiscard]] inline std::string member_id(std::string_view cid, std::uint32_t slot) {
  return cid.empty() ? "slot-" + std::to_string(slot) : std::string(cid);
}

struct MemberFacts {
  std::string id;
  std::uint32_t slot = 0;
  bool reconnected = false;
  bool muted = false;
};

// Whether the new member list holds someone worth the enter sound. The first list of
// a Room announces nobody: entering has its own rule.
[[nodiscard]] inline bool someone_entered(const std::vector<std::string>& known, const std::vector<MemberFacts>& next,
                                          std::uint32_t me) {
  if (known.empty()) return false;
  return std::ranges::any_of(next, [&](const MemberFacts& m) {
    return m.slot != me && !m.reconnected && !m.muted && std::ranges::find(known, m.id) == known.end();
  });
}

// How much of `text` fits a fixed buffer of `capacity` UTF-16 units with its null,
// without cutting a surrogate pair in two.
[[nodiscard]] constexpr std::size_t fit_utf16(std::wstring_view text, std::size_t capacity) {
  if (capacity == 0) return 0;
  std::size_t n = std::min(text.size(), capacity - 1);
  if (n > 0 && n < text.size() && text[n - 1] >= 0xD800 && text[n - 1] <= 0xDBFF) --n;
  return n;
}

}  // namespace crpp::gui::os_policy
