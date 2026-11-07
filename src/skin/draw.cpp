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
    start(area);
    frame_size_ = screen.frame;
    list(screen.first, screen.count);
  }

  void run(const Part& part, Box box, const PartCall& call) {
    start(part.snap ? canvas_.snap(box) : box);
    frame_size_ = part.frame;
    label_ = call.label;
    vars[VarHot] = call.hot ? 1.0f : 0.0f;
    vars[VarPressed] = call.pressed ? 1.0f : 0.0f;
    for (std::size_t i = 0; i < part.argument_names.size(); ++i) {
      float value_of_argument = this->value(part.argument_defaults[i]);
      for (const auto& [name, given] : call.args) {
        if (name == part.argument_names[i]) value_of_argument = given;
      }
      slots_[i] = value_of_argument;
    }
    list(part.first, part.count);
  }

  float number(std::uint16_t id) override { return source_.number(id, static_cast<int>(vars[VarI])); }

  float text_width(std::uint16_t style, std::uint32_t text) override { return canvas_.text_width(text_of(text), look_of(style)); }

  float text_height(std::uint16_t style, std::uint32_t text, float width) override { return canvas_.text_height(text_of(text), look_of(style), width); }

  float snap(float v) override { return canvas_.snap(v); }

 private:
  void start(Box area) {
    vars[VarX] = area.x;
    vars[VarY] = area.y;
    vars[VarW] = area.w;
    vars[VarH] = area.h;
    vars[VarU] = canvas_.line();
    vars[VarFontSize] = canvas_.font_size();
    frame = slots_.data();
  }

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
    return {.size = value(s.size), .line_height = value(s.line_height), .letter_spacing = s.letter_spacing, .weight = s.weight, .align = s.align, .role = s.role, .wrap = s.wrap, .ellipsis = s.ellipsis, .exact = s.exact, .keep_words = s.keep_words};
  }

  [[nodiscard]] Color color_of(Expr e) {
    const Paint* paint = pack_.paint(palette_, value(e));
    return paint ? paint->color : Color{0, 0, 0, 0};
  }

  [[nodiscard]] Color color_of(const Node& n) {
    Color c = color_of(n.paint);
    if (n.has_alpha) c.a = value(n.alpha);
    return c;
  }

  void list(std::uint32_t first, std::uint32_t count) {
    for (std::uint32_t i = 0; i < count; ++i) node(pack_.nodes[first + i]);
  }

  // Each node that drew moves flow below itself.
  void column(std::uint32_t first, std::uint32_t count) {
    for (std::uint32_t i = 0; i < count; ++i) {
      drew_ = false;
      node(pack_.nodes[first + i]);
      if (drew_) vars[VarFlow] = below_;
    }
    drew_ = false;
  }

  void enter(Box box) {
    vars[VarX] = box.x;
    vars[VarY] = box.y;
    vars[VarW] = box.w;
    vars[VarH] = box.h;
    vars[VarFlow] = 0;
  }

  // Runs this list inside this box: X Y W H are that box's while it does, x y w h its nodes'.
  void inside(Box box, std::uint32_t first, std::uint32_t count) {
    const std::array<float, VarCount> saved = vars;
    enter(box);
    list(first, count);
    vars = saved;
  }

  void node(const Node& n) {
    if (visited_ >= max_walk || depth_ >= max_walk_depth) return;
    ++visited_;
    drew_ = false;
    if (n.has_if && value(n.when) == 0) {
      if (n.slot != no_slot) std::fill_n(slots_.begin() + base_ + n.slot, n.kind == NodeKind::Let ? 1 : 4, 0.0f);
      return;
    }
    switch (n.kind) {
      case NodeKind::Let: slots_[base_ + n.slot] = value(n.extra); return;
      case NodeKind::Report: source_.report(n.bound, value(n.extra)); return;
      case NodeKind::Content: content(); return;
      default: break;
    }
    ++depth_;
    const Use* const used = n.kind == NodeKind::Use ? &pack_.uses[n.first] : nullptr;
    const std::string_view outer_label = label_;
    if (used) label_ = used->label != no_text ? text_of(used->label) : std::string_view{};  // a part's own size may depend on it

    vars[Varx] = n.from_right ? 0 : value(n.x);
    vars[Vary] = n.from_bottom ? 0 : value(n.y);
    vars[Varw] = value(n.w);
    vars[Varh] = value(n.h);
    if (n.from_right) vars[Varx] = value(n.x) - vars[Varw];
    if (n.from_bottom) vars[Vary] = value(n.y) - vars[Varh];
    if (n.slot != no_slot) std::copy_n(vars.begin() + Varx, 4, slots_.begin() + base_ + n.slot);
    const Box at{vars[VarX] + vars[Varx], vars[VarY] + vars[Vary], vars[Varw], vars[Varh]};
    if (n.note != no_hit) source_.note(n.note, static_cast<int>(vars[VarI]), at);
    const Box box = n.snap ? canvas_.snap(at) : at;
    const float hot = vars[VarHot], pressed = vars[VarPressed];
    int hit = no_hit;
    if (n.hit != no_hit) {
      hit = n.hit + static_cast<int>(n.has_hit_index ? value(n.hit_index) : vars[VarI]);
      vars[VarHot] = canvas_.hot(hit) ? 1.0f : 0.0f;
      vars[VarPressed] = canvas_.pressed(hit) ? 1.0f : 0.0f;
    }
    if (n.has_live && value(n.live) == 0) vars[VarHot] = vars[VarPressed] = 0;

    switch (n.kind) {
      case NodeKind::Fill: fill(n, box); break;
      case NodeKind::Frame: canvas_.frame(box, color_of(n)); break;
      case NodeKind::Stroke: canvas_.stroke_round(box, value(n.extra), color_of(n), value(n.width)); break;
      case NodeKind::Line: canvas_.line(box.x, box.y, vars[VarX] + value(n.extra), vars[VarY] + value(n.extra2), color_of(n.paint), value(n.width)); break;
      case NodeKind::Path: canvas_.path(std::string_view(pack_.literals).substr(n.text, n.count), box.x, box.y, value(n.extra), color_of(n.paint), value(n.width), n.round_caps); break;
      case NodeKind::Text: canvas_.text(text_of(n.text), box, look_of(n.style), color_of(n)); break;
      case NodeKind::Picture: source_.picture(n.bound, static_cast<int>(vars[VarI]), box, value(n.extra), value(n.width)); break;
      case NodeKind::Field: field(n, box); break;
      case NodeKind::Slot: slot(n, box); break;
      case NodeKind::Group:
        if (n.clip) canvas_.push_clip(box);
        inside(box, n.first, n.count);
        if (n.clip) canvas_.pop_clip();
        break;
      case NodeKind::Column: {
        if (n.clip) canvas_.push_clip(box);
        const std::array<float, VarCount> saved = vars;
        enter(box);
        column(n.first, n.count);
        const float end = vars[VarFlow];
        vars = saved;
        if (n.auto_h) {
          vars[Varh] = end;
          if (n.slot != no_slot) slots_[base_ + n.slot + 3] = end;
        }
        if (n.clip) canvas_.pop_clip();
        break;
      }
      case NodeKind::Use: use(n, *used, box, outer_label); break;
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
      case NodeKind::Content:
      case NodeKind::Let:
      case NodeKind::Report: break;
    }
    if (hit != no_hit && (!n.has_hit_if || value(n.hit_if) != 0)) canvas_.hit(at, hit);
    vars[VarHot] = hot;
    vars[VarPressed] = pressed;
    label_ = outer_label;
    --depth_;
    below_ = vars[Vary] + vars[Varh] + (n.has_after ? value(n.after) : 0);
    drew_ = true;
  }

  void fill(const Node& n, Box box) {
    const Paint* paint = pack_.paint(palette_, value(n.paint));
    if (!paint) return;
    const float radius = value(n.extra);
    if (paint->stops != 0) {
      const std::span<const GradientStop> stops(pack_.stops.data() + paint->first_stop, paint->stops);
      const float y1 = n.has_down_to ? box.y + value(n.width) : box.y + box.h;
      if (radius > 0) canvas_.fill_round_down(box, radius, box.y, y1, stops);
      else canvas_.fill_down(box, box.y, y1, stops);
    } else if (radius > 0) {
      canvas_.fill_round(box, radius, n.has_alpha ? Color{paint->color.r, paint->color.g, paint->color.b, value(n.alpha)} : paint->color);
    } else {
      canvas_.fill(box, n.has_alpha ? Color{paint->color.r, paint->color.g, paint->color.b, value(n.alpha)} : paint->color);
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
    source_.field(n.bound, static_cast<int>(vars[VarI]), box, look);
  }

  void slot(const Node& n, Box box) {
    const SlotNode& s = pack_.slots[n.first];
    std::array<float, max_slot_values> numbers;
    std::array<TextLook, max_slot_values> styles;
    std::array<Color, max_slot_values> colors;
    for (std::uint32_t i = 0; i < s.numbers; ++i) numbers[i] = value(pack_.slot_values[s.first_number + i]);
    for (std::uint32_t i = 0; i < s.styles; ++i) styles[i] = look_of(pack_.slot_styles[s.first_style + i]);
    for (std::uint32_t i = 0; i < s.colors; ++i) colors[i] = color_of(pack_.slot_values[s.first_color + i]);
    source_.slot(n.bound, static_cast<int>(vars[VarI]), box, {std::span(numbers).first(s.numbers), std::span(styles).first(s.styles), std::span(colors).first(s.colors)});
  }

  // Where a use's own list goes: it runs among the part's nodes, but with the names of the place
  // it was written in.
  struct Call {
    const Use* use;
    std::uint32_t base, frame_size;
    std::string_view label;
    const Call* outer;
  };

  void content() {
    if (!call_) return;
    const Call here = *call_;
    const Call* const inner_call = call_;
    const std::uint32_t inner_base = base_, inner_size = frame_size_;
    const std::string_view inner_label = label_;
    // Parts it uses go above what is in use now.
    frame_size_ = inner_base + inner_size - here.base;
    base_ = here.base;
    frame = slots_.data() + base_;
    label_ = here.label;
    call_ = here.outer;
    column(here.use->first, here.use->count);
    call_ = inner_call;
    base_ = inner_base;
    frame_size_ = inner_size;
    frame = slots_.data() + base_;
    label_ = inner_label;
  }

  void use(const Node& n, const Use& u, Box box, std::string_view outer_label) {
    const Part& part = pack_.parts[u.part];
    const std::uint32_t callee = base_ + frame_size_;
    if (callee + part.frame > max_frame_slots) return;
    // The arguments are read where the part is used, and go where it reads them.
    for (std::uint32_t i = 0; i < u.arguments; ++i) {
      const Argument& a = pack_.arguments[u.first_argument + i];
      slots_[callee + a.slot] = value(a.value);
    }
    const Call call{&u, base_, frame_size_, outer_label, call_};
    const Call* const outer_call = call_;
    const std::uint32_t outer_base = base_, outer_size = frame_size_;
    call_ = &call;
    base_ = callee;
    frame_size_ = part.frame;
    frame = slots_.data() + base_;
    const std::array<float, VarCount> saved = vars;
    enter(box);
    list(part.first, part.count);
    const float height = part.has_height ? value(part.height) : saved[Varh];
    vars = saved;
    call_ = outer_call;
    base_ = outer_base;
    frame_size_ = outer_size;
    frame = slots_.data() + base_;
    if (part.has_height) {
      vars[Varh] = height;
      if (n.slot != no_slot) slots_[base_ + n.slot + 3] = height;
    }
  }

  const Pack& pack_;
  int palette_;
  Canvas& canvas_;
  Source& source_;
  std::array<float, max_frame_slots> slots_;
  std::uint32_t base_ = 0, frame_size_ = 0;
  int visited_ = 0, depth_ = 0;
  std::string_view label_;
  const Call* call_ = nullptr;
  bool drew_ = false;
  float below_ = 0;
};

}  // namespace

void draw(const Pack& pack, const Screen& screen, int palette, Box area, Canvas& canvas, Source& source) {
  Walker(pack, palette, canvas, source).run(screen, area);
}

void draw_part(const Pack& pack, const Part& part, int palette, Box box, Canvas& canvas, Source& source, const PartCall& call) {
  Walker(pack, palette, canvas, source).run(part, box, call);
}

}  // namespace crpp::skin
