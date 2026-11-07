// What drawing from a Skin Pack costs against the C++ layout, on one offscreen target. Not part of
// a normal build: cmake --build build-msvc --target crpp_ui_skin_bench, then run it with a pack's
// folder. It stays until the C++ layouts are gone, for measuring each step of docs/plans/skin-packs.md.
#include <algorithm>
#include <cstdio>
#include <vector>

#include "crpp/skin/pack.hpp"
#include "ui_harness.hpp"

#include <psapi.h>  // after windows.h

using namespace crpp::ui;
namespace skin = crpp::skin;

namespace {

double seconds() {
  LARGE_INTEGER counter, frequency;
  QueryPerformanceCounter(&counter);
  QueryPerformanceFrequency(&frequency);
  return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

double median(std::vector<double>& values) {
  std::ranges::sort(values);
  return values[values.size() / 2];
}

std::size_t private_bytes() {
  PROCESS_MEMORY_COUNTERS_EX counters{};
  GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof counters);
  return counters.PrivateUsage;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: crpp_ui_skin_bench <pack folder>\n");
    return 2;
  }
  test::UiHarness h;
  if (!h.start(300, 560)) return 1;
  h.app.set_theme("luna_blue");
  h.app.show_home();
  h.app.home().set_nickname("Me");
  h.app.home().held_seats = [] { return std::vector<std::string>{"ABC234", "XYZ789"}; };
  h.app.home().held_seat_ms = []() -> std::optional<std::uint64_t> { return 60000; };
  for (int i = 0; i < 50; ++i) h.render();

  const std::size_t before = private_bytes();
  const double load_start = seconds();
  if (!h.app.load_skin_dir(argv[1])) return 1;
  const double first_load = seconds() - load_start;
  const double paint_start = seconds();
  h.render();
  const double first_paint = seconds() - paint_start;
  const std::size_t after = private_bytes();
  std::printf("first load %.2f ms, first paint %.2f ms, private bytes +%zu KB\n", first_load * 1e3, first_paint * 1e3, (after - before) / 1024);

  std::vector<double> loads;
  for (int i = 0; i < 50; ++i) {
    const double start = seconds();
    if (!h.app.load_skin_dir(argv[1])) return 1;
    loads.push_back(seconds() - start);
  }
  std::printf("load median %.2f ms\n", median(loads) * 1e3);

  const skin::Pack& pack = *h.app.ui().pack;
  std::printf("nodes %zu (%zu bytes each), instructions %zu, paints %zu, stops %zu\n", pack.nodes.size(), sizeof(skin::Node), pack.code.size(), pack.paints.size(), pack.stops.size());
  const std::size_t bytes = pack.nodes.capacity() * sizeof(skin::Node) + pack.code.capacity() * sizeof(skin::Instr) + pack.styles.capacity() * sizeof(skin::Style) +
                            pack.texts.capacity() * sizeof(skin::TextRef) + pack.literals.capacity() + pack.parts.capacity() * sizeof(skin::Part) + pack.uses.capacity() * sizeof(skin::Use) +
                            pack.arguments.capacity() * sizeof(skin::Argument) + pack.fields.capacity() * sizeof(skin::FieldNode) + pack.paints.capacity() * sizeof(skin::Paint) +
                            pack.stops.capacity() * sizeof(skin::GradientStop);
  std::printf("pack holds about %zu KB\n", bytes / 1024);

  // In turns, so that whatever else the machine is doing falls on both.
  const skin::Pack* const pack_pointer = h.app.ui().pack;
  std::vector<double> with_pack, with_code;
  for (int round = 0; round < 40; ++round) {
    for (const bool use_pack : {true, false}) {
      h.app.ui().pack = use_pack ? pack_pointer : nullptr;
      h.render();  // the first frame after a swap settles the caches
      const double start = seconds();
      for (int i = 0; i < 50; ++i) h.render();
      (use_pack ? with_pack : with_code).push_back((seconds() - start) / 50);
    }
  }
  h.app.ui().pack = pack_pointer;
  const double code = median(with_code), data = median(with_pack);
  std::printf("frame: code %.1f us, pack %.1f us, %+.1f%%\n", code * 1e6, data * 1e6, (data / code - 1) * 100);
  return 0;
}
