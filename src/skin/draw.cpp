#include "crpp/skin/draw.hpp"

#include <algorithm>
#include <array>

#include "crpp/locale/locale.hpp"

namespace crpp::skin {

namespace {

class Walker final : public Env {
 public:
  Walker(const Pack& pack, int palette, Canvas& canvas, Source& source) : pack_(pack), palette_(palette), canvas_(canvas), source_(source) {}

  void run(const Screen& screen, Box area) {
    vars[VarX] = area.x;
    vars[VarY] = area.y;
    vars[VarW] = area.w;
    vars[VarH] = area.h;
    vars[VarU] = canvas_.line();
    vars[VarFontSize] = canvas_.font_size();
    frame = slots_.data();
    frame_size_ = screen.frame;
    list(screen.first, screen.count);
  }

  float number(std::uint16_t id) override { return source_.number(id); }

  float text_width(std::uint16_t style, std::uint32_t text) override {
    return canvas_.text_width(text_of(text), look_of(style));
  }

 private:
  [[nodiscard]] float value(Expr e) { return eval(pack_.code, e, *this); }

  [[nodiscard]] std::string_view text_of(std::uint32_t ref) {
    const TextRef& t = pack_.texts[ref];
    switch (t.kind) {
      case TextRef::Kind::Literal: return std::string_view(pack_.literals).substr(t.a, t.b);
      case TextRef::Kind::Locale: return locale::key_text(t.a);
      case TextRef::Kind::Bound: return source_.text(static_cast<int>(t.a), static_cast<int>(vars[VarI]));
      case TextRef::Kind::Label: return label_;
    }
    return {};
  }

  [[nodiscard]] TextLook look_of(std::uint16_t id) {
    const Style& s = pack_.styles[id];
    return {.size = value(s.size), .line_height = value(s.line_height), .weight = s.weight, .align = s.align, .wrap = s.wrap, .ellipsis = s.ellipsis, .exact = s.exact, .keep_words = s.keep_words};
  }

  [[nodiscard]] Color color_of(Expr e) {
    const Paint* paint = pack_.paint(palette_, value(e));
    return paint ? paint->color : Color{0, 0, 0, 0};
  }

  void list(std::uint32_t first, std::uint32_t count) {
    for (std::uint32_t i = 0; i < count; ++i) node(pack_.nodes[first + i]);
  }

  // Runs this list inside this box: X Y W H are that box's while it does, x y w h its nodes'.
  void inside(Box box, std::uint32_t first, std::uint32_t count) {
    const std::array<float, VarCount> saved = vars;
    vars[VarX] = box.x;
    vars[VarY] = box.y;
    vars[VarW] = box.w;
    vars[VarH] = box.h;
    list(first, count);
    vars = saved;
  }

  void node(const Node& n) {
    if (visited_ >= max_walk || depth_ >= max_walk_depth) return;
    ++visited_;
    if (n.has_if && value(n.when) == 0) {
      if (n.slot != no_slot) std::fill_n(slots_.begin() + base_ + n.slot, n.kind == NodeKind::Let ? 1 : 4, 0.0f);
      return;
    }
    if (n.kind == NodeKind::Let) {
      slots_[base_ + n.slot] = value(n.extra);
      return;
    }
    ++depth_;
    vars[Varx] = value(n.x);
    vars[Vary] = value(n.y);
    vars[Varw] = value(n.w);
    vars[Varh] = value(n.h);
    if (n.slot != no_slot) std::copy_n(vars.begin() + Varx, 4, slots_.begin() + base_ + n.slot);
    const Box at{vars[VarX] + vars[Varx], vars[VarY] + vars[Vary], vars[Varw], vars[Varh]};
    const Box box = n.snap ? canvas_.snap(at) : at;
    const float hot = vars[VarHot], pressed = vars[VarPressed];
    int hit = no_hit;
    if (n.hit != no_hit) {
      hit = n.hit + static_cast<int>(vars[VarI]);
      vars[VarHot] = canvas_.hot(hit) ? 1.0f : 0.0f;
      vars[VarPressed] = canvas_.pressed(hit) ? 1.0f : 0.0f;
    }
    switch (n.kind) {
      case NodeKind::Fill: fill(n, box); break;
      case NodeKind::Frame: canvas_.frame(box, color_of(n.paint)); break;
      case NodeKind::Stroke: canvas_.stroke_round(box, value(n.extra), color_of(n.paint), value(n.width)); break;
      case NodeKind::Text: canvas_.text(text_of(n.text), box, look_of(n.style), color_of(n.paint)); break;
      case NodeKind::Picture: source_.picture(n.bound, box); break;
      case NodeKind::Field: field(n, box); break;
      case NodeKind::Group:
        if (n.clip) canvas_.push_clip(box);
        inside(box, n.first, n.count);
        if (n.clip) canvas_.pop_clip();
        break;
      case NodeKind::Use: use(n, box); break;
      case NodeKind::Repeat: {
        const int rounds = std::clamp(static_cast<int>(value(n.extra)), 0, max_repeat);
        const float outer = vars[VarI];
        for (int round = 0; round < rounds; ++round) {
          vars[VarI] = static_cast<float>(round);
          inside(box, n.first, n.count);
        }
        vars[VarI] = outer;
        break;
      }
      case NodeKind::Let: break;
    }
    if (hit != no_hit) canvas_.hit(at, hit);
    vars[VarHot] = hot;
    vars[VarPressed] = pressed;
    --depth_;
  }

  void fill(const Node& n, Box box) {
    const Paint* paint = pack_.paint(palette_, value(n.paint));
    if (!paint) return;
    const float radius = value(n.extra);
    if (paint->stops != 0) {
      const std::span<const GradientStop> stops(pack_.stops.data() + paint->first_stop, paint->stops);
      if (radius > 0) canvas_.fill_round_down(box, radius, stops);
      else canvas_.fill_down(box, stops);
    } else if (radius > 0) {
      canvas_.fill_round(box, radius, paint->color);
    } else {
      canvas_.fill(box, paint->color);
    }
  }

  void field(const Node& n, Box box) {
    const FieldNode& f = pack_.fields[n.first];
    const FieldLook look{.text = look_of(n.style),
                         .color = color_of(n.paint),
                         .placeholder_color = color_of(f.placeholder_color),
                         .caret = color_of(f.caret),
                         .selection = color_of(f.selection),
                         .placeholder = text_of(f.placeholder)};
    source_.field(n.bound, box, look);
  }

  void use(const Node& n, Box box) {
    const Use& u = pack_.uses[n.first];
    const Part& part = pack_.parts[u.part];
    const std::uint32_t callee = base_ + frame_size_;
    // The arguments are read where the part is used, and go where it reads them.
    for (std::uint32_t i = 0; i < u.arguments; ++i) {
      const Argument& a = pack_.arguments[u.first_argument + i];
      slots_[callee + a.slot] = value(a.value);
    }
    const std::string_view label = u.label == no_text ? std::string_view{} : text_of(u.label);
    const std::string_view outer_label = label_;
    const std::uint32_t outer_base = base_, outer_size = frame_size_;
    label_ = label;
    base_ = callee;
    frame_size_ = part.frame;
    frame = slots_.data() + base_;
    inside(box, part.first, part.count);
    base_ = outer_base;
    frame_size_ = outer_size;
    frame = slots_.data() + base_;
    label_ = outer_label;
  }

  const Pack& pack_;
  int palette_;
  Canvas& canvas_;
  Source& source_;
  std::array<float, max_frame_slots> slots_;
  std::uint32_t base_ = 0, frame_size_ = 0;
  int visited_ = 0, depth_ = 0;
  std::string_view label_;
};

}  // namespace

void draw(const Pack& pack, const Screen& screen, int palette, Box area, Canvas& canvas, Source& source) {
  Walker(pack, palette, canvas, source).run(screen, area);
}

}  // namespace crpp::skin
