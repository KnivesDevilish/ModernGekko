// Line breaks for Japanese text. Dear ImGui wraps text only at spaces and
// after a few punctuation marks, and Japanese has no spaces: a long Japanese
// line was cut wherever it ran out of room, in the middle of a name such as
// Sonic Riders, or with a 、 alone on a line. BreakLines breaks it the
// Japanese way instead:
// - between any two Japanese characters (kana, kanji, Japanese punctuation);
// - at spaces, as in English;
// - never inside a run of other characters (a word or a name: its spaces
//   written as no-break spaces, U+00A0, keep it whole), unless it is wider
//   than the whole line;
// - never before 、。」）ー, small kana and the like, nor after 「（ and the
//   like (kinsoku shori).
// The launcher uses it for the PATCH NOTES panel's lines that have Japanese
// in them. Apart from Dear ImGui (the width of each piece is measured by the
// caller), so tests/launcher_line_break_test.cpp can check it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace moderngekko::frontend
{
namespace line_break
{
// The next character of UTF-8 text at `at`, and how many bytes it takes.
inline char32_t Decode(std::string_view text, std::size_t at, std::size_t& length)
{
  const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
  const unsigned char first = byte(at);
  std::size_t count = first < 0x80 ? 1 : (first >> 5) == 0x6 ? 2 : (first >> 4) == 0xE ? 3 :
                                         (first >> 3) == 0x1E ? 4 : 1;
  if (at + count > text.size())
    count = 1;
  char32_t character = count == 1 ? first : first & (0x7F >> count);
  for (std::size_t i = 1; i < count; ++i)
  {
    if ((byte(at + i) & 0xC0) != 0x80)
    {
      length = 1;
      return first;
    }
    character = (character << 6) | (byte(at + i) & 0x3F);
  }
  length = count;
  return character;
}

// Japanese (and Chinese) characters, which a line can end after.
inline bool IsJapanese(char32_t c)
{
  return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) ||
         (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFFEF);
}

inline bool IsIn(char32_t c, std::u32string_view list)
{
  return list.find(c) != std::u32string_view::npos;
}

// Characters that can't start a line, and that can't end one.
inline bool NoLineStart(char32_t c)
{
  return IsIn(c, U"、。，．・：；？！ー－～〜…‥々ゝゞヽヾ゛゜」』）】〕〉》］｝"
                 U"ぁぃぅぇぉっゃゅょゎゕゖァィゥェォッャュョヮヵヶ"
                 U",.:;!?)]}%");
}
inline bool NoLineEnd(char32_t c)
{
  return IsIn(c, U"「『（【〔〈《［｛([{");
}

struct Piece
{
  std::size_t begin = 0;
  std::size_t end = 0;
  char32_t first = 0;
  char32_t last = 0;
  bool space = false;
  bool japanese = false;  // a line can break before and after it
};

// The text in pieces: a space, one Japanese character, or a run of anything
// else (a word, a name joined by no-break spaces, a number).
inline std::vector<Piece> Pieces(std::string_view text)
{
  std::vector<Piece> pieces;
  std::size_t at = 0;
  while (at < text.size())
  {
    std::size_t length = 1;
    const char32_t c = Decode(text, at, length);
    const bool space = c == U' ' || c == U'\t';
    const bool japanese = !space && IsJapanese(c);
    if (!space && !japanese && !pieces.empty() && !pieces.back().space &&
        !pieces.back().japanese)
    {
      pieces.back().end = at + length;
      pieces.back().last = c;
    }
    else
    {
      pieces.push_back({at, at + length, c, c, space, japanese});
    }
    at += length;
  }
  return pieces;
}

// Whether a line may break between two pieces next to each other.
inline bool MayBreakBetween(const Piece& before, const Piece& after)
{
  if (before.space || after.space)
    return false;  // the space itself is where it breaks
  if (!before.japanese && !after.japanese)
    return false;
  return !NoLineStart(after.first) && !NoLineEnd(before.last);
}

// A piece too wide for a whole line, cut into one piece per character.
inline std::vector<Piece> Characters(std::string_view text, const Piece& piece)
{
  std::vector<Piece> characters;
  std::size_t at = piece.begin;
  while (at < piece.end)
  {
    std::size_t length = 1;
    const char32_t c = Decode(text, at, length);
    characters.push_back({at, at + length, c, c, false, true});
    at += length;
  }
  return characters;
}
}  // namespace line_break

// `text` (one paragraph, no line breaks of its own) broken into lines at
// most `width` wide, joined by '\n'; `measure` gives the width of a piece of
// it. A piece wider than a whole line is cut where it must.
template <typename Measure>
std::string BreakLines(std::string_view text, float width, Measure&& measure)
{
  using namespace line_break;
  std::vector<Piece> pieces = Pieces(text);
  std::string lines;
  bool first_line = true;
  // The line being filled: where it starts, and how wide it is so far.
  std::size_t line_start = 0;
  float line_width = 0.0f;
  // Its last place to break: where the line would end, where the next one
  // would start, and how much of the width comes before that start.
  bool can_break = false;
  std::size_t break_end = 0;
  std::size_t break_next = 0;
  float width_before_next = 0.0f;

  const auto add_line = [&](std::size_t end) {
    while (end > line_start && (text[end - 1] == ' ' || text[end - 1] == '\t'))
      --end;
    if (!first_line)
      lines += '\n';
    lines.append(text.substr(line_start, end - line_start));
    first_line = false;
  };
  const auto end_line = [&](std::size_t end, std::size_t next, float width_before) {
    add_line(end);
    line_start = next;
    line_width -= width_before;
    can_break = false;
  };

  for (std::size_t i = 0; i < pieces.size(); ++i)
  {
    const Piece piece = pieces[i];
    const float piece_width = measure(text.substr(piece.begin, piece.end - piece.begin));
    const bool line_has_text = line_start < piece.begin;
    if (piece.space)
    {
      if (!line_has_text)
      {
        // Spaces starting a line are left out.
        line_start = piece.end;
        continue;
      }
      can_break = true;
      break_end = piece.begin;
      break_next = piece.end;
      line_width += piece_width;
      width_before_next = line_width;
      continue;
    }
    if (line_has_text && MayBreakBetween(pieces[i - 1], piece))
    {
      can_break = true;
      break_end = piece.begin;
      break_next = piece.begin;
      width_before_next = line_width;
    }
    if (piece_width > width && !piece.japanese)
    {
      // Wider than a whole line: one character at a time, starting on a
      // line of its own when it can.
      if (can_break)
        end_line(break_end, break_next, width_before_next);
      const std::vector<Piece> characters = Characters(text, piece);
      pieces.erase(pieces.begin() + static_cast<std::ptrdiff_t>(i));
      pieces.insert(pieces.begin() + static_cast<std::ptrdiff_t>(i), characters.begin(),
                    characters.end());
      --i;
      continue;
    }
    if (line_has_text && line_width + piece_width > width)
    {
      if (can_break)
        end_line(break_end, break_next, width_before_next);
      // Still too wide (or nowhere it was allowed): break before this piece
      // anyway.
      if (line_start < piece.begin && line_width + piece_width > width)
        end_line(piece.begin, piece.begin, line_width);
    }
    line_width += piece_width;
  }
  add_line(text.size());
  return lines;
}

// Whether a line of text has Japanese in it.
inline bool HasJapanese(std::string_view text)
{
  for (std::size_t at = 0; at < text.size();)
  {
    std::size_t length = 1;
    if (line_break::IsJapanese(line_break::Decode(text, at, length)))
      return true;
    at += length;
  }
  return false;
}
}  // namespace moderngekko::frontend
