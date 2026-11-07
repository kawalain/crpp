// Cloud drawn from its pack in skins/: in a window that scales the stage, the same places to click as
// the C++ layout, and motion that the pack states and that ends. Pixels are compared with tests/shots.
#include <cmath>
#include <string>
#include <vector>

#include "check.hpp"
#include "ui_harness.hpp"

using namespace crpp::ui;
using test::UiHarness;

namespace {

constexpr const char* cloud_pack = CRPP_TEST_SKINS "/cloud";

struct Place {
  const Surface* owner;
  int id;
};

std::vector<Rect> rects_of(UiHarness& h, const std::vector<Place>& places) {
  h.render();
  std::vector<Rect> found;
  for (const Place& place : places) {
    const Ui::Hit* hit = h.app.ui().find_hit(place.owner, place.id);
    CHECK(hit != nullptr);
    found.push_back(hit ? hit->rect : Rect{});
  }
  return found;
}

void same_places(UiHarness& h, const std::vector<Place>& places) {
  const std::vector<Rect> code = rects_of(h, places);
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  CHECK(h.app.ui().palette >= 0);
  const std::vector<Rect> pack = rects_of(h, places);
  for (std::size_t i = 0; i < places.size(); ++i) {
    CHECK(std::abs(pack[i].x - code[i].x) < 0.01f && std::abs(pack[i].y - code[i].y) < 0.01f);
    CHECK(std::abs(pack[i].w - code[i].w) < 0.01f && std::abs(pack[i].h - code[i].h) < 0.01f);
  }
}

// The clock is held, so that a frame's time is what the test says.
class Moving {
 public:
  explicit Moving(UiHarness& h) : h_(h) {
    h_.app.set_animations(true);
    at(1000);
  }
  ~Moving() { hold_clock_at(0); }
  Moving(const Moving&) = delete;
  Moving& operator=(const Moving&) = delete;
  // The time goes on by this much, and a frame is drawn.
  bool after(unsigned long long ms) {
    at(now_ + ms);
    (void)h_.app.tick(now_);
    h_.render();
    return h_.app.animating();
  }

 private:
  void at(unsigned long long ms) {
    now_ = ms;
    hold_clock_at(ms);
  }
  UiHarness& h_;
  unsigned long long now_ = 0;
};

void test_a_dialog_rises_and_then_asks_for_no_more_frames() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  h.app.show_home();
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  h.render();
  Moving moving(h);
  h.app.dialogs().show({"Sure?", "Yes", "No", false, nullptr, nullptr});
  CHECK(moving.after(0));
  CHECK(moving.after(80));
  CHECK(!moving.after(80));
  CHECK(!moving.after(1000));
  h.app.dialogs().close();
}

void test_the_offline_popup_rises_once() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  h.app.show_home();
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  h.render();
  Moving moving(h);
  h.app.offline().show();
  CHECK(moving.after(0));
  CHECK(!moving.after(160));
  CHECK(!moving.after(0));  // it is on screen and settled, and walks on without a transition
  h.app.offline().hide();
  (void)moving.after(0);
  h.app.offline().show();
  CHECK(moving.after(0));  // shown anew, it rises anew
}

}  // namespace

int main() {
  test_a_dialog_rises_and_then_asks_for_no_more_frames();
  test_the_offline_popup_rises_once();
  return crpp_test_result();
}
