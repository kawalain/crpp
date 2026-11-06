#pragma once
#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace crpp::session {

// A string with inline storage and a hard capacity; never allocates.
template <std::size_t N>
class FixedString {
 public:
  // False (and no change) if s does not fit. Safe when s views this string.
  bool assign(std::string_view s) noexcept {
    if (s.size() > N) return false;
    if (!s.empty()) std::memmove(data_.data(), s.data(), s.size());
    size_ = s.size();
    return true;
  }
  void clear() noexcept { size_ = 0; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] std::string_view view() const noexcept { return {data_.data(), size_}; }

 private:
  std::array<char, N> data_{};
  std::size_t size_ = 0;
};

}  // namespace crpp::session
