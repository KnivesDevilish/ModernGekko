#pragma once

// How the runner tells the launcher that the game can be seen: the launcher
// shows its loading screen until then, and steps aside after. The launcher
// names a file (--launcher-ready-file) and the runner creates it, once: when
// its first window (the game's, or the Netplay lobby's) has its first
// picture, or before a message box the player must see, on Windows. Without
// the option nothing is written.
//
// Meanwhile the runner can say what it is doing, for the loading screen to
// show: the Netplay lobby's first check of the game files can take minutes.
// That goes in a second file beside the first (LauncherProgressFile), one
// line "files <percent>", rewritten as the percentage grows.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>

namespace moderngekko::frontend
{
// Set once, from the command line, before any thread starts.
inline std::filesystem::path& LauncherReadyFile()
{
  static std::filesystem::path file;
  return file;
}

// The game can be seen: the file appears. Only the first call writes it; any
// thread may call (the game's first picture comes from its video thread).
inline void TellLauncherGameShown()
{
  static std::once_flag once;
  if (LauncherReadyFile().empty())
    return;
  std::call_once(once, [] { std::ofstream(LauncherReadyFile()) << "shown\n"; });
}

// Where the runner says what it is doing: "<ready file>.progress".
inline std::filesystem::path LauncherProgressFile(const std::filesystem::path& ready_file)
{
  if (ready_file.empty())
    return {};
  std::filesystem::path progress = ready_file;
  progress += ".progress";
  return progress;
}

// The game files are being checked: percent of them read. Written through a
// temporary file, so the launcher never reads half a line.
inline void TellLauncherFilesChecked(int percent)
{
  const std::filesystem::path file = LauncherProgressFile(LauncherReadyFile());
  if (file.empty())
    return;
  std::filesystem::path temporary = file;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::trunc);
    output << "files " << std::clamp(percent, 0, 100) << '\n';
    if (!output)
      return;
  }
  std::error_code ignored;
  std::filesystem::rename(temporary, file, ignored);
}

// For the launcher: the percentage of the game files checked so far, if the
// runner is checking them.
inline std::optional<int> ReadFilesChecked(const std::filesystem::path& progress_file)
{
  std::ifstream input(progress_file);
  std::string word;
  int percent = -1;
  if (!(input >> word >> percent) || word != "files" || percent < 0 || percent > 100)
    return std::nullopt;
  return percent;
}
}  // namespace moderngekko::frontend
