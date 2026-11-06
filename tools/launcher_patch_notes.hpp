#pragma once

// The PLAY page's PATCH NOTES panel reads Resources/patch-notes.txt (the
// build copies it from MODERNGEKKO_LAUNCHER_PATCH_NOTES): plain UTF-8 text,
// written by the project for its players, with a light format:
//
//   # 0.1.0                  a heading
//   - The launcher has music  a point of a list
//   (an empty line)          a gap
//   Anything else            a paragraph
//
// Lines that run long wrap in the panel, so a paragraph or a point can be
// a single line in the file.
//
// One file holds every translation: a line [en], [es] or [ja] (the
// launcher's language codes) starts that language's notes. The panel shows
// the section of the launcher's language, or [en] when there is none for it,
// or else the first section. Lines before the first section are shown in
// every language. A file without sections is shown as it is.

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace moderngekko::frontend
{
struct PatchNoteLine
{
  enum class Kind
  {
    Heading,
    Point,
    Paragraph,
    Gap,
  };
  Kind kind = Kind::Paragraph;
  std::string text;
};

namespace patch_notes_detail
{
// The file's lines, without line ends or the spaces around them.
inline std::vector<std::string_view> SplitLines(std::string_view text)
{
  // A byte order mark some editors put at the start of UTF-8 files.
  if (text.starts_with("\xEF\xBB\xBF"))
    text.remove_prefix(3);
  std::vector<std::string_view> lines;
  while (!text.empty())
  {
    const std::size_t end = text.find('\n');
    std::string_view line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    // Windows line ends, and spaces or tabs around the line.
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
      line.remove_suffix(1);
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
      line.remove_prefix(1);
    lines.push_back(line);
  }
  return lines;
}

// "es" for a line "[es]" (two or three letters, any case), or empty.
inline std::string SectionLanguage(std::string_view line)
{
  if (line.size() < 4 || line.size() > 5 || line.front() != '[' || line.back() != ']')
    return {};
  std::string language;
  for (const char c : line.substr(1, line.size() - 2))
  {
    if (!std::isalpha(static_cast<unsigned char>(c)))
      return {};
    language += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return language;
}
}  // namespace patch_notes_detail

// language: the launcher's language code ("en", "es", "ja"), which picks the
// section of a file that has them.
inline std::vector<PatchNoteLine> ParsePatchNotes(std::string_view text,
                                                  std::string_view language = "en")
{
  using namespace patch_notes_detail;
  const std::vector<std::string_view> all = SplitLines(text);

  // Which section to show: the language's own, else [en], else the first.
  std::string first_section;
  bool has_language = false;
  bool has_english = false;
  for (const std::string_view line : all)
  {
    const std::string section = SectionLanguage(line);
    if (section.empty())
      continue;
    if (first_section.empty())
      first_section = section;
    has_language = has_language || section == language;
    has_english = has_english || section == "en";
  }
  const std::string shown = has_language ? std::string(language) :
                            has_english  ? std::string("en") :
                                           first_section;

  std::vector<PatchNoteLine> lines;
  std::string section;  // empty before the first section: shown always
  for (std::string_view line : all)
  {
    if (const std::string header = SectionLanguage(line); !header.empty())
    {
      section = header;
      continue;
    }
    if (!section.empty() && section != shown)
      continue;

    PatchNoteLine parsed;
    if (line.empty())
    {
      // One gap for any number of empty lines, and none at the start.
      if (lines.empty() || lines.back().kind == PatchNoteLine::Kind::Gap)
        continue;
      parsed.kind = PatchNoteLine::Kind::Gap;
    }
    else if (line.starts_with('#'))
    {
      const std::size_t start = line.find_first_not_of('#');
      line.remove_prefix(start == std::string_view::npos ? line.size() : start);
      while (!line.empty() && line.front() == ' ')
        line.remove_prefix(1);
      if (line.empty())
        continue;
      parsed.kind = PatchNoteLine::Kind::Heading;
      parsed.text = line;
    }
    else if ((line.starts_with("- ") || line.starts_with("* ")) && line.size() > 2)
    {
      line.remove_prefix(2);
      while (!line.empty() && line.front() == ' ')
        line.remove_prefix(1);
      parsed.kind = PatchNoteLine::Kind::Point;
      parsed.text = line;
    }
    else
    {
      parsed.text = line;
    }
    lines.push_back(std::move(parsed));
  }
  // No gap at the end either.
  while (!lines.empty() && lines.back().kind == PatchNoteLine::Kind::Gap)
    lines.pop_back();
  return lines;
}
}  // namespace moderngekko::frontend
