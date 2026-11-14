// The launcher's opening. For a couple of seconds after the launcher opens,
// its window shows the program's icon (Resources/app-icon.png) on the icon's
// own background colour, the program's name under it, leaning forward, a
// line in the launcher's green and who made it; then all of it fades and the
// launcher is there. Any key, click or gamepad button makes it go at once. It
// also hides the launcher's first frames, which load its pictures, so the
// launcher doesn't appear all at once. Without the icon, the name and the
// line show on the launcher's own dark colour.
#pragma once

#include "launcher_game_select_layout.hpp"
#include "launcher_look.hpp"
#include "launcher_ui.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace moderngekko::frontend::ui
{
// When each part comes in and when it all goes, as times and amounts.
class LauncherSplash
{
public:
  static constexpr double ICON_AT = 0.15;   // seconds after the launcher's first frame
  static constexpr double ICON_IN = 0.6;
  static constexpr double TITLE_AT = 0.55;
  static constexpr double TITLE_IN = 0.45;
  static constexpr double CREDIT_AT = 0.9;
  static constexpr double CREDIT_IN = 0.4;
  static constexpr double LEAVE_AT = 2.3;   // it goes by itself
  static constexpr double LEAVE_LENGTH = 0.7;
  static constexpr double SKIP_LENGTH = 0.4;  // quicker when skipped
  // Going, the icon and the text fade first, over the first part of it, and
  // the colour behind them after: fading together, the icon's own
  // background would show as a box over the launcher.
  static constexpr float CONTENT_OUT = 0.4f;

  void Start(double now)
  {
    m_started_at = now;
    m_leave_at = now + LEAVE_AT;
    m_leave_length = LEAVE_LENGTH;
  }

  // A key, click or button: it goes now, if it isn't going already.
  void Skip(double now)
  {
    if (m_started_at && now < m_leave_at)
    {
      m_leave_at = now;
      m_leave_length = SKIP_LENGTH;
    }
  }

  bool Active(double now) const
  {
    return m_started_at && now < m_leave_at + m_leave_length;
  }
  bool Leaving(double now) const { return m_started_at && now >= m_leave_at; }

  // How much of it covers the launcher: 1, then down to 0 as it goes.
  float Cover(double now) const
  {
    using namespace game_select_motion;
    if (!Active(now))
      return 0.0f;
    return 1.0f - EaseInOut(Clamp01((Gone(now) - CONTENT_OUT) / (1.0f - CONTENT_OUT)));
  }
  // How much of the icon and the text is left: 1, then down to 0 as it goes.
  float Content(double now) const
  {
    using namespace game_select_motion;
    if (!Active(now))
      return 0.0f;
    return 1.0f - EaseInOut(Clamp01(Gone(now) / CONTENT_OUT));
  }

  // Each part's own way in, 0 to 1.
  float Icon(double now) const { return Part(now, ICON_AT, ICON_IN); }
  float Title(double now) const { return Part(now, TITLE_AT, TITLE_IN); }
  float Credit(double now) const { return Part(now, CREDIT_AT, CREDIT_IN); }

private:
  static float Seconds(double seconds) { return static_cast<float>(seconds); }
  // How far it has gone, 0 (still there) to 1.
  float Gone(double now) const
  {
    if (now < m_leave_at)
      return 0.0f;
    return game_select_motion::Clamp01(Seconds(now - m_leave_at) / Seconds(m_leave_length));
  }
  float Part(double now, double at, double length) const
  {
    if (!m_started_at)
      return 0.0f;
    return game_select_motion::EaseOut(
        game_select_motion::Clamp01(Seconds(now - *m_started_at - at) / Seconds(length)));
  }

  std::optional<double> m_started_at;
  double m_leave_at = 0.0;
  double m_leave_length = LEAVE_LENGTH;
};

// The icon and the colour behind it: the colour of the icon's top left
// corner, when the icon is opaque there, so the icon doesn't show as a box.
struct SplashArt
{
  SDL_Texture* texture = nullptr;
  float aspect = 1.0f;  // height / width
  ImU32 background = IM_COL32(8, 8, 9, 255);  // the launcher's own (ImGuiCol_WindowBg)
};

inline SplashArt LoadSplashArt(SDL_Renderer* renderer, const fs::path& release_directory)
{
  SplashArt art;
  const fs::path path = release_directory / "Resources" / "app-icon.png";
  std::error_code ec;
  if (!fs::is_regular_file(path, ec))
    return art;
  SDL_Surface* surface = SDL_LoadPNG(path.string().c_str());
  if (!surface)
    return art;
  Uint8 r = 0, g = 0, b = 0, a = 0;
  if (SDL_ReadSurfacePixel(surface, 0, 0, &r, &g, &b, &a) && a == 255)
    art.background = IM_COL32(r, g, b, 255);
  ShrinkForDrawing(surface, 1024.0f);
  if (surface->w > 0 && surface->h > 0)
  {
    art.texture = SDL_CreateTextureFromSurface(renderer, surface);
    art.aspect = static_cast<float>(surface->h) / static_cast<float>(surface->w);
  }
  SDL_DestroySurface(surface);
  return art;
}

// Before the launcher's first frame: the window shows the splash's colour
// at once, instead of an empty window while the launcher loads.
inline void ClearToSplash(SDL_Renderer* renderer, const SplashArt& art)
{
  const ImVec4 colour = ImGui::ColorConvertU32ToFloat4(art.background);
  SDL_SetRenderDrawColorFloat(renderer, colour.x, colour.y, colour.z, 1.0f);
  SDL_RenderClear(renderer);
  SDL_RenderPresent(renderer);
}

namespace splash_detail
{
// Text leaning forward, as the Netplay lobby's title: each point of the
// letters goes right by how far it is above the baseline.
inline void AddLeaningText(ImDrawList* list, ImFont* font, float size, ImVec2 position, ImU32 colour,
                           const char* text, float lean)
{
  const int first_vertex = list->VtxBuffer.Size;
  list->AddText(font, size, position, colour, text);
  const ImFontGlyph* capital = font->GetFontBaked(size)->FindGlyphNoFallback('S');
  const float baseline = position.y + (capital ? capital->Y1 : size * 0.8f);
  for (int i = first_vertex; i < list->VtxBuffer.Size; ++i)
    list->VtxBuffer[i].pos.x += (baseline - list->VtxBuffer[i].pos.y) * lean;
}

inline ImU32 WithAlpha(ImU32 colour, float alpha)
{
  const int a = static_cast<int>(((colour >> IM_COL32_A_SHIFT) & 0xFF) *
                                 game_select_motion::Clamp01(alpha));
  return (colour & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
}
}  // namespace splash_detail

// Over the whole window (origin, size): `title` is the program's name, shown
// in capitals; `credit` the line under it (none when empty).
inline void DrawLauncherSplash(ImDrawList* list, ImVec2 origin, ImVec2 size,
                               const LauncherSplash& splash, double now, const SplashArt& art,
                               float scale, ImFont* bold, const char* title,
                               const std::string& credit)
{
  using splash_detail::WithAlpha;
  const float cover = splash.Cover(now);
  if (cover <= 0.0f)
    return;
  list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                      WithAlpha(art.background, cover));
  const float content = splash.Content(now);
  if (content <= 0.0f)
    return;
  const float middle_x = origin.x + size.x * 0.5f;

  // The icon, a little above the middle, coming in from a little smaller.
  const float icon_height = std::min(size.y * 0.4f, size.x * 0.3f);
  const float icon_bottom = origin.y + size.y * 0.42f + icon_height * 0.5f;
  if (art.texture)
  {
    const float in = splash.Icon(now);
    const float zoom = 0.9f + 0.1f * in;
    const float half_h = icon_height * 0.5f * zoom;
    const float half_w = half_h / art.aspect;
    const ImVec2 centre(middle_x, icon_bottom - icon_height * 0.5f);
    list->AddImage(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(art.texture))),
                   ImVec2(centre.x - half_w, centre.y - half_h),
                   ImVec2(centre.x + half_w, centre.y + half_h), ImVec2(0.0f, 0.0f),
                   ImVec2(1.0f, 1.0f), WithAlpha(IM_COL32_WHITE, in * content));
  }

  // The name, rising a little as it comes in, and the green line under it,
  // growing from the middle.
  std::string name = title ? title : "";
  for (char& c : name)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  constexpr float LEAN = 0.2f;
  const float title_in = splash.Title(now);
  const float title_size = 40.0f * scale;
  const ImVec2 extent = bold->CalcTextSizeA(title_size, FLT_MAX, 0.0f, name.c_str());
  const float title_top = (art.texture ? icon_bottom + 18.0f * scale : origin.y + size.y * 0.4f) +
                          (1.0f - title_in) * 10.0f * scale;
  splash_detail::AddLeaningText(
      list, bold, title_size, ImVec2(middle_x - extent.x * 0.5f - extent.y * LEAN * 0.3f, title_top),
      WithAlpha(IM_COL32(236, 236, 238, 255), title_in * content), name.c_str(), LEAN);
  const float line_y = title_top + extent.y + 6.0f * scale;
  const float line_half = extent.x * 0.5f * title_in;
  if (line_half > 0.5f)
    list->AddRectFilled(ImVec2(middle_x - line_half, line_y),
                        ImVec2(middle_x + line_half, line_y + 3.0f * scale),
                        WithAlpha(ImGui::ColorConvertFloat4ToU32(LAUNCHER_ACCENT), title_in * content),
                        1.5f * scale);

  // Who made it, small and dim.
  if (!credit.empty())
  {
    ImFont* font = ImGui::GetFont();
    const float credit_size = 15.0f * scale;
    const ImVec2 credit_extent = font->CalcTextSizeA(credit_size, FLT_MAX, 0.0f, credit.c_str());
    list->AddText(font, credit_size,
                  ImVec2(middle_x - credit_extent.x * 0.5f, line_y + 16.0f * scale),
                  WithAlpha(IM_COL32(150, 150, 155, 255), splash.Credit(now) * content),
                  credit.c_str());
  }
}
}  // namespace moderngekko::frontend::ui
