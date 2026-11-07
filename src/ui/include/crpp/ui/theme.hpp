#pragma once
// A Theme's colours. The Cloud ones are upstream's (style.css :root variables). Which Themes
// there are is in themes.hpp.
#include <cstdint>

namespace crpp::ui {

struct Color {
  float r = 0, g = 0, b = 0, a = 1;
};

constexpr Color rgb(std::uint32_t hex, float a = 1.0f) {
  return {static_cast<float>((hex >> 16) & 0xFF) / 255.0f, static_cast<float>((hex >> 8) & 0xFF) / 255.0f,
          static_cast<float>(hex & 0xFF) / 255.0f, a};
}

// CSS color-mix(in srgb, a pct%, b).
constexpr Color mix(Color a, Color b, float pct) {
  const float t = pct / 100.0f;
  return {a.r * t + b.r * (1 - t), a.g * t + b.g * (1 - t), a.b * t + b.b * (1 - t), a.a * t + b.a * (1 - t)};
}

constexpr Color with_alpha(Color c, float a) { return {c.r, c.g, c.b, a}; }

// Who draws: shape and layout. See docs/adr/0005-skins-own-shape-and-layout.md.
enum class SkinId : std::uint8_t { Cloud };

struct Theme {
  SkinId skin = SkinId::Cloud;
  bool dark = false;
  Color sky, hl, hl_text, hl_text_soft, hl_text_dark, hl_soft, hl_dark, hl_pale, hl_pale2, hl_mid;
  Color surface, on_accent, droplet, entry, error_text;
  Color cloud_top, cloud_base, cloud_bottom, cloud_mid, cloud_low, cloud_foot, cloud_shadow;
  float cloud_shadow_scale = 1;  // 0 for the dark theme (no glow)
};

constexpr Theme dark_cloud_palette() {
  Theme t;
  t.dark = true;
  t.hl = rgb(0x4d7fab);
  t.hl_dark = rgb(0x6393be);
  t.hl_soft = rgb(0x3b5878);
  t.hl_text = rgb(0xd0dfed);
  t.hl_text_soft = rgb(0xa0b4c9);
  t.hl_text_dark = rgb(0xecf3fa);
  t.hl_pale = rgb(0x293a50);
  t.hl_pale2 = rgb(0x344a63);
  t.hl_mid = rgb(0x3c536e);
  t.sky = rgb(0x101925);
  t.surface = rgb(0x233145);
  t.on_accent = rgb(0xeef5fb);
  t.droplet = rgb(0x27374c);
  t.error_text = rgb(0xf39b9b);
  t.entry = rgb(0x344a63);
  t.cloud_top = t.cloud_base = t.cloud_bottom = rgb(0x27374c);
  t.cloud_mid = t.cloud_low = t.cloud_foot = rgb(0x27374c);
  t.cloud_shadow = rgb(0x000000);
  t.cloud_shadow_scale = 0;
  return t;
}

// The light Cloud Themes differ by their accent.
constexpr Theme light_cloud_palette(Color accent) {
  Theme t;
  const Color white = rgb(0xFFFFFF);
  t.hl = accent;
  t.sky = t.hl;
  t.hl_text = t.hl;
  t.hl_soft = mix(t.hl, white, 70);
  t.hl_dark = mix(t.hl, rgb(0x3a9af0), 88);
  t.hl_text_soft = mix(t.hl_text, white, 65);
  t.hl_text_dark = mix(t.hl_text, rgb(0x3a9af0), 88);
  t.hl_pale = mix(t.hl, white, 14);
  t.hl_pale2 = mix(t.hl, white, 28);
  t.hl_mid = mix(t.hl, white, 55);
  t.surface = white;
  t.on_accent = white;
  t.droplet = white;
  t.entry = rgb(0xbbe1fd);
  t.error_text = rgb(0xdf7373);
  t.cloud_top = white;
  t.cloud_base = rgb(0xf3f9ff);
  t.cloud_bottom = rgb(0xeff8ff);
  t.cloud_mid = rgb(0xf3f8fe);
  t.cloud_low = rgb(0xf1f8ff);
  t.cloud_foot = rgb(0xe0eefb);
  t.cloud_shadow = rgb(0x4f7b99);
  return t;
}

}  // namespace crpp::ui
