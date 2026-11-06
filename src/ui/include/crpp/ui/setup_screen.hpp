#pragma once
// The first start's wait for the assets.
#include "crpp/ui/surface.hpp"

namespace crpp::ui {

class SetupScreen final : public Surface {
 public:
  using Surface::Surface;

  void set(int percent) { percent_ = percent; }
  void paint(Painter& p);

 private:
  int percent_ = 0;
};

}  // namespace crpp::ui
