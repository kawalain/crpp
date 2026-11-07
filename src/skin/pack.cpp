#include "crpp/skin/pack.hpp"

#include <algorithm>
#include <charconv>
#include <utility>

#include "crpp/locale/locale.hpp"
#include "crpp/store/json.hpp"
#include "keys.hpp"

namespace crpp::skin {

namespace {

using store::Json;


[[nodiscard]] std::optional<int> find_name(std::span<const Name> names, std::string_view name) {
  for (const Name& n : names) {
    if (n.name == name) return n.id;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<Color> parse_color(std::string_view s) {
  if (s.size() != 7 && s.size() != 9) return std::nullopt;
  if (s[0] != '#') return std::nullopt;
  std::uint32_t value = 0;
  const auto [end, ec] = std::from_chars(s.data() + 1, s.data() + s.size(), value, 16);
  if (ec != std::errc{} || end != s.data() + s.size()) return std::nullopt;
  const std::uint32_t rgb = s.size() == 9 ? value >> 8 : value;
  const float a = s.size() == 9 ? static_cast<float>(value & 0xFF) / 255.0f : 1.0f;
  return Color{static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f, static_cast<float>(rgb & 0xFF) / 255.0f, a};
}

[[nodiscard]] std::optional<float> as_float(const Json& json) {
  const std::string_view text = json.number_text();
  if (text.empty()) return std::nullopt;
  float value = 0;
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc{} || end != text.data() + text.size()) return std::nullopt;
  return value;
}

[[nodiscard]] bool key_allowed(NodeKind kind, std::string_view key) {
  const auto named = [&](const KeyDoc& k) { return k.key == key; };
  return std::ranges::any_of(common_keys, named) || std::ranges::any_of(node_kinds[static_cast<std::size_t>(kind)].keys, named);
}

}  // namespace

namespace {

// Names visible where a node is being compiled.
struct Scope {
  Scope* parent = nullptr;
  std::vector<std::pair<std::string, std::uint16_t>> lets, boxes;
};

class Loader final : public Names {
 public:
  explicit Loader(const ReadFile& read) : read_(read) {}

  bool load(std::span<const ScreenSchema> schemas) {
    // What a node has when it says nothing, shared by all.
    pack_.code.push_back({Op::Var, VarW, 0});
    pack_.code.push_back({Op::Var, VarH, 0});
    pack_.code.push_back({Op::Var, VarFlow, 0});
    const auto root = parse_file("pack.jsonc", read_("pack.jsonc"));
    if (!root) return false;
    where_ = "pack.jsonc";
    const Json* format = root->find("format");
    if (!format || format->as_i64() != pack_format) return fail("this exe reads format " + std::to_string(pack_format));
    const Json* id = root->find("id");
    if (!id || !id->as_string()) return fail("no id");
    pack_.id = std::string(*id->as_string());
    if (const Json* constants = root->find("constants"); constants && !load_constants(*constants)) return false;
    if (const Json* fonts = root->find("fonts"); fonts && !load_fonts(*fonts)) return false;
    if (!load_palettes(root->find("palettes")) || !load_styles(root->find("styles")) || !load_parts(root->find("parts"))) return false;
    for (const ScreenSchema& schema : schemas) {
      if (!load_screen(schema)) return false;
    }
    pack_.paint_names_list = paint_names_;
    pack_.nodes.shrink_to_fit();
    pack_.code.shrink_to_fit();
    return true;
  }

  [[nodiscard]] const std::string& error() const { return error_; }
  [[nodiscard]] Pack take() { return std::move(pack_); }

  // --- Names ---

  std::optional<std::uint16_t> slot(std::string_view name) override {
    for (const Scope* s = scope_; s; s = s->parent) {
      for (const auto& [n, at] : s->lets) {
        if (n == name) return at;
      }
    }
    return std::nullopt;
  }

  std::optional<std::uint16_t> box(std::string_view name) override {
    if (!scope_) return std::nullopt;
    for (const auto& [n, at] : scope_->boxes) {
      if (n == name) return at;
    }
    return std::nullopt;
  }

  std::optional<float> constant(std::string_view name) override {
    for (const auto& [n, value] : pack_.constants) {
      if (n == name) return value;
    }
    return std::nullopt;
  }

  std::optional<std::uint16_t> number(std::string_view name) override {
    if (!schema_) return std::nullopt;
    const auto id = find_name(schema_->numbers, name);
    if (!id) return std::nullopt;
    return static_cast<std::uint16_t>(*id);
  }

  std::optional<std::uint16_t> paint(std::string_view name) override {
    for (std::size_t i = 0; i < paint_names_.size(); ++i) {
      if (paint_names_[i] == name) return static_cast<std::uint16_t>(i);
    }
    return std::nullopt;
  }

  std::optional<std::uint16_t> style(std::string_view name) override {
    for (std::size_t i = 0; i < style_names_.size(); ++i) {
      if (style_names_[i] == name) return static_cast<std::uint16_t>(i);
    }
    return std::nullopt;
  }

  std::optional<int> hit(std::string_view name) override { return schema_ ? find_name(schema_->hits, name) : std::nullopt; }
  std::optional<int> note(std::string_view name) override { return schema_ ? find_name(schema_->notes, name) : std::nullopt; }

  std::optional<std::uint32_t> text(std::string_view source) override {
    TextRef ref;
    if (source.starts_with('#')) {
      const auto key = locale::key_index(source.substr(1));
      if (!key) return std::nullopt;
      ref.kind = TextRef::Kind::Locale;
      ref.a = static_cast<std::uint32_t>(*key);
    } else if (source.starts_with('@')) {
      const std::string_view name = source.substr(1);
      if (!schema_) {  // a part knows nothing of screens
        if (name != "label") return std::nullopt;
        ref.kind = TextRef::Kind::Label;
      } else {
        const auto id = find_name(schema_->texts, name);
        if (!id) return std::nullopt;
        ref.kind = TextRef::Kind::Bound;
        ref.a = static_cast<std::uint32_t>(*id);
      }
    } else {
      ref.a = static_cast<std::uint32_t>(pack_.literals.size());
      ref.b = static_cast<std::uint32_t>(source.size());
      pack_.literals += source;
    }
    pack_.texts.push_back(ref);
    return static_cast<std::uint32_t>(pack_.texts.size() - 1);
  }

 private:
  bool fail(std::string what) {
    if (error_.empty()) error_ = where_ + ": " + std::move(what);
    return false;
  }

  std::optional<Json> parse_file(std::string path, const std::optional<std::string>& text) {
    where_ = std::move(path);
    if (!text) {
      fail("cannot be read");
      return std::nullopt;
    }
    if (text->size() > max_file_bytes) {
      fail("is larger than " + std::to_string(max_file_bytes) + " bytes");
      return std::nullopt;
    }
    store::JsonPosition at;
    auto json = Json::parse(*text, &at);
    if (!json) {
      where_ += ":" + std::to_string(at.line) + ":" + std::to_string(at.column);
      fail("is not JSONC");
      return std::nullopt;
    }
    if (!json->is_object()) {
      fail("is not an object");
      return std::nullopt;
    }
    return std::move(*json);
  }

  bool load_constants(const Json& constants) {
    const Json::Object* all = constants.as_object();
    if (!all) return fail("constants is not an object");
    for (const Json::Member& m : *all) {
      const auto value = as_float(m.value);
      if (!value) return fail("the constant \"" + m.key + "\" is not a number");
      pack_.constants.emplace_back(m.key, *value);
    }
    return true;
  }

  bool load_fonts(const Json& json) {
    PackFonts fonts;
    const auto names = [&](std::string_view key, std::vector<std::string>& into) {
      const Json* found = json.find(key);
      const Json::Array* list = found ? found->as_array() : nullptr;
      if (!list || list->empty()) return fail("fonts has no list \"" + std::string(key) + "\"");
      for (const Json& name : *list) {
        if (!name.as_string() || name.as_string()->empty() || name.as_string()->size() > 64) return fail("a name in fonts \"" + std::string(key) + "\" is not one");
        into.emplace_back(*name.as_string());
      }
      return true;
    };
    const auto size = [&](std::string_view key, float& into) {
      const Json* found = json.find(key);
      const auto value = found ? as_float(*found) : std::nullopt;
      if (!value || *value < 1 || *value > 72) return fail("fonts has no size \"" + std::string(key) + "\"");
      into = *value;
      return true;
    };
    if (!names("korean", fonts.korean) || !names("other", fonts.other) || !names("caption", fonts.caption)) return false;
    if (!size("korean_size", fonts.korean_size) || !size("other_size", fonts.other_size)) return false;
    pack_.fonts = std::move(fonts);
    return true;
  }

  // --- Palettes ---

  // `made` holds the entries of this Palette so far, by name.
  bool load_paint(std::string_view name, const Json& value, std::span<const std::optional<Paint>> made, Paint& out) {
    const std::string quoted = "the Palette entry \"" + std::string(name) + "\"";
    if (const auto text = value.as_string()) {
      const auto color = parse_color(*text);
      if (!color) return fail(quoted + " is not a colour");
      out.color = *color;
      return true;
    }
    const auto color_named = [&](const Json& json) -> std::optional<Color> {
      const auto other = json.as_string();
      if (!other) return std::nullopt;
      const auto at = paint(*other);
      if (!at || !made[*at] || made[*at]->stops != 0) return std::nullopt;
      return made[*at]->color;
    };
    if (const Json* mix = value.find("mix")) {
      const Json::Array* args = mix->as_array();
      if (!args || args->size() != 3) return fail(quoted + ": mix takes [a, b, percent]");
      const auto a = color_named((*args)[0]), b = color_named((*args)[1]);
      const auto percent = as_float((*args)[2]);
      if (!a || !b || !percent) return fail(quoted + ": mix takes colours above it and a percent");
      const float t = *percent / 100.0f;
      out.color = {a->r * t + b->r * (1 - t), a->g * t + b->g * (1 - t), a->b * t + b->b * (1 - t), a->a * t + b->a * (1 - t)};
      return true;
    }
    if (const Json* alpha = value.find("alpha")) {
      const Json::Array* args = alpha->as_array();
      if (!args || args->size() != 2) return fail(quoted + ": alpha takes [a, value]");
      const auto a = color_named((*args)[0]);
      const auto v = as_float((*args)[1]);
      if (!a || !v) return fail(quoted + ": alpha takes a colour above it and a value");
      out.color = {a->r, a->g, a->b, *v};
      return true;
    }
    const Json::Array* stops = value.as_array();
    if (!stops || stops->size() < 2 || stops->size() > 16) return fail(quoted + " is neither a colour nor stops");
    out.first_stop = static_cast<std::uint32_t>(pack_.stops.size());
    out.stops = static_cast<std::uint32_t>(stops->size());
    for (const Json& stop : *stops) {
      const Json::Array* pair = stop.as_array();
      if (!pair || pair->size() != 2) return fail("a stop of \"" + std::string(name) + "\" is not [at, colour]");
      const auto at = as_float((*pair)[0]);
      const auto text = (*pair)[1].as_string();
      const auto color = text ? parse_color(*text) : std::nullopt;
      if (!at || !color) return fail("a stop of \"" + std::string(name) + "\" is not [at, colour]");
      pack_.stops.push_back({*at, *color});
    }
    return true;
  }

  bool load_palettes(const Json* palettes) {
    const Json::Object* all = palettes ? palettes->as_object() : nullptr;
    if (!all || all->empty()) return fail("no palettes");
    const Json::Object* first = all->front().value.as_object();
    if (!first || first->empty()) return fail("an empty Palette");
    paint_names_.reserve(first->size());
    for (const Json::Member& m : *first) paint_names_.push_back(m.key);
    const std::size_t names = first->size();
    pack_.paint_names = static_cast<std::uint32_t>(names);
    pack_.paints.resize(names * all->size());
    std::vector<std::optional<Paint>> made(names);
    for (std::size_t p = 0; p < all->size(); ++p) {
      const Json::Member& palette = (*all)[p];
      const Json::Object* entries = palette.value.as_object();
      if (!entries) return fail("the Palette \"" + palette.key + "\" is not an object");
      if (entries->size() != names) return fail("the Palette \"" + palette.key + "\" has other entries than the first");
      pack_.palette_ids.push_back(palette.key);
      std::ranges::fill(made, std::nullopt);
      for (const Json::Member& entry : *entries) {  // in the order of its own file, for what mixes
        const auto at = paint(entry.key);
        if (!at) return fail("the Palette \"" + palette.key + "\" has an entry \"" + entry.key + "\" the first has not");
        Paint value;
        if (!load_paint(entry.key, entry.value, made, value)) return false;
        if (p > 0 && (pack_.paints[*at].stops == 0) != (value.stops == 0)) return fail("\"" + entry.key + "\" is a colour in one Palette and stops in another");
        made[*at] = value;
        pack_.paints[p * names + *at] = value;
      }
    }
    return true;
  }

  // --- Styles ---

  bool load_styles(const Json* styles) {
    const Json::Object* all = styles ? styles->as_object() : nullptr;
    if (!all) return fail("no styles");
    // A style can be read by anything, so it knows no local names; its sizes are worked out
    // for each node that uses it, in that node's box.
    for (const Json::Member& m : *all) {
      where_ = "pack.jsonc: style \"" + m.key + "\"";
      Style style;
      if (!m.value.is_object()) return fail("is not an object");
      if (!expr_of(m.value, "size", style.size) || !expr_of(m.value, "line_height", style.line_height)) return false;
      if (const Json* weight = m.value.find("weight")) {
        const auto value = weight->as_i64();
        if (!value || *value < 100 || *value > 900) return fail("weight is not 100 to 900");
        style.weight = static_cast<int>(*value);
      }
      if (const Json* align = m.value.find("align")) {
        const auto value = align->as_string();
        if (value == "left") style.align = 0;
        else if (value == "center") style.align = 1;
        else if (value == "right") style.align = 2;
        else return fail("align is not left, center or right");
      }
      if (const Json* role = m.value.find("role")) {
        const auto value = role->as_string();
        if (value == "body") style.role = 0;
        else if (value == "caption") style.role = 1;
        else return fail("role is not body or caption");
      }
      if (const Json* spacing = m.value.find("letter_spacing")) {
        const auto value = as_float(*spacing);
        if (!value) return fail("letter_spacing is not a number");
        style.letter_spacing = *value;
      }
      const auto flag = [&](std::string_view key, bool& out) {
        if (const Json* v = m.value.find(key)) out = v->as_bool().value_or(false);
      };
      flag("wrap", style.wrap);
      flag("ellipsis", style.ellipsis);
      flag("exact", style.exact);
      flag("keep_words", style.keep_words);
      pack_.styles.push_back(style);
      style_names_.push_back(m.key);
    }
    return true;
  }

  // --- Expressions ---

  bool compile_expr(const Json& value, Expr& out) {
    if (const auto number = as_float(value)) {
      out = Expr::constant(*number);
      return true;
    }
    if (const auto flag = value.as_bool()) {
      out = Expr::constant(*flag ? 1.0f : 0.0f);
      return true;
    }
    const auto text = value.as_string();
    if (!text) return fail("a value is neither a number nor an expression");
    auto compiled = compile(*text, *this, pack_.code);
    if (!compiled) return fail(compiled.error());
    if (pack_.code.size() > max_instructions) return fail("too many expressions");
    out = *compiled;
    return true;
  }

  bool expr_of(const Json& node, std::string_view key, Expr& out) {
    const Json* value = node.find(key);
    return value ? compile_expr(*value, out) : fail("no \"" + std::string(key) + "\"");
  }

  bool optional_expr(const Json& node, std::string_view key, Expr& out, bool* present = nullptr) {
    const Json* value = node.find(key);
    if (present) *present = value != nullptr;
    return value ? compile_expr(*value, out) : true;
  }

  bool text_of(const Json& node, std::string_view key, std::uint32_t& out) {
    const Json* value = node.find(key);
    const auto source = value ? value->as_string() : std::nullopt;
    if (!source) return fail("no \"" + std::string(key) + "\"");
    const auto ref = text(*source);
    if (!ref) return fail("no such text \"" + std::string(*source) + "\"");
    out = *ref;
    return true;
  }

  // A name the screen offers, or an error saying which it lacks.
  bool bound_of(const Json& node, std::string_view key, std::span<const Name> names, int& out) {
    const Json* value = node.find(key);
    const auto name = value ? value->as_string() : std::nullopt;
    const auto id = name && schema_ ? find_name(names, *name) : std::nullopt;
    if (!id) return fail("this screen has no " + std::string(key) + " \"" + std::string(name.value_or("")) + "\"");
    out = *id;
    return true;
  }

  // --- Slots ---

  bool take_slots(int how_many, std::uint16_t& out) {
    if (frame_ + how_many > max_frame_slots) return fail("too many names");
    out = static_cast<std::uint16_t>(frame_);
    frame_ += how_many;
    return true;
  }

  bool free_name(std::string_view name) {
    if (var_named(name) || name.empty()) return fail("\"" + std::string(name) + "\" cannot be a name");
    for (const auto& [n, at] : scope_->lets) {
      if (n == name) return fail("\"" + std::string(name) + "\" is here twice");
    }
    for (const auto& [n, at] : scope_->boxes) {
      if (n == name) return fail("\"" + std::string(name) + "\" is here twice");
    }
    return true;
  }

  // --- Nodes ---

  // `then` reads the names of this list, once it has them all.
  bool compile_list(const Json& list, std::uint32_t& first, std::uint32_t& count, int depth, bool flowing, const Json* then = nullptr, Expr* then_out = nullptr) {
    const Json::Array* items = list.as_array();
    if (!items) return fail("a list of nodes is not a list");
    if (depth > max_node_depth) return fail("nodes are too deep");
    if (pack_.nodes.size() + items->size() > max_nodes) return fail("too many nodes");
    first = static_cast<std::uint32_t>(pack_.nodes.size());
    count = static_cast<std::uint32_t>(items->size());
    pack_.nodes.resize(pack_.nodes.size() + items->size());
    Scope scope{scope_, {}, {}};
    scope_ = &scope;
    bool ok = true;
    for (std::size_t i = 0; ok && i < items->size(); ++i) {
      Node node;
      ok = compile_node((*items)[i], node, depth, flowing);
      if (ok) pack_.nodes[first + i] = node;  // not by reference: children grow the vector
    }
    if (ok && then) ok = compile_expr(*then, *then_out);
    scope_ = scope.parent;
    return ok;
  }

  bool compile_field(const Json& json, Node& out) {
    FieldNode field;
    if (!bound_of(json, "field", schema_ ? schema_->fields : std::span<const Name>{}, out.bound)) return false;
    if (!style_of(json, out) || !expr_of(json, "color", out.paint) || !text_of(json, "placeholder", field.placeholder) ||
        !expr_of(json, "placeholder_color", field.placeholder_color) || !expr_of(json, "caret", field.caret) ||
        !expr_of(json, "selection", field.selection)) {
      return false;
    }
    out.first = static_cast<std::uint32_t>(pack_.fields.size());
    pack_.fields.push_back(field);
    return true;
  }

  bool compile_slot(const Json& json, Node& out) {
    if (!bound_of(json, "slot", schema_ ? schema_->slots : std::span<const Name>{}, out.bound)) return false;
    SlotNode slot;
    const auto values = [&](std::string_view key, std::uint32_t& first, std::uint32_t& count) {
      first = static_cast<std::uint32_t>(pack_.slot_values.size());
      const Json* list = json.find(key);
      if (!list) return true;
      const Json::Array* items = list->as_array();
      if (!items || items->size() > max_slot_values) return fail(std::string(key) + " is not a short list");
      count = static_cast<std::uint32_t>(items->size());
      for (const Json& item : *items) {
        Expr e;
        if (!compile_expr(item, e)) return false;
        pack_.slot_values.push_back(e);
      }
      return true;
    };
    if (!values("numbers", slot.first_number, slot.numbers) || !values("colors", slot.first_color, slot.colors)) return false;
    slot.first_style = static_cast<std::uint32_t>(pack_.slot_styles.size());
    if (const Json* list = json.find("styles")) {
      const Json::Array* items = list->as_array();
      if (!items || items->size() > max_slot_values) return fail("styles is not a short list");
      slot.styles = static_cast<std::uint32_t>(items->size());
      for (const Json& item : *items) {
        const auto name = item.as_string();
        const auto id = name ? style(*name) : std::nullopt;
        if (!id) return fail("no such style \"" + std::string(name.value_or("")) + "\"");
        pack_.slot_styles.push_back(*id);
      }
    }
    out.first = static_cast<std::uint32_t>(pack_.slots.size());
    pack_.slots.push_back(slot);
    return true;
  }

  bool style_of(const Json& json, Node& out) {
    const Json* value = json.find("style");
    const auto name = value ? value->as_string() : std::nullopt;
    const auto id = name ? style(*name) : std::nullopt;
    if (!id) return fail("no such style \"" + std::string(name.value_or("")) + "\"");
    out.style = *id;
    return true;
  }

  bool compile_use(const Json& json, Node& out, int depth) {
    const auto name = json.find("use")->as_string();
    std::size_t index = 0;
    for (; index < pack_.parts.size(); ++index) {
      if (name && pack_.parts[index].name == *name) break;
    }
    if (index == pack_.parts.size()) return fail("no part \"" + std::string(name.value_or("")) + "\" before this");
    const std::vector<std::string> declared = pack_.parts[index].argument_names;
    const std::vector<Expr> defaults = pack_.parts[index].argument_defaults;
    Use use;
    use.part = static_cast<std::uint32_t>(index);
    const Json* given = json.find("args");
    if (given && !given->is_object()) return fail("args is not an object");
    if (given) {
      for (const Json::Member& m : *given->as_object()) {
        if (std::ranges::find(declared, m.key) == declared.end()) return fail("the part has no argument \"" + m.key + "\"");
      }
    }
    std::vector<Argument> arguments;
    for (std::size_t i = 0; i < declared.size(); ++i) {
      Argument a{static_cast<std::uint16_t>(i), defaults[i]};
      if (given) {
        if (const Json* value = given->find(declared[i]); value && !compile_expr(*value, a.value)) return false;
      }
      arguments.push_back(a);
    }
    use.first_argument = static_cast<std::uint32_t>(pack_.arguments.size());
    use.arguments = static_cast<std::uint32_t>(arguments.size());
    pack_.arguments.insert(pack_.arguments.end(), arguments.begin(), arguments.end());
    if (json.find("text") && !text_of(json, "text", use.label)) return false;
    if (const Json* draw = json.find("draw")) {
      if (!compile_list(*draw, use.first, use.count, depth + 1, true)) return false;
    }
    const Part& part = pack_.parts[index];
    out.snap = out.snap || part.snap;
    if (!json.find("w") && part.has_w) out.w = part.w;
    if (!json.find("h") && part.has_h) out.h = part.h;
    out.first = static_cast<std::uint32_t>(pack_.uses.size());
    pack_.uses.push_back(use);
    return true;
  }

  bool known_keys(const Json& json, NodeKind kind, std::string_view kind_key) {
    for (const Json::Member& m : *json.as_object()) {
      if (m.key != kind_key && !key_allowed(kind, m.key)) return fail("a \"" + std::string(kind_key) + "\" node has no \"" + m.key + "\"");
    }
    return true;
  }

  bool compile_box(const Json& json, Node& out, bool flowing) {
    out.w = Expr::range(0, 1);
    out.h = Expr::range(1, 1);
    if (flowing) out.y = Expr::range(2, 1);
    if (const Json* right = json.find("right")) {
      if (json.find("x")) return fail("a node has both x and right");
      out.from_right = true;
      if (!compile_expr(*right, out.x)) return false;
    } else if (!optional_expr(json, "x", out.x)) {
      return false;
    }
    if (const Json* bottom = json.find("bottom")) {
      if (json.find("y")) return fail("a node has both y and bottom");
      out.from_bottom = true;
      if (!compile_expr(*bottom, out.y)) return false;
    } else if (!optional_expr(json, "y", out.y)) {
      return false;
    }
    return optional_expr(json, "w", out.w) && optional_expr(json, "h", out.h);
  }

  bool compile_common(const Json& json, Node& out) {
    if (const Json* snap = json.find("snap")) out.snap = snap->as_bool().value_or(false);
    if (json.find("hit")) {
      if (!bound_of(json, "hit", schema_ ? schema_->hits : std::span<const Name>{}, out.hit)) return false;
    }
    if (json.find("note")) {
      if (!bound_of(json, "note", schema_ ? schema_->notes : std::span<const Name>{}, out.note)) return false;
    }
    if (json.find("hit_by")) {  // a part has the id from whoever uses it
      if (json.find("hit") || json.find("hit_index")) return fail("hit_by is instead of hit and hit_index");
      out.hit = 0;
      if (!optional_expr(json, "hit_by", out.hit_index, &out.has_hit_index)) return false;
    } else if (!optional_expr(json, "hit_index", out.hit_index, &out.has_hit_index)) {
      return false;
    }
    return optional_expr(json, "note_by", out.note_by, &out.has_note_by) && optional_expr(json, "after", out.after, &out.has_after) &&
           optional_expr(json, "live", out.live, &out.has_live) && optional_expr(json, "hit_if", out.hit_if, &out.has_hit_if) &&
           optional_expr(json, "alpha", out.alpha, &out.has_alpha);
  }

  bool compile_node(const Json& json, Node& out, int depth, bool flowing) {
    if (!json.is_object()) return fail("a node is not an object");
    std::string_view kind;
    std::size_t kind_index = 0;
    for (std::size_t i = 0; i < node_kinds.size(); ++i) {
      if (!json.find(node_kinds[i].key)) continue;
      // A use may have a text for its part, and that does not make it a text.
      if (node_kinds[i].key == "text" && json.find("use")) continue;
      if (!kind.empty()) return fail("a node is both \"" + std::string(kind) + "\" and \"" + std::string(node_kinds[i].key) + "\"");
      kind = node_kinds[i].key;
      kind_index = i;
    }
    if (kind.empty()) return fail("a node says nothing to draw");
    out.kind = static_cast<NodeKind>(kind_index);
    if (!known_keys(json, out.kind, kind)) return false;

    if (const Json* when = json.find("if")) {
      out.has_if = true;
      if (!compile_expr(*when, out.when)) return false;
    }
    if (out.kind == NodeKind::Let) {
      const auto name = json.find("let")->as_string();
      if (!name) return fail("a let has no name");
      if (!free_name(*name) || !expr_of(json, "value", out.extra) || !take_slots(1, out.slot)) return false;
      scope_->lets.emplace_back(*name, out.slot);
      return true;
    }
    if (out.kind == NodeKind::Set) {
      const auto name = json.find("set")->as_string();
      const auto at = name ? slot(*name) : std::nullopt;
      if (!at) return fail("nothing above is called " + std::string(name.value_or("")));
      out.slot = *at;
      return expr_of(json, "value", out.extra);
    }
    if (out.kind == NodeKind::Report) {
      return bound_of(json, "report", schema_ ? schema_->reports : std::span<const Name>{}, out.bound) && expr_of(json, "value", out.extra);
    }
    if (out.kind == NodeKind::Content) return true;

    if (!compile_box(json, out, flowing) || !compile_common(json, out)) return false;
    switch (out.kind) {
      case NodeKind::Fill: {
        if (!expr_of(json, "fill", out.paint) || !optional_expr(json, "radius", out.extra)) return false;
        if (!optional_expr(json, "down_to", out.width, &out.has_down_to)) return false;
        break;
      }
      case NodeKind::Frame:
        if (!expr_of(json, "frame", out.paint)) return false;
        break;
      case NodeKind::Stroke:
        if (!expr_of(json, "stroke", out.paint) || !expr_of(json, "radius", out.extra) || !expr_of(json, "width", out.width)) return false;
        break;
      case NodeKind::Line:
        if (!expr_of(json, "line", out.paint) || !expr_of(json, "x2", out.extra) || !expr_of(json, "y2", out.extra2) || !expr_of(json, "width", out.width)) return false;
        break;
      case NodeKind::Path: {
        const auto data = json.find("path")->as_string();
        if (!data) return fail("a path is not text");
        out.text = static_cast<std::uint32_t>(pack_.literals.size());
        out.count = static_cast<std::uint32_t>(data->size());
        pack_.literals += *data;
        out.extra = Expr::constant(1);
        if (!expr_of(json, "color", out.paint) || !optional_expr(json, "scale", out.extra) || !optional_expr(json, "width", out.width)) return false;
        if (const Json* caps = json.find("caps")) {
          const auto value = caps->as_string();
          if (value != "round" && value != "flat") return fail("caps is not round or flat");
          out.round_caps = value == "round";
        }
        if (const Json* soft = json.find("soft")) {
          if (!soft->as_bool()) return fail("soft is not true or false");
          out.soft = *soft->as_bool();
        }
        break;
      }
      case NodeKind::Text:
        if (!text_of(json, "text", out.text) || !style_of(json, out) || !expr_of(json, "color", out.paint)) return false;
        if (!optional_expr(json, "size", out.size, &out.has_size) || !optional_expr(json, "line_height", out.line_height, &out.has_line_height)) return false;
        break;
      case NodeKind::Picture:
        if (!bound_of(json, "picture", schema_ ? schema_->pictures : std::span<const Name>{}, out.bound)) return false;
        out.extra = Expr::constant(1);
        if (!optional_expr(json, "opacity", out.extra) || !optional_expr(json, "gray", out.width)) return false;
        break;
      case NodeKind::Field:
        if (!compile_field(json, out)) return false;
        break;
      case NodeKind::Slot:
        if (!compile_slot(json, out)) return false;
        break;
      case NodeKind::Group:
        if (const Json* clip = json.find("clip")) out.clip = clip->as_bool().value_or(false);
        if (!compile_list(*json.find("group"), out.first, out.count, depth + 1, false)) return false;
        break;
      case NodeKind::Column:
        if (const Json* clip = json.find("clip")) out.clip = clip->as_bool().value_or(false);
        out.auto_h = json.find("h") == nullptr;
        if (!compile_list(*json.find("column"), out.first, out.count, depth + 1, true)) return false;
        break;
      case NodeKind::Use:
        if (!compile_use(json, out, depth)) return false;
        break;
      case NodeKind::Repeat:
        if (!expr_of(json, "repeat", out.extra)) return false;
        if (!json.find("draw")) return fail("a repeat has no \"draw\"");
        if (!compile_list(*json.find("draw"), out.first, out.count, depth + 1, false)) return false;
        break;
      case NodeKind::Content:
      case NodeKind::Let:
      case NodeKind::Report:
        break;
    }
    if (const Json* id = json.find("id")) {
      const auto name = id->as_string();
      if (!name) return fail("an id is not a name");
      if (!free_name(*name) || !take_slots(4, out.slot)) return false;
      scope_->boxes.emplace_back(*name, out.slot);
    }
    return true;
  }

  // --- Parts ---

  bool load_parts(const Json* parts) {
    const Json::Object* all = parts ? parts->as_object() : nullptr;
    if (!all) return fail("no parts");
    for (const Json::Member& m : *all) {
      where_ = "pack.jsonc: part \"" + m.key + "\"";
      if (!m.value.is_object()) return fail("is not an object");
      const Json* draw = m.value.find("draw");
      if (!draw) return fail("has no \"draw\"");
      Part part;
      part.name = m.key;
      if (const Json* snap = m.value.find("snap")) part.snap = snap->as_bool().value_or(false);
      frame_ = 0;
      schema_ = nullptr;
      Scope root;
      scope_ = &root;
      // Its size is worked out where it is used, before it has any names of its own.
      if (!optional_expr(m.value, "w", part.w, &part.has_w) || !optional_expr(m.value, "h", part.h, &part.has_h)) return false;
      if (const Json* args = m.value.find("args")) {
        if (!args->is_object()) return fail("args is not an object");
        for (const Json::Member& a : *args->as_object()) {
          std::uint16_t slot_at = 0;
          Expr fallback;
          if (!free_name(a.key) || !take_slots(1, slot_at) || !compile_expr(a.value, fallback)) return false;
          root.lets.emplace_back(a.key, slot_at);
          part.argument_names.push_back(a.key);
          part.argument_defaults.push_back(fallback);
        }
      }
      const Json* height = m.value.find("height");
      part.has_height = height != nullptr;
      const bool ok = compile_list(*draw, part.first, part.count, 1, false, height, &part.height);
      scope_ = nullptr;
      if (!ok) return false;
      part.frame = static_cast<std::uint16_t>(frame_);
      pack_.parts.push_back(std::move(part));
    }
    return true;
  }

  // --- Screens ---

  bool load_screen(const ScreenSchema& schema) {
    const std::string path = "screens/" + std::string(schema.screen) + ".jsonc";
    const auto text = read_(path);
    if (!text) return true;
    const auto root = parse_file(path, text);
    if (!root) return false;
    const Json* draw = root->find("draw");
    if (!draw) return fail("has no \"draw\"");
    Screen screen;
    screen.name = std::string(schema.screen);
    frame_ = 0;
    schema_ = &schema;
    scope_ = nullptr;
    const bool ok = compile_list(*draw, screen.first, screen.count, 1, false);
    schema_ = nullptr;
    if (!ok) return false;
    screen.frame = static_cast<std::uint16_t>(frame_);
    pack_.screens.push_back(std::move(screen));
    return true;
  }

  const ReadFile& read_;
  Pack pack_;
  std::string error_, where_;
  std::vector<std::string> paint_names_, style_names_;
  Scope* scope_ = nullptr;
  const ScreenSchema* schema_ = nullptr;
  int frame_ = 0;
};

}  // namespace

std::optional<int> Pack::palette_of(std::string_view theme_id) const {
  for (std::size_t i = 0; i < palette_ids.size(); ++i) {
    if (palette_ids[i] == theme_id) return static_cast<int>(i);
  }
  return std::nullopt;
}

const Screen* Pack::screen(std::string_view name) const {
  for (const Screen& s : screens) {
    if (s.name == name) return &s;
  }
  return nullptr;
}

const Part* Pack::part(std::string_view name) const {
  for (const Part& p : parts) {
    if (p.name == name) return &p;
  }
  return nullptr;
}

const Paint* Pack::paint(int palette, float value) const {
  const auto entry = static_cast<std::uint32_t>(value);
  if (value < 0 || entry >= paint_names) return nullptr;
  return &paints[static_cast<std::size_t>(palette) * paint_names + entry];
}

std::span<const std::string_view> required_constants() {
  static constexpr std::string_view names[] = {"own_frame", "moves", "list_pad", "list_item_h", "list_gap", "list_max_h", "list_margin"};
  return names;
}

std::span<const std::string_view> own_frame_constants() {
  static constexpr std::string_view names[] = {
      "caption_h", "frame_w", "menu_h", "window_radius", "frame_edge", "frame_corner", "frame_button", "frame_button_gap", "frame_button_top", "frame_button_right",
      "window_home_w", "window_home_h", "window_home_min_w", "window_home_min_h",
      "window_room_w", "window_room_h", "window_room_min_w", "window_room_min_h",
      "window_prompt_w", "window_prompt_h", "window_prompt_min_w", "window_prompt_min_h",
      "window_options_w", "window_options_h", "window_options_min_w", "window_options_min_h",
      "window_changelog_w", "window_changelog_h", "window_changelog_min_w", "window_changelog_min_h"};
  return names;
}

std::optional<std::string_view> missing_constant(const Pack& pack) {
  const auto has = [&](std::string_view name) { return std::ranges::any_of(pack.constants, [&](const auto& c) { return c.first == name; }); };
  for (const std::string_view name : required_constants()) {
    if (!has(name)) return name;
  }
  if (pack.constant("own_frame") != 0) {
    for (const std::string_view name : own_frame_constants()) {
      if (!has(name)) return name;
    }
  }
  return std::nullopt;
}

float Pack::constant(std::string_view name) const {
  for (const auto& [n, value] : constants) {
    if (n == name) return value;
  }
  return 0;
}

const Paint* Pack::paint_named(int palette, std::string_view name) const {
  const auto found = std::ranges::find(paint_names_list, name);
  if (found == paint_names_list.end()) return nullptr;
  return paint(palette, static_cast<float>(found - paint_names_list.begin()));
}

std::expected<Pack, std::string> load_pack(const ReadFile& read, std::span<const ScreenSchema> schemas) {
  Loader loader(read);
  if (!loader.load(schemas)) return std::unexpected(loader.error());
  return loader.take();
}

}  // namespace crpp::skin
