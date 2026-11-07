// One page with every Luna control in each of its states, for --shot luna-controls.
#include "crpp/ui/app.hpp"
#include "crpp/ui/cursor_image.hpp"
#include "crpp/ui/luna.hpp"

namespace crpp::ui {

// The pack's cursors as they come out, for --shot cursors: at the sizes of 100% and 150%, and four
// times as large with their hot point marked, on white, black and the Skin's face.
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

void App::paint_controls_sheet(Painter& p) {
  const Theme& t = ui_.theme;
  const Skin& skin = ui_.skin();
  using namespace luna;
  float y = 12;
  const auto label = [&](std::string_view text) {
    p.text(text, {12, y, 90, 23}, body(p, 23), t.luna.ink);
  };

  label("Button");
  float x = 100;
  const auto one = [&](std::string_view text, ButtonState state) {
    button(p, t, {x, y, button_w, button_h}, text, state);
    x += button_w + 6;
  };
  one("Normal", {});
  one("Default", {.primary = true});
  one("Hover", {.hot = true});
  one("Pressed", {.pressed = true});
  one("Off", {.off = true});
  y += 31;

  label("Check");
  check_box(p, t, {100, y + 5, 13, 13}, false, false, false);
  check_box(p, t, {120, y + 5, 13, 13}, true, false, false);
  check_box(p, t, {140, y + 5, 13, 13}, false, true, false);
  check_box(p, t, {160, y + 5, 13, 13}, true, true, false);
  check_box(p, t, {180, y + 5, 13, 13}, false, false, true);
  check_box(p, t, {200, y + 5, 13, 13}, true, false, true);
  y += 31;

  label("Field");
  field(p, t, {100, y, 150, field_h});
  p.text("가나다 ABC 123", {104, y, 140, field_h}, body(p, field_h), t.luna.ink);
  field(p, t, {258, y, 150, field_h}, true);
  skin.dropdown_button(p, t, {416, y, 150, field_h}, "Choice", false, false, false);
  y += 31;

  label("Slider");
  skin.slider(p, t, {100, y, 150, 23}, 0);
  skin.slider(p, t, {258, y, 150, 23}, 0.6f);
  y += 31;

  label("Progress");
  skin.progress(p, t, {100, y + 4, 200, 14}, 45);
  skin.progress(p, t, {308, y + 4, 200, 14}, 100);
  y += 31;

  label("Toolbar");
  toolbar_button(p, t, {100, y, 90, 24}, "Rest", false, false, false);
  toolbar_button(p, t, {194, y, 90, 24}, "Hover", true, false, false);
  toolbar_button(p, t, {288, y, 90, 24}, "Pressed", false, true, false);
  toolbar_button(p, t, {382, y, 90, 24}, "On", false, false, true);
  y += 34;

  label("Pictures");
  picture_frame(p, t, {100, y, 32, 32}, 0);
  draw_svg(p, ui_.emoji_image(""), {102, y + 2, 28, 28});
  picture_frame(p, t, {148, y, 32, 32}, 1);
  draw_svg(p, ui_.emoji_image(""), {150, y + 2, 28, 28});
  link(p, t, {200, y, 100, 16}, "A link", false);
  link(p, t, {200, y + 16, 100, 16}, "A link, hovered", true);
  (void)skin.bubble(p, t, {320, y, 90, 18}, 0, {}, {}, 1);
  p.text("Tooltip", {324, y + 1, 80, 16}, body(p, 16), t.luna.tooltip_ink);
  y += 42;

  label("Lists");
  skin.panel(p, t, {100, y, 130, 3 * 17 + 2}, PanelKind::List);
  skin.list_item(p, t, {101, y + 1, 128, 17}, "First", false, false);
  skin.list_item(p, t, {101, y + 18, 128, 17}, "Selected", true, false);
  skin.list_item(p, t, {101, y + 35, 128, 17}, "Third", false, false);
  skin.panel(p, t, {240, y, 130, 3 * 22 + 2}, PanelKind::Menu);
  skin.menu_item(p, t, {241, y + 1, 128, 22}, "Item", false, false, false);
  skin.menu_item(p, t, {241, y + 23, 128, 22}, "Item, hovered", false, true, false);
  skin.menu_item(p, t, {241, y + 45, 128, 22}, "Item, ticked", true, false, false);
  skin.panel(p, t, {380, y, 180, 40}, PanelKind::Toast);
  p.text("A pale blue panel", {388, y, 160, 40}, body(p, 40), t.luna.ink);
  scrollbar(p, t, scrollbar_parts({570, y, scrollbar_w, 110}, 100, 400, 100), false, false, false);
  y += 78;

  group_box(p, t, {100, y + 8, 200, 54}, "Group box");
  p.text("Inside a group", {112, y + 22, 170, 16}, body(p, 16), t.luna.ink);
  y += 80;

  skin.panel(p, t, {100, y, 150, 40}, PanelKind::Popup);
  caption_button(p, t, {270, y, 21, 21}, CaptionGlyph::Minimize, false, false);
  caption_button(p, t, {293, y, 21, 21}, CaptionGlyph::Minimize, true, false);
  caption_button(p, t, {316, y, 21, 21}, CaptionGlyph::Minimize, false, true);
  caption_button(p, t, {345, y, 21, 21}, CaptionGlyph::Close, false, false);
  caption_button(p, t, {368, y, 21, 21}, CaptionGlyph::Close, true, false);
  caption_button(p, t, {391, y, 21, 21}, CaptionGlyph::Close, false, true);
}

}  // namespace crpp::ui
