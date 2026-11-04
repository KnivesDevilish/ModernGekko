// The PLAY page summary: time played read from Dolphin's TimePlayed.ini and the
// mod packages the runtime would load.
#include "launcher_summary.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace
{
#if defined(_WIN32)
constexpr const char* MOD_LIBRARY = "mod.dll";
constexpr const char* PACKAGED_MOD = "story.mgm.dll";
#elif defined(__APPLE__)
constexpr const char* MOD_LIBRARY = "mod.dylib";
constexpr const char* PACKAGED_MOD = "story.mgm.dylib";
#else
constexpr const char* MOD_LIBRARY = "mod.so";
constexpr const char* PACKAGED_MOD = "story.mgm.so";
#endif

void WriteFile(const fs::path& path, const std::string& text)
{
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::trunc) << text;
}
}  // namespace

int main()
{
  using namespace moderngekko::frontend;
  // Unique per run, like the other launcher tests.
  const fs::path root =
      fs::temp_directory_path() /
      ("moderngekko-launcher-summary-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

  // No file yet: nothing played.
  if (ReadTimePlayed(root, "RS9E8P") != 0)
    return 1;

  // The layout Dolphin's IniFile writes, with another game and another section.
  WriteFile(root / "Config" / "TimePlayed.ini",
            "[Other]\nRS9E8P = 5\n"
            "[TimePlayed]\nGALE01 = 1000\nRS9E8P = 45240000\n");
  if (ReadTimePlayed(root, "RS9E8P") != 45240000 || ReadTimePlayed(root, "GALE01") != 1000 ||
      ReadTimePlayed(root, "RSBE01") != 0)
    return 2;

  // A damaged value reads as nothing played instead of failing.
  WriteFile(root / "Config" / "TimePlayed.ini", "[TimePlayed]\nRS9E8P = lots\n");
  if (ReadTimePlayed(root, "RS9E8P") != 0)
    return 3;

  if (FormatTimePlayed(0) != "Not played yet" || FormatTimePlayed(59999) != "Under a minute" ||
      FormatTimePlayed(34 * 60000) != "34 min" ||
      FormatTimePlayed(45240000) != "12 h 34 min" || FormatTimePlayed(3600000) != "1 h 0 min")
    return 4;

  // Mods: an .mgm folder with the platform library, an .mgm.<suffix> file,
  // and things the runtime would not load.
  const fs::path executable_mods = root / "release" / "Mods";
  const fs::path user_mods = root / "user" / "Mods";
  if (CountInstalledMods({executable_mods, user_mods}) != 0)
    return 5;
  WriteFile(executable_mods / "speed.mgm" / MOD_LIBRARY, "");
  WriteFile(user_mods / PACKAGED_MOD, "");
  WriteFile(user_mods / "empty.mgm" / "readme.txt", "");  // no library inside
  WriteFile(user_mods / "notes.txt", "");
  if (CountInstalledMods({executable_mods, user_mods}) != 2)
    return 6;
  // The same folder twice (for example when both paths lead to one place).
  if (CountInstalledMods({user_mods, user_mods / ".." / "Mods"}) != 1)
    return 7;

  std::error_code ec;
  fs::remove_all(root, ec);
  return 0;
}
