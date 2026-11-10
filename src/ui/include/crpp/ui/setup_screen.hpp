#pragma once
// The first start's wait for its files: progress in files and bytes, or the failure with a
// way to try again or to quit. Until a Skin Pack is installed it is all the app shows.
#include <cstdint>
#include <functional>

#include "crpp/ui/surface.hpp"

namespace crpp::ui {

class SetupScreen final : public Surface {
 public:
  using Surface::Surface;
  enum Id : int { Retry, Quit, Backdrop, Card };
  [[nodiscard]] std::optional<Role> role(int id) const override {
    return id == Retry || id == Quit ? std::optional(Role::Button) : std::nullopt;
  }
  [[nodiscard]] bool holds_keys(int id) const override { return id == Backdrop; }

  struct Status {
    std::size_t files_done = 0, files = 0;
    std::uint64_t bytes_done = 0, bytes = 0;
    bool failed = false;
  };

  [[nodiscard]] static skin::ScreenSchema schema();

  std::move_only_function<void() const> on_retry, on_quit;

  void set(const Status& status) { status_ = status; }
  [[nodiscard]] const Status& status() const { return status_; }
  [[nodiscard]] int percent() const;
  void paint(Painter& p);
  void click(int id, float x, float y, bool shift) override;

 private:
  void paint_plain(Painter& p, int shown_percent, const std::string& detail);
  Status status_;
};

}  // namespace crpp::ui
