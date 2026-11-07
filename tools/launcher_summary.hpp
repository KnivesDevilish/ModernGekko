#pragma once

// What the PLAY page summarises: the time played and the installed mods. Both
// are read from the files the runtime itself uses, so the launcher never keeps
// its own copy of either.

#include "ui_text.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace moderngekko::frontend
{
namespace fs = std::filesystem;

namespace summary_detail
{
inline std::string_view Trim(std::string_view text)
{
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos)
    return {};
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

// Same suffix the runtime's ModManager looks for.
inline std::string_view ModLibrarySuffix()
{
#if defined(_WIN32)
  return ".dll";
#elif defined(__APPLE__)
  return ".dylib";
#else
  return ".so";
#endif
}
}  // namespace summary_detail

// Milliseconds Dolphin has counted for game_id. Dolphin's play time tracker
// (Core/TimePlayed.cpp) adds to Config/TimePlayed.ini every 30 seconds while
// the game runs, under [TimePlayed] as "<game id> = 0x<16 hex digits>". A
// disc ID such as RS9E8P is written as is. 0 when there is no entry yet.
inline std::uint64_t ReadTimePlayed(const fs::path& user_directory, std::string_view game_id)
{
  std::ifstream file(user_directory / "Config" / "TimePlayed.ini");
  std::string line;
  bool in_section = false;
  while (std::getline(file, line))
  {
    const std::string_view text = summary_detail::Trim(line);
    if (text.starts_with('['))
    {
      in_section = text == "[TimePlayed]";
      continue;
    }
    const auto separator = text.find('=');
    if (!in_section || separator == std::string_view::npos)
      continue;
    if (summary_detail::Trim(text.substr(0, separator)) != game_id)
      continue;
    // Dolphin writes a u64 as "0x" and 16 hex digits (Common::ValueToString)
    // and reads it back with strtoull in base 0 (Common::TryParse), which also
    // takes plain decimal. Read it the same way.
    const std::string value(summary_detail::Trim(text.substr(separator + 1)));
    if (value.empty() || value.front() == '-' || value.front() == '+')
      return 0;
    errno = 0;
    char* end = nullptr;
    const unsigned long long milliseconds = std::strtoull(value.c_str(), &end, 0);
    if (end == nullptr || *end != '\0' || errno == ERANGE)
      return 0;
    return static_cast<std::uint64_t>(milliseconds);
  }
  return 0;
}

// "12 h 34 min", "34 min", "Under a minute", or "Not played yet" for 0. The
// two sentences are English; the launcher translates them when it shows them.
inline std::string FormatTimePlayed(std::uint64_t milliseconds)
{
  if (milliseconds == 0)
    return MODERNGEKKO_UI_TEXT("Not played yet");
  const std::uint64_t minutes = milliseconds / 60000;
  if (minutes == 0)
    return MODERNGEKKO_UI_TEXT("Under a minute");
  const std::uint64_t hours = minutes / 60;
  if (hours == 0)
    return std::to_string(minutes) + " min";
  return std::to_string(hours) + " h " + std::to_string(minutes % 60) + " min";
}

// Mod packages the runtime will try to load from these folders, by the same
// rules as ModManager::LoadDirectories: "<name>.mgm" folders that hold the
// platform's mod library, and "<name>.mgm.so" (.dll, .dylib) files. A folder
// listed twice, or the same package reached through both, is listed once.
// Sorted by path.
inline std::vector<fs::path> ListInstalledMods(const std::vector<fs::path>& directories)
{
  const std::string_view suffix = summary_detail::ModLibrarySuffix();
  const std::string packaged_suffix = ".mgm" + std::string(suffix);
  std::vector<fs::path> packages;
  for (const fs::path& directory : directories)
  {
    std::error_code ec;
    if (!fs::is_directory(directory, ec))
      continue;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
    {
      const fs::path& path = it->path();
      std::error_code entry_ec;
      const bool package_folder =
          it->is_directory(entry_ec) && path.extension() == ".mgm" &&
          fs::is_regular_file(path / ("mod" + std::string(suffix)), entry_ec);
      const bool package_file =
          it->is_regular_file(entry_ec) && path.filename().string().ends_with(packaged_suffix);
      if (!package_folder && !package_file)
        continue;
      std::error_code canonical_ec;
      const fs::path canonical = fs::weakly_canonical(path, canonical_ec);
      packages.push_back(canonical_ec ? path : canonical);
    }
  }
  std::sort(packages.begin(), packages.end());
  packages.erase(std::unique(packages.begin(), packages.end()), packages.end());
  return packages;
}

inline int CountInstalledMods(const std::vector<fs::path>& directories)
{
  return static_cast<int>(ListInstalledMods(directories).size());
}

// A package's name as the Mods & expansions page shows it: "speed" for
// speed.mgm and for speed.mgm.so (.dll, .dylib).
inline std::string ModPackageName(const fs::path& package)
{
  std::string name = package.filename().string();
  const std::string packaged_suffix = ".mgm" + std::string(summary_detail::ModLibrarySuffix());
  if (name.ends_with(packaged_suffix))
    name.resize(name.size() - packaged_suffix.size());
  else if (name.ends_with(".mgm"))
    name.resize(name.size() - 4);
  return name;
}
}  // namespace moderngekko::frontend
