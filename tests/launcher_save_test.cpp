// The complete save (tools/launcher_save.hpp): the Wii save folder of a disc
// ID, the build's files without a banner.bin, nothing done before the game
// made its save, the player's save copied first and brought back as it was.
#include "launcher_save.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
namespace fs = std::filesystem;
using namespace moderngekko::frontend;

void Write(const fs::path& path, const std::string& text)
{
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

std::string Read(const fs::path& path)
{
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::size_t Count(const fs::path& folder)
{
  std::error_code ec;
  if (!fs::exists(folder, ec))
    return 0;
  return static_cast<std::size_t>(std::distance(fs::directory_iterator(folder),
                                                fs::directory_iterator()));
}
}  // namespace

int main()
{
  const fs::path root = fs::temp_directory_path() / "moderngekko_launcher_save_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  const fs::path user = root / "user";
  if (WiiSaveFolder(user, "RS9E8P") != user / "Wii/title/00010000/52533945/data" ||
      WiiSaveFolder(user, "SOUE01") != user / "Wii/title/00010000/534f5545/data" ||
      SaveBackupsFolder(user, "RS9E8P") != user / "SaveBackups/RS9E8P")
    return 109;

  const fs::path complete = root / "complete-save";
  const fs::path save = WiiSaveFolder(user, "RS9E8P");
  const fs::path backups = SaveBackupsFolder(user, "RS9E8P");
  Write(complete / "0_Games.sav", "everything unlocked");
  Write(complete / "banner.bin", "not shipped");
  Write(complete / "extra.dat", "only in the complete save");
  Write(complete / ".gitkeep", "");
  const auto files = CompleteSaveFiles(complete);
  if (files.size() != 2 || files[0].filename() != "0_Games.sav" ||
      files[1].filename() != "extra.dat")
    return 110;

  // Before the game made its save: nothing happens.
  if (InstallCompleteSave(complete, save, backups, "2026-11-07_09-30-00").error !=
          SaveError::NoGameSave ||
      Count(backups) != 0 || HasGameSave(save) || LatestSaveBackup(backups))
    return 111;
  if (InstallCompleteSave(root / "nothing", save, backups, "x").error !=
      SaveError::NoCompleteSave)
    return 112;

  // The player's save: copied, then the complete one over it, its banner kept.
  Write(save / "0_Games.sav", "my progress");
  Write(save / "banner.bin", "the game's own icon");
  const SaveResult installed = InstallCompleteSave(complete, save, backups, "2026-11-07_09-30-00");
  if (!installed || installed.backup != backups / "2026-11-07_09-30-00")
    return 113;
  if (Read(save / "0_Games.sav") != "everything unlocked" ||
      Read(save / "banner.bin") != "the game's own icon" ||
      Read(save / "extra.dat") != "only in the complete save" || Count(save) != 3)
    return 114;
  if (Read(installed.backup / "0_Games.sav") != "my progress" ||
      Read(installed.backup / "banner.bin") != "the game's own icon" ||
      Count(installed.backup) != 2)
    return 115;

  // Again in the same second: a second copy, numbered; the newest is found.
  const SaveResult again = InstallCompleteSave(complete, save, backups, "2026-11-07_09-30-00");
  if (!again || again.backup != backups / "2026-11-07_09-30-00_2" ||
      LatestSaveBackup(backups) != again.backup)
    return 116;
  // Restored: the save as it was then, and that copy gone.
  if (!RestoreSaveBackup(again.backup, save) || fs::exists(again.backup) ||
      Read(save / "0_Games.sav") != "everything unlocked" ||
      LatestSaveBackup(backups) != installed.backup)
    return 117;
  // The first copy: the player's own progress, without the complete save's
  // extra file.
  if (!RestoreSaveBackup(installed.backup, save) || Read(save / "0_Games.sav") != "my progress" ||
      fs::exists(save / "extra.dat") || Read(save / "banner.bin") != "the game's own icon" ||
      Count(save) != 2 || LatestSaveBackup(backups))
    return 118;
  if (RestoreSaveBackup(backups / "missing", save).error != SaveError::NoBackup)
    return 119;
  // No temporary files left behind.
  for (const auto& entry : fs::recursive_directory_iterator(root))
  {
    if (entry.path().extension() == ".part")
      return 120;
  }
  fs::remove_all(root, ec);
  std::puts("launcher save: ok");
  return 0;
}
