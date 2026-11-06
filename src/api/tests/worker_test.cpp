#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "check.hpp"
#include "crpp/api/worker.hpp"

using namespace crpp::api;
using namespace std::chrono_literals;

namespace {

void test_runs_in_order_off_the_calling_thread() {
  std::mutex m;
  std::condition_variable cv;
  std::vector<int> delivered;
  std::atomic<bool> off_thread{true};
  const auto caller = std::this_thread::get_id();
  {
    Worker worker([&](Worker::Done done) {
      done();
      cv.notify_all();
    });
    for (int i = 0; i < 3; ++i) {
      worker.post([&, i]() -> Worker::Done {
        if (std::this_thread::get_id() == caller) off_thread = false;
        return [&, i] {
          std::lock_guard lock(m);
          delivered.push_back(i);
        };
      });
    }
    std::unique_lock lock(m);
    CHECK(cv.wait_for(lock, 5s, [&] { return delivered.size() == 3; }));
  }
  CHECK(off_thread);
  CHECK((delivered == std::vector<int>{0, 1, 2}));
}

void test_destruction_neither_waits_nor_delivers() {
  auto release = std::make_shared<std::atomic<bool>>(false);
  auto finished = std::make_shared<std::atomic<bool>>(false);
  auto delivered = std::make_shared<std::atomic<bool>>(false);
  auto started = std::make_shared<std::atomic<bool>>(false);
  const auto wait_for = [](const std::atomic<bool>& flag) {
    const auto give_up = std::chrono::steady_clock::now() + 5s;
    while (!flag && std::chrono::steady_clock::now() < give_up) std::this_thread::sleep_for(1ms);
    return flag.load();
  };
  {
    Worker worker([delivered](Worker::Done) { *delivered = true; });
    worker.post([release, finished, started]() -> Worker::Done {
      *started = true;
      while (!*release) std::this_thread::sleep_for(1ms);
      *finished = true;
      return [] {};
    });
    CHECK(wait_for(*started));
  }  // returns while the work is still blocked
  CHECK(!*finished);
  *release = true;
  CHECK(wait_for(*finished));
  std::this_thread::sleep_for(20ms);
  CHECK(!*delivered);
}

}  // namespace

int main() {
  test_runs_in_order_off_the_calling_thread();
  test_destruction_neither_waits_nor_delivers();
  return crpp_test_result();
}
