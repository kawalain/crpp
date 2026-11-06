#pragma once
// Reports keyboard and mouse activity anywhere on the desktop, using Raw Input
// on its own thread so a busy UI cannot delay it. Nothing is observed while
// stopped. Coordinates are never read.
#include <atomic>
#include <memory>

#include "crpp/input/classifier.hpp"
#include "crpp/wire/input_frame.hpp"

namespace crpp::input {

class Sink {
 public:
  // Runs on the input thread for every event; must not block or allocate.
  virtual void on_input(wire::Kind kind) noexcept = 0;

 protected:
  ~Sink() = default;
};

class RawInputSource {
 public:
  explicit RawInputSource(Sink& sink);
  ~RawInputSource();
  RawInputSource(const RawInputSource&) = delete;
  RawInputSource& operator=(const RawInputSource&) = delete;

  // False if the OS refused. Idempotent.
  bool start();
  void stop();
  // Any thread; applies to later key presses.
  void set_privacy(Privacy privacy) noexcept { privacy_.store(privacy, std::memory_order_relaxed); }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::atomic<Privacy> privacy_{Privacy::Presence};
};

}  // namespace crpp::input
