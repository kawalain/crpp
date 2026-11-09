#include "crpp/ui/room_weather.hpp"

#include <algorithm>

namespace crpp::ui {

RoomWeather::Step RoomWeather::step_for(unsigned long long per_minute) {
  if (per_minute >= from[2]) return Storm;
  if (per_minute >= from[1]) return Rain;
  if (per_minute >= from[0]) return Cloudy;
  return Clear;
}

void RoomWeather::advance(Ms now) {
  const Ms second = now / bucket_ms;
  if (!started_) {
    started_ = true;
    second_ = second;
    return;
  }
  if (second <= second_) return;
  // Every second gone by since the newest bucket starts empty; a minute or more empties them all.
  const Ms gone = std::min<Ms>(second - second_, buckets);
  for (Ms i = 1; i <= gone; ++i) counts_[(second_ + i) % buckets] = 0;
  second_ = second;
}

unsigned long long RoomWeather::total() const {
  unsigned long long sum = 0;
  for (const auto c : counts_) sum += c;
  return sum;
}

void RoomWeather::note(Ms now, unsigned events) {
  advance(now);
  counts_[second_ % buckets] += events;
}

bool RoomWeather::update(Ms now, std::size_t online) {
  advance(now);
  last_update_ = now;
  // The window is one minute, so its total is already a rate a minute.
  const Step target = step_for(total() / std::max<std::size_t>(1, online));
  if (target == step_) {
    waiting_ = false;
    return false;
  }
  if (!waiting_ || pending_ != target) {
    pending_ = target;
    pending_since_ = now;
    waiting_ = true;
    return false;
  }
  if (now - pending_since_ < hold_ms) return false;
  step_ = target;
  waiting_ = false;
  return true;
}

RoomWeather::Ms RoomWeather::wake_at() const {
  if (waiting_) return pending_since_ + hold_ms;
  // Clear stays clear without input, and input brings its own update.
  if (step_ == Clear || !started_) return 0;
  return last_update_ + recheck_ms;
}

}  // namespace crpp::ui
