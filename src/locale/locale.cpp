#include "crpp/locale/locale.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace crpp::locale {

extern const Strings korean;
extern const Strings english;

namespace {

constexpr std::array<Locale, 2> locales{{
    {"ko", "한국어", "ko-kr", "words", &korean},
    {"en", "English", "en-us", "words/en", &english},
}};

std::atomic<const Locale*> active{&locales[0]};

constexpr char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool is_language_of(std::string_view os_language, std::string_view id) {
  if (os_language.size() < id.size()) return false;
  if (os_language.size() > id.size() && os_language[id.size()] != '-' && os_language[id.size()] != '_') return false;
  return std::ranges::equal(os_language.substr(0, id.size()), id, {}, lower);
}

std::string number(std::int64_t value) { return std::to_string(value); }

}  // namespace

std::span<const Locale> all_locales() noexcept { return locales; }

const Locale* find_locale(std::string_view id) noexcept {
  const auto found = std::ranges::find(locales, id, &Locale::id);
  return found == locales.end() ? nullptr : &*found;
}

const Locale& choose_locale(std::string_view saved, std::string_view os_language) noexcept {
  if (const Locale* locale = find_locale(saved)) return *locale;
  const auto matching = std::ranges::find_if(locales, [&](const Locale& l) { return is_language_of(os_language, l.id); });
  return matching != locales.end() ? *matching : *find_locale("en");
}

const Locale& current() noexcept { return *active.load(std::memory_order_relaxed); }

void set_current(const Locale& locale) noexcept { active.store(&locale, std::memory_order_relaxed); }

std::string fill(std::string_view pattern, std::initializer_list<std::string_view> values) {
  std::string out;
  out.reserve(pattern.size() + 16);
  for (std::size_t i = 0; i < pattern.size(); ++i) {
    if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 1] >= '0' && pattern[i + 1] <= '9' && pattern[i + 2] == '}') {
      const auto index = static_cast<std::size_t>(pattern[i + 1] - '0');
      if (index < values.size()) {
        out += values.begin()[index];
        i += 2;
        continue;
      }
    }
    out += pattern[i];
  }
  return out;
}

std::string label(std::string_view wording) {
  std::string out;
  out.reserve(wording.size());
  for (std::size_t i = 0; i < wording.size(); ++i) {
    if (wording[i] == '&' && i + 1 < wording.size()) ++i;
    out += wording[i];
  }
  return out;
}

char access_key(std::string_view wording) noexcept {
  for (std::size_t i = 0; i + 1 < wording.size(); ++i) {
    if (wording[i] != '&') continue;
    const char c = wording[i + 1];
    if (c != '&') return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
    ++i;
  }
  return 0;
}

std::string format_stay(std::int64_t seconds) {
  const Strings& s = tr();
  std::int64_t minutes = std::max<std::int64_t>(0, seconds / 60);
  const std::int64_t years = minutes / 525600;
  minutes %= 525600;
  const std::int64_t months = minutes / 43200;
  minutes %= 43200;
  const std::int64_t hours = minutes / 60;
  minutes %= 60;
  std::string out;
  const auto part = [&](std::int64_t value, const char* one, const char* many) {
    if (value == 0) return;
    if (!out.empty()) out += ' ';
    out += fill(value == 1 ? one : many, {number(value)});
  };
  part(years, s.stay_year, s.stay_years);
  part(months, s.stay_month, s.stay_months);
  part(hours, s.stay_hour, s.stay_hours);
  part(minutes, s.stay_minute, s.stay_minutes);
  return out.empty() ? s.stay_under_a_minute : out;
}

std::string format_times(std::uint64_t count) {
  std::string digits = std::to_string(count);
  for (std::size_t at = digits.size(); at > 3;) {
    at -= 3;
    digits.insert(at, 1, ',');
  }
  return fill(count == 1 ? tr().times_one : tr().times_many, {digits});
}

std::string format_clock(int hour, int minute, bool use_24h) {
  char text[8];
  if (use_24h) {
    std::snprintf(text, sizeof text, "%02d:%02d", hour % 24, minute % 60);
    return text;
  }
  char minutes[4];
  std::snprintf(minutes, sizeof minutes, "%02d", minute % 60);
  return fill(tr().clock_12h, {hour < 12 ? tr().clock_am : tr().clock_pm, number(hour % 12 == 0 ? 12 : hour % 12), minutes});
}

std::string format_release_date(std::string_view published_at) {
  // YYYY-MM-DD at the start; the time of day is not shown.
  if (published_at.size() < 10 || published_at[4] != '-' || published_at[7] != '-') return {};
  for (const std::size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u}) {
    if (published_at[i] < '0' || published_at[i] > '9') return {};
  }
  return fill(tr().release_date, {published_at.substr(0, 4), published_at.substr(5, 2), published_at.substr(8, 2)});
}

}  // namespace crpp::locale
