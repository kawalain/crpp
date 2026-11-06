#include "crpp/update/notes.hpp"

#include "check.hpp"

using namespace crpp::update;

namespace {

std::string plain(const NoteBlock& block) {
  std::string text;
  for (const NoteRun& run : block.runs) text += run.text;
  return text;
}

void reads_what_release_notes_are_made_of() {
  const auto blocks = parse_notes(
      "## 변경 사항\r\n"
      "- 음소거 버튼이 추가되었습니다.\r\n"
      "* 두 번째\r\n"
      "  - 안쪽 항목\r\n"
      "\r\n"
      "# 버그 수정 #\r\n"
      "1. 하나\r\n"
      "12. 열둘\r\n"
      "\r\n"
      "그냥 문단이\r\n"
      "이어집니다.\r\n");
  CHECK(blocks.size() == 8);
  CHECK(blocks[0].kind == NoteKind::Heading && plain(blocks[0]) == "변경 사항");
  CHECK(blocks[1].kind == NoteKind::Bullet && plain(blocks[1]) == "음소거 버튼이 추가되었습니다." && blocks[1].indent == 0);
  CHECK(blocks[2].kind == NoteKind::Bullet && plain(blocks[2]) == "두 번째");
  CHECK(blocks[3].kind == NoteKind::Bullet && blocks[3].indent == 1);
  CHECK(blocks[4].kind == NoteKind::Heading && plain(blocks[4]) == "버그 수정");
  CHECK(blocks[5].marker == "1." && plain(blocks[5]) == "하나");
  CHECK(blocks[6].marker == "12." && plain(blocks[6]) == "열둘");
  CHECK(blocks[7].kind == NoteKind::Paragraph && plain(blocks[7]) == "그냥 문단이 이어집니다.");
}

void marks_bold_and_flattens_the_rest() {
  const auto blocks = parse_notes("앞 **굵게** 뒤 *기울임* `코드` [링크](https://example.com/a) ![그림](a.png) \\*별\\*");
  CHECK(blocks.size() == 1);
  const auto& runs = blocks[0].runs;
  CHECK(runs.size() == 3);
  CHECK(runs[0].text == "앞 " && !runs[0].bold);
  CHECK(runs[1].text == "굵게" && runs[1].bold);
  CHECK(runs[2].text == " 뒤 기울임 코드 링크 그림 *별*" && !runs[2].bold);
}

void leaves_alone_what_only_looks_like_markup() {
  CHECK(plain(parse_notes("snake_case_name 2 * 3 * 4 [괄호] #해시")[0]) == "snake_case_name 2 * 3 * 4 [괄호] #해시");
  CHECK(plain(parse_notes("**닫히지 않음")[0]) == "**닫히지 않음");
  CHECK(plain(parse_notes("`닫히지 않음")[0]) == "`닫히지 않음");
}

void drops_what_is_not_for_reading() {
  // GitHub's generated notes start with a comment; rules separate sections.
  const auto blocks = parse_notes(
      "<!-- Release notes generated -->\n"
      "<!-- several\nlines -->\n"
      "---\n"
      "> 인용\n"
      "```\ncode **x**\n```\n"
      "끝");
  CHECK(blocks.size() == 3);
  CHECK(plain(blocks[0]) == "인용");
  CHECK(plain(blocks[1]) == "code **x**");
  CHECK(plain(blocks[2]) == "끝");
}

void survives_nonsense() {
  CHECK(parse_notes("").empty());
  CHECK(parse_notes("\n\n \r\n").empty());
  CHECK(parse_notes("#######").size() == 1);
  CHECK(parse_notes("# ").size() == 1);
  CHECK(parse_notes("- ").size() == 1);
  CHECK(parse_notes("[a](").size() == 1);
  CHECK(parse_notes("<!-- never closed").empty());
  const auto bold = parse_notes("**a** **b**");
  CHECK(bold[0].runs.size() == 3 && bold[0].runs[2].bold);
}

}  // namespace

int main() {
  reads_what_release_notes_are_made_of();
  marks_bold_and_flattens_the_rest();
  leaves_alone_what_only_looks_like_markup();
  drops_what_is_not_for_reading();
  survives_nonsense();
  return crpp_test_result();
}
