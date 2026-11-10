#pragma once
// A member's profile, as the server answers it. While it is open it asks again every
// second, so the stay and the input count keep moving.
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "crpp/ui/surface.hpp"
#include "crpp/ui/tip.hpp"

namespace crpp::ui {

struct ProfileView {
  std::uint32_t slot = 0;
  std::string nick, emoji;
  std::optional<std::string> handle;
  std::optional<std::int64_t> created;  // unix epoch, seconds
  std::int64_t stayed = 0;              // seconds
  std::uint64_t events = 0;
};


// How many years an account turns on this day, by the local calendar: nothing on any other day,
// and nothing in the year it was made. Months are 1-12.
[[nodiscard]] constexpr std::optional<int> anniversary_years(int made_year, int made_month, int made_day, int year, int month, int day) {
  // Made on 29 February, it is kept on 28 February in other years.
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  if (made_month == 2 && made_day == 29 && !leap) made_day = 28;
  if (month != made_month || day != made_day || year <= made_year) return std::nullopt;
  return year - made_year;
}

class ProfileCard final : public Surface {
 public:
  using Surface::Surface;
  enum Id : int { Backdrop, Card, Icon };
  [[nodiscard]] bool holds_keys(int id) const override { return id == Backdrop; }

  static constexpr unsigned long long answer_timeout_ms = 4000;
  static constexpr unsigned long long refresh_ms = 1000;

  std::move_only_function<void(std::uint32_t slot) const> on_request;
  // The own card's icon was clicked.
  std::move_only_function<void() const> on_change_icon;

  // Asks the server; the card opens when the answer comes, or a toast says it did not.
  void request(std::uint32_t slot);
  // An answer. One nobody is waiting for any more is dropped.
  void answer(ProfileView profile, bool mine);
  void set_emoji(std::string emoji) { profile_.emoji = std::move(emoji); }
  void close();
  [[nodiscard]] bool is_open() const { return open_; }
  [[nodiscard]] const ProfileView& profile() const { return profile_; }
  [[nodiscard]] bool mine() const { return mine_; }

  // What the card takes inside a window of its own.
  [[nodiscard]] static skin::ScreenSchema schema();
  [[nodiscard]] D2D1_SIZE_F content_size() const;
  void paint(Painter& p);
  void click(int id, float x, float y, bool shift) override;
  bool escape() override;
  [[nodiscard]] Cursor cursor(int id) const override { return id == Icon ? Cursor::Hand : Cursor::Arrow; }
  [[nodiscard]] bool animating() const override;
  bool tick(unsigned long long now_ms) override;
  [[nodiscard]] unsigned long long wake_at() const override;

 private:
  bool open_ = false;
  bool mine_ = false;
  ProfileView profile_;
  std::optional<std::uint32_t> pending_;  // the slot asked for
  unsigned long long give_up_at_ = 0, refresh_at_ = 0, opened_at_ = 0;
  bool was_animating_ = false;
  HoverTip tip_;
};

}  // namespace crpp::ui
