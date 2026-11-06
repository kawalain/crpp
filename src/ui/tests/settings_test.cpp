#include <ctime>

#include "check.hpp"
#include "crpp/ui/controls.hpp"
#include "crpp/ui/app.hpp"

using namespace crpp::ui;

namespace {

void test_fade_runs_only_when_animated() {
  Ui ui;
  ui.animations = true;
  ui.now_ms = 1000;
  Fade fade;
  fade.set(ui, false, 100);
  CHECK(!fade.running(ui));  // the first state does not animate
  CHECK(fade.value(ui) == 0);
  fade.set(ui, true, 100);
  CHECK(fade.running(ui));
  ui.now_ms = 1050;
  const float middle = fade.value(ui);
  CHECK(middle > 0 && middle < 1);
  fade.set(ui, false, 100);  // turning back starts from where it is
  CHECK(fade.value(ui) == middle);
  ui.now_ms = 1200;
  CHECK(!fade.running(ui));
  CHECK(fade.value(ui) == 0);
}

void test_chat_clock() {
  const long long ms = static_cast<long long>(std::time(nullptr)) * 1000;
  const std::string twelve = chat_clock_text(ms, false), full = chat_clock_text(ms, true);
  CHECK(twelve.find("오전") != std::string::npos || twelve.find("오후") != std::string::npos);
  CHECK(full.size() == 5);
  CHECK(full[2] == ':');
}

}  // namespace

int main() {
  test_fade_runs_only_when_animated();
  test_chat_clock();
  return crpp_test_result();
}
