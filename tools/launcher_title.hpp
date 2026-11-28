// The launcher's title screen and the scene behind its pages, from
// Resources/title/ beside the launcher (MODERNGEKKO_LAUNCHER_TITLE_SCREEN;
// layout in launcher_title_layout.hpp). The launcher opens on white with the
// title logo and "PRESS ANY BUTTON"; a press plays the title's sound, the
// logo fades and the camera goes through the scene's layers, each at its own
// depth, to the whole picture. That picture then stays behind the pages: its
// grids keep moving, its layers bob and lean a little with the mouse.
// Without the folder the launcher opens with its splash and background, as
// before.
#pragma once

#include "launcher_sound.hpp"
#include "launcher_splash.hpp"
#include "launcher_title_layout.hpp"
#include "launcher_ui.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "text_paths.hpp"

namespace moderngekko::frontend::ui
{
class TitleScene
{
public:
  TitleScene() = default;
  TitleScene(const TitleScene&) = delete;
  TitleScene& operator=(const TitleScene&) = delete;
  ~TitleScene() { Unload(); }

  // Resources/title/ beside the launcher. largest_width: the most pixels
  // wide a picture is kept (bigger ones are shrunk as they load).
  bool Load(SDL_Renderer* renderer, const std::filesystem::path& release_directory,
            float largest_width)
  {
    Unload();
    const std::filesystem::path folder = release_directory / "Resources" / "title";
    std::ifstream file(folder / "layout.txt");
    if (!file)
      return false;
    const std::optional<TitleLayout> layout = ParseTitleLayout(file);
    if (!layout)
    {
      std::cout << "title: nothing to show in " << text::PathText(folder / "layout.txt") << '\n';
      return false;
    }
    m_layout = *layout;
    for (const TitleLayer& layer : m_layout.layers)
    {
      const LauncherImage image = LoadLauncherImage(
          renderer, release_directory, "title/" + layer.picture, "title layer", false,
          largest_width);
      m_layers.push_back(image.texture);
    }
    m_logo = LoadLauncherImage(renderer, release_directory, "title/" + m_layout.title,
                               "title logo");
    if (!m_layers.front() || !m_logo.texture)
    {
      std::cout << "title: its first layer or its logo is missing; opening as before\n";
      Unload();
      return false;
    }
    if (!m_layout.sound.empty())
      m_sound = LoadSoundEffect(folder / text::TextPath(m_layout.sound));
    return true;
  }

  void Unload()
  {
    for (SDL_Texture* texture : m_layers)
    {
      if (texture)
        SDL_DestroyTexture(texture);
    }
    m_layers.clear();
    if (m_logo.texture)
      SDL_DestroyTexture(m_logo.texture);
    m_logo = {};
    m_sound.reset();
  }

  bool Loaded() const { return !m_layers.empty(); }
  // How long the camera's move takes (0: TitleIntro's own).
  double MoveSeconds() const { return m_layout.move_seconds; }
  const LauncherImage& Logo() const { return m_logo; }
  const SoundEffect* Sound() const { return m_sound.get(); }

  // Follows the mouse, smoothly: -1 to 1 across and down the window, 0 when
  // it is outside.
  void TrackMouse(ImVec2 mouse, ImVec2 window, float delta_seconds)
  {
    ImVec2 target(0.0f, 0.0f);
    if (mouse.x > -FLT_MAX * 0.5f && window.x > 0.0f && window.y > 0.0f)
    {
      target.x = std::clamp((mouse.x / window.x) * 2.0f - 1.0f, -1.0f, 1.0f);
      target.y = std::clamp((mouse.y / window.y) * 2.0f - 1.0f, -1.0f, 1.0f);
    }
    const float follow = 1.0f - std::exp(-std::max(delta_seconds, 0.0f) * 3.0f);
    m_mouse.x += (target.x - m_mouse.x) * follow;
    m_mouse.y += (target.y - m_mouse.y) * follow;
  }

  // The scene over the whole window. move: the camera's move, 0 (close, at
  // the focus) to 1 (the whole picture).
  void Render(SDL_Renderer* renderer, double seconds, float move) const
  {
    int window_width = 0;
    int window_height = 0;
    float picture_width = 0.0f;
    float picture_height = 0.0f;
    if (m_layers.empty() || !m_layers.front() ||
        !SDL_GetCurrentRenderOutputSize(renderer, &window_width, &window_height) ||
        !SDL_GetTextureSize(m_layers.front(), &picture_width, &picture_height) ||
        window_width <= 0 || window_height <= 0 || picture_width <= 0.0f ||
        picture_height <= 0.0f)
      return;
    // Window pixels from here on, whatever scale the interface draws at.
    float render_scale_x = 1.0f;
    float render_scale_y = 1.0f;
    SDL_GetRenderScale(renderer, &render_scale_x, &render_scale_y);
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);

    const float width = static_cast<float>(window_width);
    const float height = static_cast<float>(window_height);
    for (std::size_t i = 0; i < m_layers.size(); ++i)
    {
      SDL_Texture* texture = m_layers[i];
      const TitleLayer& layer = m_layout.layers[i];
      const float alpha = title_motion::LayerAlpha(layer, move);
      if (!texture || alpha <= 0.0f)
        continue;
      const title_motion::Rect place =
          title_motion::LayerRect(m_layout, layer, width, height, picture_width, picture_height,
                                  move, m_mouse.x, m_mouse.y, seconds);
      const SDL_FRect rect{place.x, place.y, place.w, place.h};
      SDL_SetTextureAlphaModFloat(texture, alpha);
      SDL_RenderTexture(renderer, texture, nullptr, &rect);
      for (const TitleGrid& grid : m_layout.grids)
      {
        if (grid.layer == i)
          DrawGrid(renderer, grid, place,
                   m_layout.picture_width > 0.0f ? m_layout.picture_width : picture_width, width,
                   height, seconds, alpha);
      }
    }
    SDL_SetRenderScale(renderer, render_scale_x, render_scale_y);
  }

private:
  void DrawGrid(SDL_Renderer* renderer, const TitleGrid& grid, const title_motion::Rect& place,
                float picture_width, float width, float height, double seconds,
                float alpha) const
  {
    m_mesh.clear();
    title_motion::GridMesh(grid, place, picture_width, width, height, seconds, alpha, m_mesh);
    if (m_mesh.empty())
      return;
    SDL_BlendMode previous = SDL_BLENDMODE_BLEND;
    SDL_GetRenderDrawBlendMode(renderer, &previous);
    // light: what is under a line, brighter by its colour (dst + dst x src);
    // where the renderer can't, added instead, a little.
    bool light = false;
    float strength = 1.0f;
    if (grid.blend == TitleBlend::Light)
    {
      light = SDL_SetRenderDrawBlendMode(
          renderer, SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_DST_COLOR, SDL_BLENDFACTOR_ONE,
                                               SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO,
                                               SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD));
      if (!light)
      {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
        strength = 0.35f;
      }
    }
    else
    {
      SDL_SetRenderDrawBlendMode(renderer, grid.blend == TitleBlend::Add ? SDL_BLENDMODE_ADD
                                                                         : SDL_BLENDMODE_BLEND);
    }
    m_vertices.resize(m_mesh.size());
    for (std::size_t k = 0; k < m_mesh.size(); ++k)
    {
      const title_motion::Vertex& v = m_mesh[k];
      SDL_Vertex& out = m_vertices[k];
      out.position = SDL_FPoint{v.x, v.y};
      // The light blend reads only the colour: it carries the strength.
      const float scale = light ? v.a : strength;
      out.color = SDL_FColor{v.r * scale, v.g * scale, v.b * scale, v.a};
      out.tex_coord = SDL_FPoint{0.0f, 0.0f};
    }
    SDL_RenderGeometry(renderer, nullptr, m_vertices.data(), static_cast<int>(m_vertices.size()),
                       nullptr, 0);
    SDL_SetRenderDrawBlendMode(renderer, previous);
  }

  TitleLayout m_layout;
  std::vector<SDL_Texture*> m_layers;
  LauncherImage m_logo;
  std::shared_ptr<const SoundEffect> m_sound;
  ImVec2 m_mouse{0.0f, 0.0f};
  mutable std::vector<title_motion::Vertex> m_mesh;
  mutable std::vector<SDL_Vertex> m_vertices;
};

// The title screen over the scene (origin, size: the window): the white, the
// logo and the prompt, and the black it fades in from.
inline void DrawTitleScreen(ImDrawList* list, ImVec2 origin, ImVec2 size, const TitleIntro& intro,
                            double now, const TitleScene& scene, ImFont* bold, const char* prompt)
{
  const ImVec2 end(origin.x + size.x, origin.y + size.y);
  const float white = intro.White(now);
  if (white > 0.0f)
    list->AddRectFilled(origin, end, splash_detail::WithAlpha(IM_COL32_WHITE, white));
  const LauncherImage& logo = scene.Logo();
  const float logo_alpha = intro.Logo(now);
  float logo_bottom = origin.y + size.y * 0.5f;
  if (logo.texture && logo_alpha > 0.0f && logo.aspect > 0.0f)
  {
    const float logo_width = std::min(size.x * 0.62f, size.y * 0.62f / logo.aspect);
    const float logo_height = logo_width * logo.aspect;
    const ImVec2 min(origin.x + (size.x - logo_width) * 0.5f,
                     origin.y + size.y * 0.44f - logo_height * 0.5f);
    list->AddImage(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(logo.texture))),
                   min, ImVec2(min.x + logo_width, min.y + logo_height), ImVec2(0.0f, 0.0f),
                   ImVec2(1.0f, 1.0f), splash_detail::WithAlpha(IM_COL32_WHITE, logo_alpha));
    logo_bottom = min.y + logo_height;
  }
  const float prompt_alpha = intro.Prompt(now);
  if (bold && prompt && prompt_alpha > 0.0f)
  {
    const float text_size = std::max(16.0f, size.y * 0.038f);
    const ImVec2 text = bold->CalcTextSizeA(text_size, FLT_MAX, 0.0f, prompt);
    const ImVec2 position(origin.x + (size.x - text.x) * 0.5f, logo_bottom + size.y * 0.05f);
    splash_detail::AddLeaningText(list, bold, text_size, position,
                                  splash_detail::WithAlpha(IM_COL32(12, 26, 29, 255), prompt_alpha),
                                  prompt, 0.18f);
  }
  const float black = intro.Black(now);
  if (black > 0.0f)
    list->AddRectFilled(origin, end, splash_detail::WithAlpha(IM_COL32_BLACK, black));
}
}  // namespace moderngekko::frontend::ui
