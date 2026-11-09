#pragma once
// A Room's weather, from how much its members do: the input of the whole Room over the last minute, mine
// included, per online member. Only counts go in. Pure and portable ISO C++, and nothing here allocates.
#include <array>
#include <cstddef>
#include <cstdint>

namespace crpp::ui {

class RoomWeather {
 public:
  using Ms = unsigned long long;
  enum Step : std::uint8_t { Clear, Cloudy, Rain, Storm };

  static constexpr Ms window_ms = 60000, bucket_ms = 1000;
  // A new step must stay the one the numbers say for this long before the sky follows.
  static constexpr Ms hold_ms = 10000;
  // While the sky is not clear and nothing comes in, it looks again this often, until it is.
  static constexpr Ms recheck_ms = 5000;
  // Events a minute per member from which each step begins: cloudy, rain, storm.
  static constexpr unsigned from[3] = {20, 80, 200};

  // Counts `events` that came at `now`.
  void note(Ms now, unsigned events);
  // Looks at the last minute with this many members online; true if the step changed.
  bool update(Ms now, std::size_t online);
  [[nodiscard]] Step step() const { return step_; }
  // When update() should run again though nothing comes in; 0 for never.
  [[nodiscard]] Ms wake_at() const;
  // A new Room starts clear.
  void reset() { *this = {}; }

  // What the counts of the last minute say, per member, before any holding.
  [[nodiscard]] static Step step_for(unsigned long long per_minute);

 private:
  static constexpr std::size_t buckets = window_ms / bucket_ms;
  void advance(Ms now);
  [[nodiscard]] unsigned long long total() const;

  std::array<std::uint32_t, buckets> counts_{};
  Ms second_ = 0;  // the second the newest bucket counts, from the clock's start
  bool started_ = false;
  Step step_ = Clear;
  Step pending_ = Clear;  // a step the numbers say, waiting out hold_ms
  bool waiting_ = false;
  Ms pending_since_ = 0;
  Ms last_update_ = 0;
};

}  // namespace crpp::ui
