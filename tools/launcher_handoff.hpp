#pragma once

// How the runner tells the launcher that the game can be seen: the launcher
// shows its loading screen until then, and steps aside after. The launcher
// names a file (--launcher-ready-file) and the runner creates it, once: when
// its first window (the game's, or the Netplay lobby's) has its first
// picture, or before a message box the player must see, on Windows. Without
// the option nothing is written.

#include <filesystem>
#include <fstream>
#include <mutex>

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
}  // namespace moderngekko::frontend
