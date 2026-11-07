#include "crpp/skin/schema.hpp"

#include <algorithm>
#include <initializer_list>
#include <string_view>
#include <vector>

#include "crpp/locale/locale.hpp"
#include "crpp/store/json.hpp"
#include "keys.hpp"

namespace crpp::skin {

namespace {

using store::Json;

constexpr std::string_view draft = "http://json-schema.org/draft-07/schema#";
constexpr std::string_view common_file = "common.schema.json";

constexpr std::string_view expression_doc =
    "A number, or an expression with + - * / ( ) ?: < <= > >= == != && || ! and min max floor ceil round snap, "
    "textw('style', 'text') and texth('style', 'text', width). Names: W H X Y (the box around it), x y w h (its own, once worked out), "
    "u (a whole pixel), font_size, hot, pressed, i (the round of a repeat), flow (in a column), $palette_entry, @screen_number, "
    "id.right and the like of a sibling, hit('name') and note('name') of the screen, constants, lets and a part's arguments.";

Json text_json(std::string_view value) { return Json(std::string(value)); }

Json strings(std::initializer_list<std::string_view> values) {
  Json::Array items;
  for (const std::string_view v : values) items.push_back(text_json(v));
  return Json(std::move(items));
}

Json ref_to(std::string_view to) {
  Json out = Json::object();
  out.set("$ref", text_json(to));
  return out;
}

Json common_ref(std::string_view definition) { return ref_to(std::string(common_file) + "#/definitions/" + std::string(definition)); }

Json with_doc(Json schema, std::string_view doc) {
  if (!doc.empty()) schema.set("description", text_json(doc));
  return schema;
}

Json typed(std::string_view type) {
  Json out = Json::object();
  out.set("type", text_json(type));
  return out;
}

// What a key's value is, wherever a screen or a part says no more.
Json shape_of(KeyType type) {
  switch (type) {
    case KeyType::Expr: return ref_to("#/definitions/expression");
    case KeyType::Flag: return typed("boolean");
    case KeyType::Word:
    case KeyType::Screen:
    case KeyType::Text: return typed("string");  // who uses this says which
    case KeyType::List: return typed("array");
    case KeyType::Exprs: {
      Json out = typed("array");
      out.set("items", ref_to("#/definitions/expression"));
      out.set("maxItems", Json(static_cast<int>(max_slot_values)));
      return out;
    }
    case KeyType::Words: {
      Json out = typed("array");
      out.set("items", typed("string"));
      out.set("maxItems", Json(static_cast<int>(max_slot_values)));
      return out;
    }
    case KeyType::Args: {
      Json out = typed("object");
      out.set("additionalProperties", ref_to("#/definitions/expression"));
      return out;
    }
    case KeyType::Curve: {
      Json out = Json::object();
      out.set("enum", strings({"ease", "linear"}));
      return out;
    }
    case KeyType::Jump: {
      Json out = Json::object();
      out.set("enum", strings({"jump"}));
      return out;
    }
    case KeyType::Caps: {
      Json out = Json::object();
      out.set("enum", strings({"round", "flat"}));
      return out;
    }
    case KeyType::Yes: {
      Json out = Json::object();
      out.set("const", Json(true));
      return out;
    }
  }
  return Json::object();
}

Json node_shape() {
  Json properties = Json::object();
  const auto add = [&](const KeyDoc& k) {
    if (!properties.find(k.key)) properties.set(k.key, with_doc(shape_of(k.type), k.doc));
  };
  for (const KeyDoc& k : common_keys) add(k);
  for (const KindDoc& kind : node_kinds) {
    add({kind.key, kind.type, kind.doc});
    for (const KeyDoc& k : kind.keys) add(k);
  }
  Json::Array kinds;
  for (const KindDoc& kind : node_kinds) {
    Json::Array items;
    items.push_back(text_json(kind.key));
    for (const KeyDoc& k : common_keys) items.push_back(text_json(k.key));
    for (const KeyDoc& k : kind.keys) items.push_back(text_json(k.key));
    Json allowed = Json::object();
    allowed.set("enum", Json(std::move(items)));
    Json branch = Json::object();
    branch.set("required", strings({kind.key}));
    branch.set("propertyNames", std::move(allowed));
    kinds.push_back(std::move(branch));
  }
  Json out = typed("object");
  out.set("description", text_json("One thing to draw, chosen by which of fill, frame, stroke, line, path, text, picture, field, slot, group, column, use, content, repeat, let, set or report it has."));
  out.set("properties", std::move(properties));
  out.set("anyOf", Json(std::move(kinds)));
  return out;
}

Json file_head(std::string_view title, std::string_view doc) {
  Json out = Json::object();
  out.set("$schema", text_json(draft));
  out.set("title", text_json(title));
  out.set("description", text_json(doc));
  // Not JSON Schema: VS Code takes them, and stops warning about a comma after the last item.
  out.set("allowComments", Json(true));
  out.set("allowTrailingCommas", Json(true));
  return out;
}

Json names_of(std::span<const Name> names, std::string_view prefix = {}) {
  Json::Array items;
  for (const Name& n : names) items.push_back(text_json(std::string(prefix) + std::string(n.name)));
  return Json(std::move(items));
}

// The names a screen offers under this key, and nothing else: a key with none takes nothing.
Json offered(std::span<const Name> names) {
  if (names.empty()) return Json(false);
  Json out = Json::object();
  out.set("enum", names_of(names));
  return out;
}

std::string list_of(std::span<const Name> names, std::string_view prefix = {}) {
  std::string out;
  for (const Name& n : names) {
    if (!out.empty()) out += ", ";
    out += prefix;
    out += n.name;
  }
  return out.empty() ? "none" : out;
}

Json node_with(Json extra) {
  Json out = Json::object();
  Json::Array all;
  all.push_back(common_ref("node_shape"));
  out.set("allOf", Json(std::move(all)));
  Json nodes = ref_to("#/definitions/nodes");
  extra.set("group", nodes);
  extra.set("column", nodes);
  extra.set("draw", nodes);
  out.set("properties", std::move(extra));
  return out;
}

Json nodes_definition() {
  Json out = typed("array");
  out.set("items", ref_to("#/definitions/node"));
  return out;
}

std::string dumped(const Json& json) { return json.dump(true) + "\n"; }

}  // namespace

std::string common_json_schema() {
  Json out = Json::object();
  out.set("$schema", text_json(draft));
  out.set("title", text_json("What the schemas of a Skin Pack share"));
  Json definitions = Json::object();

  Json expression = Json::object();
  expression.set("type", strings({"number", "string", "boolean"}));
  expression.set("description", text_json(expression_doc));
  definitions.set("expression", std::move(expression));

  // #keys are offered, and a # that is none is an error. @names are a screen's, or a part's @label.
  Json::Array keys;
  for (std::size_t i = 0; i < locale::strings_count; ++i) keys.push_back(text_json("#" + std::string(locale::key_name(i))));
  Json locale_keys = Json::object();
  locale_keys.set("enum", Json(std::move(keys)));
  Json words = typed("string");
  words.set("pattern", text_json("^$|^[^#@]"));
  Json::Array either;
  either.push_back(std::move(locale_keys));
  either.push_back(std::move(words));
  Json plain = Json::object();
  plain.set("anyOf", Json(std::move(either)));
  plain.set("description", text_json("A text: #locale_key, @bound, or the words themselves."));
  definitions.set("plain_text", std::move(plain));

  definitions.set("node_shape", node_shape());
  out.set("definitions", std::move(definitions));
  return dumped(out);
}

std::string screen_json_schema(const ScreenSchema& schema) {
  const std::string doc = "A screen of a Skin Pack, as {\"draw\": [nodes]}. With no file for a screen, the program draws it itself.\nNumbers: " + list_of(schema.numbers, "@") + ".\nTexts: " + list_of(schema.texts, "@") + ".\nHits: " + list_of(schema.hits) +
                          ".\nPictures: " + list_of(schema.pictures) + ".\nFields: " + list_of(schema.fields) + ".\nSlots: " + list_of(schema.slots) + ".\nReports: " + list_of(schema.reports) + ".\nNotes: " + list_of(schema.notes) + ".";
  Json out = file_head("The screen " + std::string(schema.screen) + " of a Skin Pack", doc);
  out.set("type", text_json("object"));
  out.set("required", strings({"draw"}));
  out.set("additionalProperties", Json(false));
  Json properties = Json::object();
  properties.set("$schema", typed("string"));
  properties.set("draw", ref_to("#/definitions/nodes"));
  out.set("properties", std::move(properties));

  Json::Array either;
  if (!schema.texts.empty()) {
    Json bound = Json::object();
    bound.set("enum", names_of(schema.texts, "@"));
    either.push_back(std::move(bound));
  }
  either.push_back(common_ref("plain_text"));
  Json text = Json::object();
  text.set("anyOf", Json(std::move(either)));

  Json keys = Json::object();
  keys.set("hit", offered(schema.hits));
  keys.set("note", offered(schema.notes));
  keys.set("picture", offered(schema.pictures));
  keys.set("field", offered(schema.fields));
  keys.set("slot", offered(schema.slots));
  keys.set("report", offered(schema.reports));
  keys.set("text", ref_to("#/definitions/text"));
  keys.set("placeholder", ref_to("#/definitions/text"));

  Json definitions = Json::object();
  definitions.set("text", std::move(text));
  definitions.set("nodes", nodes_definition());
  definitions.set("node", node_with(std::move(keys)));
  out.set("definitions", std::move(definitions));
  return dumped(out);
}

std::string pack_json_schema() {
  Json out = file_head("pack.jsonc of a Skin Pack", "A Skin Pack's Palettes, styles and parts. Its screens are files of their own, under screens/.");
  out.set("type", text_json("object"));
  // Palettes, styles and parts may come from what the pack extends, which the loader checks.
  out.set("required", strings({"format", "id"}));
  out.set("additionalProperties", Json(false));

  const auto map_of = [](Json value) {
    Json map = typed("object");
    map.set("additionalProperties", std::move(value));
    return map;
  };

  Json properties = Json::object();
  properties.set("$schema", typed("string"));
  Json format = Json::object();
  format.set("const", Json(pack_format));
  format.set("description", text_json("What the pack is written for; an exe that reads another number refuses it."));
  properties.set("format", std::move(format));
  properties.set("id", with_doc(typed("string"), "The name of the pack, in lower case."));
  properties.set("names", with_doc(map_of(typed("string")), "What the user calls this Skin, by locale id."));
  properties.set("extends", with_doc(typed("string"), "The id of the pack this one starts from. What this names replaces what that has by the same name."));
  Json theme = typed("object");
  theme.set("required", strings({"names"}));
  theme.set("additionalProperties", Json(false));
  Json theme_properties = Json::object();
  theme_properties.set("names", with_doc(map_of(typed("string")), "What the user sees, by locale id."));
  theme_properties.set("palette", with_doc(typed("string"), "Its Palette, when that is not named as the Theme is."));
  theme_properties.set("dark", with_doc(typed("string"), "The Palette of its dark variant. Without this it has none."));
  theme_properties.set("overlay", with_doc(typed("string"), "The folder under overlays/ it needs on top of the pack."));
  theme.set("properties", std::move(theme_properties));
  properties.set("themes", with_doc(map_of(std::move(theme)), "What a user can pick in this Skin. The pack holds only the Palettes of the Themes of one overlay at a time."));
  properties.set("default_theme", with_doc(typed("string"), "The Theme this Skin goes to when it is picked."));
  properties.set("constants", with_doc(map_of(typed("number")), "Names for numbers, anywhere in the pack. The program reads the ones this requires."));
  Json fonts = typed("object");
  fonts.set("required", strings({"korean", "other", "caption", "korean_size", "other_size"}));
  fonts.set("additionalProperties", Json(false));
  Json font_names = typed("array");
  font_names.set("items", typed("string"));
  font_names.set("minItems", Json(1));
  Json font_properties = Json::object();
  font_properties.set("korean", with_doc(font_names, "Installed fonts, the first this system has: for Korean, and for every Hangul letter."));
  font_properties.set("other", with_doc(font_names, "The same, for the other languages."));
  font_properties.set("caption", with_doc(font_names, "The same, for a style of role caption."));
  font_properties.set("korean_size", with_doc(typed("number"), "font_size in Korean."));
  font_properties.set("other_size", with_doc(typed("number"), "font_size in the other languages."));
  fonts.set("properties", std::move(font_properties));
  properties.set("fonts", with_doc(std::move(fonts), "The fonts of the time, fitted to whole pixels. Without this the program uses its own font."));
  Json cursor = typed("object");
  cursor.set("required", strings({"part", "hot"}));
  cursor.set("additionalProperties", Json(false));
  Json cursor_properties = Json::object();
  cursor_properties.set("part", with_doc(typed("string"), "A part that draws it in a box of 32 by 32."));
  Json hot = typed("array");
  hot.set("items", typed("number"));
  hot.set("minItems", Json(2));
  hot.set("maxItems", Json(2));
  cursor_properties.set("hot", with_doc(std::move(hot), "The point that clicks, in that box."));
  cursor.set("properties", std::move(cursor_properties));
  Json cursors = typed("object");
  Json cursor_names_json = Json::object();
  Json::Array required_cursors;
  for (const std::string_view name : cursor_names) {
    cursor_names_json.set(std::string(name), cursor);
    required_cursors.push_back(text_json(name));
  }
  cursor_names_json.set("hard", with_doc(typed("boolean"), "Hard edges in that box, then enlarged by whole pixels, so the steps of the grid show at any scale."));
  cursors.set("properties", std::move(cursor_names_json));
  cursors.set("required", Json(std::move(required_cursors)));
  cursors.set("additionalProperties", Json(false));
  properties.set("cursors", with_doc(std::move(cursors), "The pointer in this pack's windows, all of it or none. Without this it is the system's."));
  properties.set("palettes", with_doc(map_of(ref_to("#/definitions/palette")), "By the names themes gives them, or one per Theme without themes. Those of one overlay have the same entries."));
  properties.set("styles", with_doc(map_of(ref_to("#/definitions/style")), "Text styles."));
  properties.set("parts", with_doc(map_of(ref_to("#/definitions/part")), "Controls that screens use. A part can only use parts above it, unless the pack extends another or has an overlay, and knows nothing of screens."));
  out.set("properties", std::move(properties));

  // The constants the program reads may come from what a pack extends, so the loader checks those.
  Json definitions = Json::object();

  Json color = typed("string");
  color.set("pattern", text_json("^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
  definitions.set("color", with_doc(std::move(color), "#rrggbb or #rrggbbaa."));

  Json stop = typed("array");
  Json::Array stop_items;
  stop_items.push_back(typed("number"));
  stop_items.push_back(ref_to("#/definitions/color"));
  stop.set("items", Json(std::move(stop_items)));
  stop.set("minItems", Json(2));
  stop.set("maxItems", Json(2));
  Json stops = typed("array");
  stops.set("items", std::move(stop));
  stops.set("minItems", Json(2));
  stops.set("maxItems", Json(16));

  const auto entry_of = [](std::string_view key, std::string_view doc, Json args) {
    Json inner = Json::object();
    inner.set(key, std::move(args));
    Json out_entry = typed("object");
    out_entry.set("description", text_json(doc));
    out_entry.set("required", strings({key}));
    out_entry.set("additionalProperties", Json(false));
    out_entry.set("properties", std::move(inner));
    return out_entry;
  };
  const auto tuple = [](std::initializer_list<std::string_view> types) {
    Json::Array items;
    for (const std::string_view t : types) items.push_back(typed(t));
    Json list = typed("array");
    list.set("items", Json(std::move(items)));
    list.set("minItems", Json(static_cast<int>(types.size())));
    list.set("maxItems", Json(static_cast<int>(types.size())));
    return list;
  };
  Json::Array paints;
  paints.push_back(ref_to("#/definitions/color"));
  paints.push_back(with_doc(std::move(stops), "A gradient, as [[at, colour], ...]."));
  paints.push_back(entry_of("mix", "[a, b, percent of a], of entries above it.", tuple({"string", "string", "number"})));
  paints.push_back(entry_of("alpha", "[a, alpha], of an entry above it.", tuple({"string", "number"})));
  Json paint = Json::object();
  paint.set("anyOf", Json(std::move(paints)));
  Json palette = map_of(std::move(paint));
  Json palette_properties = Json::object();
  palette_properties.set("extends", with_doc(typed("string"), "Another Palette, whose entries this one starts from."));
  palette.set("properties", std::move(palette_properties));
  definitions.set("palette", std::move(palette));

  Json style_keys = Json::object();
  style_keys.set("size", with_doc(common_ref("expression"), "Worked out for each node using it, in that node's box."));
  style_keys.set("line_height", with_doc(common_ref("expression"), "Worked out for each node using it, in that node's box."));
  Json weight = typed("integer");
  weight.set("minimum", Json(100));
  weight.set("maximum", Json(900));
  style_keys.set("weight", std::move(weight));
  Json align = Json::object();
  align.set("enum", strings({"left", "center", "right"}));
  style_keys.set("align", std::move(align));
  Json role = Json::object();
  role.set("enum", strings({"body", "caption"}));
  style_keys.set("role", with_doc(std::move(role), "Which of the Skin's fonts."));
  style_keys.set("letter_spacing", typed("number"));
  style_keys.set("wrap", typed("boolean"));
  style_keys.set("ellipsis", typed("boolean"));
  style_keys.set("exact", with_doc(typed("boolean"), "The size means pixels, not a kind of text."));
  style_keys.set("keep_words", with_doc(typed("boolean"), "A line breaks only between words."));
  Json style = typed("object");
  style.set("required", strings({"size", "line_height"}));
  style.set("additionalProperties", Json(false));
  style.set("properties", std::move(style_keys));
  definitions.set("style", std::move(style));

  Json part_keys = Json::object();
  part_keys.set("snap", with_doc(typed("boolean"), "Its box moves to whole pixels."));
  part_keys.set("w", with_doc(common_ref("expression"), "Its width where a use says none. It may read @label."));
  part_keys.set("h", with_doc(common_ref("expression"), "Its height where a use says none. It may read @label."));
  part_keys.set("height", with_doc(common_ref("expression"), "Worked out after it has drawn, from its own names: the height it took in a column."));
  part_keys.set("args", with_doc(map_of(common_ref("expression")), "Its arguments, with what they are when a use says nothing."));
  part_keys.set("draw", ref_to("#/definitions/nodes"));
  Json part = typed("object");
  part.set("required", strings({"draw"}));
  part.set("additionalProperties", Json(false));
  part.set("properties", std::move(part_keys));
  definitions.set("part", std::move(part));

  // A part knows no screen, so of these it has only @label.
  Json label = Json::object();
  label.set("const", text_json("@label"));
  Json::Array either;
  either.push_back(std::move(label));
  either.push_back(common_ref("plain_text"));
  Json text = Json::object();
  text.set("anyOf", Json(std::move(either)));
  definitions.set("text", std::move(text));

  Json keys = Json::object();
  for (const std::string_view none : {"hit", "note", "picture", "field", "slot", "report"}) keys.set(none, Json(false));
  keys.set("text", ref_to("#/definitions/text"));
  keys.set("placeholder", ref_to("#/definitions/text"));
  definitions.set("nodes", nodes_definition());
  definitions.set("node", node_with(std::move(keys)));

  out.set("definitions", std::move(definitions));
  return dumped(out);
}

}  // namespace crpp::skin
