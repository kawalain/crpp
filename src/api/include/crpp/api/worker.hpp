#pragma once
// One background thread for blocking requests, started by the first one and blocked
// while there is nothing to do. Work runs in order and returns what to do with its
// result; `deliver` hands that to whichever thread should run it.
//
// Destruction does not wait: a blocked request may take as long as its timeout, and
// nothing should hold up the app's exit for it. After destruction nothing is
// delivered, and what the work captured must keep itself alive.
#include <functional>
#include <memory>

namespace crpp::api {

class Worker {
 public:
  using Done = std::move_only_function<void()>;
  using Work = std::move_only_function<Done()>;

  explicit Worker(std::move_only_function<void(Done)> deliver);
  ~Worker();
  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;

  void post(Work work);

 private:
  struct State;
  std::shared_ptr<State> state_;
};

}  // namespace crpp::api
