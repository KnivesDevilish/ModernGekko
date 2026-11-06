// The PATCH NOTES panel's light format: headings, points, paragraphs and
// gaps, from files written on any system, and a section per language.
#include "launcher_patch_notes.hpp"

#include <vector>

namespace
{
using moderngekko::frontend::ParsePatchNotes;
using moderngekko::frontend::PatchNoteLine;
using Kind = PatchNoteLine::Kind;

bool Same(const std::vector<PatchNoteLine>& lines,
          const std::vector<std::pair<Kind, const char*>>& expected)
{
  if (lines.size() != expected.size())
    return false;
  for (std::size_t i = 0; i < lines.size(); ++i)
  {
    if (lines[i].kind != expected[i].first || lines[i].text != expected[i].second)
      return false;
  }
  return true;
}
}  // namespace

int main()
{
  // Written on Linux.
  if (!Same(ParsePatchNotes("# 0.1.0\n"
                            "First public version.\n"
                            "\n"
                            "- Music in the launcher\n"
                            "* Netplay with invite codes\n"),
            {{Kind::Heading, "0.1.0"},
             {Kind::Paragraph, "First public version."},
             {Kind::Gap, ""},
             {Kind::Point, "Music in the launcher"},
             {Kind::Point, "Netplay with invite codes"}}))
    return 1;

  // Written on Windows: a byte order mark and \r\n line ends; extra spaces,
  // several empty lines and ## headings.
  if (!Same(ParsePatchNotes("\xEF\xBB\xBF\r\n\r\n##  0.2.0 \r\n"
                            "  -   M\xC3\xBAsica  \r\n\r\n\r\n"
                            "Texto\r\n\r\n"),
            {{Kind::Heading, "0.2.0"},
             {Kind::Point, "M\xC3\xBAsica"},
             {Kind::Gap, ""},
             {Kind::Paragraph, "Texto"}}))
    return 2;

  // Not a point without text after the dash; a lone # is nothing.
  if (!Same(ParsePatchNotes("-\n#\n-x\n- \n"),
            {{Kind::Paragraph, "-"}, {Kind::Paragraph, "-x"}, {Kind::Paragraph, "-"}}))
    return 3;

  if (!ParsePatchNotes("").empty() || !ParsePatchNotes("\n\n\r\n").empty())
    return 4;

  // One file, one section per language: the launcher's own, else English,
  // else the first one; what comes before the first section is for all.
  const char* translated = "# 0.1.0\n"
                           "[en]\n"
                           "- Music\n"
                           "[ES]\n"
                           "- M\xC3\xBAsica\n"
                           "\n"
                           "[ja]\n"
                           "- \xE9\x9F\xB3\xE6\xA5\xBD\n";
  if (!Same(ParsePatchNotes(translated, "es"),
            {{Kind::Heading, "0.1.0"}, {Kind::Point, "M\xC3\xBAsica"}}) ||
      !Same(ParsePatchNotes(translated, "ja"),
            {{Kind::Heading, "0.1.0"}, {Kind::Point, "\xE9\x9F\xB3\xE6\xA5\xBD"}}) ||
      !Same(ParsePatchNotes(translated, "en"), {{Kind::Heading, "0.1.0"}, {Kind::Point, "Music"}}) ||
      !Same(ParsePatchNotes(translated, "fr"), {{Kind::Heading, "0.1.0"}, {Kind::Point, "Music"}}))
    return 5;
  if (!Same(ParsePatchNotes("[es]\nHola\n[ja]\nKonnichiwa\n", "en"),
            {{Kind::Paragraph, "Hola"}}))
    return 6;
  // A file without sections is the same in every language; [x] and [1.0]
  // are not sections.
  if (!Same(ParsePatchNotes("[x]\n[1.0]\nText\n", "ja"),
            {{Kind::Paragraph, "[x]"}, {Kind::Paragraph, "[1.0]"}, {Kind::Paragraph, "Text"}}))
    return 7;
  return 0;
}
