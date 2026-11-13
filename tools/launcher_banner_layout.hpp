// The PLAY page's banner built from layers (launcher_banner.hpp draws it):
// what its layout.txt says, and how each layer moves, as plain numbers.
// Apart from SDL and Dear ImGui, so tests/launcher_banner_test.cpp can check
// it.
//
// layout.txt, one line each, back to front ("#" starts a comment):
//   layer <picture> <depth> [wave|float]
//       A picture, the banner's size (the first sets it). depth: how many of
//       its pixels it moves as the mouse goes from the banner's middle to its
//       edge; nearer layers move more. wave: it ripples like a flag (plain
//       parts don't show it). float: it bobs up and down. The first layer is
//       the backdrop: it is drawn a little bigger, so moving never shows its
//       edges.
//   logo <picture> <depth>
//       The logos take turns, each for logo-seconds, the next one coming in
//       as the last goes (a single one stays); they breathe a little
//       meanwhile. depth as for a layer.
//   logo-seconds <seconds>   (10 when not given)
// A line it can't read is left out; no layer at all, and the PLAY page shows
// the flat banner (launcher-banner.png) instead.
#pragma once

#include "launcher_game_select_layout.hpp"

#include <algorithm>
#include <cmath>
#include <istream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace moderngekko::frontend::ui
{
struct BannerLayer
{
  enum class Motion
  {
    Still,
    Wave,
    Float,
  };
  std::string picture;
  float depth = 0.0f;
  Motion motion = Motion::Still;
  bool logo = false;
};

struct BannerLayout
{
  std::vector<BannerLayer> layers;
  float logo_seconds = 10.0f;

  int LogoCount() const
  {
    return static_cast<int>(std::count_if(layers.begin(), layers.end(),
                                          [](const BannerLayer& layer) { return layer.logo; }));
  }
};

inline std::optional<BannerLayout> ParseBannerLayout(std::istream& in)
{
  BannerLayout layout;
  std::string line;
  while (std::getline(in, line))
  {
    if (const std::size_t hash = line.find('#'); hash != std::string::npos)
      line.erase(hash);
    std::istringstream words(line);
    std::string kind;
    if (!(words >> kind))
      continue;
    if (kind == "logo-seconds")
    {
      float seconds = 0.0f;
      if (words >> seconds && seconds >= 1.0f && seconds <= 600.0f)
        layout.logo_seconds = seconds;
      continue;
    }
    if (kind != "layer" && kind != "logo")
      continue;
    BannerLayer layer;
    layer.logo = kind == "logo";
    if (!(words >> layer.picture >> layer.depth) || layer.depth < 0.0f || layer.depth > 200.0f)
      continue;
    std::string motion;
    if (!layer.logo && words >> motion)
    {
      if (motion == "wave")
        layer.motion = BannerLayer::Motion::Wave;
      else if (motion == "float")
        layer.motion = BannerLayer::Motion::Float;
      else
        continue;
    }
    layout.layers.push_back(std::move(layer));
  }
  if (layout.layers.empty())
    return std::nullopt;
  return layout;
}

// How the layers move, in the pictures' own pixels, `seconds` after the
// banner first showed.
namespace banner_motion
{
inline constexpr float TAU = 6.2831853f;
inline constexpr float WAVE_HEIGHT = 7.0f;     // pixels
inline constexpr float WAVE_LENGTH = 650.0f;   // pixels
inline constexpr float WAVE_SECONDS = 2.6f;    // one ripple passing
inline constexpr float FLOAT_HEIGHT = 6.0f;
inline constexpr float FLOAT_SECONDS = 4.5f;
inline constexpr float LOGO_CHANGE = 0.7f;     // seconds a logo takes to come in
inline constexpr float LOGO_OUT = 0.3f;        // and to go
inline constexpr float LOGO_GAP = 0.2f;        // the next comes in this long after
inline constexpr float BREATH = 0.012f;
inline constexpr float BREATH_SECONDS = 3.2f;

// A point of a waving layer moves by this much: a ripple travelling left, up
// and down, with a little sway sideways.
struct Shift
{
  float x = 0.0f;
  float y = 0.0f;
};
inline Shift Wave(float x, float y, float seconds)
{
  const float phase = TAU * (x / WAVE_LENGTH + y / (WAVE_LENGTH * 3.0f) + seconds / WAVE_SECONDS);
  return {WAVE_HEIGHT * 0.35f * std::cos(phase), WAVE_HEIGHT * std::sin(phase)};
}

inline float Float(float seconds)
{
  return FLOAT_HEIGHT * std::sin(TAU * seconds / FLOAT_SECONDS);
}

// A logo coming in: up from a little smaller, a touch past its size, then
// settling. And one going: fading as it shrinks a little.
inline game_select_motion::Pop LogoIn(float seconds)
{
  using namespace game_select_motion;
  const float x = seconds / LOGO_CHANGE;
  if (x <= 0.0f)
    return {0.0f, 0.88f};
  if (x >= 1.0f)
    return {1.0f, 1.0f};
  if (x < 0.65f)
    return {std::min(1.0f, x / 0.5f), 0.88f + 0.155f * EaseOut(x / 0.65f)};
  return {1.0f, 1.035f - 0.035f * EaseInOut((x - 0.65f) / 0.35f)};
}
inline game_select_motion::Pop LogoOut(float seconds)
{
  const float x = std::clamp(seconds / LOGO_OUT, 0.0f, 1.0f);
  return {1.0f - game_select_motion::EaseOut(x), 1.0f - 0.06f * x};
}

// Which logo shows, and the one leaving as the next comes in: the next only
// once the last has mostly gone, so the two don't blur into each other.
struct LogoTurn
{
  int current = 0;
  game_select_motion::Pop current_look;
  int leaving = -1;  // none
  game_select_motion::Pop leaving_look;
};
inline LogoTurn LogoAt(float seconds, int count, float logo_seconds)
{
  LogoTurn turn;
  if (count <= 0)
    return turn;
  seconds = std::max(seconds, 0.0f);
  // A single logo comes in once and stays.
  const int period = count > 1 ? static_cast<int>(std::floor(seconds / logo_seconds)) : 0;
  const float into = seconds - static_cast<float>(period) * logo_seconds;
  turn.current = period % count;
  turn.current_look = LogoIn(period > 0 ? into - LOGO_GAP : into);
  if (period > 0)
  {
    turn.leaving = (period - 1) % count;
    turn.leaving_look = LogoOut(into);
    if (turn.leaving_look.alpha <= 0.0f)
      turn.leaving = -1;
  }
  const float breath = 1.0f + BREATH * std::sin(TAU * seconds / BREATH_SECONDS);
  turn.current_look.scale *= breath;
  turn.leaving_look.scale *= breath;
  return turn;
}

// Where the mouse leans the banner, -1 to 1 each way: toward the pointer
// while it is over the banner, else a slow drift of its own, so the banner
// lives with a gamepad too.
inline Shift Lean(bool hovered, float pointer_x, float pointer_y, float seconds)
{
  if (hovered)
    return {std::clamp(pointer_x, -1.0f, 1.0f), std::clamp(pointer_y, -1.0f, 1.0f)};
  return {0.3f * std::sin(TAU * seconds / 13.0f), 0.2f * std::sin(TAU * seconds / 9.0f)};
}

// The lean follows its target smoothly: `elapsed` seconds closer.
inline Shift Follow(Shift current, Shift target, float elapsed)
{
  const float keep = std::exp(-std::max(elapsed, 0.0f) / 0.2f);
  return {target.x + (current.x - target.x) * keep, target.y + (current.y - target.y) * keep};
}

// Where a layer's pictured part is (what isn't transparent), in its picture's
// pixels.
struct Box
{
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
};

// `shift` changed as little as needed so that a part from `low` to `high`,
// moved by it, stays inside a banner `length` long; as it is when the part
// doesn't fit.
inline float KeepInside(float shift, float low, float high, float length)
{
  if (high - low > length)
    return shift;
  return std::clamp(shift, -low, length - high);
}

// The same for a layer's part `box`, grown by `scale` around (middle_x,
// middle_y): a logo near the top, or Sonic at the left, isn't cut by leaning
// or by growing.
inline Shift KeepInside(Shift shift, Box box, float scale, float middle_x, float middle_y,
                        float width, float height)
{
  return {KeepInside(shift.x, middle_x + (box.left - middle_x) * scale,
                     middle_x + (box.right - middle_x) * scale, width),
          KeepInside(shift.y, middle_y + (box.top - middle_y) * scale,
                     middle_y + (box.bottom - middle_y) * scale, height)};
}

// How much bigger the backdrop is drawn so that leaning and waving never show
// its edges.
inline float BackdropZoom(float width, float height, float depth, bool waves)
{
  const float margin = depth + (waves ? WAVE_HEIGHT : 0.0f) + 2.0f;
  return std::max((width + 2.0f * margin) / width, (height + 2.0f * margin) / height);
}
}  // namespace banner_motion
}  // namespace moderngekko::frontend::ui
