#include "crpp/ui/png.hpp"

#include <wincodec.h>
#include <windows.h>

namespace crpp::ui {

bool save_png(App& app, const std::wstring& path, unsigned width, unsigned height, float dpi) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  ComPtr<IWICImagingFactory> wic;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                              reinterpret_cast<void**>(wic.put())))) {
    return false;
  }
  const auto to_pixels = [dpi](unsigned size) { return static_cast<unsigned>(static_cast<float>(size) * dpi / 96 + 0.5f); };
  const unsigned pixel_w = to_pixels(width), pixel_h = to_pixels(height);
  ComPtr<IWICBitmap> bitmap;
  if (FAILED(wic->CreateBitmap(pixel_w, pixel_h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bitmap.put()))) {
    return false;
  }
  ComPtr<ID2D1RenderTarget> rt;
  const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                  D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                  dpi, dpi);
  if (FAILED(app.factory()->CreateWicBitmapRenderTarget(bitmap.get(), props, rt.put()))) return false;
  rt->BeginDraw();
  app.paint(rt.get(), static_cast<float>(width), static_cast<float>(height));
  if (FAILED(rt->EndDraw())) return false;

  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapEncoder> encoder;
  ComPtr<IWICBitmapFrameEncode> frame;
  if (FAILED(wic->CreateStream(stream.put())) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
      FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put())) ||
      FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)) ||
      FAILED(encoder->CreateNewFrame(frame.put(), nullptr)) || FAILED(frame->Initialize(nullptr)) ||
      FAILED(frame->SetSize(pixel_w, pixel_h))) {
    return false;
  }
  WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
  frame->SetPixelFormat(&format);
  if (FAILED(frame->WriteSource(bitmap.get(), nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
    return false;
  }
  return true;
}

}  // namespace crpp::ui
