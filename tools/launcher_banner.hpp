// The PLAY page's banner built from layers (layout and motion in
// launcher_banner_layout.hpp), from Resources/banner/ beside the launcher
// (MODERNGEKKO_LAUNCHER_BANNER_LAYERS). Each frame the layers are drawn into
// a picture of the banner's own size, the backdrop waving and leaning with the
// mouse, the other layers in front at their depths, the logos taking turns,
// and that picture goes on the page like the flat banner, with its rounded
// corners. Without the folder, or with nothing in it to draw, the page shows
// the flat banner.
//
// The Netplay lobby uses the same class for its background (Resources/
// lobby-background/): DrawCover fills the whole window with the layers, as a
// picture scaled to cover it, leaning with the mouse anywhere in the window.
#pragma once

#include "launcher_banner_layout.hpp"
#include "launcher_ui.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <vector>

namespace moderngekko::frontend::ui
{
class LayeredBanner
{
public:
  LayeredBanner() = default;
  LayeredBanner(const LayeredBanner&) = delete;
  LayeredBanner& operator=(const LayeredBanner&) = delete;
  ~LayeredBanner() { Unload(); }

  // folder: Resources/banner. largest_width: the most pixels wide the banner
  // is drawn, so bigger pictures are shrunk first. True when there is a
  // banner to draw.
  bool Load(SDL_Renderer* renderer, const std::filesystem::path& folder, float largest_width)
  {
    Unload();
    std::ifstream file(folder / "layout.txt");
    if (!file)
      return false;
    const std::optional<BannerLayout> layout = ParseBannerLayout(file);
    if (!layout)
    {
      std::cout << "banner: nothing to draw in " << (folder / "layout.txt").string() << '\n';
      return false;
    }
    m_layout = *layout;
    for (const BannerLayer& layer : m_layout.layers)
    {
      Picture picture;
      picture.layer = layer;
      const std::filesystem::path path = folder / layer.picture;
      SDL_Surface* surface = SDL_LoadPNG(path.string().c_str());
      if (!surface)
      {
        std::cout << "banner: can't load " << path.string() << ": " << SDL_GetError() << '\n';
        continue;
      }
      // The first picture sets the banner's size; the rest must match it.
      if (m_width == 0)
      {
        m_width = surface->w;
        m_height = surface->h;
      }
      if (surface->w != m_width || surface->h != m_height)
      {
        std::cout << "banner: " << path.string() << " isn't " << m_width << "x" << m_height
                  << ", left out\n";
        SDL_DestroySurface(surface);
        continue;
      }
      picture.box = FindBox(surface);
      ShrinkForDrawing(surface, largest_width);
      picture.texture = SDL_CreateTextureFromSurface(renderer, surface);
      SDL_DestroySurface(surface);
      if (!picture.texture)
        continue;
      // The backdrop is copied as it is, the rest drawn over it.
      SDL_SetTextureBlendMode(picture.texture,
                              m_pictures.empty() ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
      m_pictures.push_back(picture);
    }
    for (const Picture& picture : m_pictures)
      m_logos += picture.layer.logo ? 1 : 0;
    if (m_pictures.empty())
    {
      Unload();
      return false;
    }
    std::cout << "banner: " << m_pictures.size() << " layers from " << folder.string() << '\n';
    return true;
  }

  // Its pictures go; before the renderer does.
  void Unload()
  {
    for (Picture& picture : m_pictures)
    {
      if (picture.texture)
        SDL_DestroyTexture(picture.texture);
    }
    m_pictures.clear();
    if (m_target)
      SDL_DestroyTexture(m_target);
    m_target = nullptr;
    m_target_wide = m_target_high = 0;
    m_width = m_height = 0;
    m_logos = 0;
  }

  bool Loaded() const { return !m_pictures.empty(); }
  float Aspect() const { return m_width > 0 ? static_cast<float>(m_height) / m_width : 0.0f; }

  // The banner, `width` wide at the cursor, like DrawLauncherBanner.
  void Draw(SDL_Renderer* renderer, float width, double now)
  {
    const ImVec2 size(width, std::floor(width * Aspect()));
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImGuiIO& io = ImGui::GetIO();
    const int pixels_wide = static_cast<int>(std::lround(size.x * io.DisplayFramebufferScale.x));
    const int pixels_high = static_cast<int>(std::lround(size.y * io.DisplayFramebufferScale.y));
    if (pixels_wide <= 0 || pixels_high <= 0)
      return;

    // How far the mouse leans it, eased.
    if (m_started < 0.0)
    {
      m_started = now;
      m_last = now;
    }
    const float seconds = static_cast<float>(now - m_started);
    // Not through a popup over it.
    const bool hovered =
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
        ImGui::IsMouseHoveringRect(position, ImVec2(position.x + size.x, position.y + size.y),
                                   false);
    const banner_motion::Shift target = banner_motion::Lean(
        hovered, (io.MousePos.x - (position.x + size.x * 0.5f)) / (size.x * 0.5f),
        (io.MousePos.y - (position.y + size.y * 0.5f)) / (size.y * 0.5f), seconds);
    m_lean = banner_motion::Follow(m_lean, target, static_cast<float>(now - m_last));
    m_last = now;

    if (!Compose(renderer, pixels_wide, pixels_high, seconds))
      return;
    ImGui::GetWindowDrawList()->AddImageRounded(
        ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(m_target))),
        position, ImVec2(position.x + size.x, position.y + size.y), ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f), IM_COL32_WHITE, ImGui::GetStyle().ChildRounding);
    ImGui::Dummy(size);
  }

  // The layers over the whole render output, behind everything else: scaled
  // to cover it (cut evenly at the sides, or at the top and bottom), drawn
  // with the renderer. Call it before drawing anything else in the frame.
  void DrawCover(SDL_Renderer* renderer, double now)
  {
    int output_wide = 0;
    int output_high = 0;
    if (m_width <= 0 || m_height <= 0 ||
        !SDL_GetCurrentRenderOutputSize(renderer, &output_wide, &output_high) ||
        output_wide <= 0 || output_high <= 0)
      return;
    const float cover = std::max(static_cast<float>(output_wide) / m_width,
                                 static_cast<float>(output_high) / m_height);
    const int pixels_wide = std::max(output_wide, static_cast<int>(std::ceil(m_width * cover)));
    const int pixels_high = std::max(output_high, static_cast<int>(std::ceil(m_height * cover)));

    if (m_started < 0.0)
    {
      m_started = now;
      m_last = now;
    }
    const float seconds = static_cast<float>(now - m_started);
    // The mouse leans it from anywhere in the window.
    const ImGuiIO& io = ImGui::GetIO();
    const bool inside = ImGui::IsMousePosValid() && io.DisplaySize.x > 0.0f &&
                        io.DisplaySize.y > 0.0f;
    const banner_motion::Shift target = banner_motion::Lean(
        inside, inside ? io.MousePos.x / (io.DisplaySize.x * 0.5f) - 1.0f : 0.0f,
        inside ? io.MousePos.y / (io.DisplaySize.y * 0.5f) - 1.0f : 0.0f, seconds);
    m_lean = banner_motion::Follow(m_lean, target, static_cast<float>(now - m_last));
    m_last = now;

    if (!Compose(renderer, pixels_wide, pixels_high, seconds))
      return;
    const SDL_FRect source{static_cast<float>(pixels_wide - output_wide) * 0.5f,
                           static_cast<float>(pixels_high - output_high) * 0.5f,
                           static_cast<float>(output_wide), static_cast<float>(output_high)};
    SDL_RenderTexture(renderer, m_target, &source, nullptr);
  }

private:
  struct Picture
  {
    BannerLayer layer;
    SDL_Texture* texture = nullptr;
    // Where what isn't transparent is, in the picture's pixels: moving keeps
    // it inside the banner, and a logo grows and shrinks around its middle.
    banner_motion::Box box;
  };

  static banner_motion::Box FindBox(SDL_Surface* surface)
  {
    banner_motion::Box box{0.0f, 0.0f, static_cast<float>(surface->w),
                           static_cast<float>(surface->h)};
    SDL_Surface* pixels = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    if (!pixels)
      return box;
    int left = pixels->w, top = pixels->h, right = -1, bottom = -1;
    if (SDL_LockSurface(pixels))
    {
      for (int y = 0; y < pixels->h; ++y)
      {
        const auto* row = static_cast<const std::uint8_t*>(pixels->pixels) + y * pixels->pitch;
        for (int x = 0; x < pixels->w; ++x)
        {
          if (row[x * 4 + 3] > 16)
          {
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
          }
        }
      }
      SDL_UnlockSurface(pixels);
    }
    SDL_DestroySurface(pixels);
    if (right >= left && bottom >= top)
      box = {static_cast<float>(left), static_cast<float>(top), static_cast<float>(right + 1),
             static_cast<float>(bottom + 1)};
    return box;
  }

  // Draws the layers into m_target, made or remade at this size.
  bool Compose(SDL_Renderer* renderer, int pixels_wide, int pixels_high, float seconds)
  {
    if (!m_target || m_target_wide != pixels_wide || m_target_high != pixels_high)
    {
      if (m_target)
        SDL_DestroyTexture(m_target);
      m_target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
                                   pixels_wide, pixels_high);
      if (!m_target)
        return false;
      SDL_SetTextureBlendMode(m_target, SDL_BLENDMODE_BLEND);
      m_target_wide = pixels_wide;
      m_target_high = pixels_high;
    }
    SDL_Texture* previous = SDL_GetRenderTarget(renderer);
    if (!SDL_SetRenderTarget(renderer, m_target))
      return false;
    Uint8 red = 0, green = 0, blue = 0, alpha = 0;
    SDL_GetRenderDrawColor(renderer, &red, &green, &blue, &alpha);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);

    // Picture pixels to target pixels.
    const float to_target = static_cast<float>(pixels_wide) / m_width;
    const banner_motion::LogoTurn turn =
        banner_motion::LogoAt(seconds, m_logos, m_layout.logo_seconds);
    int logo_index = 0;
    bool first = true;
    for (const Picture& picture : m_pictures)
    {
      const BannerLayer& layer = picture.layer;
      // Leaning: nearer layers move more, away from the pointer, as if
      // looking around them; up and down less, the banner being wide.
      const float lean_x = -m_lean.x * layer.depth;
      const float lean_y = -m_lean.y * layer.depth * 0.5f;
      if (layer.logo)
      {
        const int index = logo_index++;
        if (index == turn.current)
          DrawLogo(renderer, picture, turn.current_look, lean_x, lean_y, to_target);
        else if (index == turn.leaving)
          DrawLogo(renderer, picture, turn.leaving_look, lean_x, lean_y, to_target);
      }
      else
      {
        const float zoom =
            first ? banner_motion::BackdropZoom(static_cast<float>(m_width),
                                                static_cast<float>(m_height), layer.depth,
                                                layer.motion == BannerLayer::Motion::Wave) :
                    1.0f;
        const float lift =
            layer.motion == BannerLayer::Motion::Float ? banner_motion::Float(seconds) : 0.0f;
        banner_motion::Shift shift{lean_x, lean_y + lift};
        // The backdrop, bigger than the banner, can go anywhere; the rest
        // stay inside it.
        if (!first)
          shift = banner_motion::KeepInside(shift, picture.box, 1.0f, 0.0f, 0.0f,
                                            static_cast<float>(m_width),
                                            static_cast<float>(m_height));
        DrawLayer(renderer, picture, zoom, shift.x, shift.y,
                  layer.motion == BannerLayer::Motion::Wave, seconds, to_target);
      }
      first = false;
    }
    SDL_SetRenderTarget(renderer, previous);
    return true;
  }

  // A layer as a grid of quads (so a waving one can bend), zoomed around the
  // banner's middle and moved by (shift_x, shift_y) picture pixels.
  void DrawLayer(SDL_Renderer* renderer, const Picture& picture, float zoom, float shift_x,
                 float shift_y, bool waves, float seconds, float to_target)
  {
    // A still layer is four quads, not one: SDL's software renderer (the
    // last resort when no other works) left a lone quad out when tested.
    const int columns = waves ? 40 : 2;
    const int rows = waves ? 10 : 2;
    m_vertices.clear();
    m_indices.clear();
    const float middle_x = m_width * 0.5f;
    const float middle_y = m_height * 0.5f;
    for (int row = 0; row <= rows; ++row)
    {
      for (int column = 0; column <= columns; ++column)
      {
        const float u = static_cast<float>(column) / columns;
        const float v = static_cast<float>(row) / rows;
        float x = u * m_width;
        float y = v * m_height;
        if (waves)
        {
          const banner_motion::Shift wave = banner_motion::Wave(x, y, seconds);
          x += wave.x;
          y += wave.y;
        }
        x = middle_x + (x - middle_x) * zoom + shift_x;
        y = middle_y + (y - middle_y) * zoom + shift_y;
        m_vertices.push_back({{x * to_target, y * to_target}, {1.0f, 1.0f, 1.0f, 1.0f}, {u, v}});
      }
    }
    for (int row = 0; row < rows; ++row)
    {
      for (int column = 0; column < columns; ++column)
      {
        const int corner = row * (columns + 1) + column;
        const int below = corner + columns + 1;
        for (const int index : {corner, corner + 1, below, corner + 1, below + 1, below})
          m_indices.push_back(index);
      }
    }
    SDL_RenderGeometry(renderer, picture.texture, m_vertices.data(),
                       static_cast<int>(m_vertices.size()), m_indices.data(),
                       static_cast<int>(m_indices.size()));
  }

  // A logo, grown or shrunk around its middle and faded as `look` says, kept
  // inside the banner.
  void DrawLogo(SDL_Renderer* renderer, const Picture& picture, game_select_motion::Pop look,
                float shift_x, float shift_y, float to_target)
  {
    if (look.alpha <= 0.0f)
      return;
    const float w = static_cast<float>(m_width);
    const float h = static_cast<float>(m_height);
    const float middle_x = (picture.box.left + picture.box.right) * 0.5f;
    const float middle_y = (picture.box.top + picture.box.bottom) * 0.5f;
    const banner_motion::Shift shift = banner_motion::KeepInside(
        {shift_x, shift_y}, picture.box, look.scale, middle_x, middle_y, w, h);
    const auto corner = [&](float x, float y) {
      return SDL_FPoint{(middle_x + (x - middle_x) * look.scale + shift.x) * to_target,
                        (middle_y + (y - middle_y) * look.scale + shift.y) * to_target};
    };
    const SDL_FColor colour{1.0f, 1.0f, 1.0f, look.alpha};
    const SDL_Vertex quad[4] = {{corner(0.0f, 0.0f), colour, {0.0f, 0.0f}},
                                {corner(w, 0.0f), colour, {1.0f, 0.0f}},
                                {corner(w, h), colour, {1.0f, 1.0f}},
                                {corner(0.0f, h), colour, {0.0f, 1.0f}}};
    static constexpr int INDICES[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer, picture.texture, quad, 4, INDICES, 6);
  }

  BannerLayout m_layout;
  std::vector<Picture> m_pictures;
  int m_width = 0;
  int m_height = 0;
  int m_logos = 0;
  SDL_Texture* m_target = nullptr;
  int m_target_wide = 0;
  int m_target_high = 0;
  double m_started = -1.0;
  double m_last = 0.0;
  banner_motion::Shift m_lean;
  std::vector<SDL_Vertex> m_vertices;
  std::vector<int> m_indices;
};
}  // namespace moderngekko::frontend::ui
