#include "crpp/ui/chat_logic.hpp"

#include <algorithm>

#include "crpp/locale/locale.hpp"

namespace crpp::ui {

namespace {

constexpr int min_marks = 3, max_marks = 20;
constexpr ChatLimiter::Ms calm_linger_ms = 1000;  // the bubble outlasts the block by this
constexpr ChatLimiter::Ms block_margin_ms = 150;  // allowed again just before the server does

bool is_alnum(wchar_t c) { return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }

bool is_url_char(wchar_t c) {
  return is_alnum(c) || std::wstring_view(L"-._~:/?#[]@!$&'()*+,;=%").find(c) != std::wstring_view::npos;
}

}  // namespace

bool ChatLimiter::try_send(Ms now) {
  if (blocked_until_ != 0 && now >= blocked_until_) {
    blocked_until_ = 0;
    sent_count_ = 0;
  }
  std::size_t expired = 0;
  while (expired < sent_count_ && now - sent_[expired] > window_ms) ++expired;
  std::copy(sent_.begin() + static_cast<std::ptrdiff_t>(expired), sent_.begin() + static_cast<std::ptrdiff_t>(sent_count_),
            sent_.begin());
  sent_count_ -= expired;

  if (now < blocked_until_ || sent_count_ >= burst) {
    ++local_blocks_;
    note_blocked(now, cooldown_ms, true);
    return false;
  }
  sent_[sent_count_++] = now;
  return true;
}

bool ChatLimiter::server_blocked(Ms now, Ms ms) {
  const bool foreseen = local_blocks_ > 0;
  note_blocked(now, ms, !foreseen);
  if (foreseen) --local_blocks_;
  return !foreseen;
}

void ChatLimiter::note_blocked(Ms now, Ms ms, bool count_attempt) {
  if (now >= blocked_until_) marks_ = min_marks;
  else if (count_attempt) marks_ = std::min(max_marks, marks_ + 1);
  blocked_until_ = now + ms - std::min(ms, block_margin_ms);
  cooling_ = true;
  calm_ = true;
  calm_until_ = now + ms + calm_linger_ms;
}

bool ChatLimiter::expire(Ms now) {
  bool changed = false;
  if (cooling_ && now >= blocked_until_) {
    cooling_ = false;
    changed = true;
  }
  if (calm_ && now >= calm_until_) {
    calm_ = false;
    local_blocks_ = 0;
    marks_ = min_marks;
    changed = true;
  }
  return changed;
}

std::string ChatLimiter::calm_text() const {
  std::string text = locale::tr().chat_calm;
  for (int i = 0; i < marks_; ++i) text += " !";
  return text;
}

ChatLimiter::Ms ChatLimiter::wake_at() const {
  if (cooling_ && calm_) return std::min(blocked_until_, calm_until_);
  return cooling_ ? blocked_until_ : calm_ ? calm_until_ : 0;
}

namespace {

std::string ascii_lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

// UTF-8 code points: every byte but a continuation one starts one.
std::size_t code_points(std::string_view s) {
  return static_cast<std::size_t>(std::ranges::count_if(s, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

}  // namespace

bool mentions(std::string_view text, std::span<const std::string> names) {
  const std::string haystack = ascii_lower(text);
  return std::ranges::any_of(names, [&](const std::string& name) {
    return code_points(name) >= 2 && haystack.find(ascii_lower(name)) != std::string::npos;
  });
}

std::vector<LinkSpan> find_links(std::wstring_view text) {
  constexpr std::wstring_view scheme = L"https://";
  std::vector<LinkSpan> links;
  for (std::size_t at = text.find(scheme); at != std::wstring_view::npos; at = text.find(scheme, at)) {
    std::size_t end = at + scheme.size();
    if (end >= text.size() || !is_alnum(text[end])) {
      at = end;
      continue;
    }
    while (end < text.size() && is_url_char(text[end])) ++end;
    const std::size_t match_end = end;
    // Trim what is punctuation rather than part of the address.
    const auto count = [&](wchar_t c) { return std::count(text.begin() + static_cast<std::ptrdiff_t>(at), text.begin() + static_cast<std::ptrdiff_t>(end), c); };
    while (end > at) {
      const wchar_t last = text[end - 1];
      if (std::wstring_view(L".,!?;:'*").find(last) != std::wstring_view::npos) {
        --end;
      } else if ((last == L')' && count(L')') > count(L'(')) || (last == L']' && count(L']') > count(L'['))) {
        --end;
      } else {
        break;
      }
    }
    links.push_back({static_cast<std::uint32_t>(at), static_cast<std::uint32_t>(end - at)});
    at = match_end;
  }
  return links;
}

}  // namespace crpp::ui
