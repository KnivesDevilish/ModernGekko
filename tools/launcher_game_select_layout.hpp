// The game selection screen's description (Resources/game-select/layout.txt)
// and its motion, apart from SDL and ImGui so
// tests/launcher_game_select_test.cpp can check them. The screen itself is
// tools/launcher_game_select.hpp.
//
// layout.txt, one setting a line ('#' starts a comment), in pixels of the
// art's canvas:
//   canvas <width> <height>
//   left <disc ID>                the game the left half shows
//   right <disc ID>
//   halves <left end> <right start>      where each half's picture stops
//   teeth <left tip> <left valley> <right valley> <right tip> <period>
//   band <top> <bottom> <hints' right end>   the frame's black band
//   picture <name> <file> <x> <y>  a picture and its top left corner
//   font <file>                    the title's and the hints' font
//   sound <event> <file>           a WAV the screen plays over the music
//   preview <side> <video> <shape> <first>-<last>...
//                                  the intro a half plays after a while on it:
//                                  parts of a video in the game's own files
//                                  (games/<disc ID>), frame numbers counted
//                                  from 0, played in order. A video inside a
//                                  .cvm goes after a ':', as in
//                                  files/Image3.cvm:OPENING.SFD. The shape is
//                                  the picture's, as 4:3 or 16:9.
//   preview-fade <side> <in> <out> <RRGGBB> <hold>
//                                  the intro comes in from black over <in>
//                                  seconds, goes to the colour over its last
//                                  <out> seconds, and stays that colour for
//                                  <hold> more
// Pictures by name: frame, and left- or right- background, character, logo.
// Sounds, all optional: open, move, choose-left, choose-right, back, denied
// (choosing a game that isn't extracted). Previews are optional too.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace moderngekko::frontend
{
struct GameSelectPicture
{
  std::string file;
  float x = 0.0f;
  float y = 0.0f;
};

// A half's intro (layout.txt's preview and preview-fade).
struct GameSelectPreview
{
  std::string video;          // in the game's folder; "a.cvm:B.SFD" for one inside a .cvm
  float shape = 4.0f / 3.0f;  // the picture's width over its height
  std::vector<std::pair<int, int>> parts;  // first and last frame of each
  float fade_in = 0.0f;
  float fade_out = 0.0f;
  std::array<float, 3> end_colour{};  // 0..1
  float hold = 0.0f;

  int Frames() const
  {
    int frames = 0;
    for (const auto& [first, last] : parts)
      frames += last - first + 1;
    return frames;
  }
};

struct GameSelectLayout
{
  float width = 0.0f;
  float height = 0.0f;
  std::string left_game;
  std::string right_game;
  float left_end = 0.0f;
  float right_start = 0.0f;
  float left_tip = 0.0f;
  float left_valley = 0.0f;
  float right_valley = 0.0f;
  float right_tip = 0.0f;
  float period = 0.0f;
  float band_top = 0.0f;
  float band_bottom = 0.0f;
  float hints_right = 0.0f;
  std::map<std::string, GameSelectPicture, std::less<>> pictures;
  std::string font;
  std::map<std::string, std::string, std::less<>> sounds;  // event: file
  std::array<std::optional<GameSelectPreview>, 2> previews;  // left, right

  // The middle of the divider, where the title goes.
  float Middle() const { return (left_valley + right_valley) * 0.5f; }
};

namespace game_select_preview
{
// "left" 0, "right" 1, anything else -1.
inline int SideIndex(std::string_view side)
{
  return side == "left" ? 0 : side == "right" ? 1 : -1;
}

// "4:3" as 1.333; nothing when it isn't two positive numbers.
inline std::optional<float> ParseShape(std::string_view text)
{
  const std::size_t colon = text.find(':');
  if (colon == std::string_view::npos)
    return std::nullopt;
  const float width = std::strtof(std::string(text.substr(0, colon)).c_str(), nullptr);
  const float height = std::strtof(std::string(text.substr(colon + 1)).c_str(), nullptr);
  if (!(width > 0.0f) || !(height > 0.0f))
    return std::nullopt;
  return width / height;
}

// "88-212": the first and the last frame.
inline std::optional<std::pair<int, int>> ParsePart(std::string_view text)
{
  const std::size_t dash = text.find('-');
  if (dash == std::string_view::npos || dash == 0 || dash + 1 >= text.size())
    return std::nullopt;
  const std::string first_text(text.substr(0, dash));
  const std::string last_text(text.substr(dash + 1));
  char* end = nullptr;
  const long first = std::strtol(first_text.c_str(), &end, 10);
  if (*end != '\0')
    return std::nullopt;
  const long last = std::strtol(last_text.c_str(), &end, 10);
  if (*end != '\0' || first < 0 || last < first || last > 10'000'000)
    return std::nullopt;
  return std::pair<int, int>(static_cast<int>(first), static_cast<int>(last));
}

// "EEECEE" as three 0..1 values.
inline std::optional<std::array<float, 3>> ParseColour(std::string_view text)
{
  if (text.size() == 7 && text.front() == '#')
    text.remove_prefix(1);
  if (text.size() != 6)
    return std::nullopt;
  std::array<float, 3> colour{};
  for (std::size_t i = 0; i < 3; ++i)
  {
    const std::string pair(text.substr(i * 2, 2));
    char* end = nullptr;
    const long value = std::strtol(pair.c_str(), &end, 16);
    if (*end != '\0')
      return std::nullopt;
    colour[i] = static_cast<float>(value) / 255.0f;
  }
  return colour;
}

// Where an intro is `seconds` after its first frame: which of its frames to
// show (counted along its parts), and how much of the fades lies over it.
struct Moment
{
  int frame = 0;
  float black = 0.0f;   // 0..1, the fade in from black
  float colour = 0.0f;  // 0..1, the fade to the end colour
  bool ended = false;   // past the video and its hold
};

inline float VideoLength(const GameSelectPreview& preview, double fps)
{
  return fps > 0.0 ? static_cast<float>(preview.Frames() / fps) : 0.0f;
}

inline Moment MomentAt(const GameSelectPreview& preview, double fps, double seconds)
{
  Moment moment;
  const int frames = preview.Frames();
  const float length = VideoLength(preview, fps);
  const float t = static_cast<float>(std::max(0.0, seconds));
  moment.frame = std::clamp(static_cast<int>(std::floor(t * fps)), 0, std::max(0, frames - 1));
  if (preview.fade_in > 0.0f)
    moment.black = 1.0f - std::clamp(t / preview.fade_in, 0.0f, 1.0f);
  if (preview.fade_out > 0.0f)
    moment.colour = std::clamp((t - (length - preview.fade_out)) / preview.fade_out, 0.0f, 1.0f);
  else if (preview.hold > 0.0f && t >= length)
    moment.colour = 1.0f;
  moment.ended = t >= length + preview.hold;
  return moment;
}
}  // namespace game_select_preview

// The layout, or nothing when a setting is missing or makes no sense (the
// launcher then asks with plain buttons instead).
inline std::optional<GameSelectLayout> ParseGameSelectLayout(std::string_view text)
{
  GameSelectLayout layout;
  bool has_canvas = false, has_left = false, has_right = false, has_halves = false,
       has_teeth = false, has_band = false;
  // Each side's fades, joined to its preview once all is read (either line
  // can come first).
  std::array<std::optional<GameSelectPreview>, 2> fades;
  std::istringstream lines{std::string(text)};
  std::string line;
  while (std::getline(lines, line))
  {
    if (const std::size_t hash = line.find('#'); hash != std::string::npos)
      line.resize(hash);
    std::istringstream words(line);
    std::string key;
    if (!(words >> key))
      continue;
    if (key == "canvas")
      has_canvas = static_cast<bool>(words >> layout.width >> layout.height);
    else if (key == "left")
      has_left = static_cast<bool>(words >> layout.left_game);
    else if (key == "right")
      has_right = static_cast<bool>(words >> layout.right_game);
    else if (key == "halves")
      has_halves = static_cast<bool>(words >> layout.left_end >> layout.right_start);
    else if (key == "teeth")
      has_teeth = static_cast<bool>(words >> layout.left_tip >> layout.left_valley >>
                                    layout.right_valley >> layout.right_tip >> layout.period);
    else if (key == "band")
      has_band = static_cast<bool>(words >> layout.band_top >> layout.band_bottom >>
                                   layout.hints_right);
    else if (key == "picture")
    {
      std::string name;
      GameSelectPicture picture;
      if (words >> name >> picture.file >> picture.x >> picture.y)
        layout.pictures[name] = picture;
    }
    else if (key == "font")
      words >> layout.font;
    else if (key == "sound")
    {
      std::string event, file;
      if (words >> event >> file)
        layout.sounds[event] = file;
    }
    else if (key == "preview")
    {
      // A preview with anything wrong in it is left out: that half just
      // has none.
      using namespace game_select_preview;
      std::string side, shape, part_text;
      GameSelectPreview preview;
      if (!(words >> side >> preview.video >> shape) || SideIndex(side) < 0)
        continue;
      const auto parsed_shape = ParseShape(shape);
      bool good = parsed_shape.has_value();
      while (good && words >> part_text)
      {
        const auto part = ParsePart(part_text);
        good = part.has_value();
        if (good)
          preview.parts.push_back(*part);
      }
      if (good && !preview.parts.empty())
      {
        preview.shape = *parsed_shape;
        layout.previews[SideIndex(side)] = std::move(preview);
      }
    }
    else if (key == "preview-fade")
    {
      using namespace game_select_preview;
      std::string side, colour_text;
      GameSelectPreview fade;
      if (!(words >> side >> fade.fade_in >> fade.fade_out >> colour_text >> fade.hold) ||
          SideIndex(side) < 0 || fade.fade_in < 0.0f || fade.fade_out < 0.0f || fade.hold < 0.0f)
        continue;
      const auto colour = ParseColour(colour_text);
      if (!colour)
        continue;
      fade.end_colour = *colour;
      fades[SideIndex(side)] = fade;
    }
  }
  for (std::size_t side = 0; side < 2; ++side)
  {
    if (layout.previews[side] && fades[side])
    {
      layout.previews[side]->fade_in = fades[side]->fade_in;
      layout.previews[side]->fade_out = fades[side]->fade_out;
      layout.previews[side]->end_colour = fades[side]->end_colour;
      layout.previews[side]->hold = fades[side]->hold;
    }
  }
  const bool sane = layout.width > 0.0f && layout.height > 0.0f && layout.period > 0.0f &&
                    layout.left_tip < layout.left_valley &&
                    layout.left_valley < layout.right_valley &&
                    layout.right_valley < layout.right_tip &&
                    layout.left_end <= layout.right_start && layout.band_top < layout.band_bottom;
  if (!has_canvas || !has_left || !has_right || !has_halves || !has_teeth || !has_band || !sane ||
      layout.left_game == layout.right_game)
    return std::nullopt;
  for (const char* name : {"frame", "left-background", "left-character", "left-logo",
                           "right-background", "right-character", "right-logo"})
  {
    if (!layout.pictures.contains(name))
      return std::nullopt;
  }
  return layout;
}

namespace game_select_motion
{
inline float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }

// Fast, then slowing to a stop.
inline float EaseOut(float x)
{
  const float t = 1.0f - Clamp01(x);
  return 1.0f - t * t * t;
}

// Slow, fast, slow.
inline float EaseInOut(float x)
{
  const float t = Clamp01(x);
  return t * t * (3.0f - 2.0f * t);
}

struct Pop
{
  float alpha = 0.0f;
  float scale = 1.0f;
};

// A logo coming in, `seconds` after it starts: it grows a little past its
// size and settles.
inline Pop LogoPop(float seconds)
{
  const float x = seconds / 0.45f;
  if (x <= 0.0f)
    return {0.0f, 0.6f};
  if (x >= 1.0f)
    return {1.0f, 1.0f};
  if (x < 0.7f)
    return {std::min(1.0f, x * 2.5f), 0.6f + 0.48f * EaseOut(x / 0.7f)};
  return {1.0f, 1.08f - 0.08f * EaseInOut((x - 0.7f) / 0.3f)};
}

// The flash over the chosen half, `seconds` after choosing: up at once,
// then fading.
inline float ChoiceFlash(float seconds)
{
  if (seconds < 0.0f)
    return 0.0f;
  if (seconds < 0.08f)
    return seconds / 0.08f;
  return std::max(0.0f, 1.0f - (seconds - 0.08f) / 0.4f);
}

// How fast the teeth slide, in canvas pixels a second: steadily, faster for
// a moment after the choice moves, and much faster once a game is chosen.
inline float TeethSpeed(float since_switch, float since_choice)
{
  float speed = 45.0f;
  if (since_switch >= 0.0f)
    speed += 520.0f * std::exp(-since_switch / 0.25f);
  if (since_choice >= 0.0f)
    speed += 1600.0f * std::exp(-since_choice / 0.3f);
  return speed;
}
}  // namespace game_select_motion
}  // namespace moderngekko::frontend
