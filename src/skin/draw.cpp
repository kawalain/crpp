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
    notes_ = screen.notes;
    list(screen.first, screen.count);
  }

  void run(const Part& part, Box box, const PartCall& call) {
    start(part.snap ? canvas_.snap(box) : box);
    frame_size_ = part.frame;
    label_ = call.label;
    seed_ = call.seed;
    vars[VarHot] = call.hot ? 1.0f : 0.0f;
    vars[VarPressed] = call.pressed ? 1.0f : 0.0f;
    vars[VarFocused] = call.focused ? 1.0f : 0.0f;
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

  float text_width(std::uint16_t style, std::uint32_t text, float width) override { return canvas_.text_width(text_of(text), look_of(style), width); }

  float text_height(std::uint16_t style, std::uint32_t text, float width) override { return canvas_.text_height(text_of(text), look_of(style), width); }

  float snap(float v) override { return canvas_.snap(v); }

  // A worked-out colour is a value below 0, which says where it is among the last few.
  float mix_colors(float a, float b, float percent) override { return worked_out(mix(color_at(a), color_at(b), percent)); }
  float alpha_of(float color, float alpha) override { return worked_out(with_alpha(color_at(color), alpha)); }

 private:
  void start(Box area) {
    vars[VarX] = area.x;
    vars[VarY] = area.y;
    vars[VarW] = area.w;
    vars[VarH] = area.h;
    vars[VarU] = canvas_.line();
    vars[VarFontSize] = canvas_.font_size();
    vars[VarDark] = pack_.palette_is_dark(palette_) ? 1.0f : 0.0f;
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

  [[nodiscard]] Color color_at(float v) {
    if (v < 0) return worked_[static_cast<std::size_t>(-v - 1) % worked_.size()];
    const Paint* paint = pack_.paint(palette_, v);
    return paint ? paint->color : Color{0, 0, 0, 0};
  }

  [[nodiscard]] float worked_out(Color c) {
    const std::size_t at = next_worked_++ % worked_.size();
    worked_[at] = c;
    return -static_cast<float>(at) - 1;
  }

  [[nodiscard]] Color color_of(Expr e) { return color_at(value(e)); }

  [[nodiscard]] std::uint64_t key_of(const Node& n) const { return mixed(seed_, static_cast<std::uint64_t>(&n - pack_.nodes.data())); }

  [[nodiscard]] static std::uint64_t mixed(std::uint64_t a, std::uint64_t b) { return (a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6) + (a >> 2))) * 0xFF51AFD7ED558CCDull; }

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
      if (n.slot != no_slot && n.kind != NodeKind::Set) std::fill_n(slots_.begin() + base_ + n.slot, n.kind == NodeKind::Let ? 1 : 4, 0.0f);
      return;
    }
    switch (n.kind) {
      case NodeKind::Let:
        if (n.transition) {
          slots_[base_ + n.slot] = canvas_.transition(n.has_alpha ? mixed(static_cast<std::uint64_t>(&n - pack_.nodes.data()), static_cast<std::uint64_t>(value(n.alpha))) : key_of(n), {.on = value(n.extra) != 0, .ms_on = value(n.width), .ms_off = value(n.extra2), .first_jumps = n.first_jumps, .curve = n.linear ? Curve::Linear : Curve::Ease, .again = n.has_size ? value(n.size) : 0});
          return;
        }
        [[fallthrough]];
      case NodeKind::Set: slots_[base_ + n.slot] = value(n.extra); return;
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
    if (n.has_note_by) {
      // A pack's number, so it reaches the screen only as one of its notes. NaN or a value out of
      // int's range matches none, and is never converted.
      const float by = value(n.note_by);
      const auto named = std::ranges::find_if(notes_, [by](int id) { return static_cast<float>(id) == by; });
      if (named != notes_.end()) source_.note(*named, static_cast<int>(vars[VarI]), at);
    }
    const Box box = n.snap ? canvas_.snap(at) : at;
    const float hot = vars[VarHot], pressed = vars[VarPressed], focused = vars[VarFocused];
    int hit = no_hit;
    HitPlace place;
    if (n.hit != no_hit) {
      hit = n.hit + static_cast<int>(n.has_hit_index ? value(n.hit_index) : vars[VarI]);
      if (hit < 0) {
        hit = no_hit;
      } else {
        vars[VarHot] = canvas_.hot(hit) ? 1.0f : 0.0f;
        vars[VarPressed] = canvas_.pressed(hit) ? 1.0f : 0.0f;
        vars[VarFocused] = canvas_.focused(hit) ? 1.0f : 0.0f;
        place.number = canvas_.next_hit();
        if (n.has_tab_order) place.tab_order = value(n.tab_order);
      }
    }
    if (n.has_live && value(n.live) == 0) {
      vars[VarHot] = vars[VarPressed] = vars[VarFocused] = 0;
      place.live = false;
    }

    switch (n.kind) {
      case NodeKind::Fill: fill(n, box); break;
      case NodeKind::Frame: canvas_.frame(box, color_of(n)); break;
      case NodeKind::Stroke:
        if (n.ellipse) canvas_.stroke_ellipse(box, color_of(n), value(n.width));
        else canvas_.stroke_round(box, value(n.extra), color_of(n), value(n.width));
        break;
      case NodeKind::Shadow: canvas_.shadow_round(box, value(n.extra), value(n.width), value(n.extra2), color_of(n)); break;
      case NodeKind::Line: canvas_.line(box.x, box.y, vars[VarX] + value(n.extra), vars[VarY] + value(n.extra2), color_of(n.paint), value(n.width)); break;
      case NodeKind::Path: path(n, box); break;
      case NodeKind::Text: {
        TextLook look = look_of(n.style);
        if (n.has_size) look.size = value(n.size);
        if (n.has_line_height) look.line_height = value(n.line_height);
        canvas_.text(text_of(n.text), box, look, color_of(n));
        break;
      }
      case NodeKind::Picture: source_.picture(n.bound, static_cast<int>(vars[VarI]), box, value(n.extra), value(n.width)); break;
      case NodeKind::Field: field(n, box); break;
      case NodeKind::Slot: slot(n, box); break;
      case NodeKind::Group:
        if (n.clip && n.soft) canvas_.push_soft_clip(box);
        else if (n.clip) canvas_.push_clip(box);
        if (n.has_effects) with_effects(n, box);
        else inside(box, n.first, n.count);
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
        const std::uint64_t outer_seed = seed_;
        for (int round = 0; round < rounds; ++round) {
          vars[VarI] = static_cast<float>(round);
          seed_ = mixed(outer_seed, static_cast<std::uint64_t>(round));
          inside(box, n.first, n.count);
        }
        seed_ = outer_seed;
        vars[VarI] = outer;
        break;
      }
      case NodeKind::Content:
      case NodeKind::Let:
      case NodeKind::Set:
      case NodeKind::Report: break;
    }
    if (hit != no_hit && (!n.has_hit_if || value(n.hit_if) != 0)) {
      if (n.hit_shape != no_text) {
        const HitShape& shape = pack_.hit_shapes[n.hit_shape];
        canvas_.hit_shape(at, hit, std::string_view(pack_.literals).substr(shape.data, shape.size), vars[VarX] + value(shape.x), vars[VarY] + value(shape.y), place);
      } else {
        canvas_.hit(at, hit, place);
      }
    }
    vars[VarHot] = hot;
    vars[VarPressed] = pressed;
    vars[VarFocused] = focused;
    label_ = outer_label;
    --depth_;
    below_ = vars[Vary] + vars[Varh] + (n.has_after ? value(n.after) : 0);
    drew_ = true;
  }

  void with_effects(const Node& n, Box box) {
    const GroupEffects& e = pack_.effects[n.more];
    const bool layer = e.has_opacity && canvas_.push_opacity(box, value(e.opacity));
    const bool fade = e.has_fade && canvas_.push_edge_fade(box, value(e.fade_top), value(e.fade_bottom));
    if (e.has_dy) canvas_.push_down(value(e.dy));
    if (e.origin) {
      canvas_.push_scale_at(box.x, box.y, value(e.scale));
      inside({0, 0, box.w, box.h}, n.first, n.count);
      canvas_.pop_scale();
      if (e.has_dy) canvas_.pop_scale();
      if (fade) canvas_.pop_layer();
      if (layer) canvas_.pop_layer();
      return;
    }
    if (e.has_scale) {
      const float about_x = e.has_about_x ? vars[VarX] + value(e.about_x) : box.x + box.w / 2;
      const float about_y = e.has_about_y ? vars[VarY] + value(e.about_y) : box.y + box.h / 2;
      canvas_.push_scale(about_x, about_y, value(e.scale));
    }
    inside(box, n.first, n.count);
    if (e.has_scale) canvas_.pop_scale();
    if (e.has_dy) canvas_.pop_scale();
    if (fade) canvas_.pop_layer();
    if (layer) canvas_.pop_layer();
  }

  void path(const Node& n, Box box) {
    const std::string_view data = std::string_view(pack_.literals).substr(n.text, n.count);
    const float scale = value(n.extra);
    if (n.stretched) {
      canvas_.path_stretched(data, box.x, box.y, scale, value(pack_.path_extras[n.more].scale_y), color_of(n.paint));
      return;
    }
    if (n.shadow) {
      canvas_.path_shadow(data, box.x, box.y, scale, value(n.width), value(pack_.path_extras[n.more].dy), color_of(n.paint));
      return;
    }
    const float paint_at = value(n.paint);
    const Paint* paint = paint_at >= 0 ? pack_.paint(palette_, paint_at) : nullptr;
    if (n.has_stops && paint && paint->stops != 0) {
      const PathExtra& more = pack_.path_extras[n.more];
      canvas_.path_gradient(data, box.x, box.y, scale, value(more.from_y), value(more.to_y), std::span<const GradientStop>(pack_.stops.data() + paint->first_stop, paint->stops));
      return;
    }
    canvas_.path(data, box.x, box.y, scale, color_at(paint_at), value(n.width), n.round_caps, n.soft);
  }

  void fill(const Node& n, Box box) {
    const float paint_at = value(n.paint);
    if (paint_at < 0 || n.ellipse) {
      Color color = color_at(paint_at);
      if (n.has_alpha) color.a = value(n.alpha);
      const float round = value(n.extra);
      if (n.ellipse) canvas_.fill_ellipse(box, color);
      else if (round > 0) canvas_.fill_round(box, round, color);
      else canvas_.fill(box, color);
      return;
    }
    const Paint* paint = pack_.paint(palette_, paint_at);
    if (!paint) return;
    const float radius = value(n.extra);
    if (paint->stops != 0) {
      const std::span<const GradientStop> stops(pack_.stops.data() + paint->first_stop, paint->stops);
      if (n.across) {
        canvas_.fill_across(box, stops);
        return;
      }
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
                         .placeholder_text = look_of(f.has_placeholder_style ? f.placeholder_style : n.style),
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
    std::uint32_t base;
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
    const std::uint64_t outer_seed = seed_;
    seed_ = mixed(seed_, static_cast<std::uint64_t>(&n - pack_.nodes.data()));
    const Call call{&u, base_, outer_label, call_};
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
    seed_ = outer_seed;
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
  std::span<const int> notes_;  // none for a part drawn on its own, as it has no screen
  int visited_ = 0, depth_ = 0;
  std::string_view label_;
  const Call* call_ = nullptr;
  bool drew_ = false;
  float below_ = 0;
  std::uint64_t seed_ = 0;
  std::array<Color, 64> worked_;
  std::size_t next_worked_ = 0;
};

}  // namespace

void draw(const Pack& pack, const Screen& screen, int palette, Box area, Canvas& canvas, Source& source) {
  Walker(pack, palette, canvas, source).run(screen, area);
}

void draw_part(const Pack& pack, const Part& part, int palette, Box box, Canvas& canvas, Source& source, const PartCall& call) {
  Walker(pack, palette, canvas, source).run(part, box, call);
}

}  // namespace crpp::skin
