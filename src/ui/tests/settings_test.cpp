#include <ctime>

#include "check.hpp"
#include "crpp/ui/controls.hpp"
#include "crpp/ui/app.hpp"

using namespace crpp::ui;

namespace {

void test_chat_clock() {
  const long long ms = static_cast<long long>(std::time(nullptr)) * 1000;
  const std::string twelve = chat_clock_text(ms, false), full = chat_clock_text(ms, true);
  CHECK(twelve.find("오전") != std::string::npos || twelve.find("오후") != std::string::npos);
  CHECK(full.size() == 5);
  CHECK(full[2] == ':');
}

}  // namespace

int main() {
  test_chat_clock();
  return crpp_test_result();
}
