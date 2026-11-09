#pragma once
// The chat's rules, free of drawing: the rate limit the server also enforces, and
// what counts as a link. Portable ISO C++.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace crpp::ui {

// How many messages are laid out at first; scrolling up brings this many more.
inline constexpr std::size_t chat_page = 100;

// Mirrors the server's limit (10 messages in 10 s, then 5 s of silence) so the block
// shows at once, and takes the server's word when it blocks something this let through.
class ChatLimiter {
 public:
  using Ms = unsigned long long;
  static constexpr Ms cooldown_ms = 5000, window_ms = 10000;
  static constexpr std::size_t burst = 10;

  // Whether a message may go now. If not, the attempt counts as blocked.
  bool try_send(Ms now);
  // The server blocked a message for `ms`. True if this limiter had not foreseen it,
  // so the message left the input for nothing and should come back.
  bool server_blocked(Ms now, Ms ms);
  // Expires what has run out; true if something changed.
  bool expire(Ms now);
  void reset() { *this = ChatLimiter{}; }

  [[nodiscard]] bool cooling() const { return cooling_; }
  [[nodiscard]] bool calm() const { return calm_; }
  // "진정하세요 ! ! !", one more mark for every further attempt, up to 20. From the third block in
  // a row it says so more firmly.
  [[nodiscard]] std::string calm_text() const;
  // Blocks that came one after another, each within a minute of the last one's end.
  [[nodiscard]] int blocks_in_a_row() const { return streak_; }
  static constexpr int firm_from = 3;
  static constexpr Ms in_a_row_ms = 60000;
  // When expire is next needed; 0 = never.
  [[nodiscard]] Ms wake_at() const;

 private:
  void note_blocked(Ms now, Ms ms, bool count_attempt);

  std::array<Ms, burst> sent_{};  // oldest first
  std::size_t sent_count_ = 0;
  Ms blocked_until_ = 0, calm_until_ = 0;
  bool cooling_ = false, calm_ = false;
  int local_blocks_ = 0;  // blocked here, and sent anyway; their answers are expected
  int streak_ = 0;
  Ms last_calm_end_ = 0;
  int marks_ = 3;
};

// A run of a text, in the units of its string.
struct LinkSpan {
  std::uint32_t start = 0, length = 0;
};

// The https:// links in a text. Punctuation stuck to the end is left out, and a closing
// bracket stays only if the link opened it. Plain http is not a link, on purpose.
[[nodiscard]] std::vector<LinkSpan> find_links(std::wstring_view text);

// Whether a message names someone by one of their names: anywhere in it, as Korean puts a
// particle right after a name ("크롬이님"), and with Latin letters in either case. A name of a
// single character would be found everywhere, so it does not count.
[[nodiscard]] bool mentions(std::string_view text, std::span<const std::string> names);

}  // namespace crpp::ui
