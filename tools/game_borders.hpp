#pragma once

// The pictures shown beside the game when it doesn't fill the window, as when
// it is shown in 4:3 on a wide screen: one in the black bar on the left of the
// game and one in the bar on its right. The launcher chooses them (Settings >
// Display > 4:3 borders) and the runtime draws them (src/runtime/
// game_border.cpp).
//
// A build lists them in Resources/borders/borders.txt (the folder named by
// MODERNGEKKO_GAME_BORDERS), one [section] per border, in the order the
// launcher shows them:
//
//   [green-grid]
//   name = Green grid                   the name in the launcher
//   name.es = Grid verde                the same in one of its languages
//   left = Green Grid.png 176 88 355 894
//   right = Green Grid.png 1113 88 1292 894
//   left_motion = up 176 355 95.3 -0.0567
//   right_motion = up 1113 1292 11.93 0.0567
//
// left and right name a PNG in the same folder and, optionally, the part of
// it to show: x0 y0 x1 y1, in the picture's pixels (all of it without them).
// A part of 2:9 (240 x 1080) fits the bar beside a 4:3 game on a 16:9 screen.
//
// A bar seldom has exactly that shape: the window may have any size, a screen
// may not be 16:9, and the game itself is a little narrower or wider than 4:3
// (the Wii's video signal sets its width). fit says what happens then:
//
//   fit = cover     the default: the part fills the bar's height and the side
//                   away from the game is cut, or, in a bar wider than that,
//                   it fills the width and its top and bottom are cut. Right
//                   for a grid, where a cut doesn't show.
//   fit = contain   the whole part shows, as large as the bar allows, next to
//                   the game and centred up and down. Right for a picture of
//                   characters, which must not lose a hand or a logo.
//
// What contain leaves of a bar shows the side's backdrop, which fills it as
// cover does (a blurred copy of the picture, say), or stays black:
//
//   left_backdrop = Sonic Backdrop.png 0 0 120 540
//   right_backdrop = Sonic Backdrop.png 120 0 240 540
//
// <side>_motion makes a grid in the picture slide, one cell every few
// seconds: up, down, left or right, then the first and last rows (when it
// slides left or right) or columns (up or down) that move, and a and b: row or
// column i repeats every a + b * i pixels of the picture, since the cells of a
// grid in perspective grow steadily toward the viewer. The picture needs that
// much of the grid on both sides of the part, which the slide reads from.
//
// Lines starting with # are comments.

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace moderngekko::frontend
{
// A rectangle: in a picture's pixels, or in the window's.
struct BorderRect
{
  float x0 = 0.0f;
  float y0 = 0.0f;
  float x1 = 0.0f;
  float y1 = 0.0f;

  float Width() const { return x1 - x0; }
  float Height() const { return y1 - y0; }
  bool Empty() const { return !(x1 > x0) || !(y1 > y0); }
};

struct BorderMotion
{
  enum class Direction
  {
    None,
    Up,
    Down,
    Left,
    Right,
  };
  Direction direction = Direction::None;
  // The rows (sliding left or right) or columns (up or down) that slide.
  int first = 0;
  int last = -1;
  // Row or column i repeats every a + b * i pixels.
  double a = 0.0;
  double b = 0.0;

  bool Slides() const { return direction != Direction::None; }
  bool Sideways() const { return direction == Direction::Left || direction == Direction::Right; }
  double Period(double i) const { return a + b * i; }
};

// How a side's part fills a bar of another shape (see the top of this file).
enum class BorderFit
{
  Cover,
  Contain,
};

struct BorderSide
{
  // A file in the borders' folder.
  std::string picture;
  // The part of the picture to show; all of it when there is none.
  std::optional<BorderRect> part;
  BorderMotion motion;
  // Behind the part, filling what fit = contain leaves of the bar: a file in
  // the borders' folder (none when empty) and the part of it to show.
  std::string backdrop;
  std::optional<BorderRect> backdrop_part;
};

struct GameBorder
{
  std::string id;
  std::string name;
  // Translations of the name: language code and text.
  std::vector<std::pair<std::string, std::string>> names;
  BorderSide left;
  BorderSide right;
  BorderFit fit = BorderFit::Cover;

  // The name in a launcher language ("es"), or the plain one.
  const std::string& Name(std::string_view language) const
  {
    for (const auto& [code, text] : names)
    {
      if (code == language)
        return text;
    }
    return name;
  }
};

struct GameBorderList
{
  std::vector<GameBorder> borders;
  // What was wrong in borders.txt, a line each; a border with a problem is
  // left out, the others still work.
  std::vector<std::string> problems;

  const GameBorder* Find(std::string_view id) const
  {
    const auto found =
        std::ranges::find_if(borders, [&](const GameBorder& border) { return border.id == id; });
    return found != borders.end() ? &*found : nullptr;
  }
};

namespace game_borders_detail
{
inline std::string_view Trim(std::string_view text)
{
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
    text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
    text.remove_suffix(1);
  return text;
}

inline std::string Lower(std::string_view text)
{
  std::string lower(text);
  for (char& c : lower)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return lower;
}

// Letters, digits, - and _: what an id in [brackets] and in config.ini is made of.
inline bool ValidId(std::string_view id)
{
  if (id.empty() || id.size() > 64)
    return false;
  return std::ranges::all_of(id, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
  });
}

inline std::vector<std::string_view> Words(std::string_view text)
{
  std::vector<std::string_view> words;
  while (true)
  {
    text = Trim(text);
    if (text.empty())
      return words;
    const std::size_t end = text.find_first_of(" \t");
    words.push_back(text.substr(0, end));
    if (end == std::string_view::npos)
      return words;
    text.remove_prefix(end);
  }
}

inline bool ParseNumber(std::string_view word, int* value)
{
  const auto parsed = std::from_chars(word.data(), word.data() + word.size(), *value);
  return parsed.ec == std::errc{} && parsed.ptr == word.data() + word.size();
}

// A decimal such as -0.0567, always with a point whatever the system's
// language (strtod would read a comma in some), and without from_chars for
// decimals, which not every standard library has.
inline bool ParseNumber(std::string_view word, double* value)
{
  bool negative = false;
  if (!word.empty() && (word.front() == '-' || word.front() == '+'))
  {
    negative = word.front() == '-';
    word.remove_prefix(1);
  }
  double number = 0.0;
  double scale = 1.0;
  bool point = false;
  int digits = 0;
  for (const char c : word)
  {
    if (c == '.' && !point)
    {
      point = true;
      continue;
    }
    if (c < '0' || c > '9' || digits >= 15)
      return false;
    ++digits;
    if (point)
    {
      scale /= 10.0;
      number += (c - '0') * scale;
    }
    else
    {
      number = number * 10.0 + (c - '0');
    }
  }
  if (digits == 0)
    return false;
  *value = negative ? -number : number;
  return true;
}

inline bool ParseNumber(std::string_view word, float* value)
{
  double number = 0.0;
  if (!ParseNumber(word, &number))
    return false;
  *value = static_cast<float>(number);
  return true;
}

// "Green Grid.png 176 88 355 894": a file name, which may have spaces, then
// optionally four numbers.
inline bool ParseSide(std::string_view value, BorderSide* side, std::string* problem)
{
  value = Trim(value);
  const std::vector<std::string_view> words = Words(value);
  std::optional<BorderRect> part;
  std::string_view picture = value;
  if (words.size() > 4)
  {
    float numbers[4] = {};
    bool all = true;
    for (std::size_t i = 0; i < 4; ++i)
      all = all && ParseNumber(words[words.size() - 4 + i], &numbers[i]);
    if (all)
    {
      part = BorderRect{numbers[0], numbers[1], numbers[2], numbers[3]};
      picture = Trim(value.substr(0, static_cast<std::size_t>(
                                         words[words.size() - 4].data() - value.data())));
    }
  }
  if (picture.empty())
  {
    *problem = "no picture";
    return false;
  }
  // Only a file in the borders' folder.
  if (picture.find('/') != std::string_view::npos ||
      picture.find('\\') != std::string_view::npos || picture == "." || picture == "..")
  {
    *problem = "the picture must be a file in the borders' folder";
    return false;
  }
  if (part && (part->x0 < 0.0f || part->y0 < 0.0f || part->Empty()))
  {
    *problem = "the part must be x0 y0 x1 y1, with x1 > x0 and y1 > y0";
    return false;
  }
  side->picture = std::string(picture);
  side->part = part;
  return true;
}

// "up 176 355 95.3 -0.0567"
inline bool ParseMotion(std::string_view value, BorderMotion* motion, std::string* problem)
{
  const std::vector<std::string_view> words = Words(value);
  if (words.size() != 5)
  {
    *problem = "a motion is a direction, the first and last row or column, a and b";
    return false;
  }
  const std::string direction = Lower(words[0]);
  if (direction == "up")
    motion->direction = BorderMotion::Direction::Up;
  else if (direction == "down")
    motion->direction = BorderMotion::Direction::Down;
  else if (direction == "left")
    motion->direction = BorderMotion::Direction::Left;
  else if (direction == "right")
    motion->direction = BorderMotion::Direction::Right;
  else
  {
    *problem = "the direction must be up, down, left or right";
    return false;
  }
  if (!ParseNumber(words[1], &motion->first) || !ParseNumber(words[2], &motion->last) ||
      motion->first < 0 || motion->last < motion->first)
  {
    *problem = "the first and last row or column must be numbers, the last not before the first";
    return false;
  }
  if (!ParseNumber(words[3], &motion->a) || !ParseNumber(words[4], &motion->b))
  {
    *problem = "a and b must be numbers";
    return false;
  }
  // The grid must repeat at least every two pixels in all the moving rows.
  if (motion->Period(motion->first) < 2.0 || motion->Period(motion->last) < 2.0)
  {
    *problem = "a + b * i must be at least 2 for the first and last row or column";
    return false;
  }
  return true;
}
}  // namespace game_borders_detail

inline GameBorderList ParseGameBorders(std::string_view text)
{
  using namespace game_borders_detail;
  if (text.starts_with("\xEF\xBB\xBF"))
    text.remove_prefix(3);

  GameBorderList list;
  struct Pending
  {
    GameBorder border;
    bool broken = false;
  };
  std::optional<Pending> current;
  const auto finish = [&]() {
    if (!current)
      return;
    GameBorder& border = current->border;
    if (!current->broken && (border.left.picture.empty() || border.right.picture.empty()))
    {
      list.problems.push_back("[" + border.id + "] needs a left and a right picture");
      current->broken = true;
    }
    if (!current->broken)
    {
      if (border.name.empty())
        border.name = border.id;
      list.borders.push_back(std::move(border));
    }
    current.reset();
  };

  int line_number = 0;
  while (!text.empty())
  {
    const std::size_t end = text.find('\n');
    const std::string_view line = Trim(text.substr(0, end));
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    ++line_number;
    if (line.empty() || line.front() == '#')
      continue;
    const std::string where = "line " + std::to_string(line_number) + ": ";

    if (line.front() == '[')
    {
      finish();
      const std::string id = line.back() == ']' ? Lower(Trim(line.substr(1, line.size() - 2))) : "";
      current.emplace();
      current->border.id = id;
      if (!ValidId(id) || id == "none" || id == "random")
      {
        list.problems.push_back(where + "a border's id is made of letters, digits, - and _, "
                                        "and can't be none or random");
        current->broken = true;
      }
      else if (list.Find(id))
      {
        list.problems.push_back(where + "there is already a border called " + id);
        current->broken = true;
      }
      continue;
    }

    const std::size_t equals = line.find('=');
    if (!current || equals == std::string_view::npos)
    {
      list.problems.push_back(where + "expected [border] or key = value");
      continue;
    }
    if (current->broken)
      continue;
    const std::string key = Lower(Trim(line.substr(0, equals)));
    const std::string_view value = Trim(line.substr(equals + 1));
    GameBorder& border = current->border;
    std::string problem;
    bool ok = true;
    if (key == "name")
      border.name = std::string(value);
    else if (key.starts_with("name.") && key.size() > 5)
      border.names.emplace_back(key.substr(5), std::string(value));
    else if (key == "left")
      ok = ParseSide(value, &border.left, &problem);
    else if (key == "right")
      ok = ParseSide(value, &border.right, &problem);
    else if (key == "left_motion")
      ok = ParseMotion(value, &border.left.motion, &problem);
    else if (key == "right_motion")
      ok = ParseMotion(value, &border.right.motion, &problem);
    else if (key == "left_backdrop" || key == "right_backdrop")
    {
      BorderSide backdrop;
      ok = ParseSide(value, &backdrop, &problem);
      BorderSide& side = key == "left_backdrop" ? border.left : border.right;
      side.backdrop = std::move(backdrop.picture);
      side.backdrop_part = backdrop.part;
    }
    else if (key == "fit")
    {
      const std::string fit = Lower(value);
      if (fit == "cover")
        border.fit = BorderFit::Cover;
      else if (fit == "contain")
        border.fit = BorderFit::Contain;
      else
      {
        ok = false;
        problem = "fit must be cover or contain";
      }
    }
    else
    {
      ok = false;
      problem = "unknown key " + key;
    }
    if (!ok)
    {
      list.problems.push_back(where + "[" + border.id + "] " + problem);
      current->broken = true;
    }
  }
  finish();
  return list;
}

// Resources/borders/borders.txt, with the borders whose pictures are missing
// left out. An empty list when the build has none.
inline GameBorderList LoadGameBorders(const std::filesystem::path& folder)
{
  std::ifstream file(folder / "borders.txt", std::ios::binary);
  if (!file)
    return {};
  const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  GameBorderList list = ParseGameBorders(text);
  std::erase_if(list.borders, [&](const GameBorder& border) {
    for (const BorderSide* side : {&border.left, &border.right})
    {
      for (const std::string* picture : {&side->picture, &side->backdrop})
      {
        std::error_code ec;
        if (!picture->empty() && !std::filesystem::is_regular_file(folder / *picture, ec))
        {
          list.problems.push_back("[" + border.id + "] " + *picture + " is missing");
          return true;
        }
      }
    }
    return false;
  });
  return list;
}

// What config.ini's border setting may hold: none, random or a border's id.
inline std::string NormalizeBorderSetting(std::string_view setting)
{
  const std::string value = game_borders_detail::Lower(game_borders_detail::Trim(setting));
  return game_borders_detail::ValidId(value) ? value : std::string{};
}

// The border for the setting: none for "none" or an id the build doesn't
// have; for "random", the one random_number picks.
inline const GameBorder* ChooseGameBorder(const GameBorderList& list, std::string_view setting,
                                          std::uint64_t random_number)
{
  if (list.borders.empty() || setting == "none")
    return nullptr;
  if (setting == "random")
    return &list.borders[static_cast<std::size_t>(random_number % list.borders.size())];
  return list.Find(setting);
}

// Where one side of a border goes. bar: the black bar beside the game, in the
// window's pixels; part: the part of the picture to show, in its pixels;
// left_of_game: the bar is on the game's left. Either way the part keeps its
// shape.
//   cover: the part fills the bar's height; when that is wider than the bar,
//     the side away from the game is cut, and when it is narrower, the part
//     fills the bar's width instead and its top and bottom are cut evenly.
//   contain: the whole part, as large as the bar allows, against the game's
//     side of the bar and centred up and down.
struct BorderPlacement
{
  // Where it is drawn, in the window's pixels.
  BorderRect screen;
  // What of the picture is drawn there, in its pixels.
  BorderRect picture;

  // Window pixels per picture pixel, the same both ways.
  float Scale() const { return screen.Height() / picture.Height(); }
};

inline std::optional<BorderPlacement> PlaceBorder(const BorderRect& bar, const BorderRect& part,
                                                  bool left_of_game,
                                                  BorderFit fit = BorderFit::Cover)
{
  // A bar narrower than a pixel is no bar.
  if (bar.Width() < 1.0f || bar.Height() < 1.0f || part.Empty())
    return std::nullopt;
  BorderPlacement placement{bar, part};
  if (fit == BorderFit::Contain)
  {
    const float scale = std::min(bar.Width() / part.Width(), bar.Height() / part.Height());
    float width = part.Width() * scale;
    float height = part.Height() * scale;
    // Up to two pixels short of the bar still fills it, a hair off the part's
    // shape, rather than leaving a line of backdrop beside it.
    if (bar.Width() - width <= 2.0f)
      width = bar.Width();
    if (bar.Height() - height <= 2.0f)
      height = bar.Height();
    placement.screen.x0 = left_of_game ? bar.x1 - width : bar.x0;
    placement.screen.x1 = placement.screen.x0 + width;
    placement.screen.y0 = bar.y0 + (bar.Height() - height) * 0.5f;
    placement.screen.y1 = placement.screen.y0 + height;
    return placement;
  }
  float scale = bar.Height() / part.Height();
  // Up to two pixels short of the bar still fills it, a hair wider than its
  // shape: 1366x768 has 171-pixel bars for a 2:9 part 170.7 pixels wide.
  if (part.Width() * scale >= bar.Width() - 2.0f)
  {
    const float shown = std::min(part.Width(), bar.Width() / scale);
    if (left_of_game)
      placement.picture.x0 = part.x1 - shown;
    else
      placement.picture.x1 = part.x0 + shown;
  }
  else
  {
    scale = bar.Width() / part.Width();
    const float shown = bar.Height() / scale;
    const float middle = (part.y0 + part.y1) * 0.5f;
    placement.picture.y0 = middle - shown * 0.5f;
    placement.picture.y1 = middle + shown * 0.5f;
  }
  return placement;
}
}  // namespace moderngekko::frontend
