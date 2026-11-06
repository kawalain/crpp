#pragma once
// The settings droplet in the window's corner.
#include <functional>

#include "crpp/ui/surface.hpp"

namespace crpp::ui {

class SettingsPanel final : public Surface {
 public:
  using Surface::Surface;
  bool init();  // needs ui.gfx

  std::function<void()> on_settings;

  void paint(Painter& p);
  void click(int id, float x, float y, bool shift) override;

 private:
  enum Id : int { Droplet };
  ComPtr<ID2D1PathGeometry> droplet_;
};

}  // namespace crpp::ui
