#pragma once
// The last diagnostic events of this run, kept in memory always, so that a Crash report can say what
// happened just before. It has a fixed size, takes them only from the UI thread and is read by the
// crash handler on any thread: a crumb written while the handler reads may come out torn, never out
// of bounds. What it holds is what the diagnostic log takes: no input, chat, name or address.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace crpp::crash {

class Breadcrumbs {
 public:
  static constexpr std::size_t capacity = 32;
  static constexpr std::size_t detail_bytes = 110;  // 128 bytes a crumb, 4 KB in all

  struct Crumb {
    std::uint64_t ms = 0;        // on the clock the caller chose
    const char* what = nullptr;  // text that lives as long as the program
    std::uint8_t what_size = 0;
    std::uint8_t detail_size = 0;
    char detail[detail_bytes]{};
  };

  // `what` must outlive the program, as a name from a table does. The detail is cut to fit, on a
  // character boundary, and line breaks in it become spaces.
  void note(std::uint64_t ms, std::string_view what, std::string_view detail = {}) noexcept;

  // How many are held, at most `capacity`, and the one at `index`, oldest first.
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] const Crumb& at(std::size_t index) const noexcept;

 private:
  std::array<Crumb, capacity> crumbs_{};
  std::atomic<std::uint32_t> count_{0};
};

static_assert(sizeof(Breadcrumbs::Crumb) == 128);

// The one the app notes into and the crash handler reads. Zero until the first note, so it costs
// no memory before then.
inline constinit Breadcrumbs breadcrumbs;

}  // namespace crpp::crash
