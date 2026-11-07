#pragma once
// Drives an App without a window: renders it offscreen, which fills the hit list, and
// feeds it input at the places things were drawn.
#include <wincodec.h>
#include <windows.h>

#include "crpp/ui/app.hpp"

namespace crpp::ui::test {

class UiHarness {
 public:
  bool start(unsigned width = 951, unsigned height = 692) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (!app.init({})) return false;
    app.set_animations(false);
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                reinterpret_cast<void**>(wic_.put())))) {
      return false;
    }
    if (FAILED(wic_->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bitmap_.put()))) {
      return false;
    }
    const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                    96, 96);
    if (FAILED(app.factory()->CreateWicBitmapRenderTarget(bitmap_.get(), props, target_.put()))) return false;
    width_ = static_cast<float>(width);
    height_ = static_cast<float>(height);
    render();
    return true;
  }

  // False if Direct2D refused the frame, as it does for a resource of another factory.
  bool render() {
    target_->BeginDraw();
    app.paint(target_.get(), width_, height_);
    return SUCCEEDED(target_->EndDraw());
  }

  // What the window does while hidden: the target goes, then whatever it was made with.
  void drop_target() { target_.reset(); }
  bool make_target() {
    const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                    96, 96);
    return SUCCEEDED(app.factory()->CreateWicBitmapRenderTarget(bitmap_.get(), props, target_.put()));
  }

  // A target of its own at this DPI, as a window on another monitor has.
  [[nodiscard]] ComPtr<ID2D1RenderTarget> target_at(float dpi) {
    const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
    ComPtr<ID2D1RenderTarget> target;
    (void)app.factory()->CreateWicBitmapRenderTarget(bitmap_.get(), props, target.put());
    return target;
  }

  // True if the surface has this hit on screen now.
  [[nodiscard]] bool drawn(const Surface& owner, int id) { return app.ui().find_hit(&owner, id) != nullptr; }

  // Clicks the middle of the hit, as the topmost thing there would receive it, then
  // redraws. False if nothing is drawn under that id.
  bool click(const Surface& owner, int id) {
    const Ui::Hit* hit = app.ui().find_hit(&owner, id);
    if (!hit) return false;
    const float x = hit->rect.x + hit->rect.w / 2, y = hit->rect.y + hit->rect.h / 2;
    app.mouse_move(x, y);
    app.mouse_down(x, y, false);
    app.mouse_up();
    render();
    return true;
  }

  void key(unsigned vk) {
    app.key_down(vk, false, false);
    render();
  }

  App app;

 private:
  ComPtr<IWICImagingFactory> wic_;
  ComPtr<IWICBitmap> bitmap_;
  ComPtr<ID2D1RenderTarget> target_;
  float width_ = 0, height_ = 0;
};

}  // namespace crpp::ui::test
