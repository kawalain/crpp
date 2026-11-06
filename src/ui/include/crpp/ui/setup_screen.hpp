#pragma once
// The screen before assets are in place: progress, or an error with a retry.
#include <functional>
#include <string>

#include "crpp/ui/surface.hpp"

namespace crpp::ui {

class SetupScreen final : public Surface {
 public:
  using Surface::Surface;

  std::function<void()> on_retry;

  void set(int percent, std::string error) {
    percent_ = percent;
    error_ = std::move(error);
  }
  void paint(Painter& p);
  void click(int id, float x, float y, bool shift) override;

 private:
  enum Id : int { Retry };
  int percent_ = 0;
  std::string error_;
};

}  // namespace crpp::ui
