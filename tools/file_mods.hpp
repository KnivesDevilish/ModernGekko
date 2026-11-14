#pragma once

// File mods: folders that replace or add the game's files when it starts,
// without writing anything to the extracted game. Each is a folder in a Mods
// folder (the user directory's, or the program's own) with:
//
//   mod.ini   what the mod is:
//               name=Spanish voices        (shown by the launcher)
//               game=RS9E8P                (the disc ID it is for; required)
//               version=1.0
//               author=Someone
//               description=One line saying what it changes.
//   files/    laid out as the game's own files/: each file in it takes the
//             place of the game's file at the same path, or is added.
//   textures/ custom textures (src/runtime/texture_mods.hpp): they change
//             what is drawn, not the game's files.
//   patches.ini  data patches (src/runtime/data_patches.hpp): values the
//             mod changes inside the game's files.
// A mod has at least one of files/, textures/ and patches.ini.
//
// config.ini names the enabled ones, by folder name, one "mod=" line each,
// in the order they apply: a later mod's file wins over an earlier one's.
// The runner gives the runtime the enabled ones made for the game it starts
// (RuntimeConfig::file_mods), which patches the disc as it boots and loads
// their textures.
//
// Apart from the standard library, so tests/file_mods_test.cpp can check it.

#include "ui_text.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace moderngekko::frontend
{
struct FileMod
{
  std::filesystem::path folder;
  std::string id;    // the folder's name, as config.ini's mod= lines name it
  std::string name;  // mod.ini's name, or the id
  std::string game;  // the disc ID it is for, such as RS9E8P
  std::string version;
  std::string author;
  std::string description;
  bool has_files = false;     // a files/ folder
  bool has_textures = false;  // a textures/ folder
  bool has_patches = false;   // a patches.ini
};

// A folder that looks like a file mod (it has a mod.ini) but can't be used,
// and why: one of the texts below, in English, which the launcher
// translates.
struct FileModProblem
{
  std::string folder;
  const char* why = "";
};

inline constexpr const char* FILE_MOD_NO_INI = MODERNGEKKO_UI_TEXT("it has no mod.ini");
inline constexpr const char* FILE_MOD_NO_GAME =
    MODERNGEKKO_UI_TEXT("its mod.ini doesn't say which game it is for (game=RS9E8P, for example)");
inline constexpr const char* FILE_MOD_NO_FILES =
    MODERNGEKKO_UI_TEXT("it has no files folder, textures folder or patches.ini");

namespace file_mods_detail
{
inline std::string Trim(std::string_view text)
{
  const auto space = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!text.empty() && space(static_cast<unsigned char>(text.front())))
    text.remove_prefix(1);
  while (!text.empty() && space(static_cast<unsigned char>(text.back())))
    text.remove_suffix(1);
  return std::string(text);
}

// Six letters or digits, as a disc ID is.
inline bool IsDiscId(std::string_view id)
{
  return id.size() == 6 && std::ranges::all_of(id, [](char c) {
           return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
         });
}

// A path's last part as UTF-8 (the folder names config.ini keeps).
inline std::string NameOf(const std::filesystem::path& path)
{
  const std::u8string name = path.filename().u8string();
  return std::string(name.begin(), name.end());
}

inline std::string Upper(std::string text)
{
  for (char& c : text)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return text;
}
}  // namespace file_mods_detail

// The mod in `folder`; nothing, with why in *problem (one of the
// FILE_MOD_ texts), when its mod.ini is missing, has no valid game, or there
// is no files/ folder, textures/ folder or patches.ini.
inline std::optional<FileMod> ReadFileMod(const std::filesystem::path& folder,
                                          const char** problem = nullptr)
{
  namespace fs = std::filesystem;
  using namespace file_mods_detail;
  const auto fail = [&](const char* why) -> std::optional<FileMod> {
    if (problem)
      *problem = why;
    return std::nullopt;
  };
  FileMod mod;
  mod.folder = folder;
  mod.id = file_mods_detail::NameOf(folder);
  std::ifstream file(folder / "mod.ini");
  if (!file)
    return fail(FILE_MOD_NO_INI);
  std::string line;
  while (std::getline(file, line))
  {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';' || trimmed[0] == '[')
      continue;
    const std::size_t separator = trimmed.find('=');
    if (separator == std::string::npos)
      continue;
    std::string key = Trim(std::string_view(trimmed).substr(0, separator));
    for (char& c : key)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string value = Trim(std::string_view(trimmed).substr(separator + 1));
    if (key == "name")
      mod.name = std::move(value);
    else if (key == "game")
      mod.game = Upper(std::move(value));
    else if (key == "version")
      mod.version = std::move(value);
    else if (key == "author")
      mod.author = std::move(value);
    else if (key == "description")
      mod.description = std::move(value);
  }
  if (!IsDiscId(mod.game))
    return fail(FILE_MOD_NO_GAME);
  std::error_code ec;
  mod.has_files = fs::is_directory(folder / "files", ec);
  mod.has_textures = fs::is_directory(folder / "textures", ec);
  mod.has_patches = fs::is_regular_file(folder / "patches.ini", ec);
  if (!mod.has_files && !mod.has_textures && !mod.has_patches)
    return fail(FILE_MOD_NO_FILES);
  if (mod.name.empty())
    mod.name = mod.id;
  return mod;
}

// Every file mod in these folders. A mod in a later folder takes the place
// of one with the same folder name in an earlier folder. Folders that aren't
// file mods are left out (code mods, *.mgm, have their own rules); ones that
// look like file mods (they have a mod.ini) but can't be read are named in
// *problems, with why. Sorted by name, capitals or not.
inline std::vector<FileMod> ListFileMods(const std::vector<std::filesystem::path>& directories,
                                         std::vector<FileModProblem>* problems = nullptr)
{
  namespace fs = std::filesystem;
  std::vector<FileMod> mods;
  for (const fs::path& directory : directories)
  {
    std::error_code ec;
    if (!fs::is_directory(directory, ec))
      continue;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
    {
      std::error_code entry_ec;
      if (!it->is_directory(entry_ec) || it->path().extension() == ".mgm" ||
          !fs::exists(it->path() / "mod.ini", entry_ec))
        continue;
      const char* problem = "";
      auto mod = ReadFileMod(it->path(), &problem);
      if (!mod)
      {
        if (problems)
          problems->push_back({file_mods_detail::NameOf(it->path()), problem});
        continue;
      }
      std::erase_if(mods, [&](const FileMod& other) { return other.id == mod->id; });
      mods.push_back(std::move(*mod));
    }
  }
  // By name, as a person reads it: capitals don't go first.
  const auto lower = [](std::string text) {
    for (char& c : text)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
  };
  std::ranges::sort(mods, [&](const FileMod& a, const FileMod& b) {
    const std::string a_name = lower(a.name);
    const std::string b_name = lower(b.name);
    return a_name != b_name ? a_name < b_name : a.id < b.id;
  });
  return mods;
}

// The enabled mods (config.ini's mod= lines, by folder name) that are made
// for `disc_id`, in the order they are enabled in. An enabled name with no
// mod is skipped.
inline std::vector<FileMod> EnabledFileMods(const std::vector<FileMod>& mods,
                                            const std::vector<std::string>& enabled,
                                            std::string_view disc_id)
{
  std::vector<FileMod> chosen;
  for (const std::string& id : enabled)
  {
    const auto mod = std::ranges::find(mods, id, &FileMod::id);
    if (mod != mods.end() && mod->game == disc_id &&
        std::ranges::find(chosen, id, &FileMod::id) == chosen.end())
      chosen.push_back(*mod);
  }
  return chosen;
}

// The extracted game's disc ID: the first six characters of sys/boot.bin.
// Empty when it can't be read.
inline std::string ReadDiscId(const std::filesystem::path& game_root)
{
  std::ifstream file(game_root / "sys" / "boot.bin", std::ios::binary);
  std::string id(6, '\0');
  if (!file.read(id.data(), static_cast<std::streamsize>(id.size())) ||
      !file_mods_detail::IsDiscId(id))
    return {};
  return id;
}
}  // namespace moderngekko::frontend
