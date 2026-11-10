#include <algorithm>

#include "crpp/crash/breadcrumbs.hpp"
#include "crpp/crash/writer.hpp"

namespace crpp::crash {

Writer& Writer::text(std::string_view text) noexcept {
  const std::size_t room = to_.size() - used_;
  const std::size_t take = std::min(room, text.size());
  std::copy_n(text.data(), take, to_.data() + used_);
  used_ += take;
  if (take < text.size()) full_ = true;
  return *this;
}

Writer& Writer::hex(std::uint64_t value, int digits, bool upper) noexcept {
  const char* const set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  char buffer[16];
  int n = 0;
  do {
    buffer[15 - n++] = set[value & 0xf];
    value >>= 4;
  } while (value != 0 && n < 16);
  while (n < digits && n < 16) buffer[15 - n++] = '0';
  return text({buffer + 16 - n, static_cast<std::size_t>(n)});
}

Writer& Writer::dec(std::uint64_t value, int digits) noexcept {
  char buffer[20];
  int n = 0;
  do {
    buffer[19 - n++] = static_cast<char>('0' + value % 10);
    value /= 10;
  } while (value != 0 && n < 20);
  while (n < digits && n < 20) buffer[19 - n++] = '0';
  return text({buffer + 20 - n, static_cast<std::size_t>(n)});
}

Writer& Writer::seconds(std::uint64_t ms) noexcept { return dec(ms / 1000).ch('.').dec(ms % 1000 / 100); }

void Breadcrumbs::note(std::uint64_t ms, std::string_view what, std::string_view detail) noexcept {
  const std::uint32_t n = count_.load(std::memory_order_relaxed);
  Crumb& crumb = crumbs_[n % capacity];
  crumb.ms = ms;
  crumb.what = what.data();
  crumb.what_size = static_cast<std::uint8_t>(std::min<std::size_t>(what.size(), 255));
  std::size_t take = std::min(detail.size(), detail_bytes);
  // Never half a UTF-8 character: back off over continuation bytes.
  if (take < detail.size()) {
    while (take > 0 && (static_cast<unsigned char>(detail[take]) & 0xc0) == 0x80) --take;
  }
  for (std::size_t i = 0; i < take; ++i) {
    const char c = detail[i];
    crumb.detail[i] = static_cast<unsigned char>(c) < 0x20 || c == 0x7f ? ' ' : c;
  }
  crumb.detail_size = static_cast<std::uint8_t>(take);
  count_.store(n + 1, std::memory_order_release);
}

std::size_t Breadcrumbs::size() const noexcept { return std::min<std::size_t>(count_.load(std::memory_order_acquire), capacity); }

const Breadcrumbs::Crumb& Breadcrumbs::at(std::size_t index) const noexcept {
  const std::uint32_t n = count_.load(std::memory_order_acquire);
  const std::size_t oldest = n > capacity ? n % capacity : 0;
  return crumbs_[(oldest + index) % capacity];
}

}  // namespace crpp::crash
