#pragma once
// Whether the user counts as away, decided at the moments something can have changed,
// so nothing has to poll: the next check is due when the limit can first be reached,
// and while away only input ends it. Times are 32-bit millisecond ticks that wrap.
#include <cstdint>

namespace crpp::sound {

class IdleWatch {
 public:
  struct Step {
    bool idle = false;
    std::uint32_t check_after_ms = 0;  // 0: no check is needed
  };

  // A check came due, or the settings changed. `last_input` is what the OS says; it
  // may be slightly ahead of `now`, and may lag behind input already given to input().
  [[nodiscard]] Step check(std::uint32_t now, std::uint32_t last_input, std::uint32_t limit_ms) noexcept {
    std::uint32_t quiet = quiet_since(now, last_input);
    if (have_input_) {
      const std::uint32_t seen = quiet_since(now, last_seen_);
      if (seen < quiet) quiet = seen;
    }
    idle_ = quiet >= limit_ms;
    return {idle_, idle_ ? 0 : limit_ms - quiet};
  }

  // Input was seen, now.
  [[nodiscard]] Step input(std::uint32_t now, std::uint32_t limit_ms) noexcept {
    last_seen_ = now;
    have_input_ = true;
    idle_ = false;
    return {false, limit_ms};
  }

  void reset() noexcept {
    idle_ = false;
    have_input_ = false;
  }

  [[nodiscard]] bool idle() const noexcept { return idle_; }

 private:
  [[nodiscard]] static std::uint32_t quiet_since(std::uint32_t now, std::uint32_t then) noexcept {
    const auto elapsed = static_cast<std::int32_t>(now - then);
    return elapsed > 0 ? static_cast<std::uint32_t>(elapsed) : 0;
  }

  bool idle_ = false, have_input_ = false;
  std::uint32_t last_seen_ = 0;
};

}  // namespace crpp::sound
