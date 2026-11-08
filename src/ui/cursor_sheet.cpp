// The pack's cursors as they come out, for --shot cursors.
#include "crpp/ui/app.hpp"
#include "crpp/ui/cursor_image.hpp"

namespace crpp::ui {

// At the sizes of 100% and 150%, and four times as large with their hot point marked, on white, black and the Skin's face.
void App::paint_cursor_sheet(Painter& p) {
  const Color backgrounds[] = {rgb(0xFFFFFF), rgb(0x000000), rgb(0x7f9db9)};
  float y = 0;
  for (const Color background : backgrounds) {
    p.fill_rect({0, y, 1200, 220}, background);
    float x = 8;
    for (std::size_t kind = 0; kind < skin::cursor_names.size(); ++kind) {
      float from_top = y + 4;
      for (const int size : {32, 48}) {
        const auto image = make_cursor_image(ui_, static_cast<skin::CursorKind>(kind), size);
        if (!image) continue;
        ComPtr<ID2D1Bitmap> bitmap;
        const auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(p.rt()->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(size), static_cast<UINT32>(size)), image->pixels.data(), static_cast<UINT32>(size) * 4, &props, bitmap.put()))) continue;
        const float s = static_cast<float>(size);
        p.rt()->DrawBitmap(bitmap.get(), D2D1::RectF(x, from_top, x + s, from_top + s), 1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        if (size == 32) {
          const float big_x = x + 36, big_y = y + 4;
          p.rt()->DrawBitmap(bitmap.get(), D2D1::RectF(big_x, big_y, big_x + 128, big_y + 128), 1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
          p.fill_rect({big_x + static_cast<float>(image->hot_x) * 4, big_y + static_cast<float>(image->hot_y) * 4, 4, 4}, rgb(0xFF0000));
        }
        from_top += s + 4;
      }
      x += 170;
    }
    y += 220;
  }
}

}  // namespace crpp::ui
