#pragma once
// What a Skin's colours are made of.
namespace crpp::skin {

struct Color {
  float r = 0, g = 0, b = 0, a = 1;
};

// CSS color-mix(in srgb, a pct%, b).
constexpr Color mix(Color a, Color b, float pct) {
  const float t = pct / 100.0f;
  return {a.r * t + b.r * (1 - t), a.g * t + b.g * (1 - t), a.b * t + b.b * (1 - t), a.a * t + b.a * (1 - t)};
}

constexpr Color with_alpha(Color c, float a) { return {c.r, c.g, c.b, a}; }

struct GradientStop {
  float at;
  Color color;
};

}  // namespace crpp::skin
