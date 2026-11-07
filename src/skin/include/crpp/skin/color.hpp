#pragma once
// What a Skin's colours are made of.
namespace crpp::skin {

struct Color {
  float r = 0, g = 0, b = 0, a = 1;
};

struct GradientStop {
  float at;
  Color color;
};

}  // namespace crpp::skin
