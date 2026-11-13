#pragma once

// The launcher's accent colour and the files of its interface font, apart from
// launcher_ui.hpp (which needs SDL) so the in-game menu
// (src/runtime/game_menu.cpp) looks the same as the launcher.

#include "launcher_cjk_font.inc"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

namespace moderngekko::frontend::ui
{
namespace fs = std::filesystem;

// SR-Source accent: a neon lime green. Shared by the theme and the few texts that
// pick a colour explicitly (the Play header and the READY status).
inline constexpr ImVec4 LAUNCHER_ACCENT(0.72f, 1.0f, 0.18f, 1.0f);

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

struct InterfaceFontFiles
{
  fs::path regular;
  fs::path bold;  // empty when the family has no bold face here
};

// A clean sans-serif for the interface: the system's usual UI fonts first
// (Windows, Fedora/Bazzite and its Distrobox mount, Debian/Ubuntu, Arch), then
// the font Dolphin ships in Sys/. Only the ones present and readable, best
// first; the bold face is from the same family as the regular one, so the two
// match. Empty when there is none: ImGui's built-in bitmap font is left.
inline std::vector<InterfaceFontFiles> InterfaceFontCandidates(const fs::path& release_directory)
{
  std::vector<InterfaceFontFiles> candidates;
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
  std::vector<InterfaceFontFiles> found;
  for (InterfaceFontFiles& candidate : candidates)
  {
    if (!usable(candidate.regular))
      continue;
    if (!usable(candidate.bold))
      candidate.bold.clear();
    found.push_back(std::move(candidate));
  }
  return found;
}

// The interface fonts have no Japanese characters, so a small embedded Noto
// Sans CJK JP (tools/launcher_cjk_font.inc) is merged into the font added
// last: its Regular or its Bold weight, at size (0 lets ImGui pick it, for a
// font drawn at several sizes).
inline void MergeJapaneseFont(bool bold, float size)
{
  ImFontConfig config;
  config.MergeMode = true;
  config.FontDataOwnedByAtlas = false;
  const unsigned char* data = bold ? LAUNCHER_CJK_BOLD : LAUNCHER_CJK_REGULAR;
  const std::size_t bytes = bold ? sizeof(LAUNCHER_CJK_BOLD) : sizeof(LAUNCHER_CJK_REGULAR);
  ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data),
                                             static_cast<int>(bytes), size, &config);
}
}  // namespace moderngekko::frontend::ui
