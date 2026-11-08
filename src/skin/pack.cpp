#include "crpp/skin/pack.hpp"

#include <algorithm>
#include <charconv>
#include <utility>

#include "crpp/locale/locale.hpp"
#include "crpp/skin/cskin.hpp"
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
  Loader(const ReadFile& read, const LoadOptions& options) : read_(read), options_(options) {}

  bool load(std::span<const ScreenSchema> schemas) {
    // What a node has when it says nothing, shared by all.
    pack_.code.push_back({Op::Var, VarW, 0});
    pack_.code.push_back({Op::Var, VarH, 0});
    pack_.code.push_back({Op::Var, VarFlow, 0});
    auto merged = gather();
    if (!merged) return false;
    Json& root = *merged;
    where_ = pack_file();
    if (!load_themes(root) || !keep_palettes(root) || (layers_.size() > 1 && !order_parts(root))) return false;
    where_ = pack_file();
    if (const Json* constants = root.find("constants"); constants && !load_constants(*constants)) return false;
    if (const Json* fonts = root.find("fonts"); fonts && !load_fonts(*fonts)) return false;
    if (!load_palettes(root.find("palettes")) || !load_styles(root.find("styles")) || !load_parts(root.find("parts"))) return false;
    where_ = pack_file();
    if (const Json* cursors = root.find("cursors"); cursors && !load_cursors(*cursors)) return false;
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

  [[nodiscard]] std::string pack_file() const { return "pack" + ext_; }

  // --- Extending ---

  bool right_format(const Json& root) {
    const Json* format = root.find("format");
    return format && format->as_i64() == pack_format ? true : fail("this exe reads format " + std::to_string(pack_format));
  }

  // Lays what `over` names on top of what `base` has.
  static void merge_into(Json& base, const Json& over) {
    for (const Json::Member& m : *over.as_object()) {
      if (m.key == "id" || m.key == "format" || m.key == "extends" || m.key == "$schema") continue;
      if (m.key == "constants" || m.key == "styles" || m.key == "parts" || m.key == "palettes" || m.key == "themes") {
        if (!base.find(m.key)) base.set(m.key, Json::object());
        if (const Json::Object* items = m.value.as_object()) {
          for (const Json::Member& item : *items) base.find(m.key)->set(item.key, item.value);
        }
        continue;
      }
      base.set(m.key, m.value);
    }
  }

  // The pack with its parents under it and its overlay on top, and where its screens are to be found.
  std::optional<Json> gather() {
    struct Level {
      ReadFile read;
      Json root;
      std::string id;
    };
    std::vector<Level> levels;  // nearest first
    ReadFile reader = read_;
    // A pack written in the pack language says so by its pack.cskin, and so do its parents and overlays.
    auto text = reader("pack.cskin");
    if (text) {
      syntax_ = Syntax::Pack;
      ext_ = ".cskin";
    } else {
      text = reader("pack.jsonc");
    }
    std::string label = pack_file();
    while (true) {
      auto root = parse_file(label, text);
      if (!root || !right_format(*root)) return std::nullopt;
      const Json* id = root->find("id");
      if (!id || !id->as_string()) {
        fail("no id");
        return std::nullopt;
      }
      const std::string name(*id->as_string());
      if (std::ranges::any_of(levels, [&](const Level& l) { return l.id == name; })) {
        fail("extends itself through \"" + name + "\"");
        return std::nullopt;
      }
      if (static_cast<int>(levels.size()) == max_pack_levels) {
        fail("more than " + std::to_string(max_pack_levels) + " packs extend each other");
        return std::nullopt;
      }
      const Json* extends = root->find("extends");
      std::string next;
      if (extends) {
        if (!extends->as_string()) {
          fail("extends is not an id");
          return std::nullopt;
        }
        next = std::string(*extends->as_string());
      }
      levels.push_back({reader, std::move(*root), name});
      if (next.empty()) break;
      auto found = options_.parent ? options_.parent(next) : std::nullopt;
      if (!found) {
        fail("extends \"" + next + "\", which is not here");
        return std::nullopt;
      }
      reader = std::move(*found);
      label = next + "/" + pack_file();
      text = reader(pack_file());
    }
    pack_.id = levels.front().id;
    pack_.overlay = options_.overlay;

    Json merged = Json::object();
    bool overlay_found = false;
    for (auto level = levels.rbegin(); level != levels.rend(); ++level) {
      merge_into(merged, level->root);
      layers_.push_back(level->read);
      if (options_.overlay.empty()) continue;
      const std::string prefix = "overlays/" + options_.overlay + "/";
      const auto overlay = level->read(prefix + pack_file());
      if (!overlay) continue;
      const auto root = parse_file(prefix + pack_file(), overlay);
      if (!root || !right_format(*root)) return std::nullopt;
      merge_into(merged, *root);
      layers_.push_back([read = level->read, prefix](std::string_view path) { return read(prefix + std::string(path)); });
      overlay_found = true;
    }
    if (!options_.overlay.empty() && !overlay_found) {
      where_ = pack_file();
      fail("no overlay \"" + options_.overlay + "\"");
      return std::nullopt;
    }
    return merged;
  }

  bool load_themes(const Json& root) {
    if (const Json* names = root.find("names")) {
      const Json::Object* each = names->as_object();
      if (!each) return fail("names is not an object");
      for (const Json::Member& name : *each) {
        if (!name.value.as_string()) return fail("a name is not text");
        pack_.names.emplace_back(name.key, *name.value.as_string());
      }
    }
    if (const Json* fallback = root.find("default_theme")) {
      if (!fallback->as_string()) return fail("default_theme is not an id");
      pack_.default_theme = std::string(*fallback->as_string());
    }
    const Json* themes = root.find("themes");
    if (!themes) return pack_.default_theme.empty() ? true : fail("a default_theme but no themes");
    const Json::Object* all = themes->as_object();
    if (!all) return fail("themes is not an object");
    for (const Json::Member& m : *all) {
      const std::string quoted = "the Theme \"" + m.key + "\"";
      if (!m.value.is_object()) return fail(quoted + " is not an object");
      PackTheme theme;
      theme.id = m.key;
      theme.palette = m.key;
      const auto text = [&](std::string_view key, std::string& out) {
        const Json* value = m.value.find(key);
        if (!value) return true;
        if (!value->as_string()) return fail(quoted + ": " + std::string(key) + " is not text");
        out = std::string(*value->as_string());
        return true;
      };
      if (!text("palette", theme.palette) || !text("dark", theme.dark) || !text("overlay", theme.overlay)) return false;
      const Json* names = m.value.find("names");
      const Json::Object* each = names ? names->as_object() : nullptr;
      if (!each || each->empty()) return fail(quoted + " has no names");
      for (const Json::Member& name : *each) {
        if (!name.value.as_string()) return fail(quoted + ": a name is not text");
        theme.names.emplace_back(name.key, *name.value.as_string());
      }
      pack_.themes.push_back(std::move(theme));
    }
    if (!pack_.default_theme.empty() && !std::ranges::any_of(pack_.themes, [&](const PackTheme& t) { return t.id == pack_.default_theme; })) {
      return fail("default_theme names no Theme");
    }
    return true;
  }

  // A Palette with what it extends filled in.
  std::optional<Json> palette_resolved(const Json& palettes, std::string_view id, int depth) {
    const Json* own = palettes.find(id);
    if (!own || !own->is_object()) {
      fail("no Palette \"" + std::string(id) + "\"");
      return std::nullopt;
    }
    if (depth > max_palette_levels) {
      fail("Palettes extend each other too far at \"" + std::string(id) + "\"");
      return std::nullopt;
    }
    Json out = Json::object();
    if (const Json* extends = own->find("extends")) {
      const auto name = extends->as_string();
      auto base = name ? palette_resolved(palettes, *name, depth + 1) : std::nullopt;
      if (!base) {
        fail("the Palette \"" + std::string(id) + "\" extends nothing");
        return std::nullopt;
      }
      out = std::move(*base);
    }
    for (const Json::Member& m : *own->as_object()) {
      if (m.key != "extends") out.set(m.key, m.value);
    }
    return out;
  }

  // Only the Palettes that can be in use with this overlay, as they need not share entries with the others.
  bool keep_palettes(Json& root) {
    const Json* palettes = root.find("palettes");
    if (!palettes || !palettes->as_object()) return true;  // load_palettes says so
    Json kept = Json::object();
    const auto wanted = [&](const std::string& id) {
      return pack_.themes.empty() || std::ranges::any_of(pack_.themes, [&](const PackTheme& t) { return t.overlay == options_.overlay && (t.palette == id || t.dark == id); });
    };
    for (const PackTheme& theme : pack_.themes) {
      if (theme.overlay != options_.overlay) continue;
      if (!palettes->find(theme.palette)) return fail("the Theme \"" + theme.id + "\" names no Palette \"" + theme.palette + "\"");
      if (!theme.dark.empty() && !palettes->find(theme.dark)) return fail("the Theme \"" + theme.id + "\" names no Palette \"" + theme.dark + "\"");
    }
    for (const Json::Member& m : *palettes->as_object()) {
      // Each is checked, also one not in use here: a broken one should not wait for its Theme to be picked.
      auto resolved = palette_resolved(*palettes, m.key, 0);
      if (!resolved) return false;
      if (wanted(m.key)) kept.set(m.key, std::move(*resolved));
    }
    root.set("palettes", std::move(kept));
    return true;
  }

  static void uses_in(const Json& json, std::vector<std::string>& out) {
    if (const Json::Array* items = json.as_array()) {
      for (const Json& item : *items) uses_in(item, out);
    } else if (const Json::Object* members = json.as_object()) {
      for (const Json::Member& m : *members) {
        if (m.key == "use" && m.value.as_string()) out.emplace_back(*m.value.as_string());
        else uses_in(m.value, out);
      }
    }
  }

  // A part uses parts above it. Merged, a part that was replaced may use one that came after it.
  bool order_parts(Json& root) {
    const Json* parts = root.find("parts");
    const Json::Object* all = parts ? parts->as_object() : nullptr;
    if (!all) return true;  // load_parts says so
    enum class Mark : std::uint8_t { None, Open, Done };
    std::vector<Mark> marks(all->size(), Mark::None);
    Json::Object ordered;
    ordered.reserve(all->size());
    const std::function<bool(std::size_t, int)> visit = [&](std::size_t at, int depth) {
      if (marks[at] == Mark::Done) return true;
      if (marks[at] == Mark::Open || depth > max_walk_depth) return fail("the part \"" + (*all)[at].key + "\" uses itself");
      marks[at] = Mark::Open;
      std::vector<std::string> uses;
      uses_in((*all)[at].value, uses);
      for (const std::string& name : uses) {
        const auto found = std::ranges::find(*all, name, &Json::Member::key);
        if (found != all->end() && !visit(static_cast<std::size_t>(found - all->begin()), depth + 1)) return false;
      }
      marks[at] = Mark::Done;
      ordered.push_back((*all)[at]);
      return true;
    };
    for (std::size_t i = 0; i < all->size(); ++i) {
      if (!visit(i, 0)) return false;
    }
    root.set("parts", Json(std::move(ordered)));
    return true;
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
    if (syntax_ == Syntax::Pack) {
      auto read = read_cskin(*text, where_.ends_with("pack.cskin") ? SourceKind::Pack : SourceKind::Screen);
      if (!read) {
        where_ += ":" + std::to_string(read.error().line) + ":" + std::to_string(read.error().column);
        fail(read.error().what);
        return std::nullopt;
      }
      return std::move(*read);
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

  bool load_cursors(const Json& json) {
    const Json::Object* all = json.as_object();
    if (!all) return fail("cursors is not an object");
    const Json* hard = json.find("hard");
    if (hard && !hard->as_bool()) return fail("hard is not true or false");
    if (all->size() != cursor_names.size() + (hard ? 1 : 0)) return fail("cursors has to name all of them");
    pack_.cursors_hard = hard && *hard->as_bool();
    std::array<PackCursor, cursor_names.size()> cursors;
    for (std::size_t i = 0; i < cursor_names.size(); ++i) {
      const std::string name(cursor_names[i]);
      const Json* entry = json.find(name);
      if (!entry) return fail("cursors has no \"" + name + "\"");
      const Json* part = entry->find("part");
      const auto found = part && part->as_string() ? std::ranges::find(pack_.parts, *part->as_string(), &Part::name) : pack_.parts.end();
      if (found == pack_.parts.end()) return fail("the cursor \"" + name + "\" names no part");
      const Json* hot = entry->find("hot");
      const Json::Array* point = hot ? hot->as_array() : nullptr;
      if (!point || point->size() != 2) return fail("the cursor \"" + name + "\" has no hot point");
      const auto x = as_float((*point)[0]), y = as_float((*point)[1]);
      if (!x || !y || *x < 0 || *y < 0 || *x >= cursor_box || *y >= cursor_box) return fail("the hot point of the cursor \"" + name + "\" is outside it");
      cursors[i] = {static_cast<std::uint32_t>(found - pack_.parts.begin()), *x, *y};
    }
    pack_.cursors = cursors;
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
      where_ = pack_file() + ": style \"" + m.key + "\"";
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
    auto compiled = compile(*text, *this, pack_.code, syntax_);
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
    if (const Json* other = json.find("placeholder_style")) {
      const auto name = other->as_string();
      const auto id = name ? style(*name) : std::nullopt;
      if (!id) return fail("no such style \"" + std::string(name.value_or("")) + "\"");
      field.has_placeholder_style = true;
      field.placeholder_style = *id;
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

  bool flag_of(const Json& json, std::string_view key, bool& out) {
    if (const Json* value = json.find(key)) {
      if (!value->as_bool()) return fail(std::string(key) + " is not true or false");
      out = *value->as_bool();
    }
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
    if (const Json* shape = json.find("hit_path")) {
      const auto data = shape->as_string();
      if (!data) return fail("hit_path is not text");
      if (out.hit == no_hit) return fail("hit_path goes with hit or hit_by");
      HitShape hit_shape;
      hit_shape.data = static_cast<std::uint32_t>(pack_.literals.size());
      hit_shape.size = static_cast<std::uint32_t>(data->size());
      pack_.literals += *data;
      if (!optional_expr(json, "hit_path_x", hit_shape.x) || !optional_expr(json, "hit_path_y", hit_shape.y)) return false;
      out.hit_shape = static_cast<std::uint32_t>(pack_.hit_shapes.size());
      pack_.hit_shapes.push_back(hit_shape);
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
      if (!free_name(*name)) return false;
      if (json.find("to")) {
        if (json.find("value")) return fail("a let has both value and to");
        out.transition = true;
        if (!expr_of(json, "to", out.extra) || !expr_of(json, "ms", out.width)) return false;
        if (json.find("ms_off")) {
          if (!expr_of(json, "ms_off", out.extra2)) return false;
        } else {
          out.extra2 = out.width;
        }
        if (const Json* curve = json.find("curve")) {
          const auto word = curve->as_string();
          if (word != "ease" && word != "linear") return fail("curve is not ease or linear");
          out.linear = word == "linear";
        }
        if (const Json* first = json.find("first")) {
          if (first->as_string() != "jump") return fail("first is not jump");
          out.first_jumps = true;
        }
        if (json.find("again")) {
          if (!expr_of(json, "again", out.size)) return false;
          out.has_size = true;
        }
        if (json.find("key")) {
          if (!expr_of(json, "key", out.alpha)) return false;
          out.has_alpha = true;
        }
      } else if (json.find("ms") || json.find("ms_off") || json.find("curve") || json.find("first") || json.find("again") || json.find("key")) {
        return fail("ms, ms_off, curve, first, again and key go with to");
      } else if (!expr_of(json, "value", out.extra)) {
        return false;
      }
      if (!take_slots(1, out.slot)) return false;
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
        if (const Json* across = json.find("across")) {
          if (!across->as_bool()) return fail("across is not true or false");
          out.across = *across->as_bool();
          if (out.across && (json.find("radius") || out.has_down_to)) return fail("across takes no radius or down_to");
        }
        if (!flag_of(json, "ellipse", out.ellipse)) return false;
        break;
      }
      case NodeKind::Shadow:
        if (!expr_of(json, "shadow", out.paint) || !expr_of(json, "radius", out.extra) || !expr_of(json, "blur", out.width) || !optional_expr(json, "dy", out.extra2)) return false;
        break;
      case NodeKind::Frame:
        if (!expr_of(json, "frame", out.paint)) return false;
        break;
      case NodeKind::Stroke:
        if (!flag_of(json, "ellipse", out.ellipse)) return false;
        if (!expr_of(json, "stroke", out.paint) || (!out.ellipse && !expr_of(json, "radius", out.extra)) || !expr_of(json, "width", out.width)) return false;
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
        out.shadow = json.find("blur") != nullptr;
        out.has_stops = json.find("from_y") != nullptr;
        out.stretched = json.find("scale_y") != nullptr;
        if (out.stretched && (out.shadow || out.has_stops || json.find("width"))) return fail("scale_y is for a path filled with one colour");
        if (out.shadow || out.has_stops || out.stretched) {
          if (out.shadow && out.has_stops) return fail("a path is a shadow or a gradient, not both");
          PathExtra more;
          if (out.shadow && (!expr_of(json, "blur", out.width) || !optional_expr(json, "dy", more.dy))) return false;
          if (out.has_stops && (!expr_of(json, "from_y", more.from_y) || !expr_of(json, "to_y", more.to_y))) return false;
          if (out.stretched && !expr_of(json, "scale_y", more.scale_y)) return false;
          out.more = static_cast<std::uint32_t>(pack_.path_extras.size());
          pack_.path_extras.push_back(more);
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
      case NodeKind::Group: {
        if (const Json* clip = json.find("clip")) out.clip = clip->as_bool().value_or(false);
        if (const Json* soft = json.find("soft_clip")) out.soft = soft->as_bool().value_or(false);
        GroupEffects effects;
        bool top = false, bottom = false;
        if (!optional_expr(json, "scale", effects.scale, &effects.has_scale) || !optional_expr(json, "about_x", effects.about_x, &effects.has_about_x) ||
            !optional_expr(json, "about_y", effects.about_y, &effects.has_about_y) || !optional_expr(json, "opacity", effects.opacity, &effects.has_opacity) ||
            !optional_expr(json, "fade_top", effects.fade_top, &top) || !optional_expr(json, "fade_bottom", effects.fade_bottom, &bottom) ||
            !optional_expr(json, "dy", effects.dy, &effects.has_dy)) {
          return false;
        }
        effects.has_fade = top || bottom;
        if (const Json* origin = json.find("origin")) {
          effects.origin = origin->as_bool().value_or(false);
          if (!effects.has_scale) return fail("origin goes with scale");
        }
        if (effects.has_scale || effects.has_opacity || effects.has_fade || effects.has_dy) {
          out.has_effects = true;
          out.more = static_cast<std::uint32_t>(pack_.effects.size());
          pack_.effects.push_back(effects);
        }
        if (!compile_list(*json.find("group"), out.first, out.count, depth + 1, false)) return false;
        break;
      }
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
      where_ = pack_file() + ": part \"" + m.key + "\"";
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
    const std::string path = "screens/" + std::string(schema.screen) + ext_;
    std::optional<std::string> text;
    for (auto layer = layers_.rbegin(); layer != layers_.rend() && !text; ++layer) text = (*layer)(path);
    if (!text) return true;
    const auto root = parse_file(path, text);
    if (!root) return false;
    if (syntax_ == Syntax::Pack) {
      // Both are @name in a pack, so the exe must not name one of a screen's numbers like one of its own.
      for (const Name& number : schema.numbers) {
        if (var_named(number.name)) return fail("the exe names a number of this screen @" + std::string(number.name) + ", which is its own already");
      }
    }
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
  const LoadOptions& options_;
  std::vector<ReadFile> layers_;  // what lies on top comes last
  Pack pack_;
  std::string error_, where_;
  Syntax syntax_ = Syntax::Jsonc;  // how its expressions are written: by pack.cskin or pack.jsonc
  std::string ext_ = ".jsonc";
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

const PackTheme* Pack::theme_of_palette(std::string_view palette) const {
  for (const PackTheme& theme : themes) {
    if (theme.palette == palette || theme.dark == palette) return &theme;
  }
  return nullptr;
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
  static constexpr std::string_view names[] = {"own_frame", "moves", "hold_wait_ms", "hold_repeat_ms", "list_pad", "list_item_h", "list_gap", "list_max_h", "list_margin"};
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

std::span<const std::string_view> moving_constants() {
  static constexpr std::string_view names[] = {"scroll_settle_ms", "scroll_return_ms", "scroll_stretch", "edge_fade", "screen_fade_ms", "screen_rise", "speaking_hold_ms", "speaking_fade_ms"};
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
  if (pack.constant("moves") != 0) {
    for (const std::string_view name : moving_constants()) {
      if (!has(name)) return name;
    }
  }
  return std::nullopt;
}

bool Pack::palette_is_dark(int palette) const {
  if (palette < 0 || static_cast<std::size_t>(palette) >= palette_ids.size()) return false;
  const std::string& id = palette_ids[static_cast<std::size_t>(palette)];
  return std::ranges::any_of(themes, [&](const PackTheme& theme) { return theme.dark == id; });
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

std::expected<Pack, std::string> load_pack(const ReadFile& read, std::span<const ScreenSchema> schemas, const LoadOptions& options) {
  Loader loader(read, options);
  if (!loader.load(schemas)) return std::unexpected(loader.error());
  return loader.take();
}

}  // namespace crpp::skin
