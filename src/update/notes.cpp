#include "crpp/update/notes.hpp"

#include <algorithm>
#include <iterator>

namespace crpp::update {

namespace {

constexpr std::string_view blanks = " \t\r";

std::string_view trimmed(std::string_view s) {
  const auto first = s.find_first_not_of(blanks);
  if (first == std::string_view::npos) return {};
  return s.substr(first, s.find_last_not_of(blanks) - first + 1);
}

bool is_rule(std::string_view line) {
  if (line.size() < 3) return false;
  const char c = line.front();
  return (c == '-' || c == '*' || c == '_') && std::ranges::all_of(line, [c](char x) { return x == c || x == ' '; });
}

bool is_punctuation(char c) { return std::string_view("\\`*_{}[]()#+-.!<>~|").find(c) != std::string_view::npos; }

// Bold, italics (shown plain), code (shown plain), links and escapes.
std::vector<NoteRun> parse_inline(std::string_view text) {
  std::vector<NoteRun> runs;
  std::string current;
  bool bold = false;
  const auto flush = [&] {
    if (current.empty()) return;
    runs.push_back({std::move(current), bold});
    current.clear();
  };
  const auto plain_text_of = [&](std::string_view inner) {
    for (const NoteRun& run : parse_inline(inner)) current += run.text;
  };
  for (std::size_t i = 0; i < text.size();) {
    const char c = text[i];
    const std::string_view rest = text.substr(i);
    if (c == '\\' && i + 1 < text.size() && is_punctuation(text[i + 1])) {
      current += text[i + 1];
      i += 2;
    } else if (c == '`' && text.find('`', i + 1) != std::string_view::npos) {
      const auto end = text.find('`', i + 1);
      current += text.substr(i + 1, end - i - 1);
      i = end + 1;
    } else if ((rest.starts_with("**") || rest.starts_with("__")) &&
               (bold || rest.find(rest.substr(0, 2), 2) != std::string_view::npos)) {
      flush();
      bold = !bold;
      i += 2;
    } else if (c == '*' || (c == '_' && (i == 0 || text[i - 1] == ' '))) {
      // Italics only if it closes on the same line and hugs its text; a lone mark and
      // snake_case stay as they are.
      const auto end = text.find(c, i + 1);
      if (end != std::string_view::npos && end > i + 1 && text[i + 1] != ' ' && text[end - 1] != ' ') {
        plain_text_of(text.substr(i + 1, end - i - 1));
        i = end + 1;
      } else {
        current += c;
        ++i;
      }
    } else if (c == '[' || rest.starts_with("![")) {
      const std::size_t open = c == '[' ? i : i + 1;
      const auto close = text.find(']', open);
      const bool link = close != std::string_view::npos && close + 1 < text.size() && text[close + 1] == '(' &&
                        text.find(')', close) != std::string_view::npos;
      if (link) {
        plain_text_of(text.substr(open + 1, close - open - 1));
        i = text.find(')', close) + 1;
      } else {
        current += c;
        ++i;
      }
    } else {
      current += c;
      ++i;
    }
  }
  flush();
  return runs;
}

// "- x", "* x", "+ x" or "12. x"; returns where the text starts, or npos.
std::size_t list_prefix(std::string_view line, std::string& marker) {
  if (line.size() >= 2 && (line[0] == '-' || line[0] == '*' || line[0] == '+') && line[1] == ' ') return 2;
  std::size_t digits = 0;
  while (digits < line.size() && line[digits] >= '0' && line[digits] <= '9') ++digits;
  if (digits > 0 && digits + 1 < line.size() && line[digits] == '.' && line[digits + 1] == ' ') {
    marker = line.substr(0, digits + 1);
    return digits + 2;
  }
  return std::string_view::npos;
}

}  // namespace

std::vector<NoteBlock> parse_notes(std::string_view markdown) {
  std::vector<NoteBlock> blocks;
  bool in_comment = false, in_fence = false;
  bool continues = false;  // the last block is a paragraph or item this line may extend
  while (!markdown.empty()) {
    const auto end = markdown.find('\n');
    const std::string_view raw = markdown.substr(0, end);
    markdown.remove_prefix(end == std::string_view::npos ? markdown.size() : end + 1);
    std::string_view line = trimmed(raw);

    if (in_comment) {
      in_comment = line.find("-->") == std::string_view::npos;
      continue;
    }
    if (line.starts_with("<!--")) {
      in_comment = line.find("-->") == std::string_view::npos;
      continue;
    }
    if (line.starts_with("```") || line.starts_with("~~~")) {
      in_fence = !in_fence;
      continues = false;
      continue;
    }
    if (in_fence) {
      if (!line.empty()) blocks.push_back({NoteKind::Paragraph, 0, {}, {{std::string(line), false}}});
      continue;
    }
    if (line.empty() || is_rule(line)) {
      continues = false;
      continue;
    }
    if (line.starts_with('>')) {
      line = trimmed(line.substr(1));
      if (line.empty()) continue;
    }

    if (line.starts_with('#')) {
      const auto level = line.find_first_not_of('#');
      if (level != std::string_view::npos && level <= 6 && line[level] == ' ') {
        std::string_view title = trimmed(line.substr(level));
        while (title.ends_with('#')) title.remove_suffix(1);
        blocks.push_back({NoteKind::Heading, 0, {}, parse_inline(trimmed(title))});
        continues = false;
        continue;
      }
    }

    std::string marker;
    if (const auto start = list_prefix(line, marker); start != std::string_view::npos) {
      // Two spaces or a tab for each level.
      std::size_t width = 0;
      for (const char c : raw) {
        if (c == ' ') width += 1;
        else if (c == '\t') width += 2;
        else break;
      }
      blocks.push_back({NoteKind::Bullet, static_cast<int>(std::min<std::size_t>(width / 2, 4)), std::move(marker),
                        parse_inline(line.substr(start))});
      continues = true;
      continue;
    }

    if (continues && !blocks.empty()) {
      // A wrapped line of the same paragraph or item.
      auto more = parse_inline(line);
      NoteBlock& last = blocks.back();
      last.runs.push_back({" ", false});
      last.runs.insert(last.runs.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
      continue;
    }
    blocks.push_back({NoteKind::Paragraph, 0, {}, parse_inline(line)});
    continues = true;
  }
  return blocks;
}

}  // namespace crpp::update
