#pragma once

// How a mod's charge meter (mod_hud.hpp) is drawn: an arc of segments to the
// left of the middle of the game's picture, opening towards it, that fills
// from the bottom in each level's colour; the level reached in a segmented
// digit beside it; a flash at each new level and a burst at the release. Only
// Dear ImGui's draw list, so it can be drawn outside the game too.

#include "mod_hud_core.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace moderngekko::detail
{
namespace mod_hud_draw
{
inline constexpr float PI = 3.14159265358979f;

inline ImU32 Color(std::uint32_t rgb, float alpha, float bright = 1.0f, float white = 0.0f)
{
  const auto channel = [&](int shift) {
    const float v = ((rgb >> shift) & 0xFF) / 255.0f * bright;
    return std::clamp(v + (1.0f - v) * white, 0.0f, 1.0f);
  };
  return ImGui::ColorConvertFloat4ToU32(
      ImVec4(channel(16), channel(8), channel(0), std::clamp(alpha, 0.0f, 1.0f)));
}

inline void Arc(ImDrawList* list, ImVec2 center, float radius, float from, float to, ImU32 color,
                float thickness)
{
  if (to - from < 0.002f || thickness <= 0.0f)
    return;
  const int segments = std::max(2, static_cast<int>((to - from) / (PI / 48)));
  list->PathArcTo(center, radius, from, to, segments);
  list->PathStroke(color, ImDrawFlags_None, thickness);
}

// A digit as a seven-segment display, leaning forward: `pos` its top left,
// `height` its height.
inline void Digit(ImDrawList* list, int digit, ImVec2 pos, float height, ImU32 color,
                  float thickness)
{
  // Segments a b c d e f g, as on a calculator.
  static constexpr std::array<std::uint8_t, 10> SEGMENTS = {0x3F, 0x06, 0x5B, 0x4F, 0x66,
                                                            0x6D, 0x7D, 0x07, 0x7F, 0x6F};
  const float w = height * 0.52f;
  const float h = height;
  const float lean = height * 0.16f;
  const auto at = [&](float x, float y) {  // x, y from 0 to 1, y down
    return ImVec2(pos.x + x * w + (1.0f - y) * lean, pos.y + y * h);
  };
  const float m = 0.08f;  // gap at the corners
  const std::array<std::array<ImVec2, 2>, 7> lines = {{
      {at(m, 0), at(1 - m, 0)},              // a
      {at(1, m), at(1, 0.5f - m)},           // b
      {at(1, 0.5f + m), at(1, 1 - m)},       // c
      {at(m, 1), at(1 - m, 1)},              // d
      {at(0, 0.5f + m), at(0, 1 - m)},       // e
      {at(0, m), at(0, 0.5f - m)},           // f
      {at(m, 0.5f), at(1 - m, 0.5f)},        // g
  }};
  const std::uint8_t on = SEGMENTS[std::clamp(digit, 0, 9)];
  for (int s = 0; s < 7; ++s)
  {
    if (on & (1 << s))
      list->AddLine(lines[s][0], lines[s][1], color, thickness);
  }
}
}  // namespace mod_hud_draw

// Draws `look` for the meter `c`; the game's picture is the rectangle at
// `origin`, `size` pixels.
inline void DrawGauge(ImDrawList* list, const ModHudConfig& c, const GaugeLook& g, ImVec2 origin,
                      ImVec2 size)
{
  using namespace mod_hud_draw;
  const float shown = std::max(g.alpha, g.burst);
  if (shown <= 0.002f || g.levels <= 0 || c.colors.empty() || size.y <= 0.0f)
    return;
  const float radius = static_cast<float>(c.size) * size.y;
  const ImVec2 center(origin.x + static_cast<float>(c.x) * size.x,
                      origin.y + static_cast<float>(c.y) * size.y);
  const float thick = 0.17f * radius;
  // From the bottom (118°) round the left (180°) to the top (242°): screen
  // angles, y down, so it opens to the right, towards the middle.
  const float start = 118.0f * PI / 180.0f;
  const float end = 242.0f * PI / 180.0f;
  const float span = end - start;
  constexpr int SEGMENTS = 12;
  const float step = span / SEGMENTS;
  const float gap = 2.6f * PI / 180.0f;
  const float a = g.alpha;
  const bool full = g.level >= g.levels;
  const auto color_of = [&](int level) {  // the colour filling towards `level` (1 up)
    return c.colors[static_cast<std::size_t>(std::clamp(level, 1, g.levels) - 1)];
  };
  // At the last level it pulses.
  const float pulse = full ? 0.5f + 0.5f * static_cast<float>(std::sin(g.time * 2 * PI * 4.0)) : 0.0f;

  const auto segments = [&](float from_fraction, float to_fraction, ImU32 color, float width,
                            float r) {
    for (int i = 0; i < SEGMENTS; ++i)
    {
      const float s0 = static_cast<float>(i) / SEGMENTS;
      const float s1 = static_cast<float>(i + 1) / SEGMENTS;
      const float lo = std::max(s0, from_fraction);
      const float hi = std::min(s1, to_fraction);
      if (hi <= lo)
        continue;
      const float a0 = start + i * step + gap * 0.5f;
      const float a1 = start + (i + 1) * step - gap * 0.5f;
      const float f0 = start + lo * span;
      const float f1 = start + hi * span;
      Arc(list, center, r, std::max(a0, f0), std::min(a1, f1), color, width);
    }
  };

  if (a > 0.002f)
  {
    // Backplate and empty segments.
    Arc(list, center, radius, start - 0.03f, end + 0.03f, IM_COL32(6, 8, 16, static_cast<int>(150 * a)),
        thick * 1.55f);
    segments(0.0f, 1.0f, IM_COL32(44, 52, 70, static_cast<int>(215 * a)), thick, radius);

    // The level already reached: its colour, dimmer, all round.
    if (g.level >= 1 && !full)
      segments(0.0f, 1.0f, Color(color_of(g.level), 0.85f * a, 0.55f), thick, radius);

    // The fill towards the next level (or all of it, at the last one).
    const std::uint32_t fill = full ? color_of(g.levels) : color_of(g.level + 1);
    const float upto = full ? 1.0f : g.progress;
    if (upto > 0.0f)
    {
      Arc(list, center, radius, start, start + upto * span, Color(fill, (0.16f + 0.12f * pulse) * a),
          thick * 2.6f);
      segments(0.0f, upto, Color(fill, a, 1.0f, 0.15f * pulse), thick, radius);
      if (!full)
      {
        // The tip: a bright line across the bar.
        const float tip = start + upto * span;
        const ImVec2 dir(std::cos(tip), std::sin(tip));
        list->AddLine(ImVec2(center.x + dir.x * (radius - thick * 0.75f), center.y + dir.y * (radius - thick * 0.75f)),
                      ImVec2(center.x + dir.x * (radius + thick * 0.75f), center.y + dir.y * (radius + thick * 0.75f)),
                      Color(fill, a, 1.0f, 0.7f), std::max(1.5f, thick * 0.22f));
      }
    }
    // Thin edges, as a frame.
    Arc(list, center, radius + thick * 0.78f, start, end, Color(0xFFFFFF, 0.22f * a), std::max(1.0f, thick * 0.08f));
    Arc(list, center, radius - thick * 0.78f, start, end, Color(0xFFFFFF, 0.12f * a), std::max(1.0f, thick * 0.06f));

    // The level reached, beside the arc.
    if (g.level >= 1)
    {
      const float height = radius * 0.56f;
      const ImVec2 pos(center.x - radius * 0.68f, center.y - height * 0.5f);
      const std::uint32_t rgb = color_of(g.level);
      Digit(list, g.level, pos, height, IM_COL32(4, 6, 12, static_cast<int>(170 * a)), height * 0.26f);
      Digit(list, g.level, pos, height, Color(rgb, 0.35f * a), height * 0.24f);
      Digit(list, g.level, pos, height, Color(rgb, a, 1.0f, 0.25f + 0.35f * pulse), height * 0.13f);
    }

    // A new level: the arc lights up, and a ring goes out.
    if (g.flash > 0.0f)
    {
      const std::uint32_t rgb = color_of(g.level);
      segments(0.0f, 1.0f, Color(rgb, g.flash * 0.9f * a, 1.0f, 0.6f), thick * (1.0f + 0.4f * g.flash), radius);
      Arc(list, center, radius * (1.0f + 0.45f * (1.0f - g.flash)), start - 0.15f, end + 0.15f,
          Color(rgb, g.flash * a), thick * 0.45f * g.flash + 1.0f);
    }
  }
  // The release: the arc in the colour it reached flies out and fades.
  if (g.burst > 0.0f && g.burst_level >= 1)
  {
    const std::uint32_t rgb = color_of(g.burst_level);
    const float out = 1.0f - g.burst;
    Arc(list, center, radius * (1.0f + 0.7f * out), start - 0.25f * out, end + 0.25f * out,
        Color(rgb, 0.35f * g.burst), thick * (2.4f - out));
    Arc(list, center, radius * (1.0f + 0.7f * out), start - 0.25f * out, end + 0.25f * out,
        Color(rgb, g.burst, 1.0f, 0.5f * g.burst), thick * (0.9f - 0.5f * out));
  }
}
}  // namespace moderngekko::detail
