#pragma once
// A Theme's colours. The Cloud ones are upstream's (style.css :root variables). Which Themes
// there are is in themes.hpp.
#include <array>
#include <cstdint>

#include "crpp/skin/color.hpp"

namespace crpp::ui {

using skin::Color;
using skin::GradientStop;

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
enum class SkinId : std::uint8_t { Cloud, Luna };
class Skin;

// What only the Luna Skin draws with. The light values are XP.css's and the system colours of
// its period; see docs/research/retro-messenger-look.md.
struct LunaColors {
  std::array<GradientStop, 8> caption, caption_idle;
  Color caption_ink, caption_shadow;
  Color frame_out, frame_mid, frame_in, frame_low_out, frame_low_mid, frame_low_in;
  std::array<GradientStop, 2> close, close_hot, caption_button, caption_button_hot;
  Color face, field, field_line, field_off;
  Color edge_light, edge_dark, edge_out;
  std::array<GradientStop, 3> button;
  std::array<GradientStop, 4> button_down;
  Color button_line, hover_0, hover_1, hover_2, hover_3, focus_0, focus_1, focus_2, focus_3;
  Color off_ink, off_shadow;
  Color check_line, check_from, check_to, check_hot_0, check_hot_1, tick;
  Color track, track_edge, thumb_line;
  std::array<GradientStop, 2> thumb;
  Color bar_thumb, bar_thumb_in, bar_thumb_low, bar_track_from, bar_track_to, bar_arrow;
  std::array<GradientStop, 7> progress;
  Color progress_line;
  Color group_line, legend;
  Color menu, menu_line, select, select_ink;
  Color tooltip, tooltip_line, tooltip_ink;
  std::array<GradientStop, 2> header, info;
  Color info_line;
  Color glow_0, glow_1, glow_2;
  Color ink, ink_soft, link, picture_line;
};

struct Theme {
  SkinId skin = SkinId::Cloud;
  bool dark = false;
  Color sky, hl, hl_text, hl_text_soft, hl_text_dark, hl_soft, hl_dark, hl_pale, hl_pale2, hl_mid;
  Color surface, on_accent, droplet, entry, error_text;
  Color ink_strong;  // what must be easy to read: a warning, the first start's words
  Color cloud_top, cloud_base, cloud_bottom, cloud_mid, cloud_low, cloud_foot, cloud_shadow;
  float cloud_shadow_scale = 1;  // 0 for the dark theme (no glow)
  LunaColors luna{};             // only for a Theme of the Luna Skin
  const Skin* from_pack = nullptr;  // a Skin Pack draws this Theme's controls
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
  t.ink_strong = t.hl_text_dark;
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
  t.ink_strong = rgb(0x2e5f80);
  return t;
}

// The fields every Skin reads, from what a Luna palette has.
constexpr Theme luna_theme(const LunaColors& l, bool dark) {
  Theme t;
  t.skin = SkinId::Luna;
  t.dark = dark;
  t.luna = l;
  t.sky = t.surface = t.droplet = l.face;
  t.entry = l.field;
  t.hl = t.hl_dark = t.hl_mid = l.select;
  t.hl_text = t.hl_text_dark = t.ink_strong = l.ink;
  t.hl_text_soft = l.ink_soft;
  t.hl_soft = mix(l.select, l.field, 35);
  t.hl_pale = mix(l.select, l.field, 12);
  t.hl_pale2 = mix(l.select, l.field, 24);
  t.on_accent = l.select_ink;
  t.error_text = dark ? rgb(0xff8f8f) : rgb(0xc00000);
  t.cloud_shadow_scale = 0;
  return t;
}

constexpr LunaColors messenger_colors() {
  LunaColors l{};
  l.caption = {{{0, rgb(0x0997ff)}, {0.08f, rgb(0x0053ee)}, {0.40f, rgb(0x0050ee)}, {0.88f, rgb(0x0066ff)},
                {0.93f, rgb(0x0066ff)}, {0.95f, rgb(0x005bff)}, {0.96f, rgb(0x003dd7)}, {1, rgb(0x003dd7)}}};
  l.caption_idle = {{{0, rgb(0xa9c0f0)}, {0.08f, rgb(0x7a96df)}, {0.40f, rgb(0x7a96df)}, {0.88f, rgb(0x8aa7e4)},
                     {0.93f, rgb(0x8aa7e4)}, {0.95f, rgb(0x7d9ae0)}, {0.96f, rgb(0x6f8bd6)}, {1, rgb(0x6f8bd6)}}};
  l.caption_ink = rgb(0xffffff);
  l.caption_shadow = rgb(0x0f1089);
  l.frame_out = rgb(0x0831d9);
  l.frame_mid = rgb(0x166aee);
  l.frame_in = rgb(0x0855dd);
  l.frame_low_out = rgb(0x00138c);
  l.frame_low_mid = rgb(0x001ea0);
  l.frame_low_in = rgb(0x0855dd);
  l.close = {{{0, rgb(0xe46142)}, {1, rgb(0xdc451f)}}};
  l.close_hot = {{{0, rgb(0xf08a70)}, {1, rgb(0xe8603c)}}};
  l.caption_button = {{{0, rgb(0x3c7ff0)}, {1, rgb(0x1d4fd0)}}};
  l.caption_button_hot = {{{0, rgb(0x6aa0f8)}, {1, rgb(0x3468e4)}}};
  l.face = rgb(0xece9d8);
  l.field = rgb(0xffffff);
  l.field_line = rgb(0x7f9db9);
  l.field_off = rgb(0xece9d8);
  l.edge_light = rgb(0xffffff);
  l.edge_dark = rgb(0xaca899);
  l.edge_out = rgb(0x716f64);
  l.button = {{{0, rgb(0xffffff)}, {0.86f, rgb(0xecebe5)}, {1, rgb(0xd8d0c4)}}};
  l.button_down = {{{0, rgb(0xcdcac3)}, {0.08f, rgb(0xe3e3db)}, {0.94f, rgb(0xe5e5de)}, {1, rgb(0xf2f2f1)}}};
  l.button_line = rgb(0x003c74);
  l.hover_0 = rgb(0xfff0cf);
  l.hover_1 = rgb(0xfdd889);
  l.hover_2 = rgb(0xfbc761);
  l.hover_3 = rgb(0xe5a01a);
  l.focus_0 = rgb(0xcee7ff);
  l.focus_1 = rgb(0x98b8ea);
  l.focus_2 = rgb(0xbcd4f6);
  l.focus_3 = rgb(0x89ade4);
  l.off_ink = rgb(0x808080);
  l.off_shadow = rgb(0xffffff);
  l.check_line = rgb(0x1d5281);
  l.check_from = rgb(0xdcdcd7);
  l.check_to = rgb(0xffffff);
  l.check_hot_0 = rgb(0xf8b636);
  l.check_hot_1 = rgb(0xfedf9c);
  l.tick = rgb(0x22a122);
  l.track = rgb(0xecebe4);
  l.track_edge = rgb(0x9d9c99);
  l.thumb_line = rgb(0x7f9db9);
  l.thumb = {{{0, rgb(0xffffff)}, {1, rgb(0xe3e1d2)}}};
  l.bar_thumb = rgb(0xc8d6fb);
  l.bar_thumb_in = rgb(0xbad1fc);
  l.bar_thumb_low = rgb(0xb7caf5);
  l.bar_track_from = rgb(0xf3f1ec);
  l.bar_track_to = rgb(0xfefefb);
  l.bar_arrow = rgb(0x4d6185);
  l.progress = {{{0, rgb(0xacedad)}, {0.17f, rgb(0x7be47d)}, {0.33f, rgb(0x4cda50)}, {0.5f, rgb(0x2ed330)},
                 {0.67f, rgb(0x42d845)}, {0.83f, rgb(0x76e275)}, {1, rgb(0x8fe791)}}};
  l.progress_line = rgb(0x686868);
  l.group_line = rgb(0xd0d0bf);
  l.legend = rgb(0x0046d5);
  l.menu = rgb(0xffffff);
  l.menu_line = rgb(0xaca899);
  l.select = rgb(0x316ac5);
  l.select_ink = rgb(0xffffff);
  l.tooltip = rgb(0xffffe1);
  l.tooltip_line = rgb(0x000000);
  l.tooltip_ink = rgb(0x000000);
  l.header = {{{0, rgb(0xeef3fc)}, {1, rgb(0xc9d9f4)}}};
  l.info = {{{0, rgb(0xe6eefb)}, {1, rgb(0xc9daf5)}}};
  l.info_line = rgb(0x7f9db9);
  l.glow_0 = rgb(0xf8b636);
  l.glow_1 = rgb(0xfbc761);
  l.glow_2 = rgb(0xfedf9c);
  l.ink = rgb(0x000000);
  l.ink_soft = rgb(0x5a5a5a);
  l.link = rgb(0x0046d5);
  l.picture_line = rgb(0x7f9db9);
  return l;
}

// The same shapes at night: a darkened caption, deep faces, and the orange of the period kept.
// There was no such thing then, so these values are ours.
constexpr LunaColors messenger_dark_colors() {
  LunaColors l = messenger_colors();
  l.caption = {{{0, rgb(0x4a6491)}, {0.08f, rgb(0x1d2f52)}, {0.40f, rgb(0x1d2f52)}, {0.88f, rgb(0x253a63)},
                {0.93f, rgb(0x253a63)}, {0.95f, rgb(0x1d2f52)}, {0.96f, rgb(0x142440)}, {1, rgb(0x142440)}}};
  l.caption_idle = {{{0, rgb(0x3a4252)}, {0.08f, rgb(0x2a303c)}, {0.40f, rgb(0x2a303c)}, {0.88f, rgb(0x303744)},
                     {0.93f, rgb(0x303744)}, {0.95f, rgb(0x2a303c)}, {0.96f, rgb(0x232833)}, {1, rgb(0x232833)}}};
  l.caption_ink = rgb(0xf2f5fb);
  l.caption_shadow = rgb(0x0a1222);
  l.frame_out = rgb(0x142440);
  l.frame_mid = rgb(0x2a4066);
  l.frame_in = rgb(0x1d2f52);
  l.frame_low_out = rgb(0x0c1628);
  l.frame_low_mid = rgb(0x142440);
  l.frame_low_in = rgb(0x1d2f52);
  l.close = {{{0, rgb(0xb4503a)}, {1, rgb(0x96361f)}}};
  l.close_hot = {{{0, rgb(0xd86c52)}, {1, rgb(0xbc4a2e)}}};
  l.caption_button = {{{0, rgb(0x3a5484)}, {1, rgb(0x253a63)}}};
  l.caption_button_hot = {{{0, rgb(0x54719f)}, {1, rgb(0x365280)}}};
  l.face = rgb(0x232833);
  l.field = rgb(0x12161f);
  l.field_line = rgb(0x4b5a75);
  l.field_off = rgb(0x1c212b);
  l.edge_light = rgb(0x3b4252);
  l.edge_dark = rgb(0x151922);
  l.edge_out = rgb(0x0c0f15);
  l.button = {{{0, rgb(0x424a5c)}, {0.86f, rgb(0x333a49)}, {1, rgb(0x282e3a)}}};
  l.button_down = {{{0, rgb(0x1c212b)}, {0.08f, rgb(0x262c38)}, {0.94f, rgb(0x282e3a)}, {1, rgb(0x323949)}}};
  l.button_line = rgb(0x0c1628);
  l.focus_0 = rgb(0x6d8fc4);
  l.focus_1 = rgb(0x4a6491);
  l.focus_2 = rgb(0x5a7aad);
  l.focus_3 = rgb(0x3f5880);
  l.off_ink = rgb(0x6e7686);
  l.off_shadow = rgb(0x151922);
  l.check_line = rgb(0x6d8fc4);
  l.check_from = rgb(0x1c212b);
  l.check_to = rgb(0x12161f);
  l.tick = rgb(0x5cd65c);
  l.track = rgb(0x3b4252);
  l.track_edge = rgb(0x151922);
  l.thumb_line = rgb(0x6d8fc4);
  l.thumb = {{{0, rgb(0x4a5266)}, {1, rgb(0x333a49)}}};
  l.bar_thumb = rgb(0x3a4a68);
  l.bar_thumb_in = rgb(0x34435f);
  l.bar_thumb_low = rgb(0x2c3a53);
  l.bar_track_from = rgb(0x1c212b);
  l.bar_track_to = rgb(0x232833);
  l.bar_arrow = rgb(0xc4cfe4);
  l.progress_line = rgb(0x4b5a75);
  l.group_line = rgb(0x3b4252);
  l.legend = rgb(0x8db4f2);
  l.menu = rgb(0x1c212b);
  l.menu_line = rgb(0x3b4252);
  l.select = rgb(0x3a68b4);
  l.select_ink = rgb(0xffffff);
  l.tooltip = rgb(0x3d3a26);
  l.tooltip_line = rgb(0x8c8660);
  l.tooltip_ink = rgb(0xf5f1d8);
  l.header = {{{0, rgb(0x2c3546)}, {1, rgb(0x232b3b)}}};
  l.info = {{{0, rgb(0x2c3a53)}, {1, rgb(0x233046)}}};
  l.info_line = rgb(0x4b5a75);
  l.ink = rgb(0xe6ebf5);
  l.ink_soft = rgb(0xa2adc2);
  l.link = rgb(0x8db4f2);
  l.picture_line = rgb(0x4b5a75);
  return l;
}

}  // namespace crpp::ui
