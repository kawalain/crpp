#include "crpp/api/worker.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace crpp::api {

struct Worker::State {
  std::move_only_function<void(Done)> deliver;
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<Work> queue;
  bool started = false;
  bool stopped = false;
};

Worker::Worker(std::move_only_function<void(Done)> deliver) : state_(std::make_shared<State>()) {
  state_->deliver = std::move(deliver);
}

Worker::~Worker() {
  {
    std::lock_guard lock(state_->mutex);
    state_->stopped = true;
    state_->queue.clear();
  }
  state_->cv.notify_all();
}

void Worker::post(Work work) {
  {
    std::lock_guard lock(state_->mutex);
    state_->queue.push_back(std::move(work));
    if (!state_->started) {
      state_->started = true;
      std::thread([state = state_] {
        for (;;) {
          Work next;
          {
            std::unique_lock waiting(state->mutex);
            state->cv.wait(waiting, [&] { return state->stopped || !state->queue.empty(); });
            if (state->stopped) return;
            next = std::move(state->queue.front());
            state->queue.pop_front();
          }
          Done done = next();
          // Under the lock, so the destructor returning means no delivery is under way.
          std::lock_guard delivering(state->mutex);
          if (state->stopped) return;
          if (done) state->deliver(std::move(done));
        }
      }).detach();
    }
  }
  state_->cv.notify_all();
}

}  // namespace crpp::api
