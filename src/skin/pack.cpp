#include "crpp/skin/pack.hpp"

#include <algorithm>
#include <charconv>
#include <utility>

#include "crpp/locale/locale.hpp"
#include "crpp/store/json.hpp"

namespace crpp::skin {

namespace {

using store::Json;

constexpr std::string_view kind_keys[] = {"fill", "frame", "stroke", "text", "picture", "field", "group", "use", "repeat", "let"};

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
  const auto among = [&](std::initializer_list<std::string_view> keys) { return std::ranges::find(keys, key) != keys.end(); };
  if (among({"if", "id", "x", "y", "w", "h", "hit", "snap", "//"})) return true;
  switch (kind) {
    case NodeKind::Fill: return among({"radius"});
    case NodeKind::Frame: return false;
    case NodeKind::Stroke: return among({"radius", "width"});
    case NodeKind::Text: return among({"style", "color"});
    case NodeKind::Picture: return false;
    case NodeKind::Field: return among({"style", "color", "placeholder", "placeholder_color", "caret", "selection"});
    case NodeKind::Group: return among({"clip"});
    case NodeKind::Use: return among({"args", "text"});
    case NodeKind::Repeat: return among({"draw"});
    case NodeKind::Let: return among({"value"});
  }
  return false;
}

// Names visible where a node is being compiled.
struct Scope {
  Scope* parent = nullptr;
  std::vector<std::pair<std::string, std::uint16_t>> lets, boxes;
};

class Loader final : public Names {
 public:
  explicit Loader(const ReadFile& read) : read_(read) {}

  bool load(std::span<const ScreenSchema> schemas) {
    pack_.code.push_back({Op::Var, VarW, 0});  // a box's width and height when none is given
    pack_.code.push_back({Op::Var, VarH, 0});
    const auto root = parse_file("pack.json", read_("pack.json"));
    if (!root) return false;
    where_ = "pack.json";
    const Json* format = root->find("format");
    if (!format || format->as_i64() != pack_format) return fail("this exe reads format " + std::to_string(pack_format));
    const Json* id = root->find("id");
    if (!id || !id->as_string()) return fail("no id");
    pack_.id = std::string(*id->as_string());
    if (!load_palettes(root->find("palettes")) || !load_styles(root->find("styles")) || !load_parts(root->find("parts"))) return false;
    for (const ScreenSchema& schema : schemas) {
      if (!load_screen(schema)) return false;
    }
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
    auto json = Json::parse(*text);
    if (!json) {
      fail("is not JSON");
      return std::nullopt;
    }
    if (!json->is_object()) {
      fail("is not an object");
      return std::nullopt;
    }
    return std::move(*json);
  }

  // --- Palettes ---

  bool load_paint(std::string_view name, const Json& value, Paint& out) {
    if (const auto text = value.as_string()) {
      const auto color = parse_color(*text);
      if (!color) return fail("the Palette entry \"" + std::string(name) + "\" is not a colour");
      out.color = *color;
      return true;
    }
    const Json::Array* stops = value.as_array();
    if (!stops || stops->size() < 2 || stops->size() > 16) return fail("the Palette entry \"" + std::string(name) + "\" is neither a colour nor stops");
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
    pack_.paint_names = static_cast<std::uint32_t>(first->size());
    pack_.paints.reserve(first->size() * all->size());
    for (const Json::Member& palette : *all) {
      const Json::Object* entries = palette.value.as_object();
      if (!entries) return fail("the Palette \"" + palette.key + "\" is not an object");
      if (entries->size() != first->size()) return fail("the Palette \"" + palette.key + "\" has other entries than the first");
      pack_.palette_ids.push_back(palette.key);
      for (const std::string& name : paint_names_) {
        const Json* value = palette.value.find(name);
        if (!value) return fail("the Palette \"" + palette.key + "\" has no \"" + name + "\"");
        Paint paint;
        if (!load_paint(name, *value, paint)) return false;
        const Paint& model = pack_.paints.empty() || pack_.palette_ids.size() == 1 ? paint : pack_.paints[paint_index(name)];
        if ((model.stops == 0) != (paint.stops == 0)) return fail("\"" + name + "\" is a colour in one Palette and stops in another");
        pack_.paints.push_back(paint);
      }
    }
    return true;
  }

  [[nodiscard]] std::size_t paint_index(std::string_view name) const {
    return static_cast<std::size_t>(std::ranges::find(paint_names_, name) - paint_names_.begin());
  }

  // --- Styles ---

  bool load_styles(const Json* styles) {
    const Json::Object* all = styles ? styles->as_object() : nullptr;
    if (!all) return fail("no styles");
    // A style can be read by anything, so it knows no local names; its sizes are worked out
    // for each node that uses it, in that node's box.
    for (const Json::Member& m : *all) {
      where_ = "pack.json: style \"" + m.key + "\"";
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

  bool optional_expr(const Json& node, std::string_view key, Expr& out) {
    const Json* value = node.find(key);
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

  bool compile_list(const Json& list, std::uint32_t& first, std::uint32_t& count, int depth) {
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
      ok = compile_node((*items)[i], node, depth);
      if (ok) pack_.nodes[first + i] = node;  // not by reference: children grow the vector
    }
    scope_ = scope.parent;
    return ok;
  }

  bool compile_group(const Json& node, Node& out, std::string_view key, int depth) {
    return compile_list(*node.find(key), out.first, out.count, depth + 1);
  }

  bool compile_field(const Json& json, Node& out) {
    const auto name = json.find("field")->as_string();
    const auto id = name && schema_ ? find_name(schema_->fields, *name) : std::nullopt;
    if (!id) return fail("this screen has no field \"" + std::string(name.value_or("")) + "\"");
    out.bound = *id;
    FieldNode field;
    if (!style_of(json, out) || !expr_of(json, "color", out.paint) || !text_of(json, "placeholder", field.placeholder) ||
        !expr_of(json, "placeholder_color", field.placeholder_color) || !expr_of(json, "caret", field.caret) ||
        !expr_of(json, "selection", field.selection)) {
      return false;
    }
    out.first = static_cast<std::uint32_t>(pack_.fields.size());
    pack_.fields.push_back(field);
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

  bool compile_use(const Json& json, Node& out) {
    const auto name = json.find("use")->as_string();
    std::size_t index = 0;
    for (; index < pack_.parts.size(); ++index) {
      if (name && pack_.parts[index].name == *name) break;
    }
    if (index == pack_.parts.size()) return fail("no part \"" + std::string(name.value_or("")) + "\" before this");
    const std::vector<PartArgument>& declared = part_arguments_[index];
    Use use;
    use.part = static_cast<std::uint32_t>(index);
    use.first_argument = static_cast<std::uint32_t>(pack_.arguments.size());
    use.arguments = static_cast<std::uint32_t>(declared.size());
    const Json* given = json.find("args");
    if (given && !given->is_object()) return fail("args is not an object");
    if (given) {
      for (const Json::Member& m : *given->as_object()) {
        if (std::ranges::none_of(declared, [&](const PartArgument& a) { return a.name == m.key; })) return fail("the part has no argument \"" + m.key + "\"");
      }
    }
    for (const PartArgument& argument : declared) {
      Argument a{argument.slot, argument.fallback};
      if (given) {
        if (const Json* value = given->find(argument.name); value && !compile_expr(*value, a.value)) return false;
      }
      pack_.arguments.push_back(a);
    }
    if (json.find("text") && !text_of(json, "text", use.label)) return false;
    out.snap = out.snap || pack_.parts[index].snap;
    out.first = static_cast<std::uint32_t>(pack_.uses.size());
    pack_.uses.push_back(use);
    deepest_callee_ = std::max<int>(deepest_callee_, pack_.parts[index].deepest);
    return true;
  }

  bool known_keys(const Json& json, NodeKind kind, std::string_view kind_key) {
    for (const Json::Member& m : *json.as_object()) {
      if (m.key != kind_key && !key_allowed(kind, m.key)) return fail("a \"" + std::string(kind_key) + "\" node has no \"" + m.key + "\"");
    }
    return true;
  }

  bool compile_node(const Json& json, Node& out, int depth) {
    if (!json.is_object()) return fail("a node is not an object");
    std::string_view kind;
    std::size_t kind_index = 0;
    for (std::size_t i = 0; i < std::size(kind_keys); ++i) {
      if (!json.find(kind_keys[i])) continue;
      // A use may have a text for its part, and that does not make it a text.
      if (kind_keys[i] == "text" && json.find("use")) continue;
      if (!kind.empty()) return fail("a node is both \"" + std::string(kind) + "\" and \"" + std::string(kind_keys[i]) + "\"");
      kind = kind_keys[i];
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
    out.w = Expr::range(0, 1);
    out.h = Expr::range(1, 1);
    if (!optional_expr(json, "x", out.x) || !optional_expr(json, "y", out.y) || !optional_expr(json, "w", out.w) || !optional_expr(json, "h", out.h)) return false;
    if (const Json* snap = json.find("snap")) out.snap = snap->as_bool().value_or(false);
    if (const Json* hit = json.find("hit")) {
      const auto name = hit->as_string();
      const auto id = name && schema_ ? find_name(schema_->hits, *name) : std::nullopt;
      if (!id) return fail("this screen has no hit \"" + std::string(name.value_or("")) + "\"");
      out.hit = *id;
    }
    switch (out.kind) {
      case NodeKind::Fill:
        if (!expr_of(json, "fill", out.paint) || !optional_expr(json, "radius", out.extra)) return false;
        break;
      case NodeKind::Frame:
        if (!expr_of(json, "frame", out.paint)) return false;
        break;
      case NodeKind::Stroke:
        if (!expr_of(json, "stroke", out.paint) || !expr_of(json, "radius", out.extra) || !expr_of(json, "width", out.width)) return false;
        break;
      case NodeKind::Text:
        if (!text_of(json, "text", out.text) || !style_of(json, out) || !expr_of(json, "color", out.paint)) return false;
        break;
      case NodeKind::Picture: {
        const auto name = json.find("picture")->as_string();
        const auto id = name && schema_ ? find_name(schema_->pictures, *name) : std::nullopt;
        if (!id) return fail("this screen has no picture \"" + std::string(name.value_or("")) + "\"");
        out.bound = *id;
        break;
      }
      case NodeKind::Field:
        if (!compile_field(json, out)) return false;
        break;
      case NodeKind::Group:
        if (const Json* clip = json.find("clip")) out.clip = clip->as_bool().value_or(false);
        if (!compile_group(json, out, "group", depth)) return false;
        break;
      case NodeKind::Use:
        if (!compile_use(json, out)) return false;
        break;
      case NodeKind::Repeat:
        if (!expr_of(json, "repeat", out.extra)) return false;
        if (!json.find("draw")) return fail("a repeat has no \"draw\"");
        if (!compile_group(json, out, "draw", depth)) return false;
        break;
      case NodeKind::Let:
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
      where_ = "pack.json: part \"" + m.key + "\"";
      if (!m.value.is_object()) return fail("is not an object");
      const Json* draw = m.value.find("draw");
      if (!draw) return fail("has no \"draw\"");
      Part part;
      part.name = m.key;
      if (const Json* snap = m.value.find("snap")) part.snap = snap->as_bool().value_or(false);
      frame_ = 0;
      deepest_callee_ = 0;
      schema_ = nullptr;
      Scope root;
      scope_ = &root;
      std::vector<PartArgument> arguments;
      if (const Json* args = m.value.find("args")) {
        if (!args->is_object()) return fail("args is not an object");
        for (const Json::Member& a : *args->as_object()) {
          PartArgument argument;
          argument.name = a.key;
          if (!free_name(a.key) || !take_slots(1, argument.slot) || !compile_expr(a.value, argument.fallback)) return false;
          root.lets.emplace_back(a.key, argument.slot);
          arguments.push_back(std::move(argument));
        }
      }
      const bool ok = compile_list(*draw, part.first, part.count, 1);
      scope_ = nullptr;
      if (!ok) return false;
      part.frame = static_cast<std::uint16_t>(frame_);
      part.deepest = static_cast<std::uint16_t>(frame_ + deepest_callee_);
      if (part.deepest > max_frame_slots) return fail("needs too many names");
      pack_.parts.push_back(std::move(part));
      part_arguments_.push_back(std::move(arguments));
    }
    return true;
  }

  // --- Screens ---

  bool load_screen(const ScreenSchema& schema) {
    const std::string path = "screens/" + std::string(schema.screen) + ".json";
    const auto text = read_(path);
    if (!text) return true;
    const auto root = parse_file(path, text);
    if (!root) return false;
    const Json* draw = root->find("draw");
    if (!draw) return fail("has no \"draw\"");
    Screen screen;
    screen.name = std::string(schema.screen);
    frame_ = 0;
    deepest_callee_ = 0;
    schema_ = &schema;
    scope_ = nullptr;
    const bool ok = compile_list(*draw, screen.first, screen.count, 1);
    schema_ = nullptr;
    if (!ok) return false;
    screen.frame = static_cast<std::uint16_t>(frame_);
    screen.deepest = static_cast<std::uint16_t>(frame_ + deepest_callee_);
    if (screen.deepest > max_frame_slots) return fail("needs too many names");
    pack_.screens.push_back(std::move(screen));
    return true;
  }

  struct PartArgument {
    std::string name;
    std::uint16_t slot = 0;
    Expr fallback;
  };

  const ReadFile& read_;
  Pack pack_;
  std::string error_, where_;
  std::vector<std::string> paint_names_, style_names_;
  std::vector<std::vector<PartArgument>> part_arguments_;
  Scope* scope_ = nullptr;
  const ScreenSchema* schema_ = nullptr;
  int frame_ = 0, deepest_callee_ = 0;
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

const Paint* Pack::paint(int palette, float value) const {
  const auto entry = static_cast<std::uint32_t>(value);
  if (value < 0 || entry >= paint_names) return nullptr;
  return &paints[static_cast<std::size_t>(palette) * paint_names + entry];
}

std::expected<Pack, std::string> load_pack(const ReadFile& read, std::span<const ScreenSchema> schemas) {
  Loader loader(read);
  if (!loader.load(schemas)) return std::unexpected(loader.error());
  return loader.take();
}

}  // namespace crpp::skin
