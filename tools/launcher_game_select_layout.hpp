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
// Pictures by name: frame, and left- or right- background, character, logo.
#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace moderngekko::frontend
{
struct GameSelectPicture
{
  std::string file;
  float x = 0.0f;
  float y = 0.0f;
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

  // The middle of the divider, where the title goes.
  float Middle() const { return (left_valley + right_valley) * 0.5f; }
};

// The layout, or nothing when a setting is missing or makes no sense (the
// launcher then asks with plain buttons instead).
inline std::optional<GameSelectLayout> ParseGameSelectLayout(std::string_view text)
{
  GameSelectLayout layout;
  bool has_canvas = false, has_left = false, has_right = false, has_halves = false,
       has_teeth = false, has_band = false;
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
