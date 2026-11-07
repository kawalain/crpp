#include "crpp/ui/cursor_image.hpp"

#include <algorithm>
#include <cmath>

#include "crpp/skin/draw.hpp"

namespace crpp::ui {

namespace {

D2D1_COLOR_F d2d_color(skin::Color c) { return {c.r, c.g, c.b, c.a}; }

// Draws in the 32 by 32 box of a cursor's part; the target's transform makes that the wanted size.
class CursorCanvas final : public skin::Canvas {
 public:
  CursorCanvas(const Gfx& gfx, ID2D1RenderTarget* target, D2D1_MATRIX_3X2_F base) : gfx_(gfx), rt_(target), base_(base) {}

  skin::Box snap(skin::Box box) override { return box; }
  float snap(float value) override { return value; }
  float line() override { return 1; }
  float font_size() override { return gfx_.classic_size(); }
  float text_width(std::string_view, const skin::TextLook&, float) override { return 0; }
  float text_height(std::string_view, const skin::TextLook&, float) override { return 0; }
  void fill(skin::Box box, skin::Color color) override { rt_->FillRectangle(rect(box), brush(color).get()); }
  void fill_down(skin::Box, float, float, std::span<const skin::GradientStop>) override {}
  void fill_round(skin::Box box, float radius, skin::Color color) override { rt_->FillRoundedRectangle(D2D1::RoundedRect(rect(box), radius, radius), brush(color).get()); }
  void fill_round_down(skin::Box, float, float, float, std::span<const skin::GradientStop>) override {}
  void frame(skin::Box box, skin::Color color) override { rt_->DrawRectangle(D2D1::RectF(box.x + 0.5f, box.y + 0.5f, box.x + box.w - 0.5f, box.y + box.h - 0.5f), brush(color).get()); }
  void stroke_round(skin::Box box, float radius, skin::Color color, float width) override {
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(rect(box), radius, radius), brush(color).get(), width);
  }
  void line(float x1, float y1, float x2, float y2, skin::Color color, float width) override {
    rt_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush(color).get(), width);
  }
  void path(std::string_view data, float x, float y, float scale, skin::Color color, float width, bool, bool) override {
    ID2D1PathGeometry* const shape = gfx_.path(data);
    if (!shape) return;
    rt_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(x, y) * base_);
    if (width > 0) rt_->DrawGeometry(shape, brush(color).get(), width);
    else rt_->FillGeometry(shape, brush(color).get());
    rt_->SetTransform(base_);
  }
  void text(std::string_view, skin::Box, const skin::TextLook&, skin::Color) override {}
  void push_clip(skin::Box) override {}
  void pop_clip() override {}
  void hit(skin::Box, int) override {}
  bool hot(int) override { return false; }
  bool pressed(int) override { return false; }

 private:
  [[nodiscard]] static D2D1_RECT_F rect(skin::Box box) { return D2D1::RectF(box.x, box.y, box.x + box.w, box.y + box.h); }
  [[nodiscard]] ComPtr<ID2D1SolidColorBrush> brush(skin::Color color) {
    ComPtr<ID2D1SolidColorBrush> made;
    rt_->CreateSolidColorBrush(d2d_color(color), made.put());
    return made;
  }

  const Gfx& gfx_;
  ID2D1RenderTarget* rt_;
  D2D1_MATRIX_3X2_F base_;
};

std::uint32_t straight_alpha(std::uint32_t pixel) {
  const std::uint32_t a = pixel >> 24;
  if (a == 0 || a == 255) return pixel;
  const auto up = [a](std::uint32_t c) { return std::min<std::uint32_t>(255, (c * 255 + a / 2) / a); };
  return up(pixel & 255) | (up((pixel >> 8) & 255) << 8) | (up((pixel >> 16) & 255) << 16) | (a << 24);
}

}  // namespace

std::optional<CursorImage> make_cursor_image(const Ui& ui, skin::CursorKind kind, int size) {
  if (!ui.pack || !ui.pack->cursors || ui.palette < 0 || size <= 0 || size > 256) return std::nullopt;
  const skin::PackCursor& cursor = (*ui.pack->cursors)[static_cast<std::size_t>(kind)];
  ID2D1Factory* const factory = ui.gfx.d2d();
  if (!factory) return std::nullopt;
  const bool hard = ui.pack->cursors_hard;
  const int drawn = hard ? static_cast<int>(skin::cursor_box) : size;

  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof info.bmiHeader;
  info.bmiHeader.biWidth = drawn;
  info.bmiHeader.biHeight = -drawn;  // top down
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HDC dc = CreateCompatibleDC(nullptr);
  if (!dc) return std::nullopt;
  HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  std::optional<CursorImage> image;
  if (bitmap) {
    HGDIOBJ const old = SelectObject(dc, bitmap);
    ComPtr<ID2D1DCRenderTarget> target;
    const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    const RECT whole{0, 0, drawn, drawn};
    if (SUCCEEDED(factory->CreateDCRenderTarget(&props, target.put())) && SUCCEEDED(target->BindDC(dc, &whole))) {
      const float scale = static_cast<float>(size) / skin::cursor_box;
      const D2D1_MATRIX_3X2_F base = hard ? D2D1::Matrix3x2F::Identity() : D2D1::Matrix3x2F::Scale(scale, scale);
      target->BeginDraw();
      if (hard) target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
      target->Clear(D2D1::ColorF(0, 0));
      target->SetTransform(base);
      CursorCanvas canvas(ui.gfx, target.get(), base);
      skin::Source nothing;
      skin::draw_part(*ui.pack, ui.pack->parts[cursor.part], ui.palette, {0, 0, skin::cursor_box, skin::cursor_box}, canvas, nothing, {});
      if (SUCCEEDED(target->EndDraw())) {
        GdiFlush();
        image.emplace();
        image->size = size;
        image->hot_x = std::clamp(static_cast<int>(std::lround(cursor.hot_x * scale)), 0, size - 1);
        image->hot_y = std::clamp(static_cast<int>(std::lround(cursor.hot_y * scale)), 0, size - 1);
        const auto* const first = static_cast<const std::uint32_t*>(bits);
        image->pixels.resize(static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
        for (int y = 0; y < size; ++y) {  // the nearest pixel of what was drawn
          for (int x = 0; x < size; ++x) {
            image->pixels[static_cast<std::size_t>(y * size + x)] = first[static_cast<std::size_t>(y * drawn / size * drawn + x * drawn / size)];
          }
        }
      }
    }
    SelectObject(dc, old);
    DeleteObject(bitmap);
  }
  DeleteDC(dc);
  return image;
}

// As "Creating an Alpha Blended Cursor" in Microsoft's Using Cursors: a top down DIB section, and a
// mask with a bit set where nothing shows. The system copies both. That page's comment calls the
// pixels premultiplied, but DrawIconEx blends them as straight, as a .cur file holds them, on Windows 10
// 19044; a test pins it.
HCURSOR make_system_cursor(const CursorImage& image) {
  const int size = image.size;
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof info.bmiHeader;
  info.bmiHeader.biWidth = size;
  info.bmiHeader.biHeight = -size;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!color) return nullptr;
  std::ranges::transform(image.pixels, static_cast<std::uint32_t*>(bits), straight_alpha);

  // CreateBitmap takes rows of whole 16-bit words, the leftmost pixel in the highest bit.
  const int stride = (size + 15) / 16 * 2;
  std::vector<std::uint8_t> rows(static_cast<std::size_t>(stride) * static_cast<std::size_t>(size));
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      if ((image.pixels[static_cast<std::size_t>(y * size + x)] >> 24) == 0) {
        rows[static_cast<std::size_t>(y * stride + x / 8)] |= static_cast<std::uint8_t>(0x80 >> (x % 8));
      }
    }
  }
  HBITMAP mask = CreateBitmap(size, size, 1, 1, rows.data());
  HCURSOR cursor = nullptr;
  if (mask) {
    ICONINFO icon{};
    icon.fIcon = FALSE;
    icon.xHotspot = static_cast<DWORD>(image.hot_x);
    icon.yHotspot = static_cast<DWORD>(image.hot_y);
    icon.hbmMask = mask;
    icon.hbmColor = color;
    cursor = CreateIconIndirect(&icon);
    DeleteObject(mask);
  }
  DeleteObject(color);
  return cursor;
}

}  // namespace crpp::ui
