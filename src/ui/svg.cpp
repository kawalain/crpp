#include "crpp/ui/svg.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace crpp::ui {

namespace {

using Attrs = std::map<std::string, std::string, std::less<>>;

struct Tag {
  std::string name;
  Attrs attrs;
  bool closing = false;
  bool self_closing = false;
};

// Reads the next <...> tag starting at pos; skips comments, declarations and text.
bool next_tag(std::string_view s, std::size_t& pos, Tag& tag) {
  for (;;) {
    pos = s.find('<', pos);
    if (pos == std::string_view::npos) return false;
    if (s.compare(pos, 4, "<!--") == 0) {
      const auto end = s.find("-->", pos);
      if (end == std::string_view::npos) return false;
      pos = end + 3;
      continue;
    }
    if (pos + 1 < s.size() && (s[pos + 1] == '?' || s[pos + 1] == '!')) {
      const auto end = s.find('>', pos);
      if (end == std::string_view::npos) return false;
      pos = end + 1;
      continue;
    }
    break;
  }
  std::size_t i = pos + 1;
  tag = {};
  if (i < s.size() && s[i] == '/') {
    tag.closing = true;
    ++i;
  }
  const auto name_start = i;
  while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '>' && s[i] != '/') ++i;
  tag.name = std::string(s.substr(name_start, i - name_start));
  while (i < s.size() && s[i] != '>') {
    if (std::isspace(static_cast<unsigned char>(s[i]))) {
      ++i;
      continue;
    }
    if (s[i] == '/') {
      tag.self_closing = true;
      ++i;
      continue;
    }
    const auto key_start = i;
    while (i < s.size() && s[i] != '=' && s[i] != '>' && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    const std::string key(s.substr(key_start, i - key_start));
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i < s.size() && s[i] == '=') {
      ++i;
      while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
      if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
        const char q = s[i++];
        const auto v = s.find(q, i);
        if (v == std::string_view::npos) return false;
        tag.attrs[key] = std::string(s.substr(i, v - i));
        i = v + 1;
      }
    }
  }
  pos = i < s.size() ? i + 1 : i;
  return true;
}

std::string attr(const Attrs& a, std::string_view key) {
  const auto it = a.find(key);
  return it == a.end() ? std::string{} : it->second;
}

float number_attr(const Attrs& a, std::string_view key, float fallback = 0) {
  const auto it = a.find(key);
  return it == a.end() ? fallback : std::strtof(it->second.c_str(), nullptr);
}

// "fill:#fff;stroke:none" style declarations override presentation attributes.
void merge_style(Attrs& a) {
  const auto it = a.find("style");
  if (it == a.end()) return;
  std::istringstream in(it->second);
  std::string decl;
  while (std::getline(in, decl, ';')) {
    const auto colon = decl.find(':');
    if (colon == std::string::npos) continue;
    auto trim = [](std::string s) {
      while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
      while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
      return s;
    };
    a[trim(decl.substr(0, colon))] = trim(decl.substr(colon + 1));
  }
}

bool parse_color(const std::string& s, Color& out) {
  if (s.size() >= 4 && s[0] == '#') {
    unsigned v = 0;
    if (s.size() == 4) {
      for (int i = 1; i < 4; ++i) v = v << 8 | static_cast<unsigned>(std::strtoul(std::string(2, s[i]).c_str(), nullptr, 16));
    } else if (s.size() >= 7) {
      v = static_cast<unsigned>(std::strtoul(s.substr(1, 6).c_str(), nullptr, 16));
    } else {
      return false;
    }
    out = rgb(v);
    return true;
  }
  if (s == "white") { out = rgb(0xFFFFFF); return true; }
  if (s == "black") { out = rgb(0x000000); return true; }
  return false;
}

D2D1_MATRIX_3X2_F parse_transform(const std::string& t) {
  D2D1_MATRIX_3X2_F result = D2D1::Matrix3x2F::Identity();
  std::size_t i = 0;
  while (i < t.size()) {
    while (i < t.size() && (std::isspace(static_cast<unsigned char>(t[i])) || t[i] == ',')) ++i;
    const auto name_start = i;
    while (i < t.size() && std::isalpha(static_cast<unsigned char>(t[i]))) ++i;
    const std::string name = t.substr(name_start, i - name_start);
    if (name.empty() || i >= t.size() || t[i] != '(') break;
    const auto close = t.find(')', i);
    if (close == std::string::npos) break;
    std::vector<float> v;
    {
      const char* p = t.c_str() + i + 1;
      char* end = nullptr;
      while (p < t.c_str() + close) {
        const float f = std::strtof(p, &end);
        if (end == p) { ++p; continue; }
        v.push_back(f);
        p = end;
      }
    }
    D2D1_MATRIX_3X2_F m = D2D1::Matrix3x2F::Identity();
    if (name == "translate" && !v.empty()) m = D2D1::Matrix3x2F::Translation(v[0], v.size() > 1 ? v[1] : 0);
    else if (name == "scale" && !v.empty()) m = D2D1::Matrix3x2F::Scale(v[0], v.size() > 1 ? v[1] : v[0]);
    else if (name == "rotate" && !v.empty()) m = D2D1::Matrix3x2F::Rotation(v[0], D2D1::Point2F(v.size() > 2 ? v[1] : 0, v.size() > 2 ? v[2] : 0));
    else if (name == "matrix" && v.size() == 6) m = D2D1::Matrix3x2F(v[0], v[1], v[2], v[3], v[4], v[5]);
    // Transforms listed left to right apply outermost first: result = m * result.
    result = m * result;
    i = close + 1;
  }
  return result;
}

struct Inherited {
  bool fill_set = false, fill_none = false;
  Color fill{0, 0, 0, 1};
  bool stroke_set = false;
  Color stroke{0, 0, 0, 1};
  float stroke_width = 1;
  D2D1_MATRIX_3X2_F matrix = D2D1::Matrix3x2F::Identity();
};

void apply_paint(Inherited& st, const Attrs& a) {
  if (const auto f = attr(a, "fill"); !f.empty()) {
    if (f == "none") {
      st.fill_set = true;
      st.fill_none = true;
    } else if (parse_color(f, st.fill)) {
      st.fill_set = true;
      st.fill_none = false;
    }
  }
  if (const auto s = attr(a, "stroke"); !s.empty() && s != "none") {
    if (parse_color(s, st.stroke)) st.stroke_set = true;
  }
  if (a.find("stroke-width") != a.end()) st.stroke_width = number_attr(a, "stroke-width", 1);
}

}  // namespace

SvgImage parse_svg(const Gfx& gfx, std::string_view svg) {
  SvgImage image;
  std::vector<Inherited> stack(1);
  std::size_t pos = 0;
  Tag tag;
  while (next_tag(svg, pos, tag)) {
    if (tag.closing) {
      if (tag.name == "g" && stack.size() > 1) stack.pop_back();
      continue;
    }
    merge_style(tag.attrs);
    if (tag.name == "svg") {
      const auto vb = attr(tag.attrs, "viewBox");
      if (!vb.empty()) {
        float x = 0, y = 0, w = 36, h = 36;
        if (std::sscanf(vb.c_str(), "%f %f %f %f", &x, &y, &w, &h) == 4) {
          image.width = w;
          image.height = h;
          stack.back().matrix = D2D1::Matrix3x2F::Translation(-x, -y);
        }
      } else {
        image.width = number_attr(tag.attrs, "width", 36);
        image.height = number_attr(tag.attrs, "height", 36);
      }
      apply_paint(stack.back(), tag.attrs);
      continue;
    }
    if (tag.name == "g") {
      Inherited st = stack.back();
      apply_paint(st, tag.attrs);
      if (const auto t = attr(tag.attrs, "transform"); !t.empty()) st.matrix = parse_transform(t) * st.matrix;
      if (!tag.self_closing) stack.push_back(st);
      continue;
    }
    ComPtr<ID2D1Geometry> geometry;
    if (tag.name == "path") {
      auto g = gfx.parse_path(attr(tag.attrs, "d"));
      if (!g) continue;
      geometry = g.upcast<ID2D1Geometry>();
    } else if (tag.name == "circle" || tag.name == "ellipse") {
      const float rx = tag.name == "circle" ? number_attr(tag.attrs, "r") : number_attr(tag.attrs, "rx");
      const float ry = tag.name == "circle" ? rx : number_attr(tag.attrs, "ry");
      ComPtr<ID2D1EllipseGeometry> e;
      if (FAILED(gfx.d2d()->CreateEllipseGeometry(
              D2D1::Ellipse(D2D1::Point2F(number_attr(tag.attrs, "cx"), number_attr(tag.attrs, "cy")), rx, ry),
              e.put()))) {
        continue;
      }
      geometry = e.upcast<ID2D1Geometry>();
    } else {
      continue;  // unsupported element
    }
    Inherited st = stack.back();
    apply_paint(st, tag.attrs);
    SvgImage::Shape shape;
    shape.geometry = geometry;
    shape.matrix = st.matrix;
    if (const auto t = attr(tag.attrs, "transform"); !t.empty()) shape.matrix = parse_transform(t) * st.matrix;
    shape.fill = !st.fill_none;
    shape.fill_color = st.fill;
    shape.stroke = st.stroke_set;
    shape.stroke_color = st.stroke;
    shape.stroke_width = st.stroke_width;
    image.shapes.push_back(std::move(shape));
  }
  return image;
}

void draw_svg(Painter& p, const SvgImage& image, Rect dst, float opacity) {
  if (image.shapes.empty()) return;
  const float s = std::min(dst.w / image.width, dst.h / image.height);
  const float ox = dst.x + (dst.w - image.width * s) / 2;
  const float oy = dst.y + (dst.h - image.height * s) / 2;
  for (const auto& shape : image.shapes) {
    p.push(shape.matrix * D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(ox, oy));
    if (shape.fill) p.fill_geometry(shape.geometry.get(), with_alpha(shape.fill_color, shape.fill_color.a * opacity));
    if (shape.stroke) {
      p.stroke_geometry(shape.geometry.get(), with_alpha(shape.stroke_color, opacity), shape.stroke_width);
    }
  }
  p.pop();
}

const SvgImage& EmojiCache::get(const Gfx& gfx, const std::string& id) {
  for (auto it = entries_.begin(); it != entries_.end(); ++it) {
    if (it->id == id) {
      entries_.splice(entries_.begin(), entries_, it);
      return entries_.front().image;
    }
  }
  SvgImage image;
  for (const char* folder : {"emoji_profile_custom", "emoji_profile", "emoji_system"}) {
    std::ifstream in(dir_ / folder / (id + ".svg"), std::ios::binary);
    if (!in) continue;
    std::stringstream buffer;
    buffer << in.rdbuf();
    image = parse_svg(gfx, buffer.str());
    break;
  }
  entries_.push_front({id, std::move(image)});
  if (entries_.size() > capacity_) entries_.pop_back();
  return entries_.front().image;
}

}  // namespace crpp::ui
