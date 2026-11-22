// How the runner tells the launcher what it is doing while a game loads
// (tools/launcher_handoff.hpp): the progress file's lines, and the file that
// says the game can be seen.
#include "launcher_handoff.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
namespace mf = moderngekko::frontend;

int main()
{
  const fs::path folder =
      fs::temp_directory_path() /
      ("moderngekko-handoff-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(folder);
  const fs::path ready = folder / "game-shown";
  const fs::path progress = mf::LauncherProgressFile(ready);

  // Without --launcher-ready-file nothing is written.
  mf::TellLauncherFilesChecked(50);
  mf::TellLauncherWindowOpening();
  if (fs::exists(progress) || mf::ReadWindowOpening(progress) || mf::ReadFilesChecked(progress))
    return 1;

  mf::LauncherReadyFile() = ready;
  // The game files being checked: the percentage, and no window yet.
  mf::TellLauncherFilesChecked(42);
  if (mf::ReadFilesChecked(progress) != 42 || mf::ReadWindowOpening(progress))
    return 2;
  mf::TellLauncherFilesChecked(150);
  if (mf::ReadFilesChecked(progress) != 100)
    return 3;
  // The window opening: the launcher may go over it; no percentage now.
  mf::TellLauncherWindowOpening();
  if (!mf::ReadWindowOpening(progress) || mf::ReadFilesChecked(progress))
    return 4;
  // Nothing half written is left beside it.
  fs::path temporary = progress;
  temporary += ".tmp";
  if (fs::exists(temporary))
    return 5;
  // The game shown: the ready file appears.
  if (fs::exists(ready))
    return 6;
  mf::TellLauncherGameShown();
  if (!fs::exists(ready))
    return 7;

  fs::remove_all(folder);
  std::cout << "launcher handoff: ok\n";
  return 0;
}
