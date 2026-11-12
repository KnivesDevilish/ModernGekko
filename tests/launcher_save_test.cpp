// The complete save (tools/launcher_save.hpp): the Wii save folder of a disc
// ID, the build's files without a banner.bin, nothing done before the game
// made its save, the player's save copied first and brought back as it was;
// and a GameCube game's: its memory card folder, the picture and icon bytes
// of a .gci file, the player's own put into the complete one.
#include "launcher_save.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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

// A one-block .gci: a CI8 banner and one CI8 icon frame with the shared
// palette (Sonic Riders'), the picture and icon filled with `picture` and
// the rest with `data`.
std::string Gci(char picture, char data)
{
  std::string gci(0x40 + 0x2000, data);
  const std::string entry = std::string("GXEE8P") + '\xFF' + '\x01' + "SONICRIDERS_GAMEDATA_01";
  gci.replace(0, entry.size(), entry);
  gci.replace(0x28, 0x18, 0x18, '\0');
  gci[0x31] = 0x01;  // icon frame 0: CI8, shared palette
  gci[0x33] = 0x03;  // its delay; frame 1 has none
  gci[0x39] = 0x01;  // one block
  gci[0x3E] = 0x14;  // comments at 0x1400
  gci.replace(0x40, 0x1400, 0x1400, picture);
  return gci;
}

std::vector<unsigned char> Bytes(const std::string& text)
{
  return {text.begin(), text.end()};
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
  // GameCube: the memory card folder of the disc's region.
  if (GameCubeCardFolder(user, "GXEE8P") != user / "GC/USA/Card A" ||
      GameCubeCardFolder(user, "GXEP8P") != user / "GC/EUR/Card A" ||
      GameCubeCardFolder(user, "GXEJ8P") != user / "GC/JPN/Card A" || !IsGameCubeDisc("GXEE8P") ||
      IsGameCubeDisc("RS9E8P"))
    return 121;
  // The picture and icon: the banner (96x32 CI8 and its palette), the icon
  // frame (32x32) and the shared palette, after the directory entry.
  const auto range = GciImageBytes(Bytes(Gci('P', 'D')));
  if (!range || range->first != 0x40 || range->count != 0x1400)
    return 122;
  std::string no_images = Gci('P', 'D');
  no_images.replace(0x2C, 4, 4, '\xFF');
  const auto none = GciImageBytes(Bytes(no_images));
  if (!none || none->count != 0 || GciImageBytes(Bytes(Gci('P', 'D').substr(0, 0x500))) ||
      GciImageBytes({}))
    return 123;

  // The complete save comes without its picture; the player's card has the
  // game's own save and another game's.
  const fs::path gc_complete = complete / "GXEE8P";
  const fs::path card = GameCubeCardFolder(user, "GXEE8P");
  const fs::path gc_backups = SaveBackupsFolder(user, "GXEE8P");
  const std::string name = "8P-GXEE-SONICRIDERS_GAMEDATA_01.gci";
  Write(gc_complete / name, Gci('\0', 'C'));
  Write(gc_complete / "notes.txt", "not a save");
  if (CompleteGameCubeSaveFiles(gc_complete).size() != 1 ||
      InstallCompleteGameCubeSave(gc_complete, card, gc_backups, "x").error !=
          SaveError::NoGameSave ||
      HasGameCubeSave(gc_complete, card))
    return 124;
  Write(card / name, Gci('P', 'D'));
  Write(card / "01-GALE-other.gci", "another game");
  if (!HasGameCubeSave(gc_complete, card))
    return 125;
  const SaveResult gc_installed =
      InstallCompleteGameCubeSave(gc_complete, card, gc_backups, "2026-11-12_03-00-00");
  const std::string merged = Read(card / name);
  if (!gc_installed || merged.size() != 0x2040 || merged.substr(0x40, 0x1400) !=
                                                       std::string(0x1400, 'P') ||
      merged.substr(0x1440) != std::string(0x2040 - 0x1440, 'C') ||
      merged.substr(0, 0x40) != Gci('\0', 'C').substr(0, 0x40))
    return 126;
  if (Read(gc_installed.backup / name) != Gci('P', 'D') || Count(gc_installed.backup) != 1 ||
      LatestSaveBackup(gc_backups) != gc_installed.backup)
    return 127;
  // Restored: the player's file again, the other game's untouched.
  if (!RestoreGameCubeSaveBackup(gc_installed.backup, card) || Read(card / name) != Gci('P', 'D') ||
      Read(card / "01-GALE-other.gci") != "another game" || fs::exists(gc_installed.backup) ||
      LatestSaveBackup(gc_backups))
    return 128;
  // A save of another shape: nothing written, no copy made.
  Write(card / name, Gci('P', 'D').substr(0, 0x800));
  if (InstallCompleteGameCubeSave(gc_complete, card, gc_backups, "y").error != SaveError::Copy ||
      Count(gc_backups) != 0 || Read(card / name).size() != 0x800)
    return 129;

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
