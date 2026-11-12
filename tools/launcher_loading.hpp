// The loading screen between the launcher and the game
// (launcher_transition.hpp says when and how much): black, the game's logo in
// the middle, coming in as on the game selection screen and breathing, and
// under it a row of chevrons in the launcher's green, lit one after the other
// as if getting ready to boost. Without a logo (a build without the selection
// screen's art), only the chevrons.
#pragma once

#include "launcher_game_select_layout.hpp"
#include "launcher_look.hpp"
#include "launcher_transition.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace moderngekko::frontend::ui
{
// The logo to show and its size on screen; no texture for none.
struct LoadingLogo
{
  ImTextureID texture = ImTextureID_Invalid;
  float width = 0.0f;
  float height = 0.0f;
};

// Over the whole window (origin, size), as the transition has it now.
inline void DrawLoadingScreen(ImDrawList* list, ImVec2 origin, ImVec2 size,
                              const GameTransition& transition, double now,
                              const LoadingLogo& logo, float scale)
{
  using namespace game_select_motion;
  const float black = transition.Black(now);
  if (black <= 0.0f)
    return;
  list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                      IM_COL32(0, 0, 0, static_cast<int>(255.0f * black)));
  const float alpha = transition.ArtAlpha(now);
  if (alpha <= 0.001f)
    return;
  const float t = transition.ArtSeconds(now);
  constexpr float TAU = 6.2831853f;
  const ImVec2 centre(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);

  // The logo, a little above the middle; no wider than 60% of the window.
  float row_y = centre.y + 30.0f * scale;
  if (logo.texture != ImTextureID_Invalid && logo.width > 0.0f && logo.height > 0.0f)
  {
    const Pop pop = LogoPop(t);
    const float zoom = pop.scale * (1.0f + 0.02f * std::sin(t * TAU / 1.6f));
    const float fit = std::min(1.0f, size.x * 0.6f / logo.width);
    const float half_w = logo.width * fit * 0.5f;
    const float half_h = logo.height * fit * 0.5f;
    const ImVec2 middle(centre.x, centre.y - 30.0f * scale);
    list->AddImage(ImTextureRef(logo.texture),
                   ImVec2(middle.x - half_w * zoom, middle.y - half_h * zoom),
                   ImVec2(middle.x + half_w * zoom, middle.y + half_h * zoom), ImVec2(0.0f, 0.0f),
                   ImVec2(1.0f, 1.0f),
                   IM_COL32(255, 255, 255, static_cast<int>(255.0f * alpha * pop.alpha)));
    row_y = middle.y + half_h + 40.0f * scale;
  }

  // The chevrons: dim, with a light running through them left to right and
  // a little past the last before it starts over.
  constexpr int COUNT = 5;
  const float width = 13.0f * scale;
  const float height = 22.0f * scale;
  const float gap = 11.0f * scale;
  const float thickness = 4.5f * scale;
  const float row_alpha = alpha * Clamp01((t - 0.15f) / 0.3f);
  const float sweep = std::fmod(std::max(t, 0.0f) * 5.5f, COUNT + 2.5f);
  float x = centre.x - (COUNT * width + (COUNT - 1) * gap) * 0.5f;
  for (int i = 0; i < COUNT; ++i, x += width + gap)
  {
    const float lit = std::max(0.0f, 1.0f - std::abs(sweep - static_cast<float>(i) - 0.5f) / 1.6f);
    const std::array<ImVec2, 3> points = {ImVec2(x, row_y - height * 0.5f), ImVec2(x + width, row_y),
                                          ImVec2(x, row_y + height * 0.5f)};
    const auto colour = [&](float amount) {
      return ImGui::ColorConvertFloat4ToU32(ImVec4(LAUNCHER_ACCENT.x, LAUNCHER_ACCENT.y,
                                                   LAUNCHER_ACCENT.z, Clamp01(row_alpha * amount)));
    };
    if (lit > 0.0f)
      list->AddPolyline(points.data(), 3, colour(0.3f * lit), ImDrawFlags_None, thickness * 2.6f);
    list->AddPolyline(points.data(), 3, colour(0.18f + 0.82f * lit * lit), ImDrawFlags_None,
                      thickness);
  }
}
}  // namespace moderngekko::frontend::ui
