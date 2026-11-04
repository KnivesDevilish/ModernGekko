#pragma once

// The look shared by the launcher and the Netplay lobby: the theme, the
// interface font (with the embedded Japanese characters), status pills,
// bordered panels, buttons and images from Resources/. Moved here from
// moderngekko_launcher.cpp so both windows draw with the same code.

#include "launcher_cjk_font.inc"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace moderngekko::frontend::ui
{
namespace fs = std::filesystem;

// SR-ZGS accent: a neon lime green. Shared by the theme and the few texts that
// pick a colour explicitly (the Play header and the READY status).
inline constexpr ImVec4 LAUNCHER_ACCENT(0.72f, 1.0f, 0.18f, 1.0f);

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

// stb_truetype (used by ImGui) reads TrueType outlines only, and a file it cannot
// parse would trip an assert. Check the header before handing a file over.
inline bool IsTrueTypeFont(const fs::path& path)
{
  std::ifstream file(path, std::ios::binary);
  std::array<unsigned char, 4> tag{};
  if (!file.read(reinterpret_cast<char*>(tag.data()), tag.size()))
    return false;
  constexpr std::array<unsigned char, 4> version_1 = {0x00, 0x01, 0x00, 0x00};
  constexpr std::array<unsigned char, 4> apple_true = {'t', 'r', 'u', 'e'};
  return tag == version_1 || tag == apple_true;
}

struct LauncherFonts
{
  ImFont* regular = nullptr;
  ImFont* bold = nullptr;  // the regular font when no bold face of its family exists
};

// A clean sans-serif for the interface: the system's usual UI fonts first
// (Windows, Fedora/Bazzite and its Distrobox mount, Debian/Ubuntu, Arch), then
// the font Dolphin ships in Sys/, then ImGui's built-in bitmap font. The bold
// face is taken from the same family as the regular one so the two match.
inline LauncherFonts LoadInterfaceFont(const fs::path& release_directory, float size)
{
  struct Candidate
  {
    fs::path regular;
    fs::path bold;
  };
  std::vector<Candidate> candidates;
#if defined(_WIN32)
  if (const char* windows = std::getenv("WINDIR"))
  {
    candidates.push_back({fs::path(windows) / "Fonts" / "segoeui.ttf",
                          fs::path(windows) / "Fonts" / "segoeuib.ttf"});
  }
#endif
  for (const char* directory : {"/usr/share/fonts/google-noto", "/usr/local/share/fonts/google-noto",
                                "/usr/share/fonts/noto", "/usr/share/fonts/truetype/noto"})
  {
    candidates.push_back({fs::path(directory) / "NotoSans-Regular.ttf",
                          fs::path(directory) / "NotoSans-Bold.ttf"});
  }
  for (const char* directory : {"/usr/share/fonts/liberation-sans-fonts",
                                "/usr/local/share/fonts/liberation-sans-fonts",
                                "/usr/share/fonts/truetype/liberation"})
  {
    candidates.push_back({fs::path(directory) / "LiberationSans-Regular.ttf",
                          fs::path(directory) / "LiberationSans-Bold.ttf"});
  }
  for (const char* directory : {"/usr/share/fonts/TTF", "/usr/share/fonts/truetype/dejavu",
                                "/usr/share/fonts/dejavu-sans-fonts"})
  {
    candidates.push_back(
        {fs::path(directory) / "DejaVuSans.ttf", fs::path(directory) / "DejaVuSans-Bold.ttf"});
  }
  candidates.push_back({release_directory / "Sys" / "Resources" / "OSD_Font.ttf", {}});

  const auto usable = [](const fs::path& path) {
    std::error_code ec;
    return !path.empty() && fs::is_regular_file(path, ec) && IsTrueTypeFont(path);
  };
  ImGuiIO& io = ImGui::GetIO();
  // The interface fonts have no Japanese characters (the Credits page uses a
  // few), so a small embedded Noto Sans CJK JP is merged into each face.
  const auto merge_japanese = [&](const unsigned char* data, std::size_t bytes)
  {
    ImFontConfig config;
    config.MergeMode = true;
    config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), static_cast<int>(bytes),
                                   size, &config);
  };
  for (const Candidate& candidate : candidates)
  {
    if (!usable(candidate.regular))
      continue;
    LauncherFonts fonts;
    fonts.regular = io.Fonts->AddFontFromFileTTF(candidate.regular.string().c_str(), size);
    if (!fonts.regular)
      continue;
    merge_japanese(LAUNCHER_CJK_REGULAR, sizeof(LAUNCHER_CJK_REGULAR));
    ImGui::GetStyle().FontSizeBase = size;
    if (usable(candidate.bold))
      fonts.bold = io.Fonts->AddFontFromFileTTF(candidate.bold.string().c_str(), size);
    if (fonts.bold)
      merge_japanese(LAUNCHER_CJK_BOLD, sizeof(LAUNCHER_CJK_BOLD));
    std::cout << "launcher font: " << candidate.regular.string() << '\n';
    if (fonts.bold)
      std::cout << "launcher bold font: " << candidate.bold.string() << '\n';
    else
      fonts.bold = fonts.regular;
    return fonts;
  }
  LauncherFonts fonts;
  fonts.regular = io.Fonts->AddFontDefault();
  merge_japanese(LAUNCHER_CJK_REGULAR, sizeof(LAUNCHER_CJK_REGULAR));
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
};
inline LauncherImage LoadLauncherImage(SDL_Renderer* renderer, const fs::path& release_directory,
                                std::string_view file_name, std::string_view log_label)
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
  if (surface->w > 0 && surface->h > 0)
  {
    image.texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (image.texture)
    {
      image.aspect = static_cast<float>(surface->h) / static_cast<float>(surface->w);
      std::cout << log_label << ": " << path.string() << '\n';
    }
  }
  SDL_DestroySurface(surface);
  return image;
}


// The page's main action: full width, accent colour, dark bold label.
inline bool DrawPrimaryButton(const char* label, ImFont* bold, float height)
{
  ImGui::PushStyleColor(ImGuiCol_Button, LAUNCHER_ACCENT);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 1.0f, 0.40f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.62f, 0.90f, 0.10f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.04f, 0.05f, 0.02f, 1.0f));
  ImGui::PushFont(bold, ImGui::GetStyle().FontSizeBase * 1.2f);
  const bool clicked = ImGui::Button(label, ImVec2(-1.0f, height));
  ImGui::PopFont();
  ImGui::PopStyleColor(4);
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
  // A little less vertical padding than other panels, so that a page with
  // three sections (Settings) still fits the default window without scrolling.
  const ImVec2 padding = ImGui::GetStyle().WindowPadding;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding.x, padding.y * 0.75f));
  ImGui::BeginChild(id, ImVec2(0.0f, 0.0f),
                    ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
  ImGui::PopStyleVar();
  ImGui::TextDisabled("%s", heading);
}

inline void EndSection()
{
  ImGui::EndChild();
  ImGui::Spacing();
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

// A quiet text button, such as "Host Netplay" under PLAY.
inline bool DrawLinkButton(const char* label)
{
  const ImGuiStyle& style = ImGui::GetStyle();
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.06f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.10f));
  ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
  const bool clicked = ImGui::Button(label);
  ImGui::PopStyleColor(4);
  return clicked;
}
}  // namespace moderngekko::frontend::ui
