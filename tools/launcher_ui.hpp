#pragma once

// The look shared by the launcher and the Netplay lobby: the theme, the
// interface font (with the embedded Japanese characters), status pills,
// bordered panels, buttons and images from Resources/. Moved here from
// moderngekko_launcher.cpp so both windows draw with the same code. The accent
// colour and the font files are in launcher_look.hpp, which the in-game menu
// uses too.

#include "launcher_look.hpp"
#include "moving_background.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace moderngekko::frontend::ui
{
namespace fs = std::filesystem;

// Launcher colours and shapes: near-black neutral background, soft grey panels
// and the lime accent. Sizes are unscaled; the caller applies ScaleAllSizes()
// for the display's DPI afterwards.
inline void ApplyLauncherTheme(ImGuiStyle& style)
{
  style.WindowPadding = ImVec2(18.0f, 16.0f);
  style.FramePadding = ImVec2(10.0f, 6.0f);
  style.ItemSpacing = ImVec2(10.0f, 8.0f);
  style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
  style.ScrollbarSize = 12.0f;
  style.WindowRounding = 0.0f;
  style.ChildRounding = 10.0f;
  style.FrameRounding = 6.0f;
  style.PopupRounding = 8.0f;
  style.ScrollbarRounding = 8.0f;
  style.GrabRounding = 6.0f;
  style.TabRounding = 6.0f;
  style.WindowBorderSize = 0.0f;
  style.ChildBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;

  const ImVec4 accent = LAUNCHER_ACCENT;
  const auto with_alpha = [](ImVec4 color, float alpha) {
    color.w = alpha;
    return color;
  };
  ImVec4* colors = style.Colors;
  colors[ImGuiCol_Text] = ImVec4(0.94f, 0.94f, 0.94f, 1.0f);
  colors[ImGuiCol_TextDisabled] = ImVec4(0.58f, 0.58f, 0.60f, 1.0f);
  colors[ImGuiCol_WindowBg] = ImVec4(0.031f, 0.031f, 0.035f, 1.0f);
  colors[ImGuiCol_ChildBg] = ImVec4(0.067f, 0.067f, 0.075f, 1.0f);
  colors[ImGuiCol_PopupBg] = ImVec4(0.075f, 0.075f, 0.082f, 0.98f);
  colors[ImGuiCol_Border] = ImVec4(0.157f, 0.157f, 0.169f, 1.0f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  colors[ImGuiCol_FrameBg] = ImVec4(0.098f, 0.098f, 0.110f, 1.0f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.133f, 0.133f, 0.149f, 1.0f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.165f, 0.165f, 0.184f, 1.0f);
  colors[ImGuiCol_Button] = ImVec4(0.110f, 0.110f, 0.122f, 1.0f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.153f, 0.153f, 0.169f, 1.0f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.196f, 0.196f, 0.216f, 1.0f);
  colors[ImGuiCol_Header] = with_alpha(accent, 0.18f);
  colors[ImGuiCol_HeaderHovered] = with_alpha(accent, 0.28f);
  colors[ImGuiCol_HeaderActive] = with_alpha(accent, 0.38f);
  colors[ImGuiCol_CheckMark] = accent;
  colors[ImGuiCol_SliderGrab] = accent;
  colors[ImGuiCol_SliderGrabActive] = ImVec4(0.82f, 1.0f, 0.45f, 1.0f);
  colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
  colors[ImGuiCol_SeparatorHovered] = with_alpha(accent, 0.6f);
  colors[ImGuiCol_SeparatorActive] = accent;
  colors[ImGuiCol_Tab] = colors[ImGuiCol_ChildBg];
  colors[ImGuiCol_TabHovered] = with_alpha(accent, 0.25f);
  colors[ImGuiCol_TabSelected] = ImVec4(0.14f, 0.19f, 0.06f, 1.0f);
  colors[ImGuiCol_TabSelectedOverline] = accent;
  colors[ImGuiCol_TabDimmed] = colors[ImGuiCol_ChildBg];
  colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.11f, 0.14f, 0.06f, 1.0f);
  colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
  colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.26f, 0.26f, 0.29f, 1.0f);
  colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.32f, 0.32f, 0.35f, 1.0f);
  colors[ImGuiCol_PlotHistogram] = accent;
  colors[ImGuiCol_TextLink] = accent;
  colors[ImGuiCol_NavCursor] = accent;
  colors[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.6f);
  // StyleColorsDark() defaults that would still show up blue: the title bar of
  // popup windows (Netplay), selected text in input fields, the resize grip.
  colors[ImGuiCol_TitleBg] = colors[ImGuiCol_ChildBg];
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.110f, 0.110f, 0.122f, 1.0f);
  colors[ImGuiCol_TitleBgCollapsed] = colors[ImGuiCol_ChildBg];
  colors[ImGuiCol_TextSelectedBg] = with_alpha(accent, 0.30f);
  colors[ImGuiCol_ResizeGripHovered] = with_alpha(accent, 0.5f);
  colors[ImGuiCol_ResizeGripActive] = with_alpha(accent, 0.8f);
}

struct LauncherFonts
{
  ImFont* regular = nullptr;
  ImFont* bold = nullptr;  // the regular font when no bold face of its family exists
};

// The interface font (InterfaceFontCandidates), at size, with the embedded
// Japanese characters merged into each face; ImGui's built-in bitmap font when
// there is none.
inline LauncherFonts LoadInterfaceFont(const fs::path& release_directory, float size)
{
  ImGuiIO& io = ImGui::GetIO();
  for (const InterfaceFontFiles& candidate : InterfaceFontCandidates(release_directory))
  {
    LauncherFonts fonts;
    fonts.regular = io.Fonts->AddFontFromFileTTF(candidate.regular.string().c_str(), size);
    if (!fonts.regular)
      continue;
    MergeJapaneseFont(false, size);
    ImGui::GetStyle().FontSizeBase = size;
    if (!candidate.bold.empty())
      fonts.bold = io.Fonts->AddFontFromFileTTF(candidate.bold.string().c_str(), size);
    if (fonts.bold)
      MergeJapaneseFont(true, size);
    std::cout << "launcher font: " << candidate.regular.string() << '\n';
    if (fonts.bold)
      std::cout << "launcher bold font: " << candidate.bold.string() << '\n';
    else
      fonts.bold = fonts.regular;
    return fonts;
  }
  LauncherFonts fonts;
  fonts.regular = io.Fonts->AddFontDefault();
  MergeJapaneseFont(false, size);
  fonts.bold = fonts.regular;
  std::cout << "launcher font: ImGui default\n";
  return fonts;
}

// Small rounded label, e.g. the CPU core in the top bar. With in_top_bar it is
// right-aligned to the window's content edge and centred on a frame-high line,
// so it sits level with text drawn after AlignTextToFramePadding().
inline void DrawStatusPill(const char* text, ImVec4 color, float scale, bool in_top_bar)
{
  const ImVec2 padding(10.0f * scale, 3.0f * scale);
  const ImVec2 text_size = ImGui::CalcTextSize(text);
  const ImVec2 size(text_size.x + padding.x * 2.0f, text_size.y + padding.y * 2.0f);
  if (in_top_bar)
  {
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - size.x));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                         std::max(0.0f, (ImGui::GetFrameHeight() - size.y) * 0.5f));
  }
  const ImVec2 position = ImGui::GetCursorScreenPos();
  ImVec4 background = color;
  background.w = 0.14f;
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  draw_list->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y),
                           ImGui::GetColorU32(background), size.y * 0.5f);
  draw_list->AddText(ImVec2(position.x + padding.x, position.y + padding.y),
                     ImGui::GetColorU32(color), text);
  ImGui::Dummy(size);
}

// Optional images the build copies into Resources/ next to the launcher: the
// Play page banner (MODERNGEKKO_LAUNCHER_BANNER) and the controller artwork
// of the Controls page (MODERNGEKKO_LAUNCHER_CONTROLLER_ART). A missing or
// unreadable file just means no image, and the page shows its text alone.
struct LauncherImage
{
  SDL_Texture* texture = nullptr;
  float aspect = 0.0f;  // height / width
  // Backgrounds only: the floor that slides (moving_background.hpp).
  ScrollingFloor floor;
};
// Gives the window the program's icon, Resources/app-icon.png beside it
// (MODERNGEKKO_APP_ICON); without it the system's default icon stays.
inline void SetWindowIcon(SDL_Window* window, const fs::path& release_directory)
{
  const fs::path path = release_directory / "Resources" / "app-icon.png";
  std::error_code ec;
  if (!window || !fs::is_regular_file(path, ec))
    return;
  if (SDL_Surface* icon = SDL_LoadPNG(path.string().c_str()))
  {
    SDL_SetWindowIcon(window, icon);
    SDL_DestroySurface(icon);
  }
}

// A picture drawn at most largest_width pixels wide (0: not known), halved as
// often as it is still twice that or more: the renderer shrinks pictures with
// a plain linear filter, which only reads a few of their pixels each, so a
// big shrink looks grainy and jagged. Each halving averages 2x2 pixels
// (linear, exactly half the size). The surface is replaced.
inline void ShrinkForDrawing(SDL_Surface*& surface, float largest_width)
{
  while (largest_width > 0.0f && surface->w >= 2.0f * largest_width && surface->h >= 4)
  {
    SDL_Surface* half =
        SDL_ScaleSurface(surface, surface->w / 2, surface->h / 2, SDL_SCALEMODE_LINEAR);
    if (!half)
      break;
    SDL_DestroySurface(surface);
    surface = half;
  }
}

// find_floor: for a background, also look for a floor that can slide.
// largest_width: the most pixels wide the picture is ever drawn (0: not
// known); a much bigger picture is shrunk first (ShrinkForDrawing).
inline LauncherImage LoadLauncherImage(SDL_Renderer* renderer, const fs::path& release_directory,
                                       std::string_view file_name, std::string_view log_label,
                                       bool find_floor = false, float largest_width = 0.0f)
{
  LauncherImage image;
  const fs::path path = release_directory / "Resources" / file_name;
  std::error_code ec;
  if (!fs::is_regular_file(path, ec))
    return image;
  SDL_Surface* surface = SDL_LoadPNG(path.string().c_str());
  if (!surface)
  {
    std::cout << log_label << ": can't load " << path.string() << ": " << SDL_GetError()
              << '\n';
    return image;
  }
  ShrinkForDrawing(surface, largest_width);
  if (surface->w > 0 && surface->h > 0)
  {
    image.texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (image.texture)
    {
      image.aspect = static_cast<float>(surface->h) / static_cast<float>(surface->w);
      std::cout << log_label << ": " << path.string() << '\n';
    }
  }
  if (image.texture && find_floor)
  {
    // The search reads plain RGBA bytes, whatever the PNG's own layout.
    if (SDL_Surface* pixels = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32))
    {
      if (SDL_LockSurface(pixels))
      {
        image.floor = FindScrollingFloor(static_cast<const std::uint8_t*>(pixels->pixels),
                                         pixels->w, pixels->h, pixels->pitch);
        SDL_UnlockSurface(pixels);
      }
      SDL_DestroySurface(pixels);
    }
    if (image.floor.found)
    {
      std::cout << log_label << ": the floor slides, rows " << image.floor.top << " to "
                << image.floor.bottom << '\n';
    }
    else
    {
      std::cout << log_label << ": no repeating floor, the picture stays still\n";
    }
  }
  SDL_DestroySurface(surface);
  return image;
}

// Seconds the floor of a background takes to slide one cell to the left.
inline constexpr double FLOOR_SECONDS_PER_CELL = 2.5;

// A background picture under a whole window (the launcher's, the Netplay
// lobby's), drawn straight with SDL right after the window is cleared and
// before the interface. It is scaled to cover the window: a window narrower
// than the picture crops its sides, a wider one its top, so the bottom of
// the picture always shows. When the picture has a floor (see
// moving_background.hpp), the floor slides left as seconds go by: drawn in
// bands a couple of pixels high, each shifted by its own share of a cell and
// wrapped around where the picture ends.
inline void RenderBackground(SDL_Renderer* renderer, const LauncherImage& background,
                             double seconds)
{
  float picture_width = 0.0f;
  float picture_height = 0.0f;
  int window_width = 0;
  int window_height = 0;
  if (!background.texture ||
      !SDL_GetTextureSize(background.texture, &picture_width, &picture_height) ||
      !SDL_GetCurrentRenderOutputSize(renderer, &window_width, &window_height) ||
      picture_width <= 0.0f || picture_height <= 0.0f || window_width <= 0 || window_height <= 0)
    return;
  const float picture_ratio = picture_width / picture_height;
  const float window_ratio = static_cast<float>(window_width) / static_cast<float>(window_height);
  SDL_FRect source{0.0f, 0.0f, picture_width, picture_height};
  if (window_ratio < picture_ratio)
  {
    source.w = picture_height * window_ratio;
    source.x = (picture_width - source.w) * 0.5f;
  }
  else
  {
    source.h = picture_width / window_ratio;
    source.y = picture_height - source.h;
  }
  const ScrollingFloor& floor = background.floor;
  if (!floor.found)
  {
    SDL_RenderTexture(renderer, background.texture, &source, nullptr);
    return;
  }

  // Window pixels from here on, whatever scale the interface draws at.
  float render_scale_x = 1.0f;
  float render_scale_y = 1.0f;
  SDL_GetRenderScale(renderer, &render_scale_x, &render_scale_y);
  SDL_SetRenderScale(renderer, 1.0f, 1.0f);
  const float width = static_cast<float>(window_width);
  const float height = static_cast<float>(window_height);
  const float zoom = height / source.h;  // window pixels per picture pixel
  // The window rows that show the floor, on whole pixels so the bands meet
  // without gaps or overlaps.
  const float first =
      std::clamp(std::floor((static_cast<float>(floor.top) - source.y) * zoom), 0.0f, height);
  const float last = std::clamp(
      std::ceil((static_cast<float>(floor.bottom) + 1.0f - source.y) * zoom), first, height);
  // Above and below the floor: the picture as it is.
  if (first > 0.0f)
  {
    const SDL_FRect above{source.x, source.y, source.w, first / zoom};
    const SDL_FRect window_above{0.0f, 0.0f, width, first};
    SDL_RenderTexture(renderer, background.texture, &above, &window_above);
  }
  if (last < height)
  {
    const SDL_FRect below{source.x, source.y + last / zoom, source.w, (height - last) / zoom};
    const SDL_FRect window_below{0.0f, last, width, height - last};
    SDL_RenderTexture(renderer, background.texture, &below, &window_below);
  }
  const double cells = seconds / FLOOR_SECONDS_PER_CELL;
  const float progress = static_cast<float>(cells - std::floor(cells));  // 0 to 1
  // Two window pixels, and at least one picture row (SDL's software
  // renderer drops the fraction of a row, and would draw nothing).
  const float band_height = std::max(2.0f, std::ceil(zoom));
  for (float y = first; y < last; y += band_height)
  {
    const float band = std::min(band_height, last - y);
    const float row = source.y + (y + band * 0.5f) / zoom;
    const float period = static_cast<float>(floor.Period(row));
    // The picture column at the window's left edge: further right as the
    // floor goes left.
    const float start = source.x + progress * period;
    const float rows = band / zoom;
    const float picture_row = source.y + y / zoom;
    // Window columns until the picture's right edge; past it, the row goes
    // on one repeat earlier in the picture, which looks just the same.
    const float until_edge = (picture_width - start) * zoom;
    if (until_edge >= width)
    {
      const SDL_FRect part{start, picture_row, source.w, rows};
      const SDL_FRect window_part{0.0f, y, width, band};
      SDL_RenderTexture(renderer, background.texture, &part, &window_part);
      continue;
    }
    const float split = std::floor(until_edge);
    if (split > 0.0f)
    {
      const SDL_FRect part{start, picture_row, split / zoom, rows};
      const SDL_FRect window_part{0.0f, y, split, band};
      SDL_RenderTexture(renderer, background.texture, &part, &window_part);
    }
    const SDL_FRect rest{start + split / zoom - period, picture_row, (width - split) / zoom, rows};
    const SDL_FRect window_rest{split, y, width - split, band};
    SDL_RenderTexture(renderer, background.texture, &rest, &window_rest);
  }
  SDL_SetRenderScale(renderer, render_scale_x, render_scale_y);
}


// The page's main action (PLAY): a grey panel like the others with a bold
// label in the accent colour, full width unless given one. Its border lights
// up in the accent while the pointer is on it.
inline bool DrawPrimaryButton(const char* label, ImFont* bold, float height, float width = -1.0f)
{
  const ImGuiStyle& style = ImGui::GetStyle();
  ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ChildBg]);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, style.Colors[ImGuiCol_FrameBgHovered]);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.19f, 0.06f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Text, LAUNCHER_ACCENT);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, style.ChildRounding);
  ImGui::PushFont(bold, style.FontSizeBase * 1.15f);
  const bool clicked = ImGui::Button(label, ImVec2(width, height));
  ImGui::PopFont();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(4);
  ImVec4 border = style.Colors[ImGuiCol_Border];
  if (ImGui::IsItemHovered() || ImGui::IsItemActive())
  {
    border = LAUNCHER_ACCENT;
    border.w = 0.7f;
  }
  ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                      ImGui::GetColorU32(border), style.ChildRounding, 0,
                                      std::max(1.0f, style.ChildBorderSize));
  return clicked;
}

// Title of the Settings, Controls and Game Files pages, with one line of help.
inline void DrawPageHeader(const char* title, const char* subtitle, ImFont* bold)
{
  ImGui::PushFont(bold, ImGui::GetStyle().FontSizeBase * 1.35f);
  ImGui::TextUnformatted(title);
  ImGui::PopFont();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled("%s", subtitle);
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
}

// A bordered panel that grows with its content, headed by a small caps label.
// Every BeginSection() needs a matching EndSection().
inline void BeginSection(const char* id, const char* heading)
{
  // A little less vertical padding than other panels, so that a page of
  // several sections (Settings) needs less scrolling.
  const ImVec2 padding = ImGui::GetStyle().WindowPadding;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding.x, padding.y * 0.75f));
  // Flattened: a gamepad's D-pad (or the keyboard's arrows) goes from a
  // control in one panel to one in the next without entering the panel first.
  ImGui::BeginChild(id, ImVec2(0.0f, 0.0f),
                    ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                        ImGuiChildFlags_NavFlattened);
  ImGui::PopStyleVar();
  ImGui::TextDisabled("%s", heading);
}

inline void EndSection()
{
  ImGui::EndChild();
  ImGui::Spacing();
}

// Two columns of sections side by side, so a page uses the launcher's width
// instead of growing down: the first column takes first_share of the width,
// the second the rest, with the usual gap between items. Sections inside take
// their column's width. BeginPageColumns() starts the first column,
// NextPageColumn() the second, and EndPageColumns() ends both (only when
// BeginPageColumns() returned true).
inline bool BeginPageColumns(const char* id, float first_share = 0.5f)
{
  // The cells' padding is read as the table and its row start, so it only
  // changes for them, not for tables inside the columns.
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                      ImVec2(ImGui::GetStyle().ItemSpacing.x * 0.5f, 0.0f));
  const bool open = ImGui::BeginTable(id, 2, ImGuiTableFlags_NoPadOuterX);
  if (open)
  {
    ImGui::TableSetupColumn("##first", ImGuiTableColumnFlags_WidthStretch, first_share);
    ImGui::TableSetupColumn("##second", ImGuiTableColumnFlags_WidthStretch, 1.0f - first_share);
    ImGui::TableNextColumn();
  }
  ImGui::PopStyleVar();
  return open;
}

inline void NextPageColumn()
{
  ImGui::TableNextColumn();
}

inline void EndPageColumns()
{
  ImGui::EndTable();
}

// One of a row of side-by-side choices, such as Netplay's Invite code / Direct
// IP: the chosen one dark green with a lime label and underline, the others
// quiet. Returns true when clicked.
inline bool DrawChoiceButton(const char* label, bool selected, ImVec2 size, ImFont* bold,
                             float scale)
{
  const ImGuiStyle& style = ImGui::GetStyle();
  if (selected)
  {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.19f, 0.06f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.25f, 0.08f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.30f, 0.09f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, LAUNCHER_ACCENT);
  }
  else
  {
    ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_FrameBg]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, style.Colors[ImGuiCol_FrameBgHovered]);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_FrameBgActive]);
    ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
  }
  ImGui::PushFont(bold, 0.0f);
  const bool clicked = ImGui::Button(label, size);
  ImGui::PopFont();
  ImGui::PopStyleColor(4);
  if (selected)
  {
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(min.x + 6.0f * scale, max.y - 3.0f * scale), ImVec2(max.x - 6.0f * scale, max.y),
        ImGui::GetColorU32(LAUNCHER_ACCENT), 2.0f * scale);
  }
  return clicked;
}

// Opens a folder in the system's file manager, making it first when it is
// missing (a Mods or ScreenShots folder nothing was put in yet).
inline void OpenFolder(const fs::path& folder)
{
  std::error_code ec;
  fs::create_directories(folder, ec);
  std::string url = "file://";
  const std::string text = folder.generic_string();
  if (!text.starts_with('/'))
    url += '/';
  SDL_OpenURL((url + text).c_str());
}

// A text button without a frame, such as "Host Netplay" under PLAY.
inline bool DrawLinkButton(const char* label)
{
  const ImGuiStyle& style = ImGui::GetStyle();
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.06f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.10f));
  // White, so the links stand out over the launcher's background picture.
  ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_Text]);
  const bool clicked = ImGui::Button(label);
  ImGui::PopStyleColor(4);
  return clicked;
}
}  // namespace moderngekko::frontend::ui
