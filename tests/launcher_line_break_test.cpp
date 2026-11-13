// Line breaks for Japanese text (tools/launcher_line_break.hpp): between
// Japanese characters and at spaces, never inside a word or a name joined by
// no-break spaces, never with 、 or 。 starting a line or 「 ending one, and
// a word wider than the line cut where it must.
#include "launcher_line_break.hpp"

#include <cstdio>
#include <string>
#include <string_view>

namespace
{
using moderngekko::frontend::BreakLines;
using moderngekko::frontend::HasJapanese;

// A monospaced measure: Japanese characters 2 wide, the rest 1.
float Measure(std::string_view text)
{
  float width = 0.0f;
  for (std::size_t at = 0; at < text.size();)
  {
    std::size_t length = 1;
    const char32_t c = moderngekko::frontend::line_break::Decode(text, at, length);
    width += moderngekko::frontend::line_break::IsJapanese(c) ? 2.0f : 1.0f;
    at += length;
  }
  return width;
}

bool Check(int number, std::string_view text, float width, std::string_view expected)
{
  const std::string lines = BreakLines(text, width, Measure);
  if (lines == expected)
    return true;
  std::printf("case %d: got\n%s\nexpected\n%s\n", number, lines.c_str(),
              std::string(expected).c_str());
  return false;
}
}  // namespace

int main()
{
  bool ok = true;
  // English: at spaces, as Dear ImGui does.
  ok &= Check(1, "The quick brown fox", 10.0f, "The quick\nbrown fox");
  ok &= Check(2, "  hello world  ", 20.0f, "hello world");
  ok &= Check(3, "fits", 10.0f, "fits");
  ok &= Check(4, "", 10.0f, "");
  // Japanese: between any two characters.
  ok &= Check(5, "あいうえおかきくけこ", 10.0f, "あいうえお\nかきくけこ");
  // 。 and 、 never start a line: the character before them goes along.
  ok &= Check(6, "あいうえお。かき", 10.0f, "あいうえ\nお。かき");
  ok &= Check(7, "あいうえお、かき", 10.0f, "あいうえ\nお、かき");
  // ー and small kana don't either.
  ok &= Check(8, "あいうえアーかき", 10.0f, "あいうえ\nアーかき");
  ok &= Check(9, "あいうえチャかき", 10.0f, "あいうえ\nチャかき");
  // 「 never ends a line.
  ok &= Check(10, "あいうえ「かき」", 10.0f, "あいうえ\n「かき」");
  // A name joined by no-break spaces stays whole, and the 、 after it isn't
  // left alone.
  ok &= Check(11,
              "SR-ZGS\xE3\x81\xAFSonic\xC2\xA0Riders:\xC2\xA0Source\xE3\x81\xAB\xE3\x80\x81"
              "\xE3\x83\xA9\xE3\x83\xB3",
              30.0f,
              "SR-ZGS\xE3\x81\xAFSonic\xC2\xA0Riders:\xC2\xA0Source\n"
              "\xE3\x81\xAB\xE3\x80\x81\xE3\x83\xA9\xE3\x83\xB3");
  // A word next to Japanese: the line can break between them.
  ok &= Check(12, "あいうえWindowsかき", 12.0f, "あいうえ\nWindowsかき");
  // Spaces in Japanese text break as in English.
  ok &= Check(13, "あい Steam Deck かき", 10.0f, "あい Steam\nDeck かき");
  // A word wider than a whole line is cut, starting on its own line.
  ok &= Check(14, "abcdefghijkl", 5.0f, "abcde\nfghij\nkl");
  ok &= Check(15, "ab cdefghijkl", 5.0f, "ab\ncdefg\nhijkl");
  // ） stays with what it closes even at the end of the room.
  ok &= Check(16, "あいうGXEE8P）も", 12.0f, "あいう\nGXEE8P）も");

  if (!HasJapanese("\xE3\x81\x82") || HasJapanese("Sonic\xC2\xA0Riders") || HasJapanese(""))
  {
    std::puts("HasJapanese");
    ok = false;
  }
  if (!ok)
    return 1;
  std::puts("launcher line break: ok");
  return 0;
}
